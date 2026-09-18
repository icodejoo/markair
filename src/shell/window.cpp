#include "window.h"

#include <new>         // placement new(T63 侧栏在 outlineArena 上就地构造 OutlinePanel)
#include <dwmapi.h>    // DwmSetWindowAttribute(T48 标题栏深浅色,/DELAYLOAD)
#include <imm.h>       // ImmAssociateContextEx(主窗口按窗口关闭 IME,见下方说明)
#include <uxtheme.h>   // SetWindowTheme(原生滚动条深浅色,/DELAYLOAD)
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <cwchar>      // wcscmp

#include "../assets/data_uri.h"
#include "../render/theme.h"  // kLightPalette/kDarkPalette/kDarkBackgroundRgb
#include "../util/str.h"      // Utf8ToUtf16(T80 选区复制)
#include "bottom_bar.h"        // 底部操作栏(新需求):几何/命中测试
#include "find_bar.h"          // 查找条几何(2026-09-19 改版:原生 EDIT 子窗口定位)
#include "open_dialog.h"       // 底部栏"打开文档"按钮:IFileOpenDialog + 新开进程

namespace mdvn {

namespace {

// 主窗口类名,全进程唯一。
constexpr wchar_t kWindowClassName[] = L"mdvn_main_window";

// 窗口初始逻辑尺寸(DIP),按窗口所在显示器 DPI 缩放后作为物理像素尺寸。
constexpr int kInitialWidthDip = 800;
constexpr int kInitialHeightDip = 600;

// Windows 的标称基准 DPI,DPI 缩放系数 = 实际 DPI / 96。
constexpr int kBaselineDpi = 96;

// Per-Monitor V2 的 DPI_AWARENESS_CONTEXT 常量值((DPI_AWARENESS_CONTEXT)-4),
// 这里自行定义,避免依赖特定 SDK 版本的头文件宏。
constexpr INT_PTR kDpiAwarenessContextPerMonitorV2 = -4;

// T45 代码块复制按钮的"已复制"反馈:定时器 ID 与持续时长(毫秒)。
// 用一次性 SetTimer 而不是记时间戳 + 每帧比对,是因为反馈态本身要在没有任何
// 鼠标/键盘输入的情况下自动消失 —— 只有定时器能主动把重绘请求送进消息循环。
constexpr UINT_PTR kCopyFeedbackTimerId = 1;
constexpr UINT kCopyFeedbackDurationMs = 2000;

// T56:窗口矩形变化(移动/缩放)去抖 500ms 再写盘,拖一次窗口不会写几十次
// (裁决 #7 的通用要求,窗口矩形是它列举的三类触发点之一)。
constexpr UINT_PTR kWindowGeometryTimerId = 2;
constexpr UINT kWindowGeometryDebounceMs = 500;

// T63:大纲侧栏"当前阅读位置"高亮的去抖定时器 ID。时长常量
// `kOutlineHighlightDebounceMs`(150ms)定义在 outline_panel.h,滚动/键盘
// 滚动路径只重置这一个定时器,到点才做一次二分 + 局部重绘(裁决 #6)。
constexpr UINT_PTR kOutlineHighlightTimerId = 3;
// T78:内存泄漏排查探针专用循环定时器,仅在 benchLoopKind != 0 时启用。
constexpr UINT_PTR kBenchLoopTimerId = 4;
// 循环节拍(毫秒):够快跑完 100 次不用等太久,又足够让每次动作(重排/重绘)
// 真正跑完一轮消息循环,不与自身重叠。
constexpr UINT kBenchLoopIntervalMs = 30;

// Outline & History drawer slide & mask fade animation timer ID and parameters.
//
// 大纲及历史记录侧栏滑动与蒙层淡入淡出动画的定时器 ID 及参数。
constexpr UINT_PTR kOutlineAnimTimerId = 5;
constexpr UINT_PTR kHistoryAnimTimerId = 6;
constexpr UINT kOutlineAnimIntervalMs = 16;       // ~60 FPS
constexpr float kOutlineAnimDurationMs = 180.0f;  // 180 ms transition / 180毫秒过渡时长

// Debounce timer for persisting recent-files history to disk: matches the
// existing window-geometry pattern (kWindowGeometryTimerId above) — clicking
// several in-document links in quick succession must not fsync once per
// click, only once 500ms after the last one.
//
// 历史记录写盘的去抖定时器,与上面窗口矩形去抖同一手法——连续点击好几个
// 文档内链接时不应该每次点击都落一次盘,只在最后一次点击 500ms 后写一次。
constexpr UINT_PTR kRecentFilesSaveTimerId = 7;
constexpr UINT kRecentFilesSaveDebounceMs = 500;

// T56:枚举显示器/已有本程序窗口时的固定容量上限,均放在栈上,不做动态分配。
// 显示器 16 个、已有窗口 32 个,远超真实使用场景(验收要求"连开 5 个窗口"),
// 超出部分静默截断,不影响正确性,只是层叠/越界判定少看几个显示器/窗口。
constexpr u32 kMaxMonitorsForRestore = 16;
constexpr u32 kMaxExistingWindowsForCascade = 32;

// 窗口类是否已注册成功,保证 RegisterMainWindowClass 幂等(POD 全局,零初始化)。
bool g_classRegistered = false;

// T48:深色主题下窗口类背景刷是自建的(CreateSolidBrush),进程退出前要
// DeleteObject 释放;浅色主题下窗口类背景刷沿用系统内置的 COLOR_WINDOW
// 句柄,该字段保持 nullptr,ReleaseMainWindowClassResources 据此判断要不要
// 释放(POD 全局,零初始化,无副作用构造)。
HBRUSH g_darkBackgroundBrush = nullptr;

// 查找条 EDIT 子窗口的控件 ID(WM_COMMAND 的 LOWORD(wparam) 据此识别来源)。
constexpr int kFindEditControlId = 101;

// 查找条 EDIT 子窗口的背景刷,跟随亮/暗主题(与 render/theme.h 的
// kLightPalette.findBarBackground / kDarkPalette.findBarBackground 同一份
// 数值),各主题一份、懒创建,进程退出前由 ReleaseMainWindowClassResources
// 释放。数值是编译期常量,不必每帧重建,切主题也只是换选用哪一支。
HBRUSH g_findEditBgBrushLight = nullptr;
HBRUSH g_findEditBgBrushDark = nullptr;

// Convert a D2D1_COLOR_F (0~1 float channels) to a GDI COLORREF, dropping
// alpha (GDI brushes/text color have no alpha channel).
//
// D2D1_COLOR_F(0~1 浮点通道)转成 GDI 的 COLORREF,忽略 alpha(GDI 画刷/
// 文字色不支持透明通道)。
COLORREF ColorFToColorRef(const D2D1_COLOR_F& c) {
    return RGB(static_cast<BYTE>(c.r * 255.0f + 0.5f), static_cast<BYTE>(c.g * 255.0f + 0.5f),
               static_cast<BYTE>(c.b * 255.0f + 0.5f));
}

// DWMWA_USE_IMMERSIVE_DARK_MODE 的两个历史取值:20 是 Win10 2004+/Win11 的
// 正式值,19 是 Win10 1809~1903 过渡期用的旧值。自行定义而不依赖 SDK 里的
// 同名宏,是因为旧版 SDK 头文件可能压根没有这个符号(与本文件里
// kDpiAwarenessContextPerMonitorV2 的做法一致)。
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaUseImmersiveDarkModeLegacy = 19;

// 从窗口句柄取回绑定的运行期状态;尚未绑定(WM_NCCREATE 之前)时返回 nullptr。
WindowState* StateOf(HWND hwnd) {
    return reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// 查找条 EDIT 子窗口的子类过程,把 Enter/Esc/F3/Shift+F3 这几个"查找条自己的
// 快捷键"转发给父窗口(与主窗口 WM_KEYDOWN 分支同一套处理,不重复实现),
// 其余按键原样交给系统默认 EDIT 过程(光标移动/选区/剪贴板/IME 全部免费)。
// 原始 EDIT 过程指针存在该子窗口自己的 GWLP_USERDATA 上——EDIT 控件本身不用
// 这个字段,借用它不会跟系统冲突,与主窗口用同一字段存 WindowState* 是两个
// 不同 HWND,互不影响。
LRESULT CALLBACK FindEditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    WNDPROC orig = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_KEYDOWN &&
        (wparam == VK_RETURN || wparam == VK_ESCAPE || wparam == VK_F3)) {
        HWND parent = GetParent(hwnd);
        if (parent) SendMessageW(parent, WM_KEYDOWN, wparam, lparam);
        return 0;
    }
    // 查找条 EDIT 一旦拿到焦点,主窗口再也收不到 WM_KEYDOWN——之前"查找条自己
    // 的几个键"之外的全局快捷键(Ctrl+W 关窗口、Ctrl+Shift+T 切主题、Ctrl+\
    // 切大纲侧栏、Ctrl+=/-/0 缩放、F5 重新加载等)全部失效。这里按"白名单"
    // 转发这几个具体的全局快捷键给父窗口走同一套 WM_KEYDOWN 分支,其余按键
    // (含 Ctrl+Home/End/Left/Right/Backspace 这些 EDIT 自带的单词级导航/
    // 编辑组合键)一律交给默认 EDIT 过程处理。
    //
    // 代码评审(2026-09-19)发现:原先是"黑名单"写法(排除 Ctrl+A/C/V/X/Y/Z
    // 之外的所有 Ctrl 组合键都转发),误伤了 Ctrl+Home/End/Left/Right/
    // Backspace 这些 EDIT 控件自己的单词导航/删词快捷键——找不到新按键出现
    // 就会漏转发的问题,改成显式列出真正需要转发的这几个全局快捷键,与
    // 下面主窗口 WM_KEYDOWN 分支里出现的按键一一对应,不会因为将来 EDIT
    // 支持了什么新组合键而重新踩坑。
    //
    // Once the find EDIT control has keyboard focus, the main window never
    // sees WM_KEYDOWN again — every global shortcut other than the find
    // bar's own keys (Ctrl+W close window, Ctrl+Shift+T theme toggle,
    // Ctrl+\ outline toggle, Ctrl+=/-/0 zoom, F5 reload, etc.) silently
    // stopped working. Forward exactly these specific global shortcuts to
    // the parent's same WM_KEYDOWN switch by an explicit allowlist; every
    // other key (including Ctrl+Home/End/Left/Right/Backspace, the EDIT
    // control's own word-navigation/word-delete combos) goes to the default
    // EDIT proc.
    //
    // Code review (2026-09-19) found: the previous denylist approach
    // (forward every Ctrl-combo except A/C/V/X/Y/Z) also swallowed
    // Ctrl+Home/End/Left/Right/Backspace, breaking word navigation/delete
    // inside the find box. Switched to an explicit allowlist matching the
    // main window's WM_KEYDOWN branches below, so it can't regress again
    // just because the EDIT control starts using some new combo.
    if (msg == WM_KEYDOWN) {
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool isGlobalShortcut =
            (!ctrlDown && wparam == VK_F5) ||
            (ctrlDown && !shiftDown && wparam == 'W') ||
            (ctrlDown && !shiftDown &&
             (wparam == VK_OEM_PLUS || wparam == VK_OEM_MINUS || wparam == '0')) ||
            (ctrlDown && !shiftDown && wparam == VK_OEM_5) ||
            (ctrlDown && shiftDown && wparam == 'T');
        if (isGlobalShortcut) {
            HWND parent = GetParent(hwnd);
            if (parent) SendMessageW(parent, WM_KEYDOWN, wparam, lparam);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
        // 吞掉这两个键对应的 WM_CHAR,否则默认 EDIT 过程会当成"未处理的
        // 控制字符"发出系统提示音(单行 EDIT 对 Enter/Esc 没有默认动作)。
        return 0;
    }
    return CallWindowProcW(orig, hwnd, msg, wparam, lparam);
}

// T48:切标题栏深浅色。按官方口径先试
// DWMWA_USE_IMMERSIVE_DARK_MODE 新值 20,`DwmSetWindowAttribute` 返回非 S_OK
// (比如运行在不支持该属性的老系统上)再试旧值 19;两次都失败就静默放弃——
// 标题栏保持浅色,这不是错误态,不弹框、不影响其余功能。窗口创建时
// (CreateMainWindow)与 Ctrl+Shift+T 热切换时(WndProc 的 WM_KEYDOWN 分支)
// 都调用这一个函数,保证两处行为一致。
//
// 2026-09-17 真机验收发现的坑:`DwmSetWindowAttribute` 在已经显示的窗口上
// 热切换该属性时,调用本身会成功(HRESULT S_OK)但 DWM 不会主动重绘非客户区
// (标题栏视觉上保持旧状态,只有窗口下一次真正重新合成——比如失焦再获焦——
// 才会显现新颜色),必须紧跟一次 `SetWindowPos(..., SWP_FRAMECHANGED)` 强制
// 立即重绘非客户区,这里传 SWP_NOACTIVATE 避免抢焦点、传 SWP_NOMOVE/NOSIZE/
// NOZORDER 说明这只是通知重绘、不改变窗口的位置/层级。窗口创建时那次调用
// (窗口尚未 ShowWindow)理论上不需要这一步,但一起做没有额外成本,统一处理
// 更简单。
//
void ApplyTitleBarTheme(HWND hwnd, bool isDark) {
    // 2026-09-17 真机反馈定位记录:commit 0806db8(只有 DwmSetWindowAttribute+
    // 条件性 SWP_FRAMECHANGED)时用户确认标题栏本身工作正常,只有滚动条没有
    // 跟着变。后续为了让滚动条变色而加的 `SetWindowTheme` 调用 + 各种重排序/
    // 补 RedrawWindow/WM_THEMECHANGED 的尝试,反而让标题栏彻底不再跟着变了
    // (用户截图 `sreenshots/1.png` 实测证实:标题栏全白,不是残留鬼影,是
    // 完全没变)——真正的坏因是 `SetWindowTheme` 作用在主窗口 hwnd 上这件事
    // 本身,不是调用顺序。于是退回 0806db8 已证实工作正常的结构,`DwmSetWindowAttribute`
    // 仍然是唯一决定标题栏深浅色的调用;`SetWindowTheme` 挪到最后**追加**一次
    // 调用(只为了让滚动条子应用主题跟着换,不再折腾顺序)。
    //
    // 2026-09-17 用户精确复现(极有价值的线索):点击后标题栏背景仍是旧色,
    // 但标题栏按钮(最小化/最大化/关闭)图标颜色已经跟着变了;把窗口最小化
    // 再还原,标题栏背景就立刻变对。这说明 DWM 内部其实已经正确接受了新的
    // `DwmSetWindowAttribute` 状态(按钮图标证明了这一点),只是标题栏背景
    // 那块合成好的画面没有被真正"刷"到屏幕上——`SWP_FRAMECHANGED` 只是通知
    // 窗口重新核算非客户区尺寸(WM_NCCALCSIZE),不代表 DWM 合成器已经把新
    // 结果实际提交显示;而最小化/还原会触发一次真正的窗口可见性变化,强制
    // DWM 提交合成队列。这里不能用"改一下尺寸再改回去"模拟同样效果——
    // `OnSize` 每次 WM_SIZE 都会无条件 `Relayout`,伪造一次尺寸变化会违反
    // T49"切主题不得触发 Relayout"的硬约束(且尺寸不变时 Windows 根本不会
    // 发 WM_SIZE,伪造也做不到)。
    //
    // 2026-09-17 补充:先试了 `DwmFlush`(等待/强制合成队列把已提交的变更
    // 真正显示出来),用户复测无效。改成模拟"最小化再还原"里真正起作用的
    // 那部分——**可见性**变化,而不是尺寸变化:`ShowWindow(SW_HIDE)` 紧接
    // `ShowWindow(SW_SHOW)`。隐藏/显示只发 `WM_SHOWWINDOW`,不发 `WM_SIZE`
    // (客户区尺寸从未改变),不会牵连 `OnSize`/`Relayout`;但会让 DWM 把这个
    // 窗口当作"重新变为可见"来处理,和最小化→还原触发的是同一类真实合成
    // 提交路径。`SWP_NOACTIVATE` 不适用于 `ShowWindow`,改用 `SW_SHOWNA`
    // 显示但不抢激活状态/焦点。
    BOOL enable = isDark ? TRUE : FALSE;
    HRESULT hr = DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &enable, sizeof(enable));
    if (hr != S_OK) {
        hr = DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkModeLegacy, &enable, sizeof(enable));
    }
    if (hr == S_OK) {
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                      SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // 滚动条改自绘(方案A,2026-09-18)后本调用已无可视效果,但 uxtheme 的
    // /DELAYLOAD 链接与 bench/CI 的模块加载核对(见 bench.cpp、
    // ci/verify_release.ps1)都还认这个符号,保留调用避免牵连一次不必要的
    // 构建系统改动。
    SetWindowTheme(hwnd, isDark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    if (IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
        ShowWindow(hwnd, SW_SHOWNA);
    }
}

/**
 * Cycle between Light and Dark theme, apply renderer palette and titlebar, and persist immediately.
 *
 * 切换浅色与深色主题,更新渲染器调色板与标题栏样式,并触发立即持久化。
 *
 * @param hwnd Window handle.
 *
 *   主窗口句柄。
 *
 * @param state Window state containing renderer, theme setting, and callbacks.
 *
 *   包含渲染器、主题设置与回调函数的窗口状态指针。
 */
void CycleTheme(HWND hwnd, WindowState* state) {
    if (!state || !state->renderer) return;
    state->themeSetting = NextThemeSetting(state->themeSetting);
    bool isDark = ResolveEffectiveTheme(state->themeSetting);
    state->renderer->SetPalette(isDark ? &kDarkPalette : &kLightPalette);
    ApplyTitleBarTheme(hwnd, isDark);
    InvalidateRect(hwnd, nullptr, FALSE);
    // 查找条 EDIT 子窗口是独立 HWND,上面这行 InvalidateRect 不会连带失效它;
    // 主题热切换时要单独让它重绘一次,否则背景色会停在切换前的旧主题。
    if (state->findEditHwnd) InvalidateRect(state->findEditHwnd, nullptr, TRUE);
    if (state->onWindowGeometryChanged) {
        state->onWindowGeometryChanged(state->callbackUserData);
    }
}

// 取窗口当前的 DPI 缩放系数(实际 DPI / 96);系统不支持按窗口查 DPI 时回退 1.0。
float DipScaleOf(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return 1.0f;
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    GetDpiForWindowFn getDpi = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
    if (!getDpi) return 1.0f;
    UINT dpi = getDpi(hwnd);
    if (dpi == 0) return 1.0f;
    return static_cast<float>(dpi) / static_cast<float>(kBaselineDpi);
}

/**
 * Reposition the native find-bar EDIT child to match the current
 * ComputeFindBarLayout geometry (client size / DPI changed).
 *
 * 按当前 ComputeFindBarLayout 几何重新定位查找条的原生 EDIT 子窗口
 * (客户区尺寸或 DPI 变化后调用)。
 *
 * @param hwnd 主窗口句柄。
 *
 *   主窗口句柄。
 *
 * @param state 运行期状态,findEditHwnd 为空时静默返回。
 *
 *   运行期状态,findEditHwnd 为空时静默返回。
 *
 * @example RepositionFindEdit(hwnd, state);
 */
void RepositionFindEdit(HWND hwnd, WindowState* state) {
    if (!state || !state->findEditHwnd) return;
    float scale = DipScaleOf(hwnd);
    FindBarLayout layout = ComputeFindBarLayout(ClientWidthDip(hwnd), 0.0f);
    int x = static_cast<int>(layout.editLeft * scale + 0.5f);
    int y = static_cast<int>(layout.editTop * scale + 0.5f);
    int w = static_cast<int>(layout.editWidth * scale + 0.5f);
    int h = static_cast<int>(layout.editHeight * scale + 0.5f);
    SetWindowPos(state->findEditHwnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);

    // EDIT 控件字号跟 DIP 缩放走,与 renderer.cpp 画的"查找:"前缀/状态文字
    // 用同一个 kFindBarFontSizeDip,DPI 变化(含窗口拖到不同显示器)时字号
    // 要跟着重算,否则会跟旁边 D2D 画的文字大小不一致。RepositionFindEdit
    // 本身在纯尺寸变化(WM_SIZE)时也会被频繁调用(拖边框时一秒内触发多次),
    // scale 没变就不用重新 CreateFontIndirectW/DeleteObject 一次 GDI 字体
    // 对象——按 findEditFontScale 缓存的上一次缩放系数判断是否真的需要重建。
    if (state->findEditFontScale != scale) {
        HFONT oldFont = reinterpret_cast<HFONT>(SendMessageW(state->findEditHwnd, WM_GETFONT, 0, 0));
        LOGFONTW lf{};
        lf.lfHeight = -static_cast<LONG>(kFindBarFontSizeDip * scale + 0.5f);
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        HFONT newFont = CreateFontIndirectW(&lf);
        if (newFont) {
            SendMessageW(state->findEditHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(newFont), TRUE);
            if (oldFont) DeleteObject(oldFont);
            state->findEditFontScale = scale;
        }
    }
}

/**
 * Close the find session and hide/reset the native EDIT child in one call,
 * so every VK_ESCAPE / Ctrl+W / document-swap close path stays in sync.
 *
 * 一次性关掉查找会话并隐藏/清空原生 EDIT 子窗口,让"关闭查找条"的每个触发
 * 点(Esc、Ctrl+W、换文档)都不会漏掉子窗口这一半状态。
 *
 * @param hwnd 主窗口句柄,焦点收回给它。
 *
 *   主窗口句柄,关闭后把键盘焦点收回给它。
 *
 * @param state 运行期状态,find/findEditHwnd 任一为空时对应部分静默跳过。
 *
 *   运行期状态,find/findEditHwnd 任一为空时对应部分静默跳过。
 *
 * @example CloseFindUi(hwnd, state);
 */
void CloseFindUi(HWND hwnd, WindowState* state) {
    if (!state) return;
    if (state->findEditHwnd) {
        // SetWindowTextW 会对原生 EDIT 控件同步派发 EN_CHANGE,命中 WM_COMMAND
        // 里的"查询串变了"分支并调用 FindSession::SetQuery,而 SetQuery 总是
        // 把 dirty_ 置回 true——如果先调用 find->Close()(dirty_=false)再执行
        // 这一步,关闭后 Dirty() 会被这次同步回调重新置脏。因此把 find->Close()
        // 挪到 SetWindowTextW 之后,确保"关闭 = 不脏"这个不变式在函数返回时
        // 始终成立(与 OpenFindUi 里 2026-09-19 那次评审记录的是同一个坑)。
        //
        // SetWindowTextW synchronously dispatches EN_CHANGE on a plain EDIT
        // control, hitting the "query changed" branch in WM_COMMAND, which
        // calls FindSession::SetQuery — and SetQuery always sets dirty_ back
        // to true. Calling find->Close() (dirty_=false) before this line
        // would let that synchronous callback re-dirty the session right
        // after closing. Moving find->Close() to run after SetWindowTextW
        // keeps the "closed implies not dirty" invariant true when this
        // function returns (the same pitfall recorded for OpenFindUi in the
        // 2026-09-19 review note above).
        SetWindowTextW(state->findEditHwnd, L"");
        ShowWindow(state->findEditHwnd, SW_HIDE);
    }
    if (state->find) state->find->Close();
    if (GetFocus() == state->findEditHwnd) SetFocus(hwnd);
}

/**
 * Open the find bar and hand keyboard focus to its native EDIT child, with
 * any existing query text selected. Shared by Ctrl+F and the bottom-bar
 * magnifying-glass button so both trigger the exact same behavior.
 *
 * 打开查找条并把键盘焦点交给原生 EDIT 子窗口,已有查询串全选——Ctrl+F 与
 * 底部栏放大镜按钮共用这一个函数,保证两个入口行为完全一致。
 *
 * @param hwnd 主窗口句柄。
 *
 *   主窗口句柄。
 *
 * @param state 运行期状态,find 为空时静默返回。
 *
 *   运行期状态,find 为空时静默返回。
 *
 * @example OpenFindUi(hwnd, state);
 */
void OpenFindUi(HWND hwnd, WindowState* state) {
    if (!state || !state->find) return;
    // 代码评审(2026-09-19)发现:Ctrl+F/放大镜按钮在查找条已经打开时会
    // 再次触发这里,SetWindowTextW 重写同样的查询串会同步派发 EN_CHANGE,
    // 命中 WM_COMMAND 里的"查询串变了就清空匹配列表"分支,把用户已经搜到
    // 的 x/y 结果清空成 0/0——查找条已经可见时提前返回,不重复这一整套
    // 初始化,只把焦点交回去、重选文字,与真正首次打开区分开。
    //
    // Code review (2026-09-19) found: Ctrl+F / the magnifier button re-enters
    // here even when the find bar is already open. Re-writing the same query
    // text via SetWindowTextW synchronously fires EN_CHANGE, hitting the
    // "query changed -> clear matches" branch in WM_COMMAND and wiping the
    // x/y result the user already had, down to 0/0. Bail out early once the
    // bar is already visible — just re-focus and re-select, don't repeat the
    // full open sequence.
    if (state->findEditHwnd && IsWindowVisible(state->findEditHwnd)) {
        SetFocus(state->findEditHwnd);
        SendMessageW(state->findEditHwnd, EM_SETSEL, 0, -1);
        return;
    }
    state->find->Open();
    state->statusMessage = nullptr;
    if (state->findEditHwnd) {
        RepositionFindEdit(hwnd, state);
        SetWindowTextW(state->findEditHwnd, state->find->Query());
        ShowWindow(state->findEditHwnd, SW_SHOW);
        SetFocus(state->findEditHwnd);
        SendMessageW(state->findEditHwnd, EM_SETSEL, 0, -1);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// T56:取一个近似的"新窗口所在显示器"DPI 缩放系数,用于把 kCascadeOffsetDip
// 换算成物理像素。窗口此刻尚未创建,拿不到 GetDpiForWindow;这里退而求其次
// 用 GetDpiForSystem(Win10 1607+ 起 user32 导出),与 DipScaleOf 同样的
// GetProcAddress 动态取址手法,系统不支持时回退 1.0——层叠偏移本来就是体验
// 优化,近似值不影响正确性,只是高 DPI 副屏上偏移量可能略有偏差。
float DipScaleForNewWindow() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return 1.0f;
    using GetDpiForSystemFn = UINT(WINAPI*)();
    GetDpiForSystemFn getDpi = reinterpret_cast<GetDpiForSystemFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForSystem")));
    if (!getDpi) return 1.0f;
    UINT dpi = getDpi();
    if (dpi == 0) return 1.0f;
    return static_cast<float>(dpi) / static_cast<float>(kBaselineDpi);
}

// T56:EnumDisplayMonitors 收集当前系统全部显示器的完整边界 + 工作区,
// 供 ClampWindowRectToMonitors 越界判定用。回调上下文放在栈上,不做堆分配。
struct MonitorCollectContext {
    MonitorRect* items;
    u32 cap;
    u32 count;
    u32 primaryIndex;
};

BOOL CALLBACK CollectMonitorProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lparam) {
    MonitorCollectContext* ctx = reinterpret_cast<MonitorCollectContext*>(lparam);
    if (ctx->count >= ctx->cap) return TRUE;  // 容量已满,静默截断,继续枚举完计数无意义但安全

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(hMonitor, &info)) return TRUE;

    MonitorRect rect{};
    rect.bounds = RectI{info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right,
                        info.rcMonitor.bottom};
    rect.workArea = RectI{info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom};
    if (info.dwFlags & MONITORINFOF_PRIMARY) ctx->primaryIndex = ctx->count;
    ctx->items[ctx->count++] = rect;
    return TRUE;
}

// 收集当前系统的显示器列表,返回实际数量;`*primaryIndexOut` 写入主显示器
// 在返回数组里的下标(找不到时保持 0——EnumDisplayMonitors 理论上总会枚举到
// 主显示器,这里只是防御性兜底)。
u32 CollectMonitors(MonitorRect* out, u32 cap, u32* primaryIndexOut) {
    MonitorCollectContext ctx{out, cap, 0, 0};
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitorProc, reinterpret_cast<LPARAM>(&ctx));
    if (primaryIndexOut) *primaryIndexOut = ctx.primaryIndex;
    return ctx.count;
}

// T56:按窗口类名枚举本程序已有窗口,取其矩形原点(左上角),用于层叠偏移的
// "同位置是否已有窗口"判定。不新增任何跨进程共享状态——纯粹是本机已存在
// 的窗口对象的一次性快照(裁决 #8 明文要求)。
struct WindowOriginCollectContext {
    RectI* items;
    u32 cap;
    u32 count;
};

BOOL CALLBACK CollectWindowOriginProc(HWND hwnd, LPARAM lparam) {
    WindowOriginCollectContext* ctx = reinterpret_cast<WindowOriginCollectContext*>(lparam);
    if (ctx->count >= ctx->cap) return TRUE;

    wchar_t className[64]{};
    if (GetClassNameW(hwnd, className, 64) == 0) return TRUE;
    if (wcscmp(className, kWindowClassName) != 0) return TRUE;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return TRUE;
    ctx->items[ctx->count++] = RectI{rc.left, rc.top, rc.right, rc.bottom};
    return TRUE;
}

u32 CollectExistingWindowOrigins(RectI* out, u32 cap) {
    WindowOriginCollectContext ctx{out, cap, 0};
    EnumWindows(CollectWindowOriginProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.count;
}

// T56:用 GetWindowPlacement(不是 GetWindowRect——最大化态下 GetWindowRect
// 拿到的是全屏矩形)把窗口当前的"还原态矩形 + 是否最大化"同步进 state,
// 供移动/缩放/退出时的持久化读取。
void UpdateWindowGeometryState(HWND hwnd, WindowState* state) {
    if (!state) return;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd, &wp)) return;

    const RECT& r = wp.rcNormalPosition;
    state->winX = r.left;
    state->winY = r.top;
    state->winW = r.right - r.left;
    state->winH = r.bottom - r.top;
    state->winMaximized = wp.showCmd == SW_SHOWMAXIMIZED;
}

// T56:移动/缩放后统一收尾——先同步一次当前矩形,再(重新)起 500ms 去抖
// 定时器;真正的写盘发生在定时器到点时(`WM_TIMER` 分支),这里不直接调用
// `onWindowGeometryChanged`,避免拖动窗口时几十次连续写盘。
void OnWindowGeometryMaybeChanged(HWND hwnd, WindowState* state) {
    if (!state) return;
    UpdateWindowGeometryState(hwnd, state);
    if (state->onWindowGeometryChanged) {
        SetTimer(hwnd, kWindowGeometryTimerId, kWindowGeometryDebounceMs, nullptr);
    }
}

// 客户区高度/宽度(DIP)的内部别名,实现见文件末尾的公开函数 ClientHeightDip /
// ClientWidthDip(T36 的窗口内换文档需要在 app 层拿到同一口径的视口尺寸)。
// 注意:这是"原始"客户区尺寸,不含内边距收窄——正文换行宽度/滚动范围计算
// 分别在使用处另外调用 ContentWidthDip / UsableViewportHeightDip 收窄。
// 底部操作栏(新需求)是"挤压"布局:正文视口高度恒定减去底部栏固定高度,
// 只在窗口创建/resize 时计算一次(与大纲侧栏"运行期动态浮动覆盖"不同,
// 侧栏开关是运行期事件、正文高度不能因它抖动;底部栏从窗口创建起就恒定
// 占用底部空间,不存在"运行期突然改变正文高度"的问题)。极窄窗口下钳到 0,
// 不产生负数视口高度。
float ViewportHeightOf(HWND hwnd) {
    float h = ClientHeightDip(hwnd) - kBottomBarHeightDip;
    return h > 0.0f ? h : 0.0f;
}
float ViewportWidthOf(HWND hwnd) { return ContentWidthDip(ClientWidthDip(hwnd)); }

// 滚动范围计算统一用的"可用视口高度"(原始视口高度收窄掉上下内边距),
// 喂给 ClampScrollOffset/MaxScrollOffset/ApplyScrollCommand 的 viewportHeight 参数
// 以及 UpdateVisibleRange 的可见区间宽度,滚到底时才会在文档下方留出内边距空白。
float UsableViewportHeightOf(HWND hwnd) { return UsableViewportHeightDip(ViewportHeightOf(hwnd)); }

// T29:在"释放全部 layout 并重排"之前,找到当前视口顶部对应的块下标——
// 取"top <= scrollY 的块中 top 最大的那个",重排后用同一个块下标的新 top
// 作为新的 scrollY,实现"按块下标恢复,不按像素"(裁决/验收标准原文)。
u32 FindTopBlockIndex(const BlockLayoutEngine& layout, float scrollY) {
    u32 best = 0;
    float bestTop = -1.0f;
    for (u32 i = 0; i < layout.BlockCount(); ++i) {
        float top = layout.Geometry(i).top;
        if (top <= scrollY && top > bestTop) {
            best = i;
            bestTop = top;
        }
    }
    return best;
}

// T33:图片解码完成后,若"按当前缓存重排会得到不同尺寸",触发一次重排。
// 重排时按块下标恢复滚动位置(与 T29 的 ApplyZoomChange 同一手法),因此
// 图片尺寸变化只会改变块的排布,不会让用户当前看的那个块跑掉——这是
// "滚动位置不因图片加载而跳动"的实现保证。
bool RelayoutForImagesIfNeeded(HWND hwnd, WindowState* state) {
    if (!state->layout || !state->doc || !state->fonts || !state->images) return false;
    if (!state->layout->ImagePlacementChanged(*state->images)) return false;

    u32 topBlock = FindTopBlockIndex(*state->layout, state->scrollY);
    state->layout->Relayout(*state->doc, ViewportWidthOf(hwnd), state->fonts->Scale(),
                            state->images);

    float usableViewportHeight = UsableViewportHeightOf(hwnd);
    float newScrollY =
        (topBlock < state->layout->BlockCount()) ? state->layout->Geometry(topBlock).top : 0.0f;
    state->scrollY =
        ClampScrollOffset(newScrollY, state->layout->TotalHeight(), usableViewportHeight);

    // Relayout 会淘汰全部 IDWriteTextLayout,必须在这里立刻按新几何重建一次,
    // 否则紧接着的这一帧会画成"只有图片、没有文字"。
    state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                      *state->fonts, state->residency);
    return true;
}

// T63:侧栏可视高度(DIP),侧栏纵向铺满整个客户区。
float OutlinePanelViewportHeightOf(HWND hwnd) { return ClientHeightDip(hwnd); }

// 前向声明:`ToggleOutlinePanel` 打开侧栏时要立即调一次(见下方定义与调用点注释)。
void RecomputeOutlineHighlight(HWND hwnd, WindowState* state);

// T63:`Ctrl+\` 的核心动作——抽屉式滑动与蒙层淡入淡出动画:
//   - 打开:在 outlineArena 上(惰性 Init,幂等)就地构造 OutlinePanel 提取大纲,
//     启动 Opening 动画定时器;若在收起动画中途触发,则从当前进度平滑反向展开。
//   - 关闭:启动 Closing 动画定时器,侧栏向左滑出且蒙层淡出;动画完成时才置空
//     state->outline 并 Reset Arena,维持"关闭时开销为 0"的设计。
void ToggleOutlinePanel(HWND hwnd, WindowState* state) {
    if (!state || !state->outlineArena) return;

    // Bench loop stress probe (T78): bypass animation for 30ms rapid automated loop.
    //
    // 内存泄漏自动化基准循环测试 (T78): 绕过动画以适配 30ms 极短节拍。
    if (state->benchLoopKind != 0) {
        if (state->outline) {
            state->outline = nullptr;
            state->outlineAnimState = OutlineAnimState::Closed;
            state->outlineAnimProgress = 0.0f;
            if (state->outlineArena) state->outlineArena->Reset();
            KillTimer(hwnd, kOutlineHighlightTimerId);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        state->outlineArena->Init(4 * 1024 * 1024);
        state->outlineArena->Reset();
        void* mem = state->outlineArena->Alloc(sizeof(OutlinePanel), alignof(OutlinePanel));
        if (!mem) return;
        OutlinePanel* panel = new (mem) OutlinePanel(state->outlineArena);
        if (state->doc) panel->Rebuild(*state->doc);
        state->outline = panel;
        state->outlineAnimState = OutlineAnimState::Open;
        state->outlineAnimProgress = 1.0f;
        RecomputeOutlineHighlight(hwnd, state);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (state->outlineAnimState == OutlineAnimState::Open ||
        state->outlineAnimState == OutlineAnimState::Opening) {
        // Start closing animation (smooth reversal if mid-flight).
        //
        // 启动收起动画 (支持动画进行中平滑反向收起)。
        state->outlineAnimState = OutlineAnimState::Closing;
        state->outlineAnimStartTick = GetTickCount64();
        state->outlineAnimStartProgress = state->outlineAnimProgress;
        KillTimer(hwnd, kOutlineHighlightTimerId);
        SetTimer(hwnd, kOutlineAnimTimerId, kOutlineAnimIntervalMs, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Currently Closed or Closing -> Start opening animation.
    //
    // 当前处于关闭或正在收起状态 -> 启动展开动画。
    if (state->outlineAnimState == OutlineAnimState::Closed) {
        // Init 幂等(已初始化过时静默返回 false,不重复预留地址空间);Reset 把
        // 上一次打开时用过的内容整体丢弃,避免反复开关侧栏时 Arena 无限增长。
        state->outlineArena->Init(4 * 1024 * 1024);
        state->outlineArena->Reset();
        void* mem = state->outlineArena->Alloc(sizeof(OutlinePanel), alignof(OutlinePanel));
        if (!mem) return;  // Arena 耗尽(4MB 极充裕),静默放弃
        OutlinePanel* panel = new (mem) OutlinePanel(state->outlineArena);
        if (state->doc) panel->Rebuild(*state->doc);
        state->outline = panel;
        RecomputeOutlineHighlight(hwnd, state);
        state->outlineAnimStartProgress = 0.0f;
        state->outlineAnimProgress = 0.0f;
    } else {
        // Reversing from Closing mid-flight.
        //
        // 在收起中途平滑反向展开。
        state->outlineAnimStartProgress = state->outlineAnimProgress;
    }

    // If history drawer is open/opening, smoothly close it
    if (state->historyAnimState == SidebarAnimState::Open ||
        state->historyAnimState == SidebarAnimState::Opening) {
        state->historyAnimState = SidebarAnimState::Closing;
        state->historyAnimStartTick = GetTickCount64();
        state->historyAnimStartProgress = state->historyAnimProgress;
        SetTimer(hwnd, kHistoryAnimTimerId, kOutlineAnimIntervalMs, nullptr);
    }

    state->outlineAnimState = OutlineAnimState::Opening;
    state->outlineAnimStartTick = GetTickCount64();
    SetTimer(hwnd, kOutlineAnimTimerId, kOutlineAnimIntervalMs, nullptr);
    InvalidateRect(hwnd, nullptr, FALSE);
}

/**
 * Toggle history sidebar drawer (right side) with slide animation.
 *
 * 切换右侧历史记录抽屉侧栏的展开/收起状态（带滑动与蒙层动画）。
 *
 * @param hwnd Window handle.
 *
 *   窗口句柄。
 *
 * @param state Pointer to WindowState.
 *
 *   指向窗口运行期状态的指针。
 */
void ToggleHistoryPanel(HWND hwnd, WindowState* state) {
    if (!state) return;

    if (state->historyAnimState == SidebarAnimState::Open ||
        state->historyAnimState == SidebarAnimState::Opening) {
        state->historyAnimState = SidebarAnimState::Closing;
        state->historyAnimStartTick = GetTickCount64();
        state->historyAnimStartProgress = state->historyAnimProgress;
        SetTimer(hwnd, kHistoryAnimTimerId, kOutlineAnimIntervalMs, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (state->historyAnimState == SidebarAnimState::Closed) {
        state->historyAnimStartProgress = 0.0f;
        state->historyAnimProgress = 0.0f;
        state->historyScrollY = 0.0f;
        state->historyHoverIndex = kInvalidIndex;
        if (state->historyPanelWidthDip <= 0.0f) {
            state->historyPanelWidthDip = kSidebarDefaultWidthDip;
        }
    } else {
        state->historyAnimStartProgress = state->historyAnimProgress;
    }

    // If outline drawer is open/opening, smoothly close it
    if (state->outlineAnimState == OutlineAnimState::Open ||
        state->outlineAnimState == OutlineAnimState::Opening) {
        state->outlineAnimState = OutlineAnimState::Closing;
        state->outlineAnimStartTick = GetTickCount64();
        state->outlineAnimStartProgress = state->outlineAnimProgress;
        KillTimer(hwnd, kOutlineHighlightTimerId);
        SetTimer(hwnd, kOutlineAnimTimerId, kOutlineAnimIntervalMs, nullptr);
    }

    state->historyAnimState = SidebarAnimState::Opening;
    state->historyAnimStartTick = GetTickCount64();
    SetTimer(hwnd, kHistoryAnimTimerId, kOutlineAnimIntervalMs, nullptr);
    InvalidateRect(hwnd, nullptr, FALSE);
}

/**
 * Handle user clicking on a recent file entry in history sidebar.
 * If file exists, launch in new instance; if missing, prompt to delete.
 *
 * 处理用户点击历史记录侧栏条目事件。
 * 若文件存在则新窗口打开；若不存在则弹窗询问是否删除。
 *
 * @param hwnd Window handle.
 *
 *   窗口句柄。
 *
 * @param state Pointer to WindowState.
 *
 *   指向窗口运行期状态的指针。
 *
 * @param item History entry index.
 *
 *   历史记录条目下标。
 */
void OnHistoryItemClicked(HWND hwnd, WindowState* state, u32 item) {
    if (!state || !state->recentFiles || item >= state->recentFiles->count) return;

    const wchar_t* path = state->recentFiles->entries[item].path;
    if (MarkdownFileExists(path)) {
        if (!LaunchNewInstance(path)) {
            state->statusMessage = L"打开新窗口失败";
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else {
        ConfirmAndRemoveMissingHistoryEntry(hwnd, state, item);
    }
}

/**
 * Open the folder containing a history entry's file in Explorer, with the
 * file itself selected. If the file no longer exists, falls back to the
 * exact same "file missing, delete from history?" flow as a normal failed
 * open (OnHistoryItemClicked's missing-file branch) — same failure handling,
 * not a separate dialog.
 *
 * 在系统文件管理器里打开某条历史条目文件所在的文件夹，并选中该文件。若文件
 * 已不存在，走与普通打开失败(OnHistoryItemClicked 的文件缺失分支)完全相同的
 * "文件不存在，是否从历史记录中删除？"流程——同一套失败处理，不另造弹窗。
 *
 * @param hwnd Window handle.
 *
 *   窗口句柄。
 *
 * @param state Pointer to WindowState.
 *
 *   指向窗口运行期状态的指针。
 *
 * @param item History entry index whose folder to open.
 *
 *   要打开所在文件夹的历史记录条目下标。
 */
void OnHistoryItemOpenFolderClicked(HWND hwnd, WindowState* state, u32 item) {
    if (!state || !state->recentFiles || item >= state->recentFiles->count) return;

    const wchar_t* path = state->recentFiles->entries[item].path;
    if (MarkdownFileExists(path)) {
        if (!OpenContainingFolderAndSelect(path)) {
            state->statusMessage = L"打开文件夹失败";
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else {
        ConfirmAndRemoveMissingHistoryEntry(hwnd, state, item);
    }
}

/**
 * Delete a history entry via its per-row hover close button. Unlike
 * OnHistoryItemClicked, this never opens the file and never confirms —
 * clicking the explicit "X" button is itself the confirmation.
 *
 * 通过某一行悬浮出现的关闭按钮删除该历史条目。与 OnHistoryItemClicked 不同,
 * 这里永不打开文件、也不弹确认框——点击这个明确的"X"按钮本身即是确认。
 *
 * @param hwnd Window handle.
 *
 *   窗口句柄。
 *
 * @param state Pointer to WindowState.
 *
 *   指向窗口运行期状态的指针。
 *
 * @param item History entry index to remove.
 *
 *   要删除的历史记录条目下标。
 */
void OnHistoryItemDeleteClicked(HWND hwnd, WindowState* state, u32 item) {
    if (!state || !state->recentFiles || item >= state->recentFiles->count) return;

    RemoveRecentFileAt(state->recentFiles, item);
    // 走去抖写盘,避免连续点击多行删除按钮时每次都同步读写一次磁盘。
    //
    // Route through the debounced save to avoid a synchronous disk
    // read/write on every click when the user deletes several rows in a row.
    RequestRecentFilesSave(hwnd, state);
    // 删除后原下标的行不再存在,悬浮态按旧下标去比对会指错行,直接清空。
    state->historyHoverIndex = kInvalidIndex;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// T63:去抖定时器到点后的高亮重算——在 T62 已有的、按块下标天然有序的标题
// 数组上二分查找当前应高亮的条目,变化时只 InvalidateRect 侧栏矩形(局部
// 重绘,不整窗失效)。
void RecomputeOutlineHighlight(HWND hwnd, WindowState* state) {
    if (!state || !state->outline || !state->layout) return;

    OutlinePanel* panel = state->outline;
    u32 count = panel->ItemCount();
    if (count == 0) return;

    // 按块下标取每条标题对应块的顶部 y(DIP),喂给纯函数二分查找。给一个
    // 够用的栈上缓冲(300 条是验收给的量级,侧栏本来也只是展示大纲,现实中
    // 不会有上万级标题的文档;超出部分只是不参与本次高亮计算,不影响其余
    // 条目正常显示,不崩溃)。
    constexpr u32 kMaxTopsOnStack = 4096;
    float tops[kMaxTopsOnStack];
    u32 n = (count < kMaxTopsOnStack) ? count : kMaxTopsOnStack;
    for (u32 i = 0; i < n; ++i) {
        u32 blockIdx = panel->Item(i).blockIdx;
        tops[i] = (blockIdx < state->layout->BlockCount()) ? state->layout->Geometry(blockIdx).top
                                                            : 0.0f;
    }

    u32 newCurrent = FindCurrentOutlineItem(tops, n, state->scrollY);
    if (newCurrent == panel->CurrentItem()) return;  // 没变化,不重绘

    panel->SetCurrentItem(newCurrent);

    // 局部重绘:只失效侧栏那一块矩形,不整窗失效。侧栏固定浮在客户区左上角,
    // 宽度按 DPI 缩放。
    float scale = DipScaleOf(hwnd);
    RECT rc{0, 0, static_cast<int>(state->outlinePanelWidthDip * scale + 0.5f),
            static_cast<int>((OutlinePanelViewportHeightOf(hwnd) + 2.0f * kContentPaddingDip) *
                             scale + 0.5f)};
    InvalidateRect(hwnd, &rc, FALSE);
}

// 滚动偏移变化后的统一收尾:任何触发滚动的路径(滚轮/键盘/查找跳转/换文档/
// 图片解码重排/自绘滚动条拖动)都应该走这里,保证:
//   ① scrollY 按当前视口/文档高度重新夹取一次(调用方传入的值未必已经夹过);
//   ② 刷新可见范围内的 IDWriteTextLayout / 图片解码位图并请求重绘。
// 滑块视觉位置由 Renderer::DrawScrollbar 每帧按 state->scrollY 现算,不需要
// 单独同步一步。偏移夹取后没有实际变化时跳过②,避免顶部/底部到界后仍反复重绘。
// forceRefresh:换文档这类"scrollY 数值可能凑巧没变、但 layout 已经整个换掉了"
// 的场景传 true,跳过"没变化就不刷新虚拟化"的短路判断。
void SetScrollY(HWND hwnd, WindowState* state, float newY, bool forceRefresh = false) {
    if (!state || !state->layout) return;

    float totalHeight = state->layout->TotalHeight();
    float usableViewportHeight = UsableViewportHeightOf(hwnd);
    float clamped = ClampScrollOffset(newY, totalHeight, usableViewportHeight);

    bool changed = forceRefresh || (clamped != state->scrollY);
    state->scrollY = clamped;

    // 虚拟化刷新只在偏移真正变化时才需要(顶部/底部到界后重复触发没有意义);
    // 但重绘请求总是发出——调用方可能是"当前命中变了但仍在同一屏"这种场景
    // (查找条前后跳转),视觉上仍需要刷新高亮颜色。
    if (changed && state->fonts) {
        state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                          *state->fonts, state->residency);
        RelayoutForImagesIfNeeded(hwnd, state);
    }
    InvalidateRect(hwnd, nullptr, FALSE);

    // T63:滚动路径里唯一允许出现的侧栏相关调用——只重置去抖定时器,不做任何
    // 二分查找/重绘(裁决 #6 的"滚动过程中零额外开销"就是靠这一行保证的)。
    if (state->outline) {
        SetTimer(hwnd, kOutlineHighlightTimerId, kOutlineHighlightDebounceMs, nullptr);
    }
}

// T35:把一次鼠标事件翻译成命中结果(链接 / 图片 / 什么都没命中)。
// 坐标换算与命中判定全部委托给 shell/hit_test.h 的纯函数,这里只负责取 DPI。
// 客户区物理像素坐标 -> 文档坐标(DIP)。内容整体因内边距向右下平移了
// kContentPaddingDip(渲染时的水平 SetTransform + effectiveScrollY),任何
// 命中判定都必须用这同一套换算,否则点击位置会和视觉内容错位。
DocPoint ClientToDocumentPoint(HWND hwnd, const WindowState* state, int px, int py) {
    float effectiveScrollY = state->scrollY - kContentPaddingDip;
    DocPoint p = ClientToDocument(px, py, DipScaleOf(hwnd), effectiveScrollY);
    p.x -= kContentPaddingDip;
    return p;
}

HitResult HitTestAtClientPoint(HWND hwnd, WindowState* state, int px, int py) {
    if (!state || !state->layout) {
        return HitResult{HitKind::None, kInvalidIndex, kInvalidIndex, kInvalidIndex};
    }
    DocPoint p = ClientToDocumentPoint(hwnd, state, px, py);
    return HitTestDocument(*state->layout, p.x, p.y);
}

// T45:鼠标位置落在哪个代码块的复制按钮上(纯矩形判定,不走 DirectWrite 文本
// 命中)。WM_MOUSEMOVE 是高频消息,悬浮态只需要这一份廉价判定,不必为此每次
// 都跑一遍完整的 HitTestDocument。
u32 CodeCopyButtonAtClientPoint(HWND hwnd, const WindowState* state, int px, int py) {
    if (!state || !state->layout) return kInvalidIndex;
    u32 count = state->layout->BlockCount();
    if (count == 0) return kInvalidIndex;
    DocPoint p = ClientToDocumentPoint(hwnd, state, px, py);
    return FindCodeCopyButtonAt(&state->layout->Geometry(0), count, p.x, p.y);
}

// T45:鼠标移动 -> 更新复制按钮悬浮态。**只在悬浮目标真正发生变化时才
// InvalidateRect**,否则在代码块上随便动一下鼠标就会全窗口重绘一次。
void UpdateCopyButtonHover(HWND hwnd, WindowState* state, int px, int py) {
    if (!state) return;
    u32 hover = CodeCopyButtonAtClientPoint(hwnd, state, px, py);
    if (hover == state->copyButtonHover) return;
    state->copyButtonHover = hover;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// T45:点击复制按钮 -> 拼出该代码块纯文本写进剪贴板,进入"已复制"反馈态,
// 并起一个一次性定时器在 kCopyFeedbackDurationMs 之后清掉反馈态。
// 复制失败(剪贴板被占用等)时不进反馈态,避免给出与事实不符的视觉确认。
void OnCodeCopyButtonClicked(HWND hwnd, WindowState* state, u32 blockIndex) {
    if (!state || !state->doc || !state->clipboardScratch) return;
    if (!CopyCodeBlockToClipboard(hwnd, *state->doc, blockIndex, state->clipboardScratch)) return;

    state->copyButtonCopied = blockIndex;
    // 同一个定时器 ID 重复 SetTimer 会重置计时(不会堆积多个定时器),
    // 因此连续点多个按钮时只有最后一次的反馈态,时长也从最后一次重新算。
    SetTimer(hwnd, kCopyFeedbackTimerId, kCopyFeedbackDurationMs, nullptr);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Bottom-bar "copy path" button -> copies the current document's full path,
// sharing the same "copied" feedback timer (kCopyFeedbackTimerId /
// kCopyFeedbackDurationMs) as the code-block copy buttons. The button is
// hidden and never hit-tested when no document is open, so the empty-path
// check here is defensive (should never actually be reached).
//
// 底部栏"复制路径"按钮 -> 复制当前文档全路径,与代码块复制按钮共用同一套
// "已复制"反馈态定时器(kCopyFeedbackTimerId/kCopyFeedbackDurationMs)。
// 未打开文档时该按钮不显示、也不会被命中测试选中,这里的空路径判断是
// 防御性的(理论上不会被调用到)。
void OnBottomBarCopyPathClicked(HWND hwnd, WindowState* state) {
    if (!state || state->currentDocumentPath[0] == 0) return;
    u32 len = static_cast<u32>(wcslen(state->currentDocumentPath));
    if (!SetClipboardUnicodeText(hwnd, state->currentDocumentPath, len)) return;

    state->bottomBarPathCopied = true;
    SetTimer(hwnd, kCopyFeedbackTimerId, kCopyFeedbackDurationMs, nullptr);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// T80:把一次鼠标事件的屏幕坐标翻译成"文档文本位置"(块下标 + 该块内
// UTF-16 偏移),供拖选起点/终点使用。与 HitTestAtClientPoint 共用同一套
// 坐标换算,只是命中判定换成 HitTestTextPosition(支持落在块间隙/文档
// 边界外时钳到最近块)。
DocTextHit TextPositionAtClientPoint(HWND hwnd, WindowState* state, int px, int py) {
    if (!state || !state->layout) return DocTextHit{false, kInvalidIndex, 0};
    DocPoint p = ClientToDocumentPoint(hwnd, state, px, py);
    return HitTestTextPosition(*state->layout, p.x, p.y);
}

// T80:Ctrl+C——把当前选区的纯文本拼好写进剪贴板。选区为空/没有绑定
// selectionScratch 时静默无效,不弹任何提示(与 T45 复制按钮失败态同一口径)。
void CopySelectionToClipboard(HWND hwnd, WindowState* state) {
    if (!state || !state->doc || !state->selection || !state->selectionScratch) return;
    if (!state->selection->HasSelection()) return;

    SelectionRange range = state->selection->Range();
    StrSlice text = SelectionPlainTextUtf8(*state->doc, range, state->selectionScratch);
    if (!text.data || text.len == 0) return;

    // 剪贴板的 CF_UNICODETEXT 就是 UTF-16,复用 T45 已验证过的转换工具。
    // Utf8ToUtf16 在同一块 selectionScratch 上分配,紧跟在 text 之后增长,
    // 不会覆盖 text 已经写好的内存(selectionScratch 全程只 Reset 一次)。
    Utf16Slice wide = Utf8ToUtf16(text, state->selectionScratch);
    if (!wide.data) return;
    SetClipboardUnicodeText(hwnd, wide.data, wide.len);
}

// 按命中结果取回对应的 ImageBox;不是图片命中时返回 nullptr。
const ImageBox* ImageBoxOfHit(const BlockLayoutEngine& layout, const HitResult& hit) {
    if (hit.kind != HitKind::Image || hit.blockIndex >= layout.BlockCount()) return nullptr;
    const BlockGeometry& g = layout.Geometry(hit.blockIndex);
    if (hit.imageIndex >= g.imageBoxes.len) return nullptr;
    return &g.imageBoxes[hit.imageIndex];
}

// 滚动到指定块的顶部(T36 的锚点跳转、T38 的命中跳转共用)。
// 顺序很重要:**先改滚动偏移,再让虚拟化按新位置补齐 layout** —— 目标块不在
// "可见 ± 1 屏"内时,靠的正是这一次 UpdateVisibleRange 补齐,而不是提前把
// 全文 layout 都实例化(那样就破坏了架构 §5 的虚拟化策略)。
void ScrollToBlock(HWND hwnd, WindowState* state, u32 blockIndex) {
    if (!state || !state->layout || blockIndex >= state->layout->BlockCount()) return;
    SetScrollY(hwnd, state, state->layout->Geometry(blockIndex).top);
}

// T64:点击大纲侧栏某一条 -> 滚动到对应标题块顶部。先把点击的客户区物理像素
// 换算成侧栏坐标系里的 y(与渲染层同一套换算,见 FindOutlineItemAtY 的文档),
// 查到条目对应的块下标后,直接复用 T36③ 锚点跳转、T38 查找跳转共用的
// ScrollToBlock —— 不另写一套滚动逻辑,也就自动继承了"先滚动再等虚拟化
// 补齐 layout"这条约束。
void OnOutlineItemClicked(HWND hwnd, WindowState* state, int clientY) {
    if (!state || !state->outline || !state->layout) return;

    float scale = DipScaleOf(hwnd);
    float localY = static_cast<float>(clientY) / (scale > 0.0f ? scale : 1.0f);

    OutlinePanel* panel = state->outline;
    u32 item = FindOutlineItemAtY(panel->ItemCount(), localY, panel->ScrollY());
    if (item == kInvalidIndex) return;

    ScrollToBlock(hwnd, state, panel->Item(item).blockIdx);
}

// T36b:点击一张图片 -> 打开它的原始数据。
// 本地路径图直接打开原文件;data: URI / 已下载的网络图把原始字节写临时文件后打开。
// 网络图片尚未下载时,这一次点击的含义是 T34 的"点击加载",不走打开原图分支。
void OnImageClicked(HWND hwnd, WindowState* state, const ImageBox& box) {
    // T34:网络图片且还没下载过 -> 这次点击是"加载这张图",只发一次请求。
    if (box.kind == LinkTargetKind::External && state->images && state->remote) {
        if (state->images->FindRemoteBytes(box.href, nullptr) == nullptr) {
            state->remote->RequestOnUserClick(box.href);
            return;
        }
    }

    if (!state->tempFiles) return;

    const void* rawBytes = nullptr;
    u32 rawLen = 0;
    const wchar_t* extension = nullptr;

    if (box.kind == LinkTargetKind::DataUri && state->imageScratch) {
        // data: URI:重新解一次 base64 拿原始(未降采样)字节,写临时文件后打开。
        state->imageScratch->Reset();
        DataUriPayload payload = ParseDataUri(box.href, state->imageScratch);
        if (!payload.valid || payload.len == 0) return;
        rawBytes = payload.bytes;
        rawLen = payload.len;
        extension = ExtensionForMime(payload.mime);
    } else if (box.kind == LinkTargetKind::External && state->images) {
        u32 len = 0;
        const u8* raw = state->images->FindRemoteBytes(box.href, &len);
        if (!raw) return;
        rawBytes = raw;
        rawLen = len;
        extension = L".img";  // 网络图片沿用通用扩展名,由系统按内容关联程序
    }

    OpenImageOriginal(box.href, box.kind, state->documentDirectory, rawBytes, rawLen,
                      extension, state->tempFiles);
    (void)hwnd;
}

// T65:把 src 拷进 dst(容量 cap,含结尾 '\0'),超长截断,不用 CRT 的
// wcsncpy_s——与本文件其余手写拷贝循环(如 ExtractDirectory)风格一致。
void CopyTruncatedPath(wchar_t* dst, u32 cap, const wchar_t* src) {
    if (cap == 0) return;
    dst[0] = L'\0';
    if (!src) return;
    u32 i = 0;
    for (; src[i] != L'\0' && i + 1 < cap; ++i) dst[i] = src[i];
    dst[i] = L'\0';
}

// T36:点击一个链接 -> 按 DecideLinkAction 的判定分流到三类行为。
// 安全边界(非 http/https/mailto/file 的 scheme)已经在判定里挡掉,这里不会
// 出现任何"先执行再检查"的路径。
void OnLinkClicked(HWND hwnd, WindowState* state, u32 linkTargetIdx) {
    if (!state || !state->doc) return;
    if (linkTargetIdx == kInvalidIndex || linkTargetIdx >= state->doc->linkTargets.Size()) return;

    StrSlice href = state->doc->linkTargets[linkTargetIdx].href;
    switch (DecideLinkAction(href)) {
    case LinkAction::OpenExternal:
        // ① 外链交给系统默认浏览器,不内嵌任何浏览。
        OpenExternalTarget(href);
        return;

    case LinkAction::ScrollToAnchor: {
        // ③ 页内锚点:GitHub 式 slug 匹配标题块,滚动到该块顶部。
        u32 blockIndex = FindAnchorBlock(*state->doc, href);
        if (blockIndex == kInvalidIndex) {
            state->statusMessage = L"未找到该锚点";
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        state->statusMessage = nullptr;
        // T65:锚点跳转改变了滚动位置,算一次导航——记"跳转前"的
        // (当前路径, 当前 scrollY),与随后同路径下不同的 scrollY 构成
        // "同路径两条记录"。跳转本身不换文档,currentDocumentPath 不变。
        if (state->history) state->history->PushNavigation(state->currentDocumentPath, state->scrollY);
        ScrollToBlock(hwnd, state, blockIndex);
        return;
    }

    case LinkAction::OpenMarkdown: {
        // ② 相对/绝对 .md 路径:规范化后在当前窗口内替换文档(裁决 #6)。
        wchar_t fullPath[MAX_PATH * 2]{};
        if (!ResolveMarkdownPath(href, state->documentDirectory, fullPath, MAX_PATH * 2) ||
            !MarkdownFileExists(fullPath)) {
            // 路径不存在只在窗口内提示,不弹 MessageBox(架构 §9)。
            state->statusMessage = L"链接指向的文件不存在";
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (!state->openDocumentInPlace) return;

        // T65:先记下"跳转前"的路径/scrollY——一旦 openDocumentInPlace 成功,
        // state->currentDocumentPath 就会被下面覆盖,必须提前存一份副本。
        wchar_t oldPath[kHistoryPathCapacity];
        CopyTruncatedPath(oldPath, kHistoryPathCapacity, state->currentDocumentPath);
        float oldScrollY = state->scrollY;

        bool ok = state->openDocumentInPlace(state->callbackUserData, fullPath);
        state->statusMessage = ok ? nullptr : L"打开文档失败";
        if (ok) {
            // 只有真正切换成功才记这一笔导航,并清空前进栈(裁决:F5 之类的
            // "重新打开同一文档但不算导航"不会走到这里,因为那条路径完全
            // 不调用 PushNavigation,见 history.h 顶部注释)。
            if (state->history) state->history->PushNavigation(oldPath, oldScrollY);
            CopyTruncatedPath(state->currentDocumentPath, kHistoryPathCapacity, fullPath);
            // 新文档从头开始看;旧文档的查找结果指向的是旧的块下标,必须一并作废。
            CloseFindUi(hwnd, state);
            // T45:复制按钮的悬浮/已复制态同样是按旧文档的块下标记的,换文档后
            // 那个下标在新文档里可能是别的块,必须一并作废。
            state->copyButtonHover = kInvalidIndex;
            state->copyButtonCopied = kInvalidIndex;
            KillTimer(hwnd, kCopyFeedbackTimerId);
            // forceRefresh=true:新文档的 layout 已整个换掉,即使数值上恰好还是
            // 0.0f 也必须重新跑一次 UpdateVisibleRange,不能被"没变化"短路掉。
            SetScrollY(hwnd, state, 0.0f, /*forceRefresh=*/true);
            return;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    case LinkAction::Reject:
    default:
        // javascript: 之类的目标:什么都不做,连提示都不给(不给可疑链接任何反馈)。
        return;
    }
}

// T65:后退/前进导航共用的核心步骤,由 WM_SYSKEYDOWN 的 Alt+←/→ 分支调用。
// direction 为 true 时是"后退"(操作后退栈,成功后把跳转前状态压进前进栈),
// 为 false 时是"前进"(反过来)。两者除了栈的角色互换外完全对称,合成一份
// 实现避免复制粘贴出两套走样的逻辑。
void NavigateHistoryDirection(HWND hwnd, WindowState* state, bool isBack) {
    if (!state || !state->history) return;

    HistoryEntry entry;
    bool popped = isBack ? state->history->PopBack(&entry) : state->history->PopForward(&entry);
    if (!popped) return;  // 栈为空,无动作

    if (!MarkdownFileExists(entry.path)) {
        // T65 ④:目标文件已不存在——窗口内提示,记录已经在 Pop 里被摘掉了,
        // 不弹 MessageBox,也不做任何"放回栈里"的补救。
        state->statusMessage = L"该历史记录指向的文件已不存在";
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    if (!state->openDocumentInPlace) return;

    wchar_t oldPath[kHistoryPathCapacity];
    CopyTruncatedPath(oldPath, kHistoryPathCapacity, state->currentDocumentPath);
    float oldScrollY = state->scrollY;

    bool ok = state->openDocumentInPlace(state->callbackUserData, entry.path);
    if (!ok) {
        state->statusMessage = L"打开文档失败";
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // 打开成功后才把"跳转前"的状态压回对侧栈——不清空另一侧,这不是一次
    // 新导航,只是把刚离开的那份状态存起来供反方向的快捷键用。
    if (isBack) state->history->PushForwardRaw(oldPath, oldScrollY);
    else state->history->PushBackRaw(oldPath, oldScrollY);

    CopyTruncatedPath(state->currentDocumentPath, kHistoryPathCapacity, entry.path);
    state->statusMessage = nullptr;
    CloseFindUi(hwnd, state);
    state->copyButtonHover = kInvalidIndex;
    state->copyButtonCopied = kInvalidIndex;
    KillTimer(hwnd, kCopyFeedbackTimerId);
    SetScrollY(hwnd, state, entry.scrollY, /*forceRefresh=*/true);
}

// `Alt+←`:回到后退栈栈顶记录的那个位置。
void NavigateHistoryBack(HWND hwnd, WindowState* state) {
    NavigateHistoryDirection(hwnd, state, /*isBack=*/true);
}

// `Alt+→`:回到前进栈栈顶记录的那个位置。
void NavigateHistoryForward(HWND hwnd, WindowState* state) {
    NavigateHistoryDirection(hwnd, state, /*isBack=*/false);
}

// T70:F5 手动重载当前文档——重新走一遍 T36 的 openDocumentInPlace,不新写
// 一条加载路径,也不引入任何 IO 线程/目录监听(阶段 Q 的取舍原文)。
// 与 Alt+←/→ 的关键区别:①不摸 state->history 的任何 Push* 接口(F5 重新
// 加载的是同一份文档,不是一次导航,history.h 顶部注释已经写死这条约束);
// ②滚动位置按"重载前视口顶部对应的块下标"近似恢复,而不是按 scrollY 像素
// 值——文档被外部编辑器改过之后块下标当然可能对不上,但"大致回到刚才那
// 一段"已经够用,故意不做 diff/最长公共子序列匹配(那需要新旧文档同时驻留
// 内存,与本项目"零常驻开销"的取舍相反,不要为此"优化")。
void ReloadCurrentDocument(HWND hwnd, WindowState* state) {
    if (!state || !state->openDocumentInPlace) return;
    if (state->currentDocumentPath[0] == L'\0') return;

    // 重载前先记下视口顶部的块下标(局部变量,不新增任何成员状态)。
    u32 topBlockIdx = 0;
    if (state->layout) topBlockIdx = FindTopBlockIndex(*state->layout, state->scrollY);

    wchar_t path[kHistoryPathCapacity];
    CopyTruncatedPath(path, kHistoryPathCapacity, state->currentDocumentPath);

    // 先检查文件是否还在——与 OnLinkClicked/NavigateHistoryDirection 同一
    // 口径:不存在就不调用 openDocumentInPlace,从而避开它内部"打开失败就把
    // doc 换成空文档再 Relayout"的分支(那个分支是给"路径本来就不合法"用
    // 的,F5 这里必须保证失败路径下旧内容原样保留,不能先释放旧文档)。
    if (!MarkdownFileExists(path)) {
        state->statusMessage = L"文件已不存在或无法访问,内容保持不变";
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    bool ok = state->openDocumentInPlace(state->callbackUserData, path);
    if (!ok) {
        state->statusMessage = L"打开文档失败,内容保持不变";
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    state->statusMessage = nullptr;
    // 查找结果与复制按钮悬浮/已复制态都是按旧文档的块下标记的,重载后
    // 那个下标在新文档里可能指向别的块,必须一并作废(与换文档同一口径)。
    CloseFindUi(hwnd, state);
    state->copyButtonHover = kInvalidIndex;
    state->copyButtonCopied = kInvalidIndex;
    KillTimer(hwnd, kCopyFeedbackTimerId);

    // 按块下标钳制到新文档范围内近似恢复位置;新文档 0 个块时滚到顶部。
    float newScrollY = 0.0f;
    u32 blockCount = state->layout ? state->layout->BlockCount() : 0;
    if (blockCount > 0) {
        u32 clamped = ClampReloadTopBlockIndex(topBlockIdx, blockCount);
        newScrollY = state->layout->Geometry(clamped).top;
    }
    SetScrollY(hwnd, state, newScrollY, /*forceRefresh=*/true);
}

// T37/T38:按当前查询串重搜并跳到第一处命中。
void RerunFind(HWND hwnd, WindowState* state) {
    if (!state || !state->find || !state->doc) return;
    state->find->Rerun(*state->doc);
    const Match* m = state->find->CurrentMatch();
    if (m) {
        ScrollToBlock(hwnd, state, m->blockIdx);
    } else {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

// T38:跳到上一处/下一处命中并滚动过去。
void StepFind(HWND hwnd, WindowState* state, bool forward) {
    if (!state || !state->find) return;
    bool moved = forward ? state->find->GoNext() : state->find->GoPrev();
    if (!moved) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    const Match* m = state->find->CurrentMatch();
    if (m) ScrollToBlock(hwnd, state, m->blockIdx);
}

// T34:网络图片下载完成(工作线程 PostMessage 回主线程)。
// 把原始压缩字节登记进缓存去重,立刻解码一次,必要时重排后重绘。
void OnRemoteImageDone(HWND hwnd, WindowState* state, RemoteImageResult* result) {
    if (!result) return;
    if (state && state->images && result->succeeded && result->bytes && result->len > 0) {
        state->images->PutRemoteBytes(result->url, result->bytes, result->len);
    } else if (state && state->images) {
        // 下载失败:切到"加载失败"占位,而不是永远停在"点击加载"。
        state->images->Put(result->url, nullptr, 0, 0, ImageStatus::Failed, false);
    }
    ReleaseRemoteImageResult(result);

    if (state && state->layout && state->fonts) {
        float usableViewportHeight = UsableViewportHeightOf(hwnd);
        state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                          *state->fonts, state->residency);
        RelayoutForImagesIfNeeded(hwnd, state);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 把虚拟键码翻译成滚动动作;不是滚动键时返回 false。
bool ScrollCommandFromVirtualKey(WPARAM vk, ScrollCommand* out) {
    switch (vk) {
    case VK_PRIOR: *out = ScrollCommand::PageUp;   return true;
    case VK_NEXT:  *out = ScrollCommand::PageDown; return true;
    case VK_HOME:  *out = ScrollCommand::Home;     return true;
    case VK_END:   *out = ScrollCommand::End;      return true;
    case VK_UP:    *out = ScrollCommand::LineUp;   return true;
    case VK_DOWN:  *out = ScrollCommand::LineDown; return true;
    default:       return false;
    }
}

// 按当前滚动偏移画一帧:先按可见范围刷新 layout 虚拟化,再委托 Renderer 绘制。
// 首次绘制成功后触发一次 onFirstPresent 回调(T14 性能埋点用,为空时零开销)。
void PaintOnce(HWND hwnd, WindowState* state) {
    if (!state || !state->renderer) return;
    if (!state->layout || !state->doc || !state->fonts) return;

    if (state->onFrameBegin) state->onFrameBegin(state->callbackUserData);

    // 先把渲染目标建起来,再跑虚拟化 —— 图片解码需要绑定渲染目标创建位图,
    // 否则首帧会白解一次却建不出位图(见 Renderer::EnsureTarget 的注释)。
    state->renderer->EnsureTarget(hwnd);

    float usableViewportHeight = UsableViewportHeightOf(hwnd);
    state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                      *state->fonts, state->residency);
    // T33:首次解码出真实尺寸后,若与占位尺寸不同就在这里做一次性重排,
    // 之后同一批图片不会再触发(ImagePlacementChanged 会返回 false)。
    RelayoutForImagesIfNeeded(hwnd, state);

    // T42(bench 专用):首屏解码完之后,若开启了"强制全量解码",再对整份文档
    // 补一次 UpdateVisibleRange,把首屏之外的图片也解码一遍,近似"滚到底"的
    // 内存读数;只在 benchForceFullDecode 且尚未触发过首帧上报时执行一次,
    // 不影响正常交互场景下的虚拟化行为。
    if (state->benchForceFullDecode && !state->firstPresentDone && state->layout) {
        state->layout->UpdateVisibleRange(0.0f, state->layout->TotalHeight(), *state->fonts,
                                          state->residency);
        RelayoutForImagesIfNeeded(hwnd, state);
    }

    if (state->onFrameLayoutDone) state->onFrameLayoutDone(state->callbackUserData);

    // T37/T38:把查找命中集合与查找条/提示条打包成只读视图交给渲染层;
    // 两者都没有时传 nullptr,渲染层零额外开销。
    ShellOverlay overlay{};
    overlay.copyButtonHoverBlock = kInvalidIndex;
    overlay.copyButtonCopiedBlock = kInvalidIndex;
    const ShellOverlay* overlayPtr = nullptr;
    bool hasCopyButtonState =
        state->copyButtonHover != kInvalidIndex || state->copyButtonCopied != kInvalidIndex;
    bool hasSelection = state->selection && state->selection->HasSelection();
    bool hasHistory = state->recentFiles && state->historyAnimState != SidebarAnimState::Closed;
    if (state->find || state->statusMessage || hasCopyButtonState || state->outline ||
        hasSelection || hasHistory) {
        // T80:鼠标拖选高亮,选区为空时保持聚合初始化留下的 false/0,渲染层
        // 不画任何高亮,零额外开销。
        if (hasSelection) {
            SelectionRange range = state->selection->Range();
            overlay.selectionActive = true;
            overlay.selStartBlock = range.start.blockIndex;
            overlay.selStartOffset = range.start.charOffset;
            overlay.selEndBlock = range.end.blockIndex;
            overlay.selEndOffset = range.end.charOffset;
        }
        // T45:复制按钮的悬浮/已复制态同样通过叠加层视图交给渲染层,渲染层
        // 因此不需要认识"外壳层状态"这个概念(与查找高亮同一条通路)。
        overlay.copyButtonHoverBlock = state->copyButtonHover;
        overlay.copyButtonCopiedBlock = state->copyButtonCopied;
        if (state->find) {
            Span<const Match> matches = state->find->Matches();
            overlay.matches = matches.data;
            overlay.matchCount = matches.len;
            overlay.currentMatch = state->find->CurrentIndex();
            overlay.findBarVisible = state->find->Visible();
            overlay.findQuery = state->find->Query();
        } else {
            overlay.currentMatch = kInvalidIndex;
        }
        overlay.doc = state->doc;
        overlay.statusMessage = state->statusMessage;
        // T63:大纲侧栏关闭时(state->outline == nullptr)以下字段保持零初始化,
        // 渲染层据此判断"不画侧栏",零额外开销;打开时把条目数组/高亮下标/
        // 自身滚动偏移原样转交渲染层,渲染层只读,不拥有。
        if (state->outline) {
            Span<const OutlineItem> items = state->outline->Items();
            overlay.outlineItems = items.data;
            overlay.outlineItemCount = items.len;
            overlay.outlineCurrentItem = state->outline->CurrentItem();
            overlay.outlineScrollY = state->outline->ScrollY();
            overlay.outlinePanelWidthDip = state->outlinePanelWidthDip;
            overlay.outlineAnimProgress = state->outlineAnimProgress;
            // 悬浮或正在拖动都算 Active(见 theme.h 的 Idle/Active 两档透明度)。
            overlay.outlineScrollbarActive =
                state->outlineScrollbarHover ||
                state->scrollbarDragTarget == ScrollbarDragTarget::Outline;
        } else {
            overlay.outlineCurrentItem = kInvalidIndex;
            overlay.outlineAnimProgress = 0.0f;
        }

        // 历史记录侧栏 (右侧抽屉)
        if (hasHistory) {
            overlay.historyEntries = state->recentFiles ? state->recentFiles->entries : nullptr;
            overlay.historyItemCount = state->recentFiles ? state->recentFiles->count : 0u;
            overlay.historyHoverItem = state->historyHoverIndex;
            overlay.historyScrollY = state->historyScrollY;
            overlay.historyPanelWidthDip = state->historyPanelWidthDip;
            overlay.historyAnimProgress = state->historyAnimProgress;
            overlay.historyScrollbarActive =
                state->historyScrollbarHover ||
                state->scrollbarDragTarget == ScrollbarDragTarget::History;
        } else {
            overlay.historyHoverItem = kInvalidIndex;
            overlay.historyAnimProgress = 0.0f;
        }

        overlayPtr = &overlay;
    }
    // 内容整体向下推 kContentPaddingDip 实现"上边距":RenderFrame 内部各 DrawXxx
    // 早就在算 `g.top - scrollY`,传一个减去内边距的 scrollY 即等效于内容下移。
    float effectiveScrollY = state->scrollY - kContentPaddingDip;
    // 悬浮或正在拖动都算 Active(见 theme.h 的 Idle/Active 两档透明度);
    // 侧栏打开时正文滚动条本来就不画,这个值被 RenderFrame 忽略。
    bool mainScrollbarActive =
        state->mainScrollbarHover || state->scrollbarDragTarget == ScrollbarDragTarget::Main;
    // 底部栏右侧状态区(2026-09-18 新增):当前文档路径 + 大小,未打开文件时
    // currentDocumentPath 是空字符串,渲染层据此不画任何文字。悬浮提示气泡
    // 同批传入:bottomBarHoverButton 转成裸下标,render 层不认识这个枚举。
    bool presented = state->renderer->RenderFrame(
        hwnd, *state->layout, effectiveScrollY, kContentPaddingDip, overlayPtr,
        mainScrollbarActive, state->currentDocumentPath, state->currentDocumentSizeBytes,
        static_cast<u32>(state->bottomBarHoverButton), state->bottomBarPathCopied);

    if (state->onFrameEnd) state->onFrameEnd(state->callbackUserData);

    if (presented && !state->firstPresentDone) {
        state->firstPresentDone = true;
        if (state->onFirstPresent) state->onFirstPresent(state->callbackUserData);
    }
}

// 客户区尺寸变化:同步渲染目标尺寸、按新视口宽度重跑布局(不重解析),
// 并把滚动偏移重新夹到新的合法区间(窗口变高时文档可能已不足以滚那么远)。
void OnSize(HWND hwnd, WindowState* state, UINT32 width, UINT32 height) {
    if (!state) return;

    if (state->renderer) {
        state->renderer->OnResize(width, height);
    }
    if (state->layout && state->doc) {
        // 视口宽度变化只重跑布局,不重新解析(架构 §5/§9)。布局用 DIP,故先按 DPI 换算,
        // 换行宽度再收窄掉左右内边距(ContentWidthDip)。
        float scale = DipScaleOf(hwnd);
        float fontScale = state->fonts ? state->fonts->Scale() : 1.0f;
        float contentWidth = ContentWidthDip(static_cast<float>(width) / scale);
        state->layout->Relayout(*state->doc, contentWidth, fontScale, state->images);
        // 滚动范围夹取同样要用"可用视口高度"(收窄掉上下内边距),口径与
        // ClampScrollOffset 的其余调用点一致,滚到底才会正确留出底部内边距空白。
        float rawViewportHeight = static_cast<float>(height) / scale - kBottomBarHeightDip;
        if (rawViewportHeight < 0.0f) rawViewportHeight = 0.0f;
        float usableViewportHeight = UsableViewportHeightDip(rawViewportHeight);
        state->scrollY = ClampScrollOffset(state->scrollY, state->layout->TotalHeight(),
                                           usableViewportHeight);
    }
    // 查找条 EDIT 子窗口跟客户区宽度绑定(贴右上角),尺寸变化要重新定位;
    // RepositionFindEdit 内部对 findEditHwnd 为空静默返回,不额外判断可见性
    // (隐藏态重定位没有副作用,下次 Show 时位置已经是对的)。
    RepositionFindEdit(hwnd, state);
    // T56:尺寸变化(含最大化/还原)去抖后写盘;放在这里而不是只放 WM_MOVE,
    // 因为纯拖边框改尺寸不会触发 WM_MOVE。
    OnWindowGeometryMaybeChanged(hwnd, state);
    // 强制整个客户区重绘:窗口变大时,Windows 会为新露出的区域自动生成
    // WM_PAINT,让人误以为"重排生效了";窗口变小时没有新露出的区域,系统
    // 不会自动触发 WM_PAINT,若这里不主动 Invalidate,画面会停留在旧的
    // (更宽的)布局上,被新的窄窗口边框直接裁切,表现为文字截断。
    InvalidateRect(hwnd, nullptr, FALSE);
}

// T29:字号缩放变更后的统一收尾。调用前调用方须已经调过
// FontSubsystem::ZoomIn()/ZoomOut()/ResetZoom() 之一,使新档位生效
// (释放旧 IDWriteTextFormat 并重建);这里负责:记住当前视口顶部块 ->
// 释放全部 IDWriteTextLayout 并按新字号重排 -> 用同一个块下标的新 top
// 恢复滚动位置("按块下标恢复,不按像素")-> 刷新虚拟化范围并请求重绘。
void ApplyZoomChange(HWND hwnd, WindowState* state) {
    if (!state || !state->layout || !state->doc || !state->fonts) return;

    u32 topBlock = FindTopBlockIndex(*state->layout, state->scrollY);

    state->layout->Relayout(*state->doc, ViewportWidthOf(hwnd), state->fonts->Scale(),
                            state->images);

    float usableViewportHeight = UsableViewportHeightOf(hwnd);
    float newScrollY =
        (topBlock < state->layout->BlockCount()) ? state->layout->Geometry(topBlock).top : 0.0f;
    state->scrollY =
        ClampScrollOffset(newScrollY, state->layout->TotalHeight(), usableViewportHeight);
    state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                      *state->fonts, state->residency);
    InvalidateRect(hwnd, nullptr, FALSE);
    // T57:字号缩放持久化(接过 M1 T29 的挂账)。复用 T56 的
    // onWindowGeometryChanged 钩子而不是新起一套回调——调用方(main.cpp)的
    // 钩子实现里已经会顺带读取 state->fonts->Scale() 一起写出,两个持久化
    // 项走同一条 T55"读-改-写 + 命名互斥体"通道,不绕过它。缩放是离散按键
    // 触发、不连续,不需要像窗口拖拽那样去抖。
    if (state->onWindowGeometryChanged) state->onWindowGeometryChanged(state->callbackUserData);
}

// 底部操作栏(新需求):6 个按钮各自的动作全部复用现有函数,不另写一套——
// 放大/缩小复用 T29 的 FontSubsystem::ZoomIn/ZoomOut + ApplyZoomChange(与
// Ctrl+± 同一路径),主题复用 T47 的 CycleTheme(与 Ctrl+Shift+T 同一路径),
// 大纲复用 T63 的 ToggleOutlinePanel(与 Ctrl+\ 同一路径)。"打开文档"是唯一
// 新增行为:弹出 IFileOpenDialog,选中后用 CreateProcessW 新开一个独立
// mdvn.exe 进程——不调用 openDocumentInPlace,不替换当前正在看的文档。
void OnBottomBarButtonClicked(HWND hwnd, WindowState* state, BottomBarButton btn) {
    if (!state) return;
    switch (btn) {
    case BottomBarButton::ZoomIn:
        if (state->fonts) {
            state->fonts->ZoomIn();
            ApplyZoomChange(hwnd, state);
        }
        return;
    case BottomBarButton::ZoomOut:
        if (state->fonts) {
            state->fonts->ZoomOut();
            ApplyZoomChange(hwnd, state);
        }
        return;
    case BottomBarButton::Theme:
        CycleTheme(hwnd, state);
        return;
    case BottomBarButton::Find:
        OpenFindUi(hwnd, state);
        return;
    case BottomBarButton::Outline:
        ToggleOutlinePanel(hwnd, state);
        return;
    case BottomBarButton::OpenDoc: {
        wchar_t path[MAX_PATH]{};
        if (!ShowOpenMarkdownDialog(hwnd, path, MAX_PATH)) return;  // 用户取消,静默无行为
        if (!LaunchNewInstance(path)) {
            state->statusMessage = L"打开新窗口失败";
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }
    case BottomBarButton::CopyPath:
        OnBottomBarCopyPathClicked(hwnd, state);
        return;
    case BottomBarButton::History:
        ToggleHistoryPanel(hwnd, state);
        return;
    case BottomBarButton::None:
    default:
        return;
    }
}

// DPI 变化:先应用系统建议的新窗口矩形,再通知渲染器按新 DPI 重建 D2D 资源。
void OnDpiChanged(HWND hwnd, WindowState* state, UINT newDpi, const RECT* suggested) {
    if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (state && state->renderer) {
        state->renderer->OnDpiChanged(static_cast<float>(newDpi));
    }
    RepositionFindEdit(hwnd, state);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 大纲/历史侧栏展开或正在动画时,渲染层的半透明蒙层会整块盖住底部栏(裁决:
// 蒙层应完整遮住正文可交互区域,底部栏也不例外,见 renderer.cpp 对应注释)。
// WM_LBUTTONDOWN 与 WM_MOUSEMOVE 的底部栏命中测试都要用同一个判断,抽成
// 一个函数而不是各自重复一遍这个布尔表达式——代码评审(2026-09-19)发现
// WM_MOUSEMOVE 那份复制漏掉了这个判断,导致蒙层盖住底部栏期间悬浮态/光标
// 仍显示成"可点按钮",与实际点击行为(关闭侧栏)不一致。
//
// While the outline/history sidebar is open or animating, the renderer's
// translucent mask fully covers the bottom bar (per decision: the mask
// should completely cover the interactive content area, bottom bar
// included — see the matching comment in renderer.cpp). Both
// WM_LBUTTONDOWN and WM_MOUSEMOVE need this same check for the bottom bar
// hit-test; factored into one function instead of each repeating the
// boolean expression — code review (2026-09-19) found the WM_MOUSEMOVE
// copy was missing this check, so hover feedback/cursor still showed a
// "clickable button" while the mask covered it, disagreeing with the
// actual click behavior (closes the sidebar).
bool SidebarMaskCoversBottomBar(const WindowState* state) {
    return state && ((state->outline && state->outlineAnimState != OutlineAnimState::Closed) ||
                      state->historyAnimState != SidebarAnimState::Closed);
}

// 主窗口过程:绘制、尺寸/DPI 变化、滚轮与键盘滚动、Ctrl+W / Esc 关闭。
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        // 创建时把调用方传入的 WindowState 绑定到窗口上,后续消息直接取用。
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    WindowState* state = StateOf(hwnd);

    switch (msg) {
    case WM_PAINT:
        PaintOnce(hwnd, state);
        ValidateRect(hwnd, nullptr);
        return 0;

    case WM_SIZE:
        OnSize(hwnd, state, LOWORD(lparam), HIWORD(lparam));
        return 0;

    case WM_MOVE:
        // T56:纯移动(不改尺寸)也要去抖后写盘。
        OnWindowGeometryMaybeChanged(hwnd, state);
        return 0;

    case WM_DPICHANGED:
        OnDpiChanged(hwnd, state, LOWORD(wparam), reinterpret_cast<const RECT*>(lparam));
        return 0;

    case WM_SETTINGCHANGE: {
        // System light/dark setting changed broadcast by Explorer ("ImmersiveColorSet").
        // Theme only has Light and Dark now; system theme is only read once at software
        // initialization. Runtime OS theme change does not alter app palette.
        //
        // Explorer 广播的系统深浅色切换消息("ImmersiveColorSet")。
        // 当前主题仅有亮色和暗色,系统色仅在软件初始化时读取一次,运行期系统变色不再联动改变程序主题。
        const wchar_t* settingName = reinterpret_cast<const wchar_t*>(lparam);
        if (settingName && wcscmp(settingName, L"ImmersiveColorSet") == 0 && state) {
            state->systemIsDark = DetectSystemIsDark();
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        // 查找条打开时,"上一个"/"下一个"箭头按钮优先命中——它俩浮在最上层,
        // 且只在查找条可见时才存在(与其余命中测试用同一套"指针/状态为空
        // 即不存在"口径)。
        if (state && state->find && state->find->Visible()) {
            float scale = DipScaleOf(hwnd);
            float dipX = static_cast<float>(GET_X_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float dipY = static_cast<float>(GET_Y_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            FindBarLayout layout = ComputeFindBarLayout(ClientWidthDip(hwnd), 0.0f);
            FindBarNavHit hit = FindBarNavHitTest(layout, dipX, dipY);
            if (hit == FindBarNavHit::Prev) {
                StepFind(hwnd, state, /*forward=*/false);
                return 0;
            }
            if (hit == FindBarNavHit::Next) {
                StepFind(hwnd, state, /*forward=*/true);
                return 0;
            }
            if (hit == FindBarNavHit::Close) {
                CloseFindUi(hwnd, state);
                return 0;
            }
        }

        // 底部操作栏(新需求):最高优先级短路——它是持久化、常驻在最上层
        // (最后一个画,盖在正文/大纲侧栏之上)的控件带,点击落在其区域内时
        // 一律不再往下走任何正文/侧栏命中测试。但大纲/历史侧栏展开或正在
        // 动画时,渲染层的半透明蒙层会整块盖住底部栏(裁决:蒙层应完整遮住
        // 正文可交互区域,底部栏也不例外,见 renderer.cpp 对应注释),此时
        // 点击底部栏的视觉区域实际点在蒙层上,应该走下面的蒙层命中测试
        // (点蒙层关闭侧栏),不能再当成底部栏按钮点击处理。
        bool sidebarMaskCoversBottomBar = SidebarMaskCoversBottomBar(state);
        if (state && !sidebarMaskCoversBottomBar) {
            float scale = DipScaleOf(hwnd);
            float dipX = static_cast<float>(GET_X_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float dipY = static_cast<float>(GET_Y_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float clientHeightDip = ClientHeightDip(hwnd);
            if (IsPointInBottomBar(clientHeightDip, dipY)) {
                float clientWidthDip = ClientWidthDip(hwnd);
                bool hasDocument = state->currentDocumentPath[0] != 0;
                BottomBarButton btn = HitTestBottomBar(clientWidthDip, dipX, hasDocument);
                OnBottomBarButtonClicked(hwnd, state, btn);
                return 0;
            }
        }

        // T63b:大纲侧栏右边缘拖拽调宽度的抓手——仅在侧栏完全展开时允许。
        if (state && state->outline && state->outlineAnimState == OutlineAnimState::Open) {
            float scale = DipScaleOf(hwnd);
            if (IsPointInOutlinePanelResizeHandle(GET_X_LPARAM(lparam), scale,
                                                  state->outlinePanelWidthDip)) {
                state->outlinePanelResizing = true;
                state->outlinePanelResizeStartMouseXDip =
                    static_cast<float>(GET_X_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
                state->outlinePanelResizeStartWidthDip = state->outlinePanelWidthDip;
                SetCapture(hwnd);
                return 0;
            }
        }

        // 历史记录侧栏左边缘拖拽调宽度的抓手——仅在历史侧栏完全展开时允许。
        if (state && state->historyAnimState == SidebarAnimState::Open) {
            float scale = DipScaleOf(hwnd);
            float dipX = static_cast<float>(GET_X_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float dipY = static_cast<float>(GET_Y_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            SidebarHitArea hit = SidebarHitTest(
                SidebarDirection::Right, ClientWidthDip(hwnd), ClientHeightDip(hwnd),
                state->historyPanelWidthDip, state->historyAnimProgress, dipX, dipY);
            if (hit == SidebarHitArea::ResizeHandle) {
                state->historyPanelResizing = true;
                state->historyPanelResizeStartMouseXDip = dipX;
                state->historyPanelResizeStartWidthDip = state->historyPanelWidthDip;
                SetCapture(hwnd);
                return 0;
            }
        }

        // 自绘滚动条(方案A):按下即可能开始拖动滑块,优先级仅次于底部操作栏、
        // 高于侧栏条目点击/正文命中测试——点在滑块上不应该被当成"点了条目/
        // 文本"。侧栏打开时只判它自己的滚动条(浮在最上层);侧栏关闭时判正文的。
        if (state) {
            float scale = DipScaleOf(hwnd);
            float dipX = static_cast<float>(GET_X_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float dipY = static_cast<float>(GET_Y_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);

            if (state->outline && state->outlineAnimState == OutlineAnimState::Open &&
                IsPointInOutlinePanelRect(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), scale,
                                          state->outlinePanelWidthDip, ClientHeightDip(hwnd))) {
                float viewportHeight = OutlinePanelViewportHeightOf(hwnd);
                float contentHeight = OutlinePanelContentHeightDip(state->outline->ItemCount());
                ScrollbarMetrics m = CalcScrollbarMetrics(state->outlinePanelWidthDip, viewportHeight,
                                                          contentHeight, state->outline->ScrollY());
                if (IsPointInScrollbarThumb(m, dipX, dipY)) {
                    state->scrollbarDragTarget = ScrollbarDragTarget::Outline;
                    state->scrollbarDragStartMouseYDip = dipY;
                    state->scrollbarDragStartScrollY = state->outline->ScrollY();
                    SetCapture(hwnd);
                    return 0;
                } else if (m.visible && dipY >= 0.0f && dipY <= viewportHeight &&
                           IsPointInScrollbarColumn(state->outlinePanelWidthDip, dipX)) {
                    // Click on the outline scrollbar track: jump and initiate dragging.
                    float newY = ScrollYAfterTrackClick(dipY, viewportHeight, contentHeight);
                    state->outline->SetScrollY(newY, viewportHeight);
                    state->scrollbarDragTarget = ScrollbarDragTarget::Outline;
                    state->scrollbarDragStartMouseYDip = dipY;
                    state->scrollbarDragStartScrollY = newY;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            } else if (state->historyAnimState == SidebarAnimState::Open &&
                       dipX >= ClientWidthDip(hwnd) - state->historyPanelWidthDip) {
                float viewportHeight = ClientHeightDip(hwnd);
                u32 count = state->recentFiles ? state->recentFiles->count : 0u;
                float contentHeight =
                    kSidebarHeaderHeightDip + kSidebarRowHeightDip * static_cast<float>(count);
                float localX = dipX - (ClientWidthDip(hwnd) - state->historyPanelWidthDip);
                ScrollbarMetrics m = CalcScrollbarMetrics(state->historyPanelWidthDip, viewportHeight,
                                                          contentHeight, state->historyScrollY);
                if (IsPointInScrollbarThumb(m, localX, dipY)) {
                    state->scrollbarDragTarget = ScrollbarDragTarget::History;
                    state->scrollbarDragStartMouseYDip = dipY;
                    state->scrollbarDragStartScrollY = state->historyScrollY;
                    SetCapture(hwnd);
                    return 0;
                } else if (m.visible && dipY >= 0.0f && dipY <= viewportHeight &&
                           IsPointInScrollbarColumn(state->historyPanelWidthDip, localX)) {
                    // Click on history scrollbar track
                    float newY = ScrollYAfterTrackClick(dipY, viewportHeight, contentHeight);
                    state->historyScrollY = ClampScrollOffset(newY, contentHeight, viewportHeight);
                    state->scrollbarDragTarget = ScrollbarDragTarget::History;
                    state->scrollbarDragStartMouseYDip = dipY;
                    state->scrollbarDragStartScrollY = newY;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            } else if ((!state->outline || state->outlineAnimState == OutlineAnimState::Closed) &&
                       state->historyAnimState == SidebarAnimState::Closed && state->layout) {
                float viewportHeight = ViewportHeightOf(hwnd);
                float viewportWidth = ClientWidthDip(hwnd);
                ScrollbarMetrics m = CalcScrollbarMetrics(viewportWidth, viewportHeight,
                                                          state->layout->TotalHeight(), state->scrollY);
                if (IsPointInScrollbarThumb(m, dipX, dipY)) {
                    state->scrollbarDragTarget = ScrollbarDragTarget::Main;
                    state->scrollbarDragStartMouseYDip = dipY;
                    state->scrollbarDragStartScrollY = state->scrollY;
                    SetCapture(hwnd);
                    return 0;
                } else if (m.visible && dipY >= 0.0f && dipY <= viewportHeight &&
                           IsPointInScrollbarColumn(viewportWidth, dipX)) {
                    // Click on the main scrollbar track: jump and initiate dragging.
                    float newY = ScrollYAfterTrackClick(dipY, viewportHeight, state->layout->TotalHeight());
                    SetScrollY(hwnd, state, newY);
                    state->scrollbarDragTarget = ScrollbarDragTarget::Main;
                    state->scrollbarDragStartMouseYDip = dipY;
                    state->scrollbarDragStartScrollY = newY;
                    SetCapture(hwnd);
                    return 0;
                }
            }
        }

        // T64:大纲侧栏区域与正文互不干扰 —— 侧栏打开或正在动画时,
        // 短路掉正文的命中测试,不让下面的链接/图片/复制按钮命中再跑一遍。
        if (state && (state->outline || state->outlineAnimState != OutlineAnimState::Closed)) {
            float scale = DipScaleOf(hwnd);
            float dipX = static_cast<float>(GET_X_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float visibleWidth = state->outlinePanelWidthDip * state->outlineAnimProgress;
            if (dipX < visibleWidth) {
                if (state->outlineAnimState == OutlineAnimState::Open) {
                    OnOutlineItemClicked(hwnd, state, GET_Y_LPARAM(lparam));
                }
            } else {
                // 点击蒙层区域(侧栏之外的正文区域):关闭侧栏,同 Ctrl+\。
                ToggleOutlinePanel(hwnd, state);
            }
            return 0;
        }

        // 历史记录侧栏区域与正文互不干扰 —— 历史侧栏打开或正在动画时短路正文命中
        if (state && state->historyAnimState != SidebarAnimState::Closed) {
            float scale = DipScaleOf(hwnd);
            float dipX = static_cast<float>(GET_X_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float dipY = static_cast<float>(GET_Y_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            SidebarHitArea hit = SidebarHitTest(
                SidebarDirection::Right, ClientWidthDip(hwnd), ClientHeightDip(hwnd),
                state->historyPanelWidthDip, state->historyAnimProgress, dipX, dipY);
            if (hit == SidebarHitArea::InsideDrawer) {
                if (state->historyAnimState == SidebarAnimState::Open) {
                    u32 count = state->recentFiles ? state->recentFiles->count : 0u;
                    i32 item = SidebarHitTestItem(
                        SidebarDirection::Right, ClientWidthDip(hwnd), ClientHeightDip(hwnd),
                        state->historyPanelWidthDip, count,
                        state->historyScrollY, dipX, dipY);
                    if (item >= 0) {
                        // 悬浮该行时右侧会画关闭按钮 + 紧贴其左侧的文件夹按钮
                        // (见 DrawHistoryPanel);分别命中删除/打开所在文件夹,
                        // 否则才是正常的"打开该文档"。
                        float localX = dipX - (ClientWidthDip(hwnd) - state->historyPanelWidthDip);
                        SidebarRectDip closeRect = SidebarCloseButtonLocalRectDip(
                            state->historyPanelWidthDip, static_cast<u32>(item),
                            state->historyScrollY);
                        SidebarRectDip folderRect = SidebarFolderButtonLocalRectDip(
                            state->historyPanelWidthDip, static_cast<u32>(item),
                            state->historyScrollY);
                        bool onCloseButton = localX >= closeRect.left && localX < closeRect.right &&
                                             dipY >= closeRect.top && dipY < closeRect.bottom;
                        bool onFolderButton = localX >= folderRect.left && localX < folderRect.right &&
                                              dipY >= folderRect.top && dipY < folderRect.bottom;
                        if (onCloseButton) {
                            OnHistoryItemDeleteClicked(hwnd, state, static_cast<u32>(item));
                        } else if (onFolderButton) {
                            OnHistoryItemOpenFolderClicked(hwnd, state, static_cast<u32>(item));
                        } else {
                            OnHistoryItemClicked(hwnd, state, static_cast<u32>(item));
                        }
                    }
                }
            } else if (hit == SidebarHitArea::Mask) {
                ToggleHistoryPanel(hwnd, state);
            }
            return 0;
        }

        // T35/T36/T36b:统一走命中测试 —— 链接走链接行为,图片走"打开原图"
        // (网络图未下载时是 T34 的"点击加载",见 OnImageClicked)。
        if (state && state->layout) {
            HitResult hit = HitTestAtClientPoint(hwnd, state, GET_X_LPARAM(lparam),
                                                  GET_Y_LPARAM(lparam));
            // T45:代码块复制按钮走与链接同一个消息(WM_LBUTTONDOWN),保持
            // 全窗口"按下即生效"的一致手感。
            if (hit.kind == HitKind::CodeCopyButton) {
                OnCodeCopyButtonClicked(hwnd, state, hit.blockIndex);
                return 0;
            }
            if (hit.kind == HitKind::Link) {
                OnLinkClicked(hwnd, state, hit.linkTargetIdx);
                return 0;
            }
            const ImageBox* box = ImageBoxOfHit(*state->layout, hit);
            if (box) {
                OnImageClicked(hwnd, state, *box);
                return 0;
            }

            // T80:没有命中任何可交互内容——落在正文文本上,开始一次拖选。
            // Begin() 把锚点/焦点都设成按下位置,天然清掉了上一次的选区
            // (符合"点击文档任意位置应清除已有选区"的直觉预期);真的没有
            // 可选文本(如空文档)时 DocTextHit::valid 为 false,不进入拖选。
            if (state->selection) {
                DocTextHit textHit =
                    TextPositionAtClientPoint(hwnd, state, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                if (textHit.valid) {
                    state->selection->Begin(DocTextPos{textHit.blockIndex, textHit.charOffset});
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_MOUSEMOVE: {
        // T63b:正在拖拽侧栏右边缘调宽度——按鼠标横向位移换算新宽度,夹到
        // 合法区间。整窗重绘,因为宽度变化牵连蒙层/正文可用宽度这些跨区域
        // 的几何,不值得为此单独算一块局部矩形。
        if (state && state->outlinePanelResizing && (wparam & MK_LBUTTON)) {
            float scale = DipScaleOf(hwnd);
            float dipX = static_cast<float>(GET_X_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float dragDelta = dipX - state->outlinePanelResizeStartMouseXDip;
            state->outlinePanelWidthDip =
                ClampOutlinePanelWidth(state->outlinePanelResizeStartWidthDip + dragDelta);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // 历史记录侧栏左边缘拖拽调宽度——按鼠标横向位移换算新宽度(向左拖增加宽度)。
        if (state && state->historyPanelResizing && (wparam & MK_LBUTTON)) {
            float scale = DipScaleOf(hwnd);
            float dipX = static_cast<float>(GET_X_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float dragDelta = state->historyPanelResizeStartMouseXDip - dipX;
            state->historyPanelWidthDip =
                ClampSidebarWidth(state->historyPanelResizeStartWidthDip + dragDelta, ClientWidthDip(hwnd));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // 自绘滚动条(方案A):正在拖滑块——按鼠标位移换算新的滚动偏移。
        // 与 T80 文本拖选互斥(按下时二者只会有一个进入,见 WM_LBUTTONDOWN),
        // 这里提前 return,不再往下走选区更新/悬浮态判定。
        if (state && state->scrollbarDragTarget != ScrollbarDragTarget::None &&
            (wparam & MK_LBUTTON)) {
            float scale = DipScaleOf(hwnd);
            float dipY = static_cast<float>(GET_Y_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float dragDelta = dipY - state->scrollbarDragStartMouseYDip;

            if (state->scrollbarDragTarget == ScrollbarDragTarget::Main && state->layout) {
                float viewportHeight = ViewportHeightOf(hwnd);
                float newY = ScrollYAfterThumbDrag(state->scrollbarDragStartScrollY, dragDelta,
                                                   viewportHeight, state->layout->TotalHeight());
                SetScrollY(hwnd, state, newY);
            } else if (state->scrollbarDragTarget == ScrollbarDragTarget::Outline && state->outline) {
                float viewportHeight = OutlinePanelViewportHeightOf(hwnd);
                float contentHeight = OutlinePanelContentHeightDip(state->outline->ItemCount());
                float newY = ScrollYAfterThumbDrag(state->scrollbarDragStartScrollY, dragDelta,
                                                   viewportHeight, contentHeight);
                state->outline->SetScrollY(newY, viewportHeight);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (state->scrollbarDragTarget == ScrollbarDragTarget::History) {
                float viewportHeight = ClientHeightDip(hwnd);
                u32 count = state->recentFiles ? state->recentFiles->count : 0u;
                float contentHeight =
                    kSidebarHeaderHeightDip + kSidebarRowHeightDip * static_cast<float>(count);
                float newY = ScrollYAfterThumbDrag(state->scrollbarDragStartScrollY, dragDelta,
                                                   viewportHeight, contentHeight);
                state->historyScrollY = ClampScrollOffset(newY, contentHeight, viewportHeight);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        // T80:左键按住且正在拖选——更新选区终点(与代码块复制按钮悬浮态
        // 互不冲突,拖选优先,悬浮态判定仍照常跑,方便拖选途中扫过复制按钮
        // 也能正确恢复默认态)。
        if (state && state->layout && state->selection && state->selection->IsDragging() &&
            (wparam & MK_LBUTTON)) {
            DocTextHit textHit =
                TextPositionAtClientPoint(hwnd, state, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (textHit.valid) {
                state->selection->Update(DocTextPos{textHit.blockIndex, textHit.charOffset});
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        // T45:只更新代码块复制按钮的悬浮态(变化时才重绘)。链接/图片的手型
        // 光标仍由 WM_SETCURSOR 负责,这里不重复做文本命中。
        if (state && state->layout) {
            UpdateCopyButtonHover(hwnd, state, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            // 订阅一次 WM_MOUSELEAVE:鼠标直接移出窗口时把悬浮态收掉,
            // 否则按钮会停在悬浮样式上(TrackMouseEvent 是一次性的,每次
            // 鼠标移动都要重新订阅)。
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(TRACKMOUSEEVENT);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
        }

        // 自绘滚动条 Idle/Active 透明度(见 theme.h):只判"鼠标是否落在滚动条
        // 横向范围内",纵坐标夹在对应视口高度内(排除底部操作栏这类其他控件
        // 占用的区域)。侧栏打开时只判侧栏自己的,关闭时只判正文的——与两者
        // 互斥的绘制/拖动逻辑保持同一套"非此即彼"口径。
        if (state) {
            float scale = DipScaleOf(hwnd);
            float dipX = static_cast<float>(GET_X_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);
            float dipY = static_cast<float>(GET_Y_LPARAM(lparam)) / (scale > 0.0f ? scale : 1.0f);

            bool newOutlineHover = false;
            bool newHistoryHover = false;
            bool newMainHover = false;
            u32 newHistoryItemHover = kInvalidIndex;

            if (state->outline && state->outlineAnimState == OutlineAnimState::Open) {
                float viewportHeight = OutlinePanelViewportHeightOf(hwnd);
                newOutlineHover = dipY >= 0.0f && dipY <= viewportHeight &&
                                   IsPointInScrollbarColumn(state->outlinePanelWidthDip, dipX);
            } else if (state->historyAnimState == SidebarAnimState::Open) {
                float drawerLeft = ClientWidthDip(hwnd) - state->historyPanelWidthDip;
                float viewportHeight = ClientHeightDip(hwnd);
                if (dipX >= drawerLeft) {
                    float localX = dipX - drawerLeft;
                    newHistoryHover = dipY >= 0.0f && dipY <= viewportHeight &&
                                      IsPointInScrollbarColumn(state->historyPanelWidthDip, localX);
                    u32 count = state->recentFiles ? state->recentFiles->count : 0u;
                    i32 hitItem = SidebarHitTestItem(
                        SidebarDirection::Right, ClientWidthDip(hwnd), ClientHeightDip(hwnd),
                        state->historyPanelWidthDip, count,
                        state->historyScrollY, dipX, dipY);
                    newHistoryItemHover = (hitItem >= 0) ? static_cast<u32>(hitItem) : kInvalidIndex;
                }
            } else if ((!state->outline || state->outlineAnimState == OutlineAnimState::Closed) &&
                       state->historyAnimState == SidebarAnimState::Closed && state->layout) {
                float viewportHeight = ViewportHeightOf(hwnd);
                float viewportWidth = ClientWidthDip(hwnd);
                newMainHover = dipY >= 0.0f && dipY <= viewportHeight &&
                                IsPointInScrollbarColumn(viewportWidth, dipX);
            }
            if (newOutlineHover != state->outlineScrollbarHover ||
                newHistoryHover != state->historyScrollbarHover ||
                newMainHover != state->mainScrollbarHover ||
                newHistoryItemHover != state->historyHoverIndex) {
                state->outlineScrollbarHover = newOutlineHover;
                state->historyScrollbarHover = newHistoryHover;
                state->mainScrollbarHover = newMainHover;
                state->historyHoverIndex = newHistoryItemHover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            // 底部栏图标按钮悬浮态(2026-09-18 新增,取代常驻文字标签):复用
            // 已有的 IsPointInBottomBar/HitTestBottomBar 判定,与上面滚动条
            // 悬浮态同一条"高频消息,只做廉价矩形判定"的口径。命中状态区
            // (BottomBarButton::None)时同样不显示提示。
            float clientHeightDip = ClientHeightDip(hwnd);
            BottomBarButton newBottomBarHover = BottomBarButton::None;
            if (!SidebarMaskCoversBottomBar(state) && IsPointInBottomBar(clientHeightDip, dipY)) {
                bool hasDocument = state->currentDocumentPath[0] != 0;
                newBottomBarHover = HitTestBottomBar(ClientWidthDip(hwnd), dipX, hasDocument);
            }
            if (newBottomBarHover != state->bottomBarHoverButton) {
                state->bottomBarHoverButton = newBottomBarHover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_LBUTTONUP: {
        // T63b:结束侧栏调宽度拖拽。
        if (state && state->outlinePanelResizing) {
            state->outlinePanelResizing = false;
            ReleaseCapture();
        }
        if (state && state->historyPanelResizing) {
            state->historyPanelResizing = false;
            ReleaseCapture();
        }
        // 自绘滚动条(方案A):结束拖动。
        if (state && state->scrollbarDragTarget != ScrollbarDragTarget::None) {
            state->scrollbarDragTarget = ScrollbarDragTarget::None;
            ReleaseCapture();
        }
        // T80:结束拖选,选区本身保留(直到下一次点击/拖选覆盖它)。
        if (state && state->selection && state->selection->IsDragging()) {
            state->selection->End();
            ReleaseCapture();
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_MOUSELEAVE: {
        // T45:鼠标离开客户区 -> 清掉复制按钮悬浮态("已复制"反馈态不受影响,
        // 它由定时器负责收尾)。滚动条悬浮态同理清掉(正在拖动时不受影响——
        // 拖动靠 SetCapture 持续接收 WM_MOUSEMOVE,光标短暂移出客户区边界
        // 不代表用户想结束拖动)。
        bool changed = false;
        if (state && state->copyButtonHover != kInvalidIndex) {
            state->copyButtonHover = kInvalidIndex;
            changed = true;
        }
        if (state && state->scrollbarDragTarget == ScrollbarDragTarget::None &&
            (state->mainScrollbarHover || state->outlineScrollbarHover || state->historyScrollbarHover)) {
            state->mainScrollbarHover = false;
            state->outlineScrollbarHover = false;
            state->historyScrollbarHover = false;
            changed = true;
        }
        if (state && state->historyHoverIndex != kInvalidIndex) {
            state->historyHoverIndex = kInvalidIndex;
            changed = true;
        }
        // 底部栏图标悬浮提示同理清掉,鼠标移出窗口就不该再挂着提示气泡。
        if (state && state->bottomBarHoverButton != BottomBarButton::None) {
            state->bottomBarHoverButton = BottomBarButton::None;
            changed = true;
        }
        if (changed) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_TIMER: {
        // T45:"已复制"反馈态到点 -> 清状态、杀掉一次性定时器、重绘回默认态。
        if (wparam == kCopyFeedbackTimerId) {
            KillTimer(hwnd, kCopyFeedbackTimerId);
            if (state) {
                state->copyButtonCopied = kInvalidIndex;
                state->bottomBarPathCopied = false;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // T56:窗口矩形去抖到点 -> 杀掉一次性定时器、真正触发一次写盘回调。
        if (wparam == kWindowGeometryTimerId) {
            KillTimer(hwnd, kWindowGeometryTimerId);
            if (state && state->onWindowGeometryChanged) {
                state->onWindowGeometryChanged(state->callbackUserData);
            }
            return 0;
        }
        // 历史记录写盘去抖到点 -> 杀掉一次性定时器、真正落一次盘。
        //
        // Recent-files save debounce fires -> kill the one-shot timer,
        // actually write to disk now.
        if (wparam == kRecentFilesSaveTimerId) {
            KillTimer(hwnd, kRecentFilesSaveTimerId);
            if (state && state->recentFiles) {
                SaveRecentFiles(*state->recentFiles);
            }
            return 0;
        }
        // T63:大纲侧栏阅读位置高亮的去抖到点 -> 杀掉一次性定时器、做一次
        // 二分查找 + (变化时)局部重绘。
        if (wparam == kOutlineHighlightTimerId) {
            KillTimer(hwnd, kOutlineHighlightTimerId);
            RecomputeOutlineHighlight(hwnd, state);
            return 0;
        }
        // Outline drawer slide & mask fade animation timer tick (~60 FPS).
        //
        // 大纲侧栏滑动与蒙层淡入淡出动画节拍 (~60 FPS)。
        if (wparam == kOutlineAnimTimerId) {
            if (!state) return 0;
            ULONGLONG now = GetTickCount64();
            float elapsed = static_cast<float>(now - state->outlineAnimStartTick);
            float t = elapsed / kOutlineAnimDurationMs;
            if (t > 1.0f) t = 1.0f;
            float easeT = EaseOutCubic(t);

            if (state->outlineAnimState == OutlineAnimState::Opening) {
                state->outlineAnimProgress =
                    state->outlineAnimStartProgress + (1.0f - state->outlineAnimStartProgress) * easeT;
                if (t >= 1.0f || state->outlineAnimProgress >= 1.0f) {
                    state->outlineAnimProgress = 1.0f;
                    state->outlineAnimState = OutlineAnimState::Open;
                    KillTimer(hwnd, kOutlineAnimTimerId);
                }
            } else if (state->outlineAnimState == OutlineAnimState::Closing) {
                state->outlineAnimProgress =
                    state->outlineAnimStartProgress * (1.0f - easeT);
                if (t >= 1.0f || state->outlineAnimProgress <= 0.0001f) {
                    state->outlineAnimProgress = 0.0f;
                    state->outlineAnimState = OutlineAnimState::Closed;
                    state->outline = nullptr;
                    if (state->outlineArena) {
                        state->outlineArena->Reset();
                    }
                    KillTimer(hwnd, kOutlineAnimTimerId);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // History drawer slide & mask fade animation timer tick (~60 FPS).
        //
        // 历史记录侧栏滑动与蒙层淡入淡出动画节拍 (~60 FPS)。
        if (wparam == kHistoryAnimTimerId) {
            if (!state) return 0;
            ULONGLONG now = GetTickCount64();
            float elapsed = static_cast<float>(now - state->historyAnimStartTick);
            float t = elapsed / kSidebarAnimDurationMs;
            if (t > 1.0f) t = 1.0f;
            float easeT = EaseOutCubic(t);

            if (state->historyAnimState == SidebarAnimState::Opening) {
                state->historyAnimProgress =
                    state->historyAnimStartProgress + (1.0f - state->historyAnimStartProgress) * easeT;
                if (t >= 1.0f || state->historyAnimProgress >= 1.0f) {
                    state->historyAnimProgress = 1.0f;
                    state->historyAnimState = SidebarAnimState::Open;
                    KillTimer(hwnd, kHistoryAnimTimerId);
                }
            } else if (state->historyAnimState == SidebarAnimState::Closing) {
                state->historyAnimProgress =
                    state->historyAnimStartProgress * (1.0f - easeT);
                if (t >= 1.0f || state->historyAnimProgress <= 0.0001f) {
                    state->historyAnimProgress = 0.0f;
                    state->historyAnimState = SidebarAnimState::Closed;
                    KillTimer(hwnd, kHistoryAnimTimerId);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // T78:内存泄漏排查探针的循环节拍——每次到点执行一次对应动作,
        // 复用与真实快捷键完全相同的代码路径(不是重新实现一遍语义)。
        if (wparam == kBenchLoopTimerId && state && state->benchLoopKind != 0) {
            switch (state->benchLoopKind) {
                case 1:  // 就地换文档,复用 T36 openDocumentInPlace
                    if (state->openDocumentInPlace && state->benchLoopFiles &&
                        state->benchLoopFileCount > 0) {
                        const wchar_t* path =
                            state->benchLoopFiles[state->benchLoopDone % state->benchLoopFileCount];
                        state->openDocumentInPlace(state->callbackUserData, path);
                    }
                    break;
                case 2:  // 主题循环,同 Ctrl+Shift+T
                    CycleTheme(hwnd, state);
                    break;
                case 3:  // 大纲侧栏开关,同 Ctrl+反斜杠
                    ToggleOutlinePanel(hwnd, state);
                    break;
                case 4:  // F5 重载,同 F5
                    ReloadCurrentDocument(hwnd, state);
                    break;
                default:
                    break;
            }
            ++state->benchLoopDone;
            if (state->onBenchLoopTick) {
                state->onBenchLoopTick(state->callbackUserData, state->benchLoopDone);
            }
            if (state->benchLoopDone >= state->benchLoopTotal) {
                KillTimer(hwnd, kBenchLoopTimerId);
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_SETCURSOR: {
        // T63b:侧栏调宽度抓手——正在拖拽时,或鼠标悬浮在抓手上时,都给
        // 左右缩放光标,优先级最高(拖动状态下不管光标当前在哪都要保持)。
        if (state && (state->outlinePanelResizing || state->historyPanelResizing)) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32644)));  // IDC_SIZEWE
            return TRUE;
        }
        if (state && state->outline && state->outlineAnimState == OutlineAnimState::Open &&
            LOWORD(lparam) == HTCLIENT) {
            POINT pt{};
            if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt) &&
                IsPointInOutlinePanelResizeHandle(pt.x, DipScaleOf(hwnd),
                                                  state->outlinePanelWidthDip)) {
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32644)));  // IDC_SIZEWE
                return TRUE;
            }
        }
        if (state && state->historyAnimState == SidebarAnimState::Open &&
            LOWORD(lparam) == HTCLIENT) {
            POINT pt{};
            if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) {
                float scale = DipScaleOf(hwnd);
                float dipX = static_cast<float>(pt.x) / (scale > 0.0f ? scale : 1.0f);
                float dipY = static_cast<float>(pt.y) / (scale > 0.0f ? scale : 1.0f);
                SidebarHitArea hit = SidebarHitTest(
                    SidebarDirection::Right, ClientWidthDip(hwnd), ClientHeightDip(hwnd),
                    state->historyPanelWidthDip, state->historyAnimProgress, dipX, dipY);
                if (hit == SidebarHitArea::ResizeHandle) {
                    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32644)));  // IDC_SIZEWE
                    return TRUE;
                } else if (hit == SidebarHitArea::InsideDrawer) {
                    u32 count = state->recentFiles ? state->recentFiles->count : 0u;
                    i32 item = SidebarHitTestItem(
                        SidebarDirection::Right, ClientWidthDip(hwnd), ClientHeightDip(hwnd),
                        state->historyPanelWidthDip, count,
                        state->historyScrollY, dipX, dipY);
                    if (item >= 0) {
                        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));  // IDC_HAND
                        return TRUE;
                    }
                }
            }
        }
        if (state && state->bottomBarHoverButton != BottomBarButton::None && LOWORD(lparam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));  // IDC_HAND
            return TRUE;
        }
        // T35:鼠标移到链接或可点击的图片/占位块上时给手型光标;
        // T45 的代码块复制按钮同理(它就是个按钮,手型光标是最符合直觉的提示)。
        if (state && state->layout && (!state->outline || state->outlineAnimState == OutlineAnimState::Closed) &&
            state->historyAnimState == SidebarAnimState::Closed && LOWORD(lparam) == HTCLIENT) {
            POINT pt{};
            if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) {
                if (ShouldUseHandCursor(HitTestAtClientPoint(hwnd, state, pt.x, pt.y))) {
                    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));  // IDC_HAND
                    return TRUE;
                }
            }
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_COMMAND: {
        // T37(2026-09-19 改版):查找条查询串改由原生 EDIT 子窗口的 EN_CHANGE
        // 通知同步(取代旧版逐字符 WM_CHAR 拼接)。只清掉上一次查询串留下的
        // 命中/高亮,不做全文扫描——真正的搜索挪到 Enter/F3 那一下。
        if (HIWORD(wparam) == EN_CHANGE && LOWORD(wparam) == kFindEditControlId &&
            state && state->find && state->findEditHwnd) {
            wchar_t buf[kMaxFindQueryChars + 1];
            int len = GetWindowTextW(state->findEditHwnd, buf, kMaxFindQueryChars + 1);
            state->find->SetQuery(buf, len > 0 ? static_cast<u32>(len) : 0u);
            state->find->ClearMatches();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_CTLCOLOREDIT: {
        // 查找条 EDIT 子窗口背景/文字色跟随当前生效主题(render/theme.h 的
        // findBarBackground/findBarText),与 renderer.cpp 画的"查找:"前缀/
        // 状态文字用同一份色值,两处视觉才不会一亮一暗对不上。
        HDC dc = reinterpret_cast<HDC>(wparam);
        HWND ctrl = reinterpret_cast<HWND>(lparam);
        if (ctrl == (state ? state->findEditHwnd : nullptr)) {
            bool isDark = ResolveEffectiveTheme(state->themeSetting, state->systemIsDark);
            const Palette& palette = isDark ? kDarkPalette : kLightPalette;
            HBRUSH& brush = isDark ? g_findEditBgBrushDark : g_findEditBgBrushLight;
            if (!brush) brush = CreateSolidBrush(ColorFToColorRef(palette.findBarBackground));
            SetTextColor(dc, ColorFToColorRef(palette.findBarText));
            SetBkColor(dc, ColorFToColorRef(palette.findBarBackground));
            return reinterpret_cast<LRESULT>(brush);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case kWmRemoteImageDone:
        OnRemoteImageDone(hwnd, state, reinterpret_cast<RemoteImageResult*>(lparam));
        return 0;

    case WM_MOUSEWHEEL: {
        // T29:Ctrl+滚轮缩放,不与普通滚动共享同一分支。
        bool ctrlDown = (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) != 0;
        if (ctrlDown && state && state->fonts) {
            int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            if (delta > 0) state->fonts->ZoomIn();
            else if (delta < 0) state->fonts->ZoomOut();
            ApplyZoomChange(hwnd, state);
            return 0;
        }
        // 大纲侧栏打开且鼠标落在其区域内时,滚轮滚动侧栏自身,不滚正文——
        // WM_MOUSEWHEEL 携带的是屏幕坐标,先 ScreenToClient 换算成客户区坐标。
        if (state && state->outline && state->outlineAnimState == OutlineAnimState::Open) {
            POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (ScreenToClient(hwnd, &pt) &&
                IsPointInOutlinePanelRect(pt.x, pt.y, DipScaleOf(hwnd), state->outlinePanelWidthDip,
                                          ClientHeightDip(hwnd))) {
                OutlinePanel* panel = state->outline;
                float panelHeight = OutlinePanelViewportHeightOf(hwnd);
                float contentHeight = OutlinePanelContentHeightDip(panel->ItemCount());
                float newY = ScrollByWheel(panel->ScrollY(), GET_WHEEL_DELTA_WPARAM(wparam),
                                           contentHeight, panelHeight);
                panel->SetScrollY(newY, panelHeight);

                // 局部重绘:只失效侧栏那一块矩形,与 RecomputeOutlineHighlight
                // 同一套矩形口径,不整窗失效。
                float scale = DipScaleOf(hwnd);
                RECT rc{0, 0, static_cast<int>(state->outlinePanelWidthDip * scale + 0.5f),
                        static_cast<int>((panelHeight + 2.0f * kContentPaddingDip) *
                                         scale + 0.5f)};
                InvalidateRect(hwnd, &rc, FALSE);
                return 0;
            }
        }

        // 历史记录侧栏打开且鼠标落在其区域内时,滚轮滚动历史侧栏自身
        if (state && state->historyAnimState == SidebarAnimState::Open) {
            POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (ScreenToClient(hwnd, &pt)) {
                float scale = DipScaleOf(hwnd);
                float dipX = static_cast<float>(pt.x) / (scale > 0.0f ? scale : 1.0f);
                float dipY = static_cast<float>(pt.y) / (scale > 0.0f ? scale : 1.0f);
                SidebarHitArea hit = SidebarHitTest(
                    SidebarDirection::Right, ClientWidthDip(hwnd), ClientHeightDip(hwnd),
                    state->historyPanelWidthDip, state->historyAnimProgress, dipX, dipY);
                if (hit == SidebarHitArea::InsideDrawer) {
                    float panelHeight = ClientHeightDip(hwnd);
                    u32 count = state->recentFiles ? state->recentFiles->count : 0u;
                    float contentHeight =
                        kSidebarHeaderHeightDip + kSidebarRowHeightDip * static_cast<float>(count);
                    float newY = ScrollByWheel(state->historyScrollY, GET_WHEEL_DELTA_WPARAM(wparam),
                                               contentHeight, panelHeight);
                    state->historyScrollY = ClampScrollOffset(newY, contentHeight, panelHeight);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
        }

        // 侧栏打开时,正文被半透明蒙层盖住,不接受滚轮——上面的分支已经
        // 处理了"滚在侧栏自身范围内"的情况,走到这里说明鼠标落在蒙层区域,
        // 直接忽略,不能穿透蒙层滚动看不见的正文。
        if (state && state->layout && (!state->outline || state->outlineAnimState == OutlineAnimState::Closed) &&
            state->historyAnimState == SidebarAnimState::Closed) {
            float newY = ScrollByWheel(state->scrollY, GET_WHEEL_DELTA_WPARAM(wparam),
                                       state->layout->TotalHeight(), UsableViewportHeightOf(hwnd));
            SetScrollY(hwnd, state, newY);
        }
        return 0;
    }

    case WM_SYSKEYDOWN: {
        // T65:Alt+←/→ 走历史后退/前进。Alt 被按住时系统发的是 WM_SYSKEYDOWN
        // 而不是 WM_KEYDOWN,这里只拦 VK_LEFT/VK_RIGHT 这两个键,其余(尤其
        // Alt+F4/Alt+空格这类系统本身要处理的组合)一律交回 DefWindowProcW,
        // 不改变默认行为。
        if (state && state->history && (wparam == VK_LEFT || wparam == VK_RIGHT)) {
            if (wparam == VK_LEFT) NavigateHistoryBack(hwnd, state);
            else NavigateHistoryForward(hwnd, state);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_KEYDOWN: {
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        // T37:Esc 先关查找条(查找条没开时才是"关窗口")。
        if (wparam == VK_ESCAPE && state && state->find && state->find->Visible()) {
            CloseFindUi(hwnd, state);
            state->statusMessage = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // Esc:如果历史记录侧栏或大纲侧栏打开,则先关闭侧栏
        if (wparam == VK_ESCAPE && state &&
            (state->historyAnimState == SidebarAnimState::Open ||
             state->historyAnimState == SidebarAnimState::Opening)) {
            ToggleHistoryPanel(hwnd, state);
            return 0;
        }
        if (wparam == VK_ESCAPE && state &&
            (state->outlineAnimState == OutlineAnimState::Open ||
             state->outlineAnimState == OutlineAnimState::Opening)) {
            ToggleOutlinePanel(hwnd, state);
            return 0;
        }
        // Ctrl+W / Esc:关闭当前窗口(每个文件一个独立窗口,关掉即退出本进程)。
        if (wparam == VK_ESCAPE || (ctrlDown && wparam == 'W')) {
            DestroyWindow(hwnd);
            return 0;
        }
        // T80:Ctrl+C 复制当前选中的正文文本(纯文本,跨块拼接)。查找条打开时
        // 也允许——两者不冲突,查找条本身没有可选文本,复制的是正文选区。
        if (ctrlDown && wparam == 'C' && state) {
            CopySelectionToClipboard(hwnd, state);
            return 0;
        }
        // T37:Ctrl+F 打开查找条(与底部栏放大镜按钮同一路径,见 OpenFindUi)。
        if (ctrlDown && wparam == 'F' && state && state->find) {
            OpenFindUi(hwnd, state);
            return 0;
        }
        // T38(2026-09-19 改为"回车才搜"):查询串改过(Dirty)后第一次 Enter
        // 触发一次全文重搜并跳到第一处命中;查询串没变的后续 Enter/Shift+Enter
        // 才是纯粹的命中间前后跳转,与 F3/Shift+F3 同一逻辑。
        if (state && state->find && state->find->Visible() && wparam == VK_RETURN) {
            if (state->find->Dirty()) {
                RerunFind(hwnd, state);
            } else {
                StepFind(hwnd, state, !shiftDown);
            }
            return 0;
        }
        // F3/Shift+F3 只在查找条已打开时生效——补上与上面 VK_RETURN 分支一致的
        // Visible() 判断,避免查找条已关闭(此时 matches_ 已被 Close() 清空)
        // 时仍走一遍 RerunFind/StepFind,白白触发一次全窗口重绘。
        //
        // F3/Shift+F3 should only take effect while the find bar is open —
        // match the Visible() guard the VK_RETURN branch above already has,
        // so pressing F3 after the bar is closed (when matches_ has already
        // been cleared by Close()) doesn't still run RerunFind/StepFind and
        // trigger a wasted full-window repaint.
        if (state && state->find && state->find->Visible() && wparam == VK_F3) {
            if (state->find->Dirty()) {
                RerunFind(hwnd, state);
            } else {
                StepFind(hwnd, state, !shiftDown);
            }
            return 0;
        }
        // T29:Ctrl+=/Ctrl+-/Ctrl+0 字号缩放(放大/缩小一档/复位到 1.0)。
        if (ctrlDown && state && state->fonts &&
            (wparam == VK_OEM_PLUS || wparam == VK_OEM_MINUS || wparam == '0')) {
            if (wparam == VK_OEM_PLUS) state->fonts->ZoomIn();
            else if (wparam == VK_OEM_MINUS) state->fonts->ZoomOut();
            else state->fonts->ResetZoom();
            ApplyZoomChange(hwnd, state);
            return 0;
        }
        // T63:Ctrl+\ 切换大纲侧栏,默认关闭。
        if (ctrlDown && wparam == VK_OEM_5 && state) {
            ToggleOutlinePanel(hwnd, state);
            return 0;
        }
        // T70:F5(不带任何修饰键)重新加载当前文档。带 Ctrl/Shift 的组合一律
        // 忽略,不做"强制刷新"之类的第二档语义;Alt+F5 走 WM_SYSKEYDOWN,不会
        // 到这里,天然被排除。
        if (!ctrlDown && !shiftDown && wparam == VK_F5 && state) {
            ReloadCurrentDocument(hwnd, state);
            return 0;
        }
        // T47:Ctrl+Shift+T 在 System/Light/Dark 三态间循环,立即按新态重算
        // 生效主题并切调色板——只改指针 + 触发重绘,不做任何重排/重建。
        // T48:标题栏也要跟着热切换,追加一次 DwmSetWindowAttribute 调用;
        // 窗口类背景刷是注册时一次性决定的(Win32 限制,运行期无法改
        // WNDCLASS),热切换时不改也改不了,只影响"D2D 内容重绘之前"那一瞬间
        // (比如 resize 来不及重绘的边角),不在这里处理。
        if (ctrlDown && shiftDown && wparam == 'T' && state && state->renderer) {
            CycleTheme(hwnd, state);
            return 0;
        }
        ScrollCommand command = ScrollCommand::Home;
        if (!ctrlDown && state && state->layout &&
            ScrollCommandFromVirtualKey(wparam, &command)) {
            float newY = ApplyScrollCommand(state->scrollY, command,
                                            state->layout->TotalHeight(),
                                            UsableViewportHeightOf(hwnd));
            SetScrollY(hwnd, state, newY);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_DESTROY:
        // T45:窗口销毁前把可能还在跑的一次性定时器收掉。
        KillTimer(hwnd, kCopyFeedbackTimerId);
        // T63:同理,收掉大纲高亮的去抖定时器与滑动动画定时器。
        KillTimer(hwnd, kOutlineHighlightTimerId);
        KillTimer(hwnd, kOutlineAnimTimerId);
        KillTimer(hwnd, kHistoryAnimTimerId);
        // T56:窗口即将销毁前立即兜底写一次(而不是等 500ms 去抖到点,那时
        // 窗口可能已经没了),取消掉可能还在等待的去抖定时器。
        KillTimer(hwnd, kWindowGeometryTimerId);
        // 同理:历史记录写盘去抖也要在窗口销毁前兜底落一次盘,不能等 500ms。
        //
        // Same reasoning: flush the recent-files debounce immediately before
        // the window is gone, instead of waiting for the 500ms timer.
        KillTimer(hwnd, kRecentFilesSaveTimerId);
        // T78:同理收掉泄漏探针的循环定时器(正常路径下该定时器从未被 Set,
        // KillTimer 一个不存在的定时器是安全的空操作)。
        KillTimer(hwnd, kBenchLoopTimerId);
        if (state) {
            UpdateWindowGeometryState(hwnd, state);
            if (state->onWindowGeometryChanged) {
                state->onWindowGeometryChanged(state->callbackUserData);
            }
            if (state->recentFiles) {
                SaveRecentFiles(*state->recentFiles);
            }
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}  // namespace

/**
 * Request a debounced (500ms) write of the recent-files history to disk.
 * Callers only mutate the in-memory RecentFiles list synchronously
 * (mdvn::AddRecentFile is a cheap array shift, no I/O); the actual
 * CreateFileW/WriteFile/FlushFileBuffers happens on the WM_TIMER tick, so
 * clicking several in-document links back-to-back does not fsync once per
 * click — same debounce pattern as window-geometry persistence.
 *
 * 请求一次去抖(500ms)的历史记录写盘。调用方只需同步更新内存里的
 * RecentFiles 列表(mdvn::AddRecentFile 只是数组移位,不涉及 I/O);真正的
 * CreateFileW/WriteFile/FlushFileBuffers 发生在 WM_TIMER 到点时,连续点击
 * 好几个文档内链接不会每次点击都落一次盘——与窗口矩形持久化同一去抖手法。
 *
 * @param hwnd 主窗口句柄,用于挂载去抖定时器。
 *
 *   Main window handle, used to host the debounce timer.
 *
 * @param state 运行期状态,recentFiles 为空时静默返回。
 *
 *   Runtime state; silently returns if recentFiles is null.
 *
 * @example mdvn::RequestRecentFilesSave(hwnd, &windowState);
 */
void RequestRecentFilesSave(HWND hwnd, WindowState* state) {
    if (!state || !state->recentFiles) return;
    SetTimer(hwnd, kRecentFilesSaveTimerId, kRecentFilesSaveDebounceMs, nullptr);
}

void ConfirmAndRemoveMissingHistoryEntry(HWND hwnd, WindowState* state, u32 item) {
    // MB_DEFBUTTON2:把默认焦点放在"否"上——这是一个删除确认框,误按回车/
    // 空格不应该触发删除这个有损操作,只有显式选"是"才删除。
    //
    // MB_DEFBUTTON2: default focus on "No" — this is a delete confirmation,
    // so an accidental Enter/Space press must not trigger the destructive
    // action; only an explicit "Yes" choice removes the entry.
    int choice = MessageBoxW(hwnd, L"文件不存在，是否从历史记录中删除？", L"提示",
                             MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (choice == IDYES) {
        RemoveRecentFileAt(state->recentFiles, item);
        // 走去抖写盘(与 AddRecentFile 一致),而不是每次删除都同步落盘。
        //
        // Route through the debounced save (consistent with AddRecentFile)
        // instead of synchronously hitting disk on every delete.
        RequestRecentFilesSave(hwnd, state);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

bool EnablePerMonitorV2DpiAwareness() {
    // 选择代码方式而非 manifest:不需要改动构建脚本,且能按系统能力优雅退化。
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return false;

    using SetContextFn = BOOL(WINAPI*)(HANDLE);
    SetContextFn setContext = reinterpret_cast<SetContextFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    if (!setContext) return false;  // Win10 1703 以前:退化为系统 DPI 感知

    return setContext(reinterpret_cast<HANDLE>(kDpiAwarenessContextPerMonitorV2)) != FALSE;
}

bool RegisterMainWindowClass(HINSTANCE instance, bool isDarkTheme) {
    if (g_classRegistered) return true;

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    // 标准箭头光标(IDC_ARROW 的资源序号 32512;工程未定义 UNICODE 宏,
    // 这里显式用宽字符版本的资源 ID 以匹配 LoadCursorW)。
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    // T48:背景刷跟随注册时的生效主题,避免窗口首次显示到首帧绘制之间出现与
    // 主题不符的白闪(架构 §6)。深色下自建一支与 kDarkPalette.background 同色
    // 的纯色刷(进程退出前由 ReleaseMainWindowClassResources 释放);浅色沿用
    // 系统内置的 COLOR_WINDOW 句柄(不需要也不能 DeleteObject)。
    if (isDarkTheme) {
        UINT32 rgb = kDarkBackgroundRgb;
        g_darkBackgroundBrush = CreateSolidBrush(
            RGB((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu));
    }
    // CreateSolidBrush 理论上会失败(系统 GDI 句柄耗尽等极端情况),失败时退回
    // COLOR_WINDOW——深色主题下这一帧仍会白闪,但好过 hbrBackground 为空
    // (那会导致背景完全不擦除,残留任意脏内容)。
    wc.hbrBackground = g_darkBackgroundBrush ? g_darkBackgroundBrush
                                              : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    g_classRegistered = RegisterClassW(&wc) != 0;
    return g_classRegistered;
}

void ReleaseMainWindowClassResources() {
    if (g_darkBackgroundBrush) {
        DeleteObject(g_darkBackgroundBrush);
        g_darkBackgroundBrush = nullptr;
    }
    if (g_findEditBgBrushLight) {
        DeleteObject(g_findEditBgBrushLight);
        g_findEditBgBrushLight = nullptr;
    }
    if (g_findEditBgBrushDark) {
        DeleteObject(g_findEditBgBrushDark);
        g_findEditBgBrushDark = nullptr;
    }
}

HWND CreateMainWindow(HINSTANCE instance, const wchar_t* title, WindowState* state) {
    if (!state) return nullptr;
    state->scrollY = 0.0f;
    state->firstPresentDone = false;
    // T45:两个瞬时交互态必须初始化成 kInvalidIndex —— 零值会被当成
    // "第 0 个块的复制按钮处于悬浮/已复制态"。
    state->copyButtonHover = kInvalidIndex;
    state->copyButtonCopied = kInvalidIndex;
    // 底部栏悬浮提示(2026-09-18 新增):零值会被当成"悬浮在第 0 个按钮
    // (ZoomIn)上",必须显式置为 None,与上面两个 kInvalidIndex 同一条理由。
    state->bottomBarHoverButton = BottomBarButton::None;
    state->bottomBarPathCopied = false;
    // 自绘滚动条(方案A):默认没有任何一个在被拖动,也没有悬浮。
    state->scrollbarDragTarget = ScrollbarDragTarget::None;
    state->mainScrollbarHover = false;
    state->outlineScrollbarHover = false;
    // T63b:侧栏默认宽度,未拖拽过时就是这个值;默认没有在拖拽调宽。
    state->outlinePanelWidthDip = kOutlinePanelWidthDip;
    state->outlinePanelResizing = false;
    // T63:显式确认默认关闭——调用方应已经把这个字段填成 nullptr,这里再赋
    // 一次是防御性写法(与其余"由调用方填好"的字段一致,不额外分配任何东西)。
    state->outline = nullptr;
    state->outlineAnimState = OutlineAnimState::Closed;
    state->outlineAnimProgress = 0.0f;
    state->outlineAnimStartTick = 0;
    state->outlineAnimStartProgress = 0.0f;

    // 历史记录抽屉侧栏 (右侧) 初始状态
    state->historyScrollY = 0.0f;
    state->historyPanelWidthDip = kSidebarDefaultWidthDip;
    state->historyPanelResizing = false;
    state->historyPanelResizeStartMouseXDip = 0.0f;
    state->historyPanelResizeStartWidthDip = 0.0f;
    state->historyAnimState = SidebarAnimState::Closed;
    state->historyAnimProgress = 0.0f;
    state->historyAnimStartTick = 0;
    state->historyAnimStartProgress = 0.0f;
    state->historyScrollbarHover = false;
    state->historyHoverIndex = kInvalidIndex;

    // T56:winW/winH <= 0 表示从未存过窗口矩形(首次启动),走原来的默认
    // 位置/尺寸;否则按上次记住的矩形恢复,先做多显示器越界钳制,再做
    // "同位置已有本程序窗口"的层叠偏移(裁决 #8)。
    bool hasSavedRect = state->winW > 0 && state->winH > 0;
    bool restoreMaximized = hasSavedRect && state->winMaximized;
    int createX = CW_USEDEFAULT;
    int createY = CW_USEDEFAULT;
    int createW = kInitialWidthDip;
    int createH = kInitialHeightDip;

    if (hasSavedRect) {
        MonitorRect monitors[kMaxMonitorsForRestore];
        u32 primaryIndex = 0;
        u32 monitorCount = CollectMonitors(monitors, kMaxMonitorsForRestore, &primaryIndex);

        RectI saved{state->winX, state->winY, state->winX + state->winW,
                    state->winY + state->winH};
        RectI clamped = ClampWindowRectToMonitors(saved, monitors, monitorCount, primaryIndex);

        RectI existing[kMaxExistingWindowsForCascade];
        u32 existingCount = CollectExistingWindowOrigins(existing, kMaxExistingWindowsForCascade);

        // 层叠偏移用"clamped 矩形落在的那个显示器"的工作区判断是否碰到边界;
        // 极端兜底(理论上不该发生:显示器列表在上面刚枚举过,clamped 又是
        // ClampWindowRectToMonitors 的输出)时退回主屏工作区,再退回矩形自身。
        i32 monitorIdx =
            monitorCount > 0 ? FindMonitorContaining(clamped, monitors, monitorCount) : -1;
        RectI cascadeWorkArea = clamped;
        if (monitorIdx >= 0) {
            cascadeWorkArea = monitors[static_cast<u32>(monitorIdx)].workArea;
        } else if (monitorCount > 0) {
            cascadeWorkArea = monitors[primaryIndex].workArea;
        }

        i32 offsetPx = static_cast<i32>(kCascadeOffsetDip * DipScaleForNewWindow() + 0.5f);
        RectI finalRect =
            ApplyCascadeOffset(clamped, existing, existingCount, cascadeWorkArea, offsetPx);

        createX = finalRect.left;
        createY = finalRect.top;
        createW = finalRect.Width();
        createH = finalRect.Height();
    }

    // 标准 Windows 标题栏(WS_OVERLAPPEDWINDOW),不自绘(裁决 #9)。
    // 滚动条改为自绘(方案A,2026-09-18):不再声明 WS_VSCROLL,正文与大纲
    // 侧栏统一用 Renderer::DrawScrollbar 画同一套 8px 圆角滑块,见 scrollbar.h。
    // WS_CLIPCHILDREN:查找条内嵌原生 EDIT 子窗口后必加——没这个标志,父窗口
    // 每次 D2D 重绘(滚动/动画/idle)都会整块覆盖到子窗口区域上面,子 EDIT 只有
    // 自己重绘时(比如获得焦点触发的光标闪烁)才会把内容画回来,表现为"查找
    // 框失焦时文字/背景色不对,一聚焦又正常"。
    HWND hwnd = CreateWindowExW(
        0, kWindowClassName, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        createX, createY, createW, createH,
        nullptr, nullptr, instance, state);
    if (!hwnd) return nullptr;

    // 只给查找条的 EDIT 子窗口留 IME、主框架窗口本身按窗口摘掉 IME 上下文
    // (IACE_IGNORENOCONTEXT):进程级 ImmDisableIME 已在 T37 被推翻(查找条要
    // 支持中文输入),但主窗口本身从不接受文字输入,不该为它触发 TSF 激活
    // ——回归排查(2026-09-19)证实全局启用会连带把第三方输入法模块(实测
    // SogouPy.ime/SogouTSF.ime)一起载入主窗口所在线程,私有内存从~25MB
    // 涨到~28MB、暖启动首屏中位数也从~63ms 涨到~95ms。按窗口摘掉后主窗口
    // 不再触发该线程的 TSF 激活,findEditHwnd 保持默认关联,中文查找不受影响。
    ImmAssociateContextEx(hwnd, nullptr, IACE_IGNORENOCONTEXT);

    // T37(2026-09-19 改版):查找条的原生 EDIT 子窗口,创建时先隐藏
    // (WS_VISIBLE 不设),Ctrl+F 打开查找条时才 ShowWindow;子类化后
    // Enter/Esc/F3 转发给父窗口,其余按键走系统默认 EDIT 处理。
    // find 为空表示这个窗口不支持查找功能(纯渲染场景/单测),不创建。
    if (state->find) {
        state->findEditHwnd = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFindEditControlId)),
            instance, nullptr);
        if (state->findEditHwnd) {
            LONG_PTR origProc = SetWindowLongPtrW(
                state->findEditHwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(FindEditSubclassProc));
            SetWindowLongPtrW(state->findEditHwnd, GWLP_USERDATA, origProc);
            // 关掉这个控件的视觉主题(Uxtheme):主题化的 EDIT 在深色背景下会
            // 忽略 WM_CTLCOLOREDIT 里 SetTextColor 设的文字色,固定按主题引擎
            // 自己的浅色方案画黑字,在深色查找条底色上完全看不见——这是已知
            // 的 Win32 坑,禁用主题后才会真正采用经典消息路径的自定义颜色。
            SetWindowTheme(state->findEditHwnd, L"", L"");
        }
    }

    // T48:标题栏深浅色紧跟着 HWND 一起定下来,与窗口类背景刷用的是同一份
    // 生效主题判断(themeSetting/systemIsDark 由调用方在创建窗口前填好)。
    ApplyTitleBarTheme(hwnd, ResolveEffectiveTheme(state->themeSetting, state->systemIsDark));

    // 窗口创建成功、显示之前触发一次回调(T14 性能埋点用,为空时零开销)。
    if (state->onWindowCreated) state->onWindowCreated(state->callbackUserData);

    if (!hasSavedRect) {
        // Per-Monitor V2 下窗口尺寸是物理像素,按实际所在显示器的 DPI 放大
        // 初始尺寸,让高 DPI 屏上的初始窗口与 100% 缩放时视觉大小一致。
        // 有保存矩形时不需要这一步——那份矩形本来就是上次的物理像素矩形。
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        GetDpiForWindowFn getDpi = user32 ? reinterpret_cast<GetDpiForWindowFn>(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow"))) : nullptr;
        if (getDpi) {
            UINT dpi = getDpi(hwnd);
            if (dpi != 0 && dpi != static_cast<UINT>(kBaselineDpi)) {
                int width = MulDiv(kInitialWidthDip, static_cast<int>(dpi), kBaselineDpi);
                int height = MulDiv(kInitialHeightDip, static_cast<int>(dpi), kBaselineDpi);
                SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }

    // T56:恢复最大化态——`ShowWindow(SW_SHOWMAXIMIZED)` 会以刚才创建时的
    // 矩形作为还原态(`rcNormalPosition`),再整体最大化,与
    // `WINDOWPLACEMENT::rcNormalPosition` 才是还原态矩形的口径一致。
    ShowWindow(hwnd, restoreMaximized ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(hwnd);

    // T56:创建完成后把 winX/Y/W/H/winMaximized 更新为窗口当前的实际值
    // (不再是"待恢复值"),供后续移动/缩放/退出时的持久化读取。
    UpdateWindowGeometryState(hwnd, state);

    // T78:内存泄漏排查探针——benchLoopKind != 0 时才启动这个定时器,默认
    // (不带 --bench-loop)恒为 0,不产生任何额外调用。
    state->benchLoopDone = 0;
    if (state->benchLoopKind != 0 && state->benchLoopTotal > 0) {
        SetTimer(hwnd, kBenchLoopTimerId, kBenchLoopIntervalMs, nullptr);
    }
    return hwnd;
}

float ClientWidthDip(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    return static_cast<float>(rc.right - rc.left) / DipScaleOf(hwnd);
}

float ClientHeightDip(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    return static_cast<float>(rc.bottom - rc.top) / DipScaleOf(hwnd);
}

int RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

}  // namespace mdvn
