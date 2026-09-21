// markair 的窗口外壳(T12):窗口类注册、主窗口创建、窗口过程、DPI 感知与
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
#include "bottom_bar.h"
#include "button.h"
#include "clipboard.h"
#include "copy_data.h"
#include "find.h"
#include "folder_scan.h"
#include "history.h"
#include "hit_test.h"
#include "navigate.h"
#include "outline_panel.h"
#include "sidebar.h"
#include "scroll.h"
#include "scrollbar.h"
#include "selection.h"
#include "theme_state.h"
#include "welcome_screen.h"
#include "window_state.h"
#include "../util/recent_files.h"

namespace markair {

/**
 * 自绘滚动条(方案A)当前正在拖动哪一个实例;`None` 表示都没有。
 * 正文、大纲侧栏与历史记录侧栏各自的滑块几何/内容高度不同,但共用同一套
 * shell/scrollbar.h 纯函数,这里只记"当前拖的是哪一个"。
 */
enum class ScrollbarDragTarget {
    None,
    Main,     // 正文滚动条
    Outline,  // 大纲侧栏滚动条
    History,  // 历史记录侧栏滚动条
    Folder,   // 文件夹列表侧栏滚动条
};

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

    // T63 大纲侧栏(`Ctrl+\` 切换,默认关闭)。"指针为空即不存在":为空表示
    // 侧栏关闭,此时不会有任何 ExtractOutline/IDWriteTextLayout/Arena 分配
    // 发生;由 window.cpp 在切换打开时用 `outlineArena` 现场构造。
    OutlinePanel* outline;
    // 供 outline 对象自身与其内部 Vec<OutlineItem> 分配的 Arena,由调用方
    // (main.cpp)持有并传入指针;`Init` 延迟到首次打开侧栏才调用,默认关闭
    // 状态下永远不会被 Init,保持"不分配任何 Arena"的验收口径。可为空表示
    // 不支持大纲侧栏(Ctrl+\ 静默无效)。
    Arena* outlineArena;

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

    /**
     * 进程内新开一个顶层窗口打开 `fullPath`(取代旧版 `CreateProcessW` 新开
     * 独立进程的做法——多开文档窗口不再线性翻倍内存)。若同一文件已有窗口
     * 打开，实现方应前置该窗口而不是重复创建。由 app 层实现，外壳层只在
     * "打开文件"按钮/菜单与点击 `.md` 链接这两类场景调用。
     * 为空表示不支持(纯渲染场景/单测)。
     *
     * @param userData 即 `callbackUserData`。
     * @param fullPath 目标文件路径(可为相对路径，由实现方规范化)。
     * @return 新窗口创建成功，或已前置一个同文件的既有窗口，返回 true；
     *         两者都失败返回 false(外壳层据此显示窗口内提示)。
     */
    bool (*openNewWindow)(void* userData, const wchar_t* fullPath);

    /**
     * 本窗口即将销毁(`WM_DESTROY` 末尾)时调用一次，用于递减进程内窗口计数、
     * 释放本窗口专属的堆上资源；由实现方决定计数归零时是否 `PostQuitMessage`。
     * 为空表示不需要该收尾(例如纯渲染场景/单测)。
     * @param userData 即 `callbackUserData`。
     */
    void (*onWindowClosed)(void* userData);

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

    // 表格行悬浮态(斑马纹之上叠加高亮),同样由 `CreateMainWindow` 初始化成
    // `kInvalidIndex`。hoverTableRow 是该表**表体**内的行号(0 起,不含表头)。
    u32 hoverTableBlock;
    u32 hoverTableRow;

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

    // T65:历史前进/后退栈(`Alt+←`/`Alt+→`)。为空表示不支持历史导航
    // (纯渲染场景/单测),此时两个快捷键静默无效。由调用方(main.cpp)持有
    // 并传入指针,生命周期须覆盖整个消息循环。
    History* history;

    // T65:当前文档的完整路径,供 `history->PushNavigation` 在"替换文档"或
    // "锚点跳转"之前记一笔"跳转前的路径"。由调用方在启动期初始化为首次
    // 打开的文档路径;此后每次窗口内换文档成功后由 window.cpp 自己更新,
    // main.cpp 不需要、也不应该改写这个字段(避免两处各写一份产生分歧)。
    // 未显式初始化的字段(不在 CreateMainWindow 之前的聚合初始化列表里)
    // 按聚合初始化规则清零,等价于空字符串。
    wchar_t currentDocumentPath[kHistoryPathCapacity];

    // T76:逐帧重绘耗时埋点钩子,与上面 onWindowCreated/onFirstPresent 同一套
    // "外壳层不认识 bench 模块"的约定。三者分别在一次重绘的开始、虚拟化阶段
    // 结束、D2D 绘制返回时被调用;为空(默认/非 --bench)时每帧只多两三次
    // 空指针判断,不产生任何计时调用。
    void (*onFrameBegin)(void* userData);
    void (*onFrameLayoutDone)(void* userData);
    void (*onFrameEnd)(void* userData);

    // T78:内存泄漏排查探针专用的"同进程内连续循环"驱动开关(--bench-loop=
    // <kind>:<count>),与上面几组回调同一套"外壳层不认识 bench 模块"的约定,
    // 默认(不带该参数)全为零值,不产生任何额外行为。
    // benchLoopKind: 0=未启用,1=就地换文档(复用 T36 openDocumentInPlace),
    // 2=主题循环(Ctrl+Shift+T 同路径),3=大纲侧栏开关(Ctrl+\ 同路径),
    // 4=F5 重载(同路径)。
    int benchLoopKind;
    int benchLoopTotal;   // 目标循环次数
    int benchLoopDone;    // 内部状态:已完成次数,调用方应初始化为 0
    // benchLoopKind==1 时使用:待循环打开的语料文件路径数组与个数,由调用方
    // (main.cpp)持有,生命周期须覆盖整个消息循环;其余 kind 下可为空/0。
    const wchar_t* const* benchLoopFiles;
    int benchLoopFileCount;
    // 每完成一次循环动作调用一次(1-based 序号),为空表示不采样。
    void (*onBenchLoopTick)(void* userData, int iteration);

    // T80:鼠标拖选文本 + Ctrl+C 复制。为空表示不支持选择(纯渲染场景/单测),
    // 此时拖选与 Ctrl+C 静默无效,其余功能不受影响。生命周期须覆盖整个消息
    // 循环,由调用方(main.cpp)持有并传入指针。
    SelectionState* selection;
    // 提取选区跨块纯文本用的临时 Arena(Ctrl+C 复制前整体 Reset),与
    // clipboardScratch 分开是为了不让"复制选中文本"和"点代码块复制按钮"
    // 互相抹掉对方正在用的临时缓冲。为空时 Ctrl+C 静默无效。
    Arena* selectionScratch;

    // 自绘滚动条(方案A,替代原生 WS_VSCROLL,2026-09-18):拖动滑块过程中的
    // 状态机,`None` 表示当前没有在拖任何滚动条。与 T80 的文本拖选(见上面
    // `selection`)互斥——按下时优先判定是否命中滑块,命中则不再进入拖选。
    ScrollbarDragTarget scrollbarDragTarget;
    float scrollbarDragStartMouseYDip;   // 拖动起始时的鼠标纵坐标(DIP)
    float scrollbarDragStartScrollY;     // 拖动起始时的滚动偏移(DIP,正文/侧栏各自的)

    // 自绘滚动条 Idle/Active 双档透明度(2026-09-18):鼠标是否落在对应滚动条
    // 的横向范围内(不含正在拖动的情况——拖动态由 scrollbarDragTarget 单独
    // 判断,渲染层把"悬浮"和"拖动"都当 Active 处理,见 window.cpp 的调用点)。
    bool mainScrollbarHover;
    bool outlineScrollbarHover;

    // T63b:大纲侧栏宽度支持拖拽调整(2026-09-18)。DIP,初始值由
    // CreateMainWindow 设为 kOutlinePanelWidthDip 的默认值;拖拽范围钳制在
    // [kOutlinePanelMinWidthDip, kOutlinePanelMaxWidthDip](见 outline_panel.h)。
    float outlinePanelWidthDip;
    bool outlinePanelResizing;            // 是否正在拖拽侧栏右边缘调宽度
    float outlinePanelResizeStartMouseXDip;  // 拖拽起始时的鼠标横坐标(DIP)
    float outlinePanelResizeStartWidthDip;   // 拖拽起始时的侧栏宽度(DIP)

    // Outline drawer slide & mask fade animation state machine.
    //
    // 大纲抽屉式侧栏滑动与蒙层淡入淡出动画状态机。
    OutlineAnimState outlineAnimState;

    // Current normalized animation progress in [0.0f, 1.0f] (0.0f = closed, 1.0f = fully open).
    //
    // 当前归一化动画进度，取值范围 [0.0f, 1.0f] (0.0f 表示完全收起，1.0f 表示完全展开)。
    float outlineAnimProgress;

    // Millisecond timestamp when the current animation phase started (via GetTickCount64).
    //
    // 当前动画阶段开始时的毫秒时间戳 (通过 GetTickCount64 获取)。
    ULONGLONG outlineAnimStartTick;

    // Animation progress value when starting current transition (allows smooth reversal mid-flight).
    //
    // 动画本次过渡开始时的初始进度值 (支持动画进行中反向切换时的平滑过渡)。
    float outlineAnimStartProgress;

    // 底部栏右侧状态区(2026-09-18 新增):与 `currentDocumentPath` 同步维护
    // 的当前文档字节数,换文档成功的每处都要一并更新,画进状态区的
    // "路径 (大小)"文字。未打开文件时保持聚合初始化留下的 0。
    u64 currentDocumentSizeBytes;

    // 底部栏左侧图标按钮的悬浮态(2026-09-18 新增,取代常驻文字标签):
    // `WM_MOUSEMOVE` 用现有的 `IsPointInBottomBar`/`HitTestBottomBar` 判定
    // 结果写这里,`None` 表示鼠标未落在任何按钮上(含落在右侧状态区)。
    // 渲染层据此在悬浮的按钮上方画一个纯 D2D 文字气泡当提示,不引入
    // Win32 TOOLTIPS_CLASS 控件(风险更小、改动更集中)。
    BottomBarButton bottomBarHoverButton;

    // 欢迎屏(未打开任何文档时的静态引导页,取代示例 markdown):true 表示
    // 不走正常的 DrawBlock 渲染管线,改画"Welcome to markair" + 打开文件
    // 按钮。由调用方(main.cpp)在文档未打开成功时设为 true,window.cpp
    // 运行期只读不改写。
    bool showWelcomeScreen;

    // 欢迎屏"打开文件"按钮悬浮态,判定口径与 bottomBarHoverButton 相同:
    // WM_MOUSEMOVE 命中时置真,鼠标移出时置假,CreateMainWindow 显式初始化
    // 为 false。仅在 showWelcomeScreen 为真时有意义。
    bool welcomeButtonHover;

    // Pointer to recent files history records collection (owned by caller).
    //
    // 指向最近打开文件历史记录数据集合的指针（由调用方持有）。
    RecentFiles* recentFiles;

    // History sidebar drawer slide & mask fade animation state machine.
    //
    // 历史记录抽屉式侧栏滑动与蒙层淡入淡出动画状态机。
    SidebarAnimState historyAnimState;

    // Current normalized animation progress for history sidebar in [0.0f, 1.0f].
    //
    // 历史记录侧栏当前归一化动画进度，取值范围 [0.0f, 1.0f]。
    float historyAnimProgress;

    // Millisecond timestamp when the history sidebar animation phase started.
    //
    // 历史记录侧栏当前动画阶段开始时的毫秒时间戳。
    ULONGLONG historyAnimStartTick;

    // History sidebar animation progress value at transition start.
    //
    // 历史记录侧栏动画本次过渡开始时的初始进度值。
    float historyAnimStartProgress;

    // Current width of history sidebar in DIPs.
    //
    // 历史记录侧栏当前宽度（DIP）。
    float historyPanelWidthDip;

    // Vertical scroll offset of history sidebar in DIPs.
    //
    // 历史记录侧栏纵向滚动偏移（DIP）。
    float historyScrollY;

    // Whether history sidebar scrollbar is hovered by mouse.
    //
    // 历史记录侧栏滚动条是否处于鼠标悬浮态。
    bool historyScrollbarHover;

    // Whether history sidebar width is currently being resized by mouse drag.
    //
    // 历史记录侧栏宽度当前是否正在被鼠标拖拽调整。
    bool historyPanelResizing;

    // Mouse horizontal coordinate in DIPs when history resize drag started.
    //
    // 历史记录侧栏开始拖拽调宽时的鼠标横坐标（DIP）。
    float historyPanelResizeStartMouseXDip;

    // History sidebar width in DIPs when history resize drag started.
    //
    // 历史记录侧栏开始拖拽调宽时的初始宽度（DIP）。
    float historyPanelResizeStartWidthDip;

    // Currently hovered history list item index; kInvalidIndex indicates none.
    //
    // 当前鼠标悬浮的历史记录条目下标；kInvalidIndex 表示无。
    u32 historyHoverIndex;

    // Whether the bottom bar's "copy full path" button is currently showing
    // its brief post-click success checkmark (auto-clears via
    // kCopyFeedbackTimerId, same timer as the code-block copy button).
    //
    // 底部栏"复制全路径"按钮当前是否处于点击后短暂显示的成功勾选反馈态
    // (通过 kCopyFeedbackTimerId 自动清除，与代码块复制按钮共用同一个定时器)。
    bool bottomBarPathCopied;

    // 查找条查询串编辑用的原生 Win32 EDIT 子窗口(2026-09-19 改版,取代自绘
    // 假输入框)。由 CreateMainWindow 创建,与主窗口同生命周期,子窗口随父
    // 窗口销毁自动回收,这里不需要显式 DestroyWindow。find 为空时不会创建
    // (无查找条也就不需要这个控件);追加在结构体末尾,不打乱既有字段的
    // 位置初始化顺序(main.cpp 用聚合初始化按位置填充前面的字段)。
    HWND findEditHwnd;

    // Last DPI scale factor the find-edit's HFONT was built for; 0 means no
    // font has been created yet. RepositionFindEdit only calls
    // CreateFontIndirectW when the scale actually changes, instead of
    // reallocating a GDI font object on every WM_SIZE (which fires
    // repeatedly during an interactive window resize).
    //
    // 查找条 EDIT 控件当前 HFONT 对应的 DPI 缩放系数；0 表示还没建过字体。
    // RepositionFindEdit 只在缩放系数真正变化时才调用 CreateFontIndirectW，
    // 而不是每次 WM_SIZE(交互式拖边框时会连续触发很多次)都重新分配一个
    // GDI 字体对象。
    float findEditFontScale;

    // 欢迎屏"打开文件夹"按钮悬浮态
    bool welcomeFolderButtonHover;

    // 文件夹穿透功能状态:
    Arena* folderArena;               // 文件夹条目专用 Arena
    Vec<FolderEntry> folderEntries;    // 扫描到的文件夹条目动态数组
    wchar_t folderRootPath[MAX_PATH];  // 选中的文件夹绝对根路径
    wchar_t folderRootName[MAX_PATH];  // 选中的文件夹根目录名称
    u32 folderCurrentItem;             // 当前打开文件匹配的条目下标; kInvalidIndex 表示无
    u32 folderHoverIndex;              // 鼠标悬浮的条目下标; kInvalidIndex 表示无
    SidebarAnimState folderAnimState;  // 侧栏滑动动画状态机
    float folderAnimProgress;          // 动画进度 [0.0f, 1.0f]
    ULONGLONG folderAnimStartTick;     // 动画开始时间戳
    float folderAnimStartProgress;     // 动画过渡初值
    float folderPanelWidthDip;         // 侧栏宽度(DIP)
    float folderScrollY;               // 纵向滚动偏移(DIP)
    bool folderScrollbarHover;         // 滚动条是否处于悬浮态
    bool folderPanelResizing;          // 是否正在拖拽调宽
    float folderPanelResizeStartMouseXDip; // 拖拽调宽起始时的鼠标横坐标(DIP)
    float folderPanelResizeStartWidthDip;  // 拖拽调宽起始时的初始宽度(DIP)

    // 文件夹侧栏增强：过滤输入框与按钮交互状态
    HWND folderFilterEditHwnd;               // 过滤原生 EDIT 控件句柄
    float folderFilterFontScale;             // 过滤输入框当前 HFONT 对应的 DPI 缩放系数
    wchar_t folderFilterQuery[128];          // 当前过滤文本
    u32 folderFilterQueryLen;                // 当前过滤文本长度
    Vec<u32> folderFilteredIndices;          // 过滤后的条目在 folderEntries 中的原始下标数组
    bool folderHeaderButtonHover;            // 顶部"打开根目录"文件夹按钮悬浮态
    bool folderItemFolderButtonHover;        // 条目"打开所在文件夹"按钮悬浮态
    bool folderItemNewWindowButtonHover;     // 条目"新窗口打开"按钮悬浮态
};

/**
 * 打开并扫描指定文件夹，生成 Markdown 列表并展开文件夹侧栏。
 *
 * @param hwnd 主窗口句柄。
 * @param state 窗口运行期状态。
 * @param folderPath 要扫描的文件夹绝对路径。
 * @param openSidebar 是否在扫描完成后自动启动动画展开侧栏，打开单文件时为 false。
 */
void OpenAndScanFolder(HWND hwnd, WindowState* state, const wchar_t* folderPath, bool openSidebar = true);

/**
 * 换文档后把正文滚动位置复位到顶部,并同步刷新虚拟化可见区间。
 * 供 app 层(main.cpp 的 OpenDocumentInPlace)在换文档/侧栏切换文档时调用——
 * 换文档前后 state->layout 已经是新文档的布局,直接调用带 forceRefresh 的
 * 内部滚动收尾逻辑,不需要 app 层重复实现一遍夹取/虚拟化刷新。
 *
 * @param hwnd 主窗口句柄。
 * @param state 窗口运行期状态,scrollY 与虚拟化区间都会被更新。
 * @example markair::ResetScrollToTop(hwnd, state);
 */
void ResetScrollToTop(HWND hwnd, WindowState* state);

/**
 * 切换文件夹侧栏的展开/折叠状态。
 *
 * @param hwnd 主窗口句柄。
 * @param state 窗口运行期状态。
 */
void ToggleFolderPanel(HWND hwnd, WindowState* state);

/**
 * Prompt the user (Yes/No) to remove a history entry whose target file no
 * longer exists on disk. Shared by every code path that can hit a missing
 * history-entry file (row click, folder-icon click, startup auto-open of
 * the most recent entry) so they all fail the exact same way instead of
 * each growing its own dialog.
 *
 * 弹窗(是/否)询问是否从历史记录中删除一条目标文件已不存在的历史条目。
 * 所有可能撞上"文件已不存在"的路径(点击整行、点击文件夹图标、启动时
 * 自动打开最近一条历史记录)共用这一个函数,失败处理方式完全一致,不各自
 * 另造一份弹窗。
 *
 * @param hwnd Window handle, used as the message box owner.
 *
 *   窗口句柄,作为弹窗的父窗口。
 *
 * @param state Pointer to WindowState.
 *
 *   指向窗口运行期状态的指针。
 *
 * @param item History entry index whose file is missing.
 *
 *   目标文件已不存在的历史记录条目下标。
 */
void ConfirmAndRemoveMissingHistoryEntry(HWND hwnd, WindowState* state, u32 item);

/**
 * Request a debounced (500ms) write of the recent-files history to disk.
 * Callers only mutate the in-memory RecentFiles list synchronously
 * (markair::AddRecentFile is a cheap array shift, no I/O); the actual disk
 * write happens on a WM_TIMER tick owned by the main window, so clicking
 * several in-document links back-to-back does not fsync once per click.
 *
 * 请求一次去抖(500ms)的历史记录写盘。调用方只需同步更新内存里的
 * RecentFiles 列表(markair::AddRecentFile 只是数组移位,不涉及 I/O);真正的
 * 磁盘写入发生在主窗口拥有的 WM_TIMER 到点时,连续点击好几个文档内链接
 * 不会每次点击都落一次盘。
 *
 * @param hwnd Main window handle, used to host the debounce timer.
 *
 *   主窗口句柄,用于挂载去抖定时器。
 *
 * @param state Runtime state; silently returns if recentFiles is null.
 *
 *   运行期状态,recentFiles 为空时静默返回。
 *
 * @example markair::RequestRecentFilesSave(hwnd, &windowState);
 */
void RequestRecentFilesSave(HWND hwnd, WindowState* state);

/**
 * 开启 Per-Monitor V2 DPI 感知,须在创建任何窗口之前调用。
 *
 * 实现方式是运行期从 user32.dll 取 `SetProcessDpiAwarenessContext`,系统不支持
 * (Win10 1703 以前)时静默退化为系统 DPI 感知,不影响程序启动。
 *
 * @return 成功开启 Per-Monitor V2 返回 true;系统不支持或调用失败返回 false。
 * @example
 *   int WINAPI wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int) {
 *       markair::EnablePerMonitorV2DpiAwareness();
 *       // ... 之后再创建窗口
 *   }
 */
bool EnablePerMonitorV2DpiAwareness();

/**
 * 注册 markair 主窗口类(幂等:重复调用只在首次真正注册,`isDarkTheme` 只在
 * 首次真正注册时生效)。
 * @param instance 当前进程实例句柄。
 * @param isDarkTheme 注册时的生效主题是否为深色(调用方用
 *        `ResolveEffectiveTheme` 提前算好);为 true 时窗口类背景刷改用
 *        与 `kDarkPalette.background` 同色的自建刷子(T48),避免深色主题下
 *        首帧白闪,为 false 时沿用系统 `COLOR_WINDOW`。
 * @return 注册成功(或此前已注册成功)返回 true。
 * @example markair::RegisterMainWindowClass(hInstance, true);  // isDarkTheme=true
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
 *   int exitCode = markair::RunMessageLoop();
 *   markair::ReleaseMainWindowClassResources();
 *   return exitCode;
 */
void ReleaseMainWindowClassResources();

/**
 * 单实例 IPC 用:在本机按窗口类名枚举出任意一个本程序的主窗口。
 * 找不到(尚无主实例在跑,或已跑的实例还没建好窗口)时返回 nullptr。
 * @return 找到的主窗口句柄,或 nullptr。
 * @example HWND main = markair::FindAnyMainWindow();  // 拿去发 WM_COPYDATA
 */
HWND FindAnyMainWindow();

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
 *   markair::WindowState state{};
 *   state.fonts = &fonts; state.layout = &layout;  // ...其余字段同上...
 *   state.winX = settings.winX; state.winY = settings.winY;
 *   state.winW = settings.winW; state.winH = settings.winH;
 *   state.winMaximized = settings.winMaximized;
 *   state.onWindowGeometryChanged = &OnWindowGeometryChangedHook;
 *   HWND hwnd = markair::CreateMainWindow(hInstance, L"markair", &state);
 */
HWND CreateMainWindow(HINSTANCE instance, const wchar_t* title, WindowState* state);

/**
 * 取窗口客户区宽度(DIP)。客户区本身是物理像素,除以窗口所在显示器的 DPI
 * 缩放换算成逻辑单位,与布局/渲染使用的 DIP 坐标系保持一致。
 * @param hwnd 目标窗口。
 * @return 客户区宽度(DIP);取不到 DPI 时按 96 DPI 计算。
 * @example layout.Relayout(doc, markair::ClientWidthDip(hwnd), fonts.Scale(), &cache);
 */
float ClientWidthDip(HWND hwnd);

/**
 * 取窗口客户区高度(DIP),口径同 `ClientWidthDip`。
 * @param hwnd 目标窗口。
 * @return 客户区高度(DIP)。
 * @example float vh = markair::ClientHeightDip(hwnd);
 */
float ClientHeightDip(HWND hwnd);

/**
 * 计算文件夹侧栏（挤压模式）当前应从正文可用宽度中占用的宽度（DIP）。
 * 正文换行宽度需要在窗口内换文档（main.cpp::OpenDocumentInPlace）、窗口
 * 尺寸变化（OnSize）与侧栏自身动画/拖拽调宽（window.cpp 内部）这几处
 * 保持同一口径，因此抽成导出函数，不各自重复算一遍。
 *
 * @param state 窗口运行期状态，可为 nullptr（此时返回 0）。
 * @return 挤压宽度（DIP），文件夹侧栏关闭时为 0。
 * @example float w = markair::ContentWidthDip(markair::ClientWidthDip(hwnd)) - markair::FolderSqueezeWidthDip(state);
 */
float FolderSqueezeWidthDip(const WindowState* state);

/**
 * 跑标准的 `GetMessage` 消息循环,直到窗口关闭(收到 `WM_QUIT`)。
 * @return `WM_QUIT` 携带的退出码,可直接作为 `wWinMain` 的返回值。
 * @example return markair::RunMessageLoop();
 */
int RunMessageLoop();

}  // namespace markair
