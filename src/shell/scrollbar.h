// mdvn 的滚动条几何计算:正文与大纲侧栏共用的自绘滚动条(方案A,替代正文原
// 原生 WS_VSCROLL),全部是纯数字函数,不依赖 HWND/D2D,可脱离 Win32 单测
// (与 shell/scroll.h、shell/hit_test.h 同一套设计口径)。
//
// 约定:滑块贴视口右边缘,宽度固定 kScrollbarWidthDip,两端圆角;内容不超过
// 一屏时不显示(ScrollbarMetrics::visible == false)。
#pragma once

#include "scroll.h"

namespace mdvn {

// 滚动条宽度(DIP),正文与大纲侧栏共用同一数值。
constexpr float kScrollbarWidthDip = 8.0f;

// 滑块圆角半径(DIP),取宽度的一半画成"胶囊"形状。
constexpr float kScrollbarCornerRadiusDip = 4.0f;

// 滑块距视口右边缘的外边距(DIP)。
constexpr float kScrollbarMarginDip = 4.0f;

// 滑块最小高度(DIP):内容极长时按比例算出的滑块可能只有几个像素,夹到这个
// 下限保证鼠标仍能点中/拖动。
constexpr float kScrollbarMinThumbHeightDip = 24.0f;

/** 滚动条滑块的几何(视口局部坐标系,DIP)。 */
struct ScrollbarMetrics {
    bool visible;   // 内容未超过一屏时为 false,此时其余字段无意义
    float left;
    float top;
    float right;
    float bottom;
};

/**
 * 纯几何计算:算出滑块矩形。
 * @param viewportWidthDip 视口宽度(DIP),滑块贴其右边缘。
 * @param viewportHeightDip 视口高度(DIP)。
 * @param totalHeightDip 内容总高度(DIP)。
 * @param scrollYDip 当前滚动偏移(DIP)。
 * @return 滑块几何;内容不超过一屏或视口高度非正时 `visible` 为 false。
 * @example auto m = mdvn::CalcScrollbarMetrics(800.0f, 600.0f, 2000.0f, 100.0f);
 */
inline ScrollbarMetrics CalcScrollbarMetrics(float viewportWidthDip, float viewportHeightDip,
                                              float totalHeightDip, float scrollYDip) {
    ScrollbarMetrics m{};
    if (viewportHeightDip <= 0.0f || totalHeightDip <= viewportHeightDip) return m;

    m.visible = true;
    m.right = viewportWidthDip - kScrollbarMarginDip;
    m.left = m.right - kScrollbarWidthDip;

    float thumbHeight = viewportHeightDip * (viewportHeightDip / totalHeightDip);
    if (thumbHeight < kScrollbarMinThumbHeightDip) thumbHeight = kScrollbarMinThumbHeightDip;
    if (thumbHeight > viewportHeightDip) thumbHeight = viewportHeightDip;

    float maxScrollY = totalHeightDip - viewportHeightDip;
    float maxThumbTop = viewportHeightDip - thumbHeight;
    float ratio = (maxScrollY > 0.0f) ? (scrollYDip / maxScrollY) : 0.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    m.top = ratio * maxThumbTop;
    m.bottom = m.top + thumbHeight;
    return m;
}

/**
 * 命中测试:一个视口局部坐标点是否落在滑块矩形内。
 * @param m `CalcScrollbarMetrics` 的结果。
 * @param localXDip 视口局部横坐标(DIP)。
 * @param localYDip 视口局部纵坐标(DIP)。
 * @return 落在滑块内返回 true;`m.visible == false` 时恒为 false。
 * @example bool hit = mdvn::IsPointInScrollbarThumb(m, 794.0f, 50.0f);
 */
inline bool IsPointInScrollbarThumb(const ScrollbarMetrics& m, float localXDip, float localYDip) {
    if (!m.visible) return false;
    return localXDip >= m.left && localXDip <= m.right && localYDip >= m.top && localYDip <= m.bottom;
}

/**
 * 判断一个点是否落在滚动条的横向范围内(不看纵坐标)——用于"鼠标是否进入
 * 滚动条区域"的悬浮判定:轨道常驻贴视口右边缘、纵向铺满整个视口,只要横
 * 坐标落在轨道宽度内就算"进入",调用方自行再夹一次纵坐标在 `[0, 视口高度]`
 * 区间内(不含底部操作栏等其他控件占用的区域)。
 * @param viewportWidthDip 视口宽度(DIP),轨道贴其右边缘。
 * @param localXDip 视口局部横坐标(DIP)。
 * @return 落在轨道横向范围内返回 true。
 * @example bool inColumn = mdvn::IsPointInScrollbarColumn(800.0f, 794.0f);
 */
inline bool IsPointInScrollbarColumn(float viewportWidthDip, float localXDip) {
    float right = viewportWidthDip - kScrollbarMarginDip;
    float left = right - kScrollbarWidthDip;
    return localXDip >= left && localXDip <= right;
}

/**
 * 拖动滑块:把鼠标纵向位移换算成新的滚动偏移(已夹到合法区间)。
 * 换算比例是"滑块可移动距离 : 内容可滚动距离",与 `CalcScrollbarMetrics`
 * 用同一套滑块高度公式,保证拖到底/拖到顶时滑块与内容同时到边。
 * @param dragStartScrollYDip 本次拖动开始时的滚动偏移(DIP)。
 * @param dragDeltaYDip 鼠标纵向位移(当前位置 - 按下位置,DIP)。
 * @param viewportHeightDip 视口高度(DIP)。
 * @param totalHeightDip 内容总高度(DIP)。
 * @return 夹取后的新滚动偏移(DIP);内容不超过一屏时原样返回 `dragStartScrollYDip`。
 * @example float y = mdvn::ScrollYAfterThumbDrag(100.0f, 20.0f, 600.0f, 2000.0f);
 */
inline float ScrollYAfterThumbDrag(float dragStartScrollYDip, float dragDeltaYDip,
                                    float viewportHeightDip, float totalHeightDip) {
    if (viewportHeightDip <= 0.0f || totalHeightDip <= viewportHeightDip) return dragStartScrollYDip;

    float thumbHeight = viewportHeightDip * (viewportHeightDip / totalHeightDip);
    if (thumbHeight < kScrollbarMinThumbHeightDip) thumbHeight = kScrollbarMinThumbHeightDip;
    if (thumbHeight > viewportHeightDip) thumbHeight = viewportHeightDip;

    float maxThumbTop = viewportHeightDip - thumbHeight;
    if (maxThumbTop <= 0.0f) return dragStartScrollYDip;

    float maxScrollY = totalHeightDip - viewportHeightDip;
    float newScrollY = dragStartScrollYDip + dragDeltaYDip * (maxScrollY / maxThumbTop);
    return ClampScrollOffset(newScrollY, totalHeightDip, viewportHeightDip);
}

}  // namespace mdvn
