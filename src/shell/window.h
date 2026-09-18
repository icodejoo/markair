// mdvn 的窗口外壳(T12):窗口类注册、主窗口创建、窗口过程、DPI 感知与
// 滚动状态管理。本模块只负责"消息 -> 状态变更 -> 触发重绘",不做解析/布局/
// 渲染本身的工作,那些分别属于 doc/layout/render 模块。
//
// 设计要点:
//   - 标准 Windows 标题栏(WS_OVERLAPPEDWINDOW),不自绘(裁决 #9)。
//   - Per-Monitor V2 DPI 感知,`WM_DPICHANGED` 应用系统建议矩形并重建 D2D 资源。
//   - 窗口类背景刷跟随注册时的生效主题(T48):浅色沿用系统 COLOR_WINDOW,
//     深色改用与 kDarkPalette.background 同色的自建刷子,避免首帧白闪(架构 §6)。
//     标题栏深浅色另由 DwmSetWindowAttribute 控制(创建时 + Ctrl+Shift+T 热切换)。
//   - 滚动数值计算全部委托给 shell/scroll.h 里的纯函数,便于单元测试。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../assets/cache.h"
#include "../assets/remote.h"
#include "../doc/model.h"
#include "../layout/layout.h"
#include "../render/renderer.h"
#include "../text/font.h"
#include "clipboard.h"
#include "find.h"
#include "hit_test.h"
#include "navigate.h"
#include "scroll.h"
#include "theme_state.h"
#include "window_state.h"

namespace mdvn {

/**
 * 窗口运行期状态:外壳层需要用到的各子系统引用 + 当前滚动偏移。
 *
 * 各指针指向的实例由调用方(`wWinMain`)持有,生命周期须覆盖整个消息循环;
 * 本结构体自身也由调用方分配(通常放在栈上),外壳层不做任何动态分配。
 * 全部字段为 POD/裸指针,不含有副作用的构造函数(编码规范第 6 条)。
 */
struct WindowState {
    FontSubsystem* fonts;        // 字体子系统,用于按可见范围生成 IDWriteTextLayout
    BlockLayoutEngine* layout;   // 块级布局引擎
    const Document* doc;         // 当前文档模型,视口宽度变化时用于重跑布局
    Renderer* renderer;          // D2D 渲染器
    float scrollY;               // 当前纵向滚动偏移(DIP),恒在 [0, maxScroll] 内

    // 以下为 M1 图片子系统(T32/T33/T34/T36b)所需的引用,均可为空:
    // 为空时图片一律画占位块、点击图片无行为,其余功能不受影响。
    ImageCache* images;                  // 图片缓存(尺寸常驻 + 位图随虚拟化生灭)
    ImageResidencyManager* residency;    // 图片驻留管理器,挂在块虚拟化触发点上
    RemoteImageLoader* remote;           // 网络图片加载器(默认关闭)
    TempFileRegistry* tempFiles;         // T36b 临时文件清单(退出前统一清理)
    Arena* imageScratch;                 // data: URI 解码用的临时 Arena
    const wchar_t* documentDirectory;     // 当前文档所在目录,相对路径图片据此解析

    // 以下为 M1 交互层(T36/T37/T38)所需,均可为空:
    // 为空时对应功能静默失效(不崩溃),其余功能不受影响。
    FindSession* find;                    // Ctrl+F 查找会话(T37/T38)
    const wchar_t* statusMessage;         // 窗口内提示(如"文件不存在"),不弹 MessageBox

    /**
     * T36 ②:在当前窗口内替换文档(裁决 #6,不新开进程)。由 app 层实现——
     * 外壳层不知道文档是怎么加载的,只负责在链接被点击时发起这次替换。
     * 实现方应释放旧的 Document / BlockLayoutEngine / 图片缓存后重建并重排,
     * 并就地更新 `doc` / `documentDirectory` 等字段。
     * 为空表示不支持文档内跳转(纯渲染场景/单测)。
     *
     * @param userData 即 `callbackUserData`。
     * @param fullPath 已规范化的绝对路径(以 '\0' 结尾)。
     * @return 加载成功返回 true;失败返回 false(外壳层据此显示窗口内提示)。
     */
    bool (*openDocumentInPlace)(void* userData, const wchar_t* fullPath);

    // 以下三个字段是给调用方(main.cpp/T14 性能埋点)预留的通用回调钩子,
    // 外壳层本身不关心它们的用途,只在对应时机原样调用;均可为空指针,
    // 为空时不产生任何额外调用开销。这样保持 shell 层不直接依赖 bench 模块。
    void (*onWindowCreated)(void* userData);  // 窗口创建成功、显示之前调用一次
    void (*onFirstPresent)(void* userData);   // 首次绘制成功返回后调用一次
    void* callbackUserData;                    // 传给以上两个回调的自定义指针

    bool firstPresentDone;       // 内部状态:onFirstPresent 是否已触发过,调用方应初始化为 false

    // T42(bench 专用,测试路径,不改变默认行为):为 true 时,首次绘制会在正常的
    // "首屏可见 ± 1 屏"解码之后,额外对整份文档([0, TotalHeight()])调用一次
    // UpdateVisibleRange,强制把全部图片都解码一遍——用于近似"滚到底"的内存
    // 测量口径(--bench 模式本身不模拟真实滚动,这是唯一的替代)。为 false(默认)
    // 时不产生任何额外调用,非 bench 场景行为与之前完全一致。
    bool benchForceFullDecode;

    // T45 代码块复制按钮:拼接剪贴板文本用的临时 Arena(每次复制前整体 Reset)。
    // 为空时点击复制按钮静默无行为,其余功能不受影响。
    Arena* clipboardScratch;

    // T45 复制按钮的两种瞬时交互态,都由 `CreateMainWindow` 初始化成
    // `kInvalidIndex`("没有任何按钮处于该状态"),调用方不必自行填写。
    // 这两个字段直接喂给 `ShellOverlay` 的同名字段供渲染层分三态绘制。
    u32 copyButtonHover;   // 鼠标当前悬浮的复制按钮所属块下标
    u32 copyButtonCopied;  // 处于"已复制"反馈态的复制按钮所属块下标

    // T47:主题三态状态机。`themeSetting` 是用户偏好(state.ini 的 theme 键 /
    // Ctrl+Shift+T 循环),`systemIsDark` 是最近一次探测到的系统深浅色结果
    // (启动时探测一次,之后由 WM_SETTINGCHANGE 更新)。两者都由调用方
    // (main.cpp)负责初始化,CreateMainWindow 不负责清零它们——与其余"由
    // 调用方填好"的字段是同一套职责划分,不单独新造一套。
    ThemeSetting themeSetting;
    bool systemIsDark;

    // T56 窗口状态记忆(裁决 #8:全局一份 + 层叠偏移)。
    //
    // 传入 `CreateMainWindow` 之前:代表"待恢复"的窗口矩形(物理像素,来自
    // `state.ini` 的 win_x/y/w/h)与是否要以最大化态打开;`winW`/`winH` <= 0
    // 表示从未存过(首次启动),此时 `CreateMainWindow` 走系统默认位置/尺寸,
    // 不进入越界钳制/层叠偏移流程。
    //
    // `CreateMainWindow` 返回之后、以及此后任何一次移动/缩放/退出:这 5 个
    // 字段被 `window.cpp` 内部持续更新为"当前实时的还原态矩形"(用
    // `GetWindowPlacement` 的 `rcNormalPosition`,不是最大化后的矩形),
    // 调用方(main.cpp)在 `onWindowGeometryChanged` 回调触发时读取这些字段
    // 写回 `state.ini`。
    i32 winX;
    i32 winY;
    i32 winW;
    i32 winH;
    bool winMaximized;

    /**
     * 窗口矩形变化(移动/缩放)去抖 500ms 后,或窗口即将销毁前立即触发一次,
     * 通知调用方把当前的 `winX/winY/winW/winH/winMaximized` 写进 `state.ini`
     * (裁决 #7 的读-改-写 + 命名互斥体通道)。为空表示不持久化窗口状态。
     * @param userData 即 `callbackUserData`。
     */
    void (*onWindowGeometryChanged)(void* userData);
};

/**
 * 开启 Per-Monitor V2 DPI 感知,须在创建任何窗口之前调用。
 *
 * 实现方式是运行期从 user32.dll 取 `SetProcessDpiAwarenessContext`,系统不支持
 * (Win10 1703 以前)时静默退化为系统 DPI 感知,不影响程序启动。
 *
 * @return 成功开启 Per-Monitor V2 返回 true;系统不支持或调用失败返回 false。
 * @example
 *   int WINAPI wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int) {
 *       mdvn::EnablePerMonitorV2DpiAwareness();
 *       // ... 之后再创建窗口
 *   }
 */
bool EnablePerMonitorV2DpiAwareness();

/**
 * 注册 mdvn 主窗口类(幂等:重复调用只在首次真正注册,`isDarkTheme` 只在
 * 首次真正注册时生效)。
 * @param instance 当前进程实例句柄。
 * @param isDarkTheme 注册时的生效主题是否为深色(调用方用
 *        `ResolveEffectiveTheme` 提前算好);为 true 时窗口类背景刷改用
 *        与 `kDarkPalette.background` 同色的自建刷子(T48),避免深色主题下
 *        首帧白闪,为 false 时沿用系统 `COLOR_WINDOW`。
 * @return 注册成功(或此前已注册成功)返回 true。
 * @example mdvn::RegisterMainWindowClass(hInstance, true);  // isDarkTheme=true
 */
bool RegisterMainWindowClass(HINSTANCE instance, bool isDarkTheme);

/**
 * 释放 `RegisterMainWindowClass` 深色主题下自建的窗口类背景刷(T48)。
 * 幂等:未创建过深色刷子、或已释放过,再次调用都是安全的空操作。
 * 浅色主题下窗口类背景刷是系统内置的 `COLOR_WINDOW` 句柄,不需要也不能
 * `DeleteObject`,本函数只处理自建的那一支。
 *
 * 调用方(`wWinMain`)应在消息循环结束、进程退出前调用一次。
 * @example
 *   int exitCode = mdvn::RunMessageLoop();
 *   mdvn::ReleaseMainWindowClassResources();
 *   return exitCode;
 */
void ReleaseMainWindowClassResources();

/**
 * 创建并显示主窗口,把窗口过程需要的运行期状态绑定到该窗口上。
 *
 * 调用前须先成功调用 `RegisterMainWindowClass`。**T56 起**创建位置/尺寸由
 * `state->winW`/`winH` 决定:<= 0(从未存过)时走原来的行为——标准位置、
 * 初始逻辑尺寸 800x600 并按所在显示器 DPI 缩放;否则按 `state->winX/Y/W/H`
 * 恢复(先做多显示器越界钳制,再做同位置多开层叠偏移),并按
 * `state->winMaximized` 决定是否以最大化态显示。拿到 HWND 后会立即按
 * `state->themeSetting`/`state->systemIsDark` 解出的生效主题调用一次
 * `DwmSetWindowAttribute` 切标题栏深浅色(T48)。返回前会把 `winX/Y/W/H/
 * winMaximized` 更新为窗口创建后的实际当前值,供后续移动/缩放/退出时的
 * 持久化读取。
 *
 * @param instance 当前进程实例句柄。
 * @param title 窗口标题(UTF-16,非空)。
 * @param state 运行期状态,生命周期须覆盖整个消息循环;函数内部会把
 *              `scrollY`/`firstPresentDone` 归零、把两个 `copyButtonXxx`
 *              置为 `kInvalidIndex`,其余字段由调用方填好(`winX/Y/W/H/
 *              winMaximized` 填"待恢复值",`winW`/`winH` <= 0 表示不恢复)。
 *              `onWindowCreated` 会在窗口创建成功、显示之前被调用一次
 *              (若非空)。
 * @return 创建成功返回窗口句柄,失败返回 nullptr。
 * @example
 *   mdvn::WindowState state{};
 *   state.fonts = &fonts; state.layout = &layout;  // ...其余字段同上...
 *   state.winX = settings.winX; state.winY = settings.winY;
 *   state.winW = settings.winW; state.winH = settings.winH;
 *   state.winMaximized = settings.winMaximized;
 *   state.onWindowGeometryChanged = &OnWindowGeometryChangedHook;
 *   HWND hwnd = mdvn::CreateMainWindow(hInstance, L"mdvn", &state);
 */
HWND CreateMainWindow(HINSTANCE instance, const wchar_t* title, WindowState* state);

/**
 * 取窗口客户区宽度(DIP)。客户区本身是物理像素,除以窗口所在显示器的 DPI
 * 缩放换算成逻辑单位,与布局/渲染使用的 DIP 坐标系保持一致。
 * @param hwnd 目标窗口。
 * @return 客户区宽度(DIP);取不到 DPI 时按 96 DPI 计算。
 * @example layout.Relayout(doc, mdvn::ClientWidthDip(hwnd), fonts.Scale(), &cache);
 */
float ClientWidthDip(HWND hwnd);

/**
 * 取窗口客户区高度(DIP),口径同 `ClientWidthDip`。
 * @param hwnd 目标窗口。
 * @return 客户区高度(DIP)。
 * @example float vh = mdvn::ClientHeightDip(hwnd);
 */
float ClientHeightDip(HWND hwnd);

/**
 * 跑标准的 `GetMessage` 消息循环,直到窗口关闭(收到 `WM_QUIT`)。
 * @return `WM_QUIT` 携带的退出码,可直接作为 `wWinMain` 的返回值。
 * @example return mdvn::RunMessageLoop();
 */
int RunMessageLoop();

}  // namespace mdvn
