#include "window.h"

#include <new>         // placement new(T63 侧栏在 outlineArena 上就地构造 OutlinePanel)
#include <dwmapi.h>    // DwmSetWindowAttribute(T48 标题栏深浅色,/DELAYLOAD)
#include <uxtheme.h>   // SetWindowTheme(原生滚动条深浅色,/DELAYLOAD)
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <cwchar>      // wcscmp

#include "../assets/data_uri.h"
#include "../render/theme.h"  // kLightPalette/kDarkPalette/kDarkBackgroundRgb

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

// T48:切标题栏深浅色 + 原生滚动条深浅色(2026-09-17 真机验收用户反馈补充:
// 标题栏跟着变了,滚动条没跟着变——WS_VSCROLL 的滑块/滑槽外观完全交给系统
// 主题绘制,不受 Palette 影响,需要单独告知系统去画哪一套)。按官方口径先试
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
// 滚动条深浅色走 `SetWindowTheme(hwnd, L"DarkMode_Explorer"/"Explorer", nullptr)`
// ——这是 Win10 1809+ 起系统滚动条/资源管理器控件识别的子应用名约定(公开 API
// `SetWindowTheme`,子应用名字符串是系统主题引擎认的惯例值,不是私有 API),
// `SWP_FRAMECHANGED` 同一次调用会一并让滚动条跟着重绘,不需要额外强制刷新。
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
    SetWindowTheme(hwnd, isDark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    if (IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
        ShowWindow(hwnd, SW_SHOWNA);
    }
}

// T47/T48:主题三态循环的核心动作,抽成一个函数供 Ctrl+Shift+T 复用——只改
// 指针 + 触发重绘,不做任何重排/重建。
void CycleTheme(HWND hwnd, WindowState* state) {
    state->themeSetting = NextThemeSetting(state->themeSetting);
    bool isDark = ResolveEffectiveTheme(state->themeSetting, state->systemIsDark);
    state->renderer->SetPalette(isDark ? &kDarkPalette : &kLightPalette);
    ApplyTitleBarTheme(hwnd, isDark);
    InvalidateRect(hwnd, nullptr, FALSE);
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
float ViewportHeightOf(HWND hwnd) { return ClientHeightDip(hwnd); }
float ViewportWidthOf(HWND hwnd) { return ContentWidthDip(ClientWidthDip(hwnd)); }

// 滚动范围计算统一用的"可用视口高度"(原始视口高度收窄掉上下内边距),
// 喂给 ClampScrollOffset/MaxScrollOffset/ApplyScrollCommand 的 viewportHeight 参数
// 以及 UpdateVisibleRange 的可见区间宽度,滚到底时才会在文档下方留出内边距空白。
float UsableViewportHeightOf(HWND hwnd) { return UsableViewportHeightDip(ViewportHeightOf(hwnd)); }

// float 滚动范围值夹到 SCROLLINFO 的 int 字段能装下的区间,避免溢出/负数。
// 文档高度在真实场景里远不会逼近这个上限,这里只是防御性夹取。
int ClampToScrollInfoRange(float value) {
    constexpr float kScrollInfoMaxValue = 2000000000.0f;
    if (value <= 0.0f) return 0;
    if (value >= kScrollInfoMaxValue) return static_cast<int>(kScrollInfoMaxValue);
    return static_cast<int>(value + 0.5f);  // 四舍五入,精度损失是滚动条 UI 的固有限制
}

// 把 WindowState::scrollY 同步到原生垂直滚动条(WS_VSCROLL)。任何改动过
// scrollY 或者可能改变了总高度/视口尺寸的地方都应该在收尾调用一次,否则会出现
// "用滚轮/键盘滚动了,滑块位置却没跟着动"这种明显 bug。
void SyncScrollBar(HWND hwnd, const WindowState* state) {
    if (!state || !state->layout) return;

    float totalHeight = state->layout->TotalHeight();
    float usableViewportHeight = UsableViewportHeightOf(hwnd);

    SCROLLINFO si{};
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = ClampToScrollInfoRange(totalHeight);
    si.nPage = static_cast<UINT>(ClampToScrollInfoRange(usableViewportHeight));
    si.nPos = ClampToScrollInfoRange(state->scrollY);
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

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
    SyncScrollBar(hwnd, state);

    // Relayout 会淘汰全部 IDWriteTextLayout,必须在这里立刻按新几何重建一次,
    // 否则紧接着的这一帧会画成"只有图片、没有文字"。
    state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                      *state->fonts, state->residency);
    return true;
}

// T63:侧栏可视高度(DIP),与正文视口高度取同一个"可用视口高度"——两者
// 都是"客户区高度减去上下内边距",侧栏本身不额外留白。
float OutlinePanelViewportHeightOf(HWND hwnd) { return UsableViewportHeightOf(hwnd); }

// T63:`Ctrl+\` 的核心动作——严格的"指针为空即不存在"实现:
//   - 打开:在 outlineArena 上(惰性 Init,幂等)placement-new 构造一个
//     OutlinePanel,提取一次大纲,`state->outline` 从 nullptr 变为该实例。
//   - 关闭:直接把指针置空(成员全是 POD/Arena 绑定容器,无需析构),杀掉
//     去抖定时器。
// 两个分支都只做"构造/置空 + 一次全窗重绘",不触发任何 Relayout、不释放任何
// IDWriteTextLayout——与 T49 主题切换"纯重绘"同一口径。
void ToggleOutlinePanel(HWND hwnd, WindowState* state) {
    if (!state || !state->outlineArena) return;

    if (state->outline) {
        state->outline = nullptr;
        KillTimer(hwnd, kOutlineHighlightTimerId);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Init 幂等(已初始化过时静默返回 false,不重复预留地址空间);Reset 把
    // 上一次打开时用过的内容整体丢弃,避免反复开关侧栏时 Arena 无限增长。
    state->outlineArena->Init(4 * 1024 * 1024);
    state->outlineArena->Reset();
    void* mem = state->outlineArena->Alloc(sizeof(OutlinePanel), alignof(OutlinePanel));
    if (!mem) return;  // Arena 耗尽(几乎不可能:4MB 对大纲条目数组绰绰有余),静默放弃
    OutlinePanel* panel = new (mem) OutlinePanel(state->outlineArena);
    if (state->doc) panel->Rebuild(*state->doc);
    state->outline = panel;
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
    RECT rc{0, 0, static_cast<int>(kOutlinePanelWidthDip * scale + 0.5f),
            static_cast<int>((OutlinePanelViewportHeightOf(hwnd) + 2.0f * kContentPaddingDip) *
                             scale + 0.5f)};
    InvalidateRect(hwnd, &rc, FALSE);
}

// 滚动偏移变化后的统一收尾:任何触发滚动的路径(滚轮/键盘/查找跳转/换文档/
// 图片解码重排/原生滚动条拖动)都应该走这里,保证:
//   ① scrollY 按当前视口/文档高度重新夹取一次(调用方传入的值未必已经夹过);
//   ② 原生滚动条(WS_VSCROLL)滑块位置与 scrollY 同步;
//   ③ 刷新可见范围内的 IDWriteTextLayout / 图片解码位图并请求重绘。
// 偏移夹取后没有实际变化时跳过②之后的步骤,避免顶部/底部到界后仍反复重绘,
// 但滚动条仍会同步一次(窗口尺寸/文档高度可能已经变了,即使 scrollY 没变)。
// forceRefresh:换文档这类"scrollY 数值可能凑巧没变、但 layout 已经整个换掉了"
// 的场景传 true,跳过"没变化就不刷新虚拟化"的短路判断。
void SetScrollY(HWND hwnd, WindowState* state, float newY, bool forceRefresh = false) {
    if (!state || !state->layout) return;

    float totalHeight = state->layout->TotalHeight();
    float usableViewportHeight = UsableViewportHeightOf(hwnd);
    float clamped = ClampScrollOffset(newY, totalHeight, usableViewportHeight);

    bool changed = forceRefresh || (clamped != state->scrollY);
    state->scrollY = clamped;
    SyncScrollBar(hwnd, state);

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
            if (state->find) state->find->Close();
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
    if (state->find) state->find->Close();
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
        SyncScrollBar(hwnd, state);
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

    // 先把渲染目标建起来,再跑虚拟化 —— 图片解码需要绑定渲染目标创建位图,
    // 否则首帧会白解一次却建不出位图(见 Renderer::EnsureTarget 的注释)。
    state->renderer->EnsureTarget(hwnd);

    float usableViewportHeight = UsableViewportHeightOf(hwnd);
    state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                      *state->fonts, state->residency);
    // T33:首次解码出真实尺寸后,若与占位尺寸不同就在这里做一次性重排,
    // 之后同一批图片不会再触发(ImagePlacementChanged 会返回 false)。
    RelayoutForImagesIfNeeded(hwnd, state);
    SyncScrollBar(hwnd, state);

    // T42(bench 专用):首屏解码完之后,若开启了"强制全量解码",再对整份文档
    // 补一次 UpdateVisibleRange,把首屏之外的图片也解码一遍,近似"滚到底"的
    // 内存读数;只在 benchForceFullDecode 且尚未触发过首帧上报时执行一次,
    // 不影响正常交互场景下的虚拟化行为。
    if (state->benchForceFullDecode && !state->firstPresentDone && state->layout) {
        state->layout->UpdateVisibleRange(0.0f, state->layout->TotalHeight(), *state->fonts,
                                          state->residency);
        RelayoutForImagesIfNeeded(hwnd, state);
    }

    // T37/T38:把查找命中集合与查找条/提示条打包成只读视图交给渲染层;
    // 两者都没有时传 nullptr,渲染层零额外开销。
    ShellOverlay overlay{};
    overlay.copyButtonHoverBlock = kInvalidIndex;
    overlay.copyButtonCopiedBlock = kInvalidIndex;
    const ShellOverlay* overlayPtr = nullptr;
    bool hasCopyButtonState =
        state->copyButtonHover != kInvalidIndex || state->copyButtonCopied != kInvalidIndex;
    if (state->find || state->statusMessage || hasCopyButtonState || state->outline) {
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
        } else {
            overlay.outlineCurrentItem = kInvalidIndex;
        }
        overlayPtr = &overlay;
    }
    // 内容整体向下推 kContentPaddingDip 实现"上边距":RenderFrame 内部各 DrawXxx
    // 早就在算 `g.top - scrollY`,传一个减去内边距的 scrollY 即等效于内容下移。
    float effectiveScrollY = state->scrollY - kContentPaddingDip;
    bool presented = state->renderer->RenderFrame(hwnd, *state->layout, effectiveScrollY,
                                                   kContentPaddingDip, overlayPtr);

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
        float usableViewportHeight = UsableViewportHeightDip(static_cast<float>(height) / scale);
        state->scrollY = ClampScrollOffset(state->scrollY, state->layout->TotalHeight(),
                                           usableViewportHeight);
        // 窗口尺寸变化必然改变滚动条的 nMax/nPage,即使 scrollY 数值没变也要同步。
        SyncScrollBar(hwnd, state);
    }
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
    SyncScrollBar(hwnd, state);
    InvalidateRect(hwnd, nullptr, FALSE);
    // T57:字号缩放持久化(接过 M1 T29 的挂账)。复用 T56 的
    // onWindowGeometryChanged 钩子而不是新起一套回调——调用方(main.cpp)的
    // 钩子实现里已经会顺带读取 state->fonts->Scale() 一起写出,两个持久化
    // 项走同一条 T55"读-改-写 + 命名互斥体"通道,不绕过它。缩放是离散按键
    // 触发、不连续,不需要像窗口拖拽那样去抖。
    if (state->onWindowGeometryChanged) state->onWindowGeometryChanged(state->callbackUserData);
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
    InvalidateRect(hwnd, nullptr, FALSE);
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
        // T47:系统深浅色切换时,Explorer 广播 WM_SETTINGCHANGE 并把 lParam
        // 指向字符串 "ImmersiveColorSet"(其余系统设置变化也走这个消息,
        // 用字符串内容筛掉不相关的那些)。重新探测系统值缓存起来;只有当前
        // 偏好是 System 时才据此改变实际显示的调色板,否则只更新缓存不重绘。
        const wchar_t* settingName = reinterpret_cast<const wchar_t*>(lparam);
        if (settingName && wcscmp(settingName, L"ImmersiveColorSet") == 0 && state) {
            state->systemIsDark = DetectSystemIsDark();
            if (state->themeSetting == ThemeSetting::System && state->renderer) {
                state->renderer->SetPalette(state->systemIsDark ? &kDarkPalette : &kLightPalette);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        // T64:大纲侧栏区域与正文互不干扰 —— 侧栏打开且点击落在侧栏区域内时,
        // 短路掉正文的命中测试,不让下面的链接/图片/复制按钮命中再跑一遍,
        // 否则会出现"点侧栏结果打开了底下的链接"。
        if (state && state->outline &&
            IsPointInOutlinePanel(GET_X_LPARAM(lparam), DipScaleOf(hwnd), kOutlinePanelWidthDip)) {
            OnOutlineItemClicked(hwnd, state, GET_Y_LPARAM(lparam));
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
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_MOUSEMOVE: {
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
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_MOUSELEAVE: {
        // T45:鼠标离开客户区 -> 清掉复制按钮悬浮态("已复制"反馈态不受影响,
        // 它由定时器负责收尾)。
        if (state && state->copyButtonHover != kInvalidIndex) {
            state->copyButtonHover = kInvalidIndex;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_TIMER: {
        // T45:"已复制"反馈态到点 -> 清状态、杀掉一次性定时器、重绘回默认态。
        if (wparam == kCopyFeedbackTimerId) {
            KillTimer(hwnd, kCopyFeedbackTimerId);
            if (state) state->copyButtonCopied = kInvalidIndex;
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
        // T63:大纲侧栏阅读位置高亮的去抖到点 -> 杀掉一次性定时器、做一次
        // 二分查找 + (变化时)局部重绘。
        if (wparam == kOutlineHighlightTimerId) {
            KillTimer(hwnd, kOutlineHighlightTimerId);
            RecomputeOutlineHighlight(hwnd, state);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_SETCURSOR: {
        // T35:鼠标移到链接或可点击的图片/占位块上时给手型光标;
        // T45 的代码块复制按钮同理(它就是个按钮,手型光标是最符合直觉的提示)。
        if (state && state->layout && LOWORD(lparam) == HTCLIENT) {
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

    case WM_CHAR: {
        // T37:查找条可见时,可打印字符进查询串、退格删末尾,增量输入即时重搜。
        if (state && state->find && state->find->Visible()) {
            wchar_t ch = static_cast<wchar_t>(wparam);
            bool changed = (ch == 0x08) ? state->find->Backspace() : state->find->AppendChar(ch);
            if (changed) RerunFind(hwnd, state);
            return 0;
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
        if (state && state->layout) {
            float newY = ScrollByWheel(state->scrollY, GET_WHEEL_DELTA_WPARAM(wparam),
                                       state->layout->TotalHeight(), UsableViewportHeightOf(hwnd));
            SetScrollY(hwnd, state, newY);
        }
        return 0;
    }

    case WM_VSCROLL: {
        // 原生垂直滚动条(WS_VSCROLL)拖动/点击箭头/点击滑槽的统一入口。
        if (state && state->layout) {
            int code = LOWORD(wparam);
            if (IsThumbScrollCode(code)) {
                // MSDN 明确建议:WM_VSCROLL 的 HIWORD(wParam) 只有 16 位,文档高度
                // 一旦超过 65535 DIP 就会截断,拖动滑块/松手时改用 GetScrollInfo 的
                // SIF_TRACKPOS 读取完整精度的滑块位置。
                SCROLLINFO si{};
                si.cbSize = sizeof(SCROLLINFO);
                si.fMask = SIF_TRACKPOS;
                float target = state->scrollY;
                if (GetScrollInfo(hwnd, SB_VERT, &si)) {
                    target = static_cast<float>(si.nTrackPos);
                }
                SetScrollY(hwnd, state, target);
            } else {
                ScrollCommand command;
                if (ScrollCommandFromScrollBarCode(code, &command)) {
                    float newY = ApplyScrollCommand(state->scrollY, command,
                                                    state->layout->TotalHeight(),
                                                    UsableViewportHeightOf(hwnd));
                    SetScrollY(hwnd, state, newY);
                }
            }
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
            state->find->Close();
            state->statusMessage = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // Ctrl+W / Esc:关闭当前窗口(每个文件一个独立窗口,关掉即退出本进程)。
        if (wparam == VK_ESCAPE || (ctrlDown && wparam == 'W')) {
            DestroyWindow(hwnd);
            return 0;
        }
        // T37:Ctrl+F 打开查找条。
        if (ctrlDown && wparam == 'F' && state && state->find) {
            state->find->Open();
            state->statusMessage = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // T38:Enter / Shift+Enter / F3 / Shift+F3 在命中之间前后跳转。
        if (state && state->find && state->find->Visible() && wparam == VK_RETURN) {
            StepFind(hwnd, state, !shiftDown);
            return 0;
        }
        if (state && state->find && wparam == VK_F3) {
            StepFind(hwnd, state, !shiftDown);
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
        // T63:同理,收掉大纲高亮的去抖定时器。
        KillTimer(hwnd, kOutlineHighlightTimerId);
        // T56:窗口即将销毁前立即兜底写一次(而不是等 500ms 去抖到点,那时
        // 窗口可能已经没了),取消掉可能还在等待的去抖定时器。
        KillTimer(hwnd, kWindowGeometryTimerId);
        if (state) {
            UpdateWindowGeometryState(hwnd, state);
            if (state->onWindowGeometryChanged) {
                state->onWindowGeometryChanged(state->callbackUserData);
            }
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}  // namespace

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
}

HWND CreateMainWindow(HINSTANCE instance, const wchar_t* title, WindowState* state) {
    if (!state) return nullptr;
    state->scrollY = 0.0f;
    state->firstPresentDone = false;
    // T45:两个瞬时交互态必须初始化成 kInvalidIndex —— 零值会被当成
    // "第 0 个块的复制按钮处于悬浮/已复制态"。
    state->copyButtonHover = kInvalidIndex;
    state->copyButtonCopied = kInvalidIndex;
    // T63:显式确认默认关闭——调用方应已经把这个字段填成 nullptr,这里再赋
    // 一次是防御性写法(与其余"由调用方填好"的字段一致,不额外分配任何东西)。
    state->outline = nullptr;

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
    // WS_VSCROLL:原生右侧滚动条,外观完全交给系统主题,不自绘。
    HWND hwnd = CreateWindowExW(
        0, kWindowClassName, title, WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        createX, createY, createW, createH,
        nullptr, nullptr, instance, state);
    if (!hwnd) return nullptr;

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

    // 保险起见显式同步一次滚动条:WM_SIZE 通常会在窗口创建/显示过程中触发并
    // 顺带同步(见 OnSize),这里再补一次是为了在极端情况下(比如 WM_SIZE 没有
    // 如预期触发)也不会出现"滚动条还没配置好"的窗口。
    SyncScrollBar(hwnd, state);

    // T56:恢复最大化态——`ShowWindow(SW_SHOWMAXIMIZED)` 会以刚才创建时的
    // 矩形作为还原态(`rcNormalPosition`),再整体最大化,与
    // `WINDOWPLACEMENT::rcNormalPosition` 才是还原态矩形的口径一致。
    ShowWindow(hwnd, restoreMaximized ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(hwnd);

    // T56:创建完成后把 winX/Y/W/H/winMaximized 更新为窗口当前的实际值
    // (不再是"待恢复值"),供后续移动/缩放/退出时的持久化读取。
    UpdateWindowGeometryState(hwnd, state);
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
