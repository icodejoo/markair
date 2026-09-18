// mdvn 大纲侧栏(T63):`Ctrl+\` 切换,默认关闭。
//
// 硬性约束(裁决 #5/#6/#7,写死在这里避免后续维护者放松):
//   - "关闭时开销为 0"从结构上保证,不是靠一个 bool 开关——本类实例本身
//     只在侧栏被打开那一刻才被构造(`window.cpp` 里 `state->outline` 从
//     `nullptr` 变成非空指针),关闭时指针直接置空,不留一个"常驻对象只是
//     隐藏"的影子。侧栏关闭状态下不会有任何 `OutlinePanel` 实例存在,
//     因此也不会调用 `ExtractOutline`、不会创建 `IDWriteTextLayout`、
//     不会分配任何 Arena。
//   - 布局关系是"悬浮覆盖":本类不参与 `BlockLayoutEngine::Relayout`,
//     只读取已有的块几何(`BlockGeometry::top`)算自己的高亮定位,开关侧栏
//     是纯重绘。
//   - 当前阅读位置高亮只在滚动停止后更新(150ms 去抖,由 window.cpp 的
//     `SetTimer`/`WM_TIMER` 驱动,本文件只提供去抖时长常量与二分查找的
//     纯函数),滚动过程中不重复计算。
//
// 本文件不 include <windows.h>,二分查找/截断/自身滚动裁剪都是纯数字函数,
// 可以直接链进 mdvn_tests.exe 单测(见 tests/test_outline_panel.cpp)。
#pragma once

#include "../doc/outline.h"
#include "../util/arena.h"
#include "../util/span.h"
#include "../util/types.h"
#include "scroll.h"
#include "sidebar.h"

namespace mdvn {

// 侧栏默认宽度(DIP),随 DPI/字号缩放(调用方按需再乘缩放系数)。T63b 起
// 支持拖拽调宽(WindowState::outlinePanelWidthDip 才是运行期的实际宽度),
// 这个常量只作为"打开侧栏时的初始值"与其余不知道运行期宽度的场景(比如
// render 层内部按同一数值重复定义的那份,见 renderer.cpp 顶部注释)的兜底。
constexpr float kOutlinePanelWidthDip = 220.0f;

// 拖拽调宽的合法区间(DIP):太窄放不下标题文字,太宽会挤占太多正文空间。
constexpr float kOutlinePanelMinWidthDip = 160.0f;
constexpr float kOutlinePanelMaxWidthDip = 480.0f;

/**
 * 把拖拽中的侧栏宽度夹到合法区间 `[kOutlinePanelMinWidthDip, kOutlinePanelMaxWidthDip]`。
 * 纯数字函数,不依赖 Win32,可脱离窗口环境单测。
 * @param widthDip 待夹取的宽度(DIP)。
 * @return 夹取后的合法宽度(DIP)。
 * @example float w = mdvn::ClampOutlinePanelWidth(50.0f); // 得 160
 */
inline float ClampOutlinePanelWidth(float widthDip) {
    if (widthDip < kOutlinePanelMinWidthDip) return kOutlinePanelMinWidthDip;
    if (widthDip > kOutlinePanelMaxWidthDip) return kOutlinePanelMaxWidthDip;
    return widthDip;
}

// Animation state machine for outline drawer and mask overlay: reuses
// sidebar.h's SidebarAnimState (Closed/Opening/Open/Closing) rather than
// redeclaring the identical 4-state machine — the history drawer (sidebar.h,
// new alongside this alias) and the outline drawer are two instances of the
// exact same open/close animation shape. sidebar.h sits below this header
// (already #included above) so it, not this file, is the single source of
// truth; this alias keeps every existing `OutlineAnimState::Closed` etc.
// call site unchanged.
//
// 大纲抽屉与蒙层叠加层的动画状态机:复用 sidebar.h 的 SidebarAnimState
// (Closed/Opening/Open/Closing),不再重复声明同一套 4 态状态机——历史记录
// 侧栏(sidebar.h,与本别名同时引入)和大纲侧栏是同一套展开/收起动画形状的
// 两个实例。sidebar.h 在本文件的依赖链更底层(上方已 #include),因此单一
// 定义应放在那边而不是这里;这个别名让现有所有 `OutlineAnimState::Closed`
// 之类的调用点保持不变,不需要改一处调用。
using OutlineAnimState = SidebarAnimState;

/**
 * Compute horizontal drawing X offset (DIP) for the animated sliding outline panel.
 *
 * 计算大纲侧栏滑动动画的水平绘制 X 偏移量 (DIP)。
 *
 * @param progress Animation progress in [0.0f, 1.0f] (0.0f = fully closed, 1.0f = fully open).
 *
 *   动画进度，范围 [0.0f, 1.0f] (0.0f 表示完全收起，1.0f 表示完全展开)。
 *
 * @param panelWidth Outline panel width (DIP).
 *
 *   侧栏宽度 (DIP)。
 *
 * @returns Translation X offset (DIP): (progress - 1.0f) * panelWidth.
 *
 *   水平平移 X 偏移 (DIP)：(progress - 1.0f) * panelWidth。
 */
inline float OutlinePanelSlideOffsetDip(float progress, float panelWidth) {
    if (progress <= 0.0f) return -panelWidth;
    if (progress >= 1.0f) return 0.0f;
    return (progress - 1.0f) * panelWidth;
}

/**
 * Compute visible width on screen (DIP) for the sliding outline panel.
 *
 * 计算滑动中的大纲侧栏在屏幕上的可见宽度 (DIP)。
 *
 * @param progress Animation progress in [0.0f, 1.0f] (0.0f = fully closed, 1.0f = fully open).
 *
 *   动画进度，范围 [0.0f, 1.0f] (0.0f 表示完全收起，1.0f 表示完全展开)。
 *
 * @param panelWidth Outline panel width (DIP).
 *
 *   侧栏宽度 (DIP)。
 *
 * @returns Visible width on screen (DIP): progress * panelWidth.
 *
 *   在屏幕上的可见宽度 (DIP)：progress * panelWidth。
 */
inline float OutlinePanelVisibleWidthDip(float progress, float panelWidth) {
    if (progress <= 0.0f) return 0.0f;
    if (progress >= 1.0f) return panelWidth;
    return progress * panelWidth;
}

// 每条大纲条目的行高(DIP),条目 y 坐标 = 下标 * 本常量。
constexpr float kOutlineItemHeightDip = 28.0f;

// 每级标题的缩进步长(DIP);level 1 不缩进,level 2 缩进一步,以此类推。
constexpr float kOutlineIndentStepDip = 14.0f;

// 侧栏内容左右内边距(DIP),条目文字/截断宽度据此收窄。
constexpr float kOutlinePanelPaddingDip = 10.0f;

// 阅读位置高亮的去抖时长(毫秒):滚动/键盘滚动只重置这一个定时器,
// 到点才做一次二分查找 + 局部重绘,取裁决给的 100~200ms 区间中值。
constexpr int kOutlineHighlightDebounceMs = 150;

/**
 * 标题级别(1-6)换算成缩进量(DIP)。纯数字函数,不依赖任何渲染上下文。
 * @param level 标题级别,1-6(与 `Block::level` 一致);超出范围按边界钳制。
 * @return 缩进量(DIP),level 1 为 0,每升一级增加 `kOutlineIndentStepDip`。
 * @example float indent = mdvn::OutlineItemIndentDip(3); // 得 28.0f
 */
inline float OutlineItemIndentDip(u8 level) {
    u8 step = (level >= 1) ? static_cast<u8>(level - 1) : 0;
    if (step > 5) step = 5;  // 最深钳到 h6 对应的缩进,避免离谱级别把条目挤没
    return kOutlineIndentStepDip * static_cast<float>(step);
}

/**
 * 大纲条目在侧栏自身坐标系里的顶部 y 坐标(未减去侧栏自身滚动偏移)。
 * @param indexInPanel 条目在大纲数组里的下标(非块下标)。
 * @return `indexInPanel * kOutlineItemHeightDip`。
 * @example float y = mdvn::OutlineItemTopDip(2); // 得 56.0f
 */
inline float OutlineItemTopDip(u32 indexInPanel) {
    return static_cast<float>(indexInPanel) * kOutlineItemHeightDip;
}

/**
 * 侧栏自身内容总高度(DIP),用于夹取侧栏自身滚动偏移。
 * @param itemCount 大纲条目总数。
 * @return `itemCount * kOutlineItemHeightDip`。
 * @example float h = mdvn::OutlinePanelContentHeightDip(10);
 */
inline float OutlinePanelContentHeightDip(u32 itemCount) {
    return static_cast<float>(itemCount) * kOutlineItemHeightDip;
}

/**
 * 文本宽度测量回调:侧栏截断逻辑不直接依赖 DirectWrite,由调用方注入真实的
 * `IDWriteTextLayout` 测量或(单测里)一个按字符数估算的假测量函数。
 */
struct OutlineTextMeasurer {
    // 测量一段文本的显示宽度(DIP)。
    float (*measureWidth)(void* ctx, const wchar_t* text, u32 len);
    void* ctx;  // 传给 measureWidth 的调用方上下文,可为 nullptr
};

/**
 * 把一段标题文本截断进 `outBuf`,超宽时截断并追加省略号"..."。
 *
 * 纯函数:通过 `measurer` 注入测量能力,不直接创建任何 `IDWriteTextLayout`
 * (由调用方决定测量方式),因此可以脱离 D2D/DirectWrite 单独单测。
 *
 * @param text 标题原文,不要求以 '\0' 结尾。
 * @param len text 的长度(UTF-16 code unit)。
 * @param maxWidthDip 允许的最大显示宽度(DIP)。
 * @param measurer 文本宽度测量回调,非空。
 * @param outBuf 输出缓冲,函数保证以 '\0' 结尾。
 * @param outBufCap outBuf 的容量(含结尾 '\0'),必须 >= 4(至少能放下"..."+结尾)。
 * @return 实际写入 outBuf 的字符数(不含结尾 '\0')。
 * @example
 *   wchar_t buf[64];
 *   u32 n = mdvn::TruncateOutlineTitle(title, len, 180.0f, measurer, buf, 64);
 */
u32 TruncateOutlineTitle(const wchar_t* text, u32 len, float maxWidthDip,
                          const OutlineTextMeasurer& measurer,
                          wchar_t* outBuf, u32 outBufCap);

/**
 * 在按块下标(等价按 y 坐标)天然升序的大纲条目上,二分查找"当前阅读位置"
 * 应高亮的条目——即"最后一个块顶 y <= 当前视口顶 y"的条目,O(log n)。
 *
 * 口径(写死,不做成可配置项):
 *   - 视口顶在第一个标题之前(`viewportTop < itemTops[0]`)时**无高亮**,
 *     返回 `kInvalidIndex`(裁决二选一,选"无高亮":滚到文档最前面、
 *     一个标题都还没露出时,侧栏不应该"假装"高亮第一条)。
 *   - 视口顶正好落在某标题顶部(相等)时,高亮该标题本身。
 *   - 视口顶落在两个标题之间时,高亮上面那一条(最后一个 top <= viewportTop 的)。
 *   - 视口顶落在最后一个标题之后时,高亮最后一条。
 *
 * @param itemTops 每条大纲条目对应块的顶部 y 坐标数组(DIP),按下标升序排列,
 *                 长度为 count;调用方从 `BlockGeometry::top` 按 blockIdx 取出。
 * @param count itemTops 的长度;为 0 时直接返回 `kInvalidIndex`。
 * @param viewportTop 当前视口顶部 y 坐标(DIP)。
 * @return 应高亮的条目下标;无高亮返回 `kInvalidIndex`。
 * @example u32 idx = mdvn::FindCurrentOutlineItem(tops, n, scrollY);
 */
u32 FindCurrentOutlineItem(const float* itemTops, u32 count, float viewportTop);

/**
 * 点击大纲侧栏时,把"侧栏自身坐标系里的点击 y"换算成命中的条目下标(T64)。
 *
 * 与渲染层 `rowTop = i * kOutlineItemHeightDip - scrollY` 用同一套换算
 * (见 `renderer.cpp` 的 `DrawOutlinePanel`),因此点击命中的条目与视觉上看到
 * 的条目永远一致。
 *
 * @param itemCount 大纲条目总数;为 0 时直接返回 `kInvalidIndex`。
 * @param panelLocalY 点击位置在侧栏坐标系里的 y(DIP,未减去侧栏自身滚动偏移)。
 * @param scrollY 侧栏自身滚动偏移(DIP),即 `OutlinePanel::ScrollY()`。
 * @return 命中的条目下标;点落在侧栏内容之外(顶部之前/末条之后)返回
 *         `kInvalidIndex`。
 * @example u32 idx = mdvn::FindOutlineItemAtY(panel.ItemCount(), localY, panel.ScrollY());
 */
u32 FindOutlineItemAtY(u32 itemCount, float panelLocalY, float scrollY);

/**
 * 大纲侧栏运行期状态(T63)。
 *
 * "关闭时开销为 0"的实现手法:本类不做任何隐藏的常驻实例——
 * `window.cpp` 只有在用户按下 `Ctrl+\` 打开侧栏时,才会在专门为侧栏准备的
 * Arena 上构造一个本类实例,`WindowState::outline` 由 `nullptr` 变为该实例
 * 指针;再次按下 `Ctrl+\` 关闭时指针重新置空,不调用任何析构逻辑
 * (成员全部是 POD/Arena 绑定容器,无需析构)。
 *
 * @example
 *   mdvn::Arena arena;
 *   arena.Init(4 * 1024 * 1024);
 *   mdvn::OutlinePanel panel(&arena);
 *   panel.Rebuild(doc);
 *   u32 idx = panel.CurrentItem();
 */
class OutlinePanel {
public:
    /**
     * 绑定一块 Arena,不立即提取大纲(`Rebuild` 才会做,不在构造函数里做,
     * 保持"构造即副作用"最小化)。
     * @param arena 大纲条目数组所在 Arena,生命周期须覆盖本对象;不拥有。
     */
    explicit OutlinePanel(Arena* arena)
        : items_(arena), currentItem_(kInvalidIndex), scrollY_(0.0f) {}

    OutlinePanel(const OutlinePanel&) = delete;
    OutlinePanel& operator=(const OutlinePanel&) = delete;

    /**
     * 提取文档大纲(重新构建条目数组),侧栏刚打开或文档被替换时调用一次。
     * @param doc 当前文档模型。
     * @return 提取到的条目数(与 `ItemCount()` 相同)。
     * @example u32 n = panel.Rebuild(doc);
     */
    u32 Rebuild(const Document& doc) {
        currentItem_ = kInvalidIndex;
        scrollY_ = 0.0f;
        return ExtractOutline(doc, &items_);
    }

    // 当前大纲条目总数。
    u32 ItemCount() const { return items_.Size(); }

    // 按下标取一条大纲条目,调用方保证 index < ItemCount()。
    const OutlineItem& Item(u32 index) const { return items_[index]; }

    // 取全部条目的只读视图(按块下标升序),供渲染层批量读取;可能为空
    // (`ItemCount() == 0` 时 `data` 为 nullptr)。
    Span<const OutlineItem> Items() const {
        return Span<const OutlineItem>{items_.Data(), items_.Size()};
    }

    // 当前应高亮的条目下标;`kInvalidIndex` 表示无高亮。
    u32 CurrentItem() const { return currentItem_; }

    // 直接设置当前高亮条目(由 window.cpp 在去抖定时器到点后调用)。
    void SetCurrentItem(u32 index) { currentItem_ = index; }

    // 侧栏自身的滚动偏移(DIP),0 表示第一条顶部对齐侧栏顶部。
    float ScrollY() const { return scrollY_; }

    /**
     * 设置侧栏自身滚动偏移,夹到 `[0, MaxScrollOffset(内容总高度, 侧栏高度)]`。
     * @param y 期望的滚动偏移(DIP)。
     * @param panelHeightDip 侧栏可视高度(DIP)。
     * @example panel.SetScrollY(40.0f, 500.0f);
     */
    void SetScrollY(float y, float panelHeightDip) {
        scrollY_ = ClampScrollOffset(y, OutlinePanelContentHeightDip(ItemCount()), panelHeightDip);
    }

private:
    Vec<OutlineItem> items_;  // 大纲条目数组,绑定在调用方给的 Arena 上
    u32 currentItem_;         // 当前高亮条目下标,4 字节,kInvalidIndex 表示无
    float scrollY_;           // 侧栏自身滚动偏移(DIP)
};

}  // namespace mdvn
