// mdvn find bar geometry computation (2026-09-19 revision: query editing is
// now delegated to a native Win32 EDIT child window, gaining cursor/
// selection/clipboard/IME support for free instead of hand-drawing a fake
// input box with no cursor). The find bar itself is still a plain D2D-drawn
// rounded floating bar; it only carves out a fixed rectangle for the EDIT
// control — four fixed-width segments ("Find: " prefix / edit box / status
// text like "9/11" / prev+next arrow buttons) laid out left to right. The
// prefix width never resizes with query content, so the layout stays stable
// for the whole find session.
//
// Pure geometry functions with no HWND/D2D dependency, unit-testable outside
// Win32 (same design convention as shell/scrollbar.h and shell/hit_test.h).
// The render layer redraws background/prefix/status text/arrows using the
// same numbers and never includes this header back (same "render never
// includes shell" constraint as shell/scrollbar.h and similar headers).
//
// mdvn 查找条几何计算(2026-09-19 改版:查询串编辑交给原生 Win32 EDIT 子
// 窗口,换来光标/选区/剪贴板/IME 全套免费能力,不再自己拼一个没有光标的
// 假输入框)。查找条本身仍是纯 D2D 画的圆角浮出条,只是内部让出一块固定
// 矩形给 EDIT 控件——四段("查找: " 前缀 / 编辑框 / "9/11"这类状态文字 /
// 上一个+下一个箭头按钮)固定宽度左右排列,不随查询串内容动态量前缀文字
// 宽度,布局在整个查找会话期间保持稳定。
//
// 纯几何函数,不依赖 HWND/D2D,可脱离 Win32 单测(与 shell/scrollbar.h、
// shell/hit_test.h 同一套设计口径)。render 层按同一套数值重复画背景/前缀/
// 状态文字/箭头,不反向 include 本头文件(与 shell/scrollbar.h 等头文件同一条
// "render 不反向 include shell" 约束)。
#pragma once

#include "../util/types.h"

namespace mdvn {

constexpr float kFindBarPaddingXDip = 10.0f;
constexpr float kFindBarPrefixWidthDip = 44.0f;   // "查找: " 标签宽度
constexpr float kFindBarEditWidthDip = 150.0f;    // 查询串编辑框宽度
constexpr float kFindBarGapDip = 8.0f;            // 各段之间的间距
constexpr float kFindBarStatusNavGapDip = 4.0f;   // 状态文字("9/11")与后面箭头按钮之间的间距,比其余段间距更紧凑
constexpr float kFindBarStatusWidthDip = 50.0f;   // 状态文字("9/11"/"0/0")宽度
constexpr float kFindBarNavButtonWidthDip = 22.0f; // 上一个/下一个箭头按钮的正方形边长
constexpr float kFindBarCloseButtonWidthDip = 22.0f; // 关闭按钮的正方形边长,与箭头按钮同宽
constexpr float kFindBarHeightDip = 26.0f;
constexpr float kFindBarMarginDip = 10.0f;        // 距客户区右上角外边距
constexpr float kFindBarEditInsetYDip = 3.0f;     // 编辑框相对查找条上下各留的边距
constexpr float kFindBarFontSizeDip = 13.0f;

// 查找条总宽度:两侧内边距 + 前缀 + 编辑框 + 间距 + 状态文字 + 紧凑间距 +
// 两个箭头按钮 + 间距 + 关闭按钮,固定值,不随查询串内容变化(与旧版"整条
// 文字一起量宽度"的自适应气泡不同)。
constexpr float kFindBarWidthDip = kFindBarPaddingXDip * 2.0f + kFindBarPrefixWidthDip +
                                    kFindBarEditWidthDip + kFindBarGapDip + kFindBarStatusWidthDip +
                                    kFindBarStatusNavGapDip + kFindBarNavButtonWidthDip * 2.0f +
                                    kFindBarGapDip + kFindBarCloseButtonWidthDip;

/**
 * Full geometry of the find bar within the client area (DIP), top-left
 * origin of each part's rectangle.
 *
 * 查找条在客户区(DIP)里的完整几何,各部分矩形左上角原点。
 */
struct FindBarLayout {
    // Find bar background rectangle.
    //
    // 查找条背景矩形。
    float left, top, width, height;

    // Native EDIT control rectangle.
    //
    // 原生 Edit 控件矩形。
    float editLeft, editTop, editWidth, editHeight;

    // "Find: " label draw origin x (vertically centered on the same row as top).
    //
    // "查找: " 标签绘制起点 x(与 top 同一行居中)。
    float prefixLeft;

    // Status text ("9/11") draw origin x.
    //
    // 状态文字绘制起点 x。
    float statusLeft;

    // "Previous" arrow button rectangle's top-left x.
    //
    // "上一个"箭头按钮矩形左上角 x。
    float prevLeft;

    // "Next" arrow button rectangle's top-left x.
    //
    // "下一个"箭头按钮矩形左上角 x。
    float nextLeft;

    // Close button rectangle's top-left x.
    //
    // 关闭按钮矩形左上角 x。
    float closeLeft;
};

/**
 * Pure geometry calculation: works out where the find bar (including the
 * embedded Edit control's rectangle) sits in the client area. Anchored to
 * the client area's top-right corner; clamped as a whole to
 * `kFindBarMarginDip` when it would overflow the left edge (a fallback for
 * very narrow windows, using the same clamping convention as the original
 * DrawOverlayBar).
 *
 * 纯几何计算:算出查找条(含内嵌 Edit 控件矩形)在客户区里的位置。贴客户区
 * 右上角,超出左边界时整体钳到 `kFindBarMarginDip`(极窄窗口下的兜底,与
 * DrawOverlayBar 原有的钳位口径一致)。
 *
 * @param clientWidthDip Client area width (DIP).
 *
 *   客户区宽度(DIP)。
 *
 * @param topOffsetDip Extra downward offset (when the find bar and an
 *   in-window hint appear together, the hint bar uses this to push itself
 *   below the find bar; the find bar itself always passes 0).
 *
 *   额外下移量(查找条 + 窗口内提示同时出现时,提示条用这个把自己推到查找条
 *   下方;查找条本身恒传 0)。
 *
 * @return The find bar's part rectangles.
 *
 *   查找条各部分矩形。
 *
 * @example auto layout = mdvn::ComputeFindBarLayout(1200.0f, 0.0f);
 */
inline FindBarLayout ComputeFindBarLayout(float clientWidthDip, float topOffsetDip) {
    FindBarLayout r{};
    r.width = kFindBarWidthDip;
    r.height = kFindBarHeightDip;
    float right = clientWidthDip - kFindBarMarginDip;
    r.left = right - r.width;
    if (r.left < kFindBarMarginDip) r.left = kFindBarMarginDip;
    r.top = kFindBarMarginDip + topOffsetDip;

    r.prefixLeft = r.left + kFindBarPaddingXDip;
    r.editLeft = r.prefixLeft + kFindBarPrefixWidthDip;
    r.editTop = r.top + kFindBarEditInsetYDip;
    r.editWidth = kFindBarEditWidthDip;
    r.editHeight = r.height - kFindBarEditInsetYDip * 2.0f;
    r.statusLeft = r.editLeft + r.editWidth + kFindBarGapDip;
    r.prevLeft = r.statusLeft + kFindBarStatusWidthDip + kFindBarStatusNavGapDip;
    r.nextLeft = r.prevLeft + kFindBarNavButtonWidthDip;
    r.closeLeft = r.nextLeft + kFindBarNavButtonWidthDip + kFindBarGapDip;
    return r;
}

/**
 * Clickable arrow buttons on the find bar; `None` means the click landed on
 * the rest of the find bar's area (or outside it).
 *
 * 查找条上可点击的箭头按钮;`None` 表示点击落在查找条其余区域(或条外)。
 */
enum class FindBarNavHit {
    None,
    Prev,
    Next,
    Close,
};

/**
 * Hit test: does the client-area point land on the find bar's "previous" /
 * "next" arrow buttons.
 *
 * 命中测试:客户区坐标点落在查找条的"上一个"/"下一个"箭头按钮上吗。
 *
 * @param layout The find bar geometry computed by `ComputeFindBarLayout`.
 *
 *   `ComputeFindBarLayout` 算出的查找条几何。
 *
 * @param xDip Mouse x (DIP, client-area coordinate).
 *
 *   鼠标 x(DIP,客户区坐标)。
 *
 * @param yDip Mouse y (DIP, client-area coordinate).
 *
 *   鼠标 y(DIP,客户区坐标)。
 *
 * @return The hit arrow button, or `FindBarNavHit::None`.
 *
 *   命中的箭头按钮,或 `FindBarNavHit::None`。
 *
 * @example auto hit = mdvn::FindBarNavHitTest(layout, dipX, dipY);
 */
inline FindBarNavHit FindBarNavHitTest(const FindBarLayout& layout, float xDip, float yDip) {
    if (yDip < layout.top || yDip > layout.top + layout.height) return FindBarNavHit::None;
    if (xDip >= layout.prevLeft && xDip < layout.prevLeft + kFindBarNavButtonWidthDip) {
        return FindBarNavHit::Prev;
    }
    if (xDip >= layout.nextLeft && xDip < layout.nextLeft + kFindBarNavButtonWidthDip) {
        return FindBarNavHit::Next;
    }
    if (xDip >= layout.closeLeft && xDip < layout.closeLeft + kFindBarCloseButtonWidthDip) {
        return FindBarNavHit::Close;
    }
    return FindBarNavHit::None;
}

}  // namespace mdvn
