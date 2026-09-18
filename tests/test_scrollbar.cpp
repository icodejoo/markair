// 自绘滚动条(方案A)的纯函数覆盖测试:滑块几何计算 / 命中测试 / 拖动映射。
// 与 test_scroll.cpp 同一套设计口径,只测"不依赖 HWND/D2D"的那一层。
#include <cmath>

#include "mdvn_test.h"
#include "../src/shell/scrollbar.h"

using mdvn::CalcScrollbarMetrics;
using mdvn::IsPointInScrollbarColumn;
using mdvn::IsPointInScrollbarThumb;
using mdvn::kScrollbarMinThumbHeightDip;
using mdvn::ScrollbarMetrics;
using mdvn::ScrollYAfterThumbDrag;
using mdvn::ScrollYAfterTrackClick;

namespace {
bool NearlyEqual(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }
}  // namespace

// 内容不超过一屏:不显示滚动条。
MDVN_TEST(ScrollbarHiddenWhenContentFitsViewport) {
    ScrollbarMetrics m = CalcScrollbarMetrics(800.0f, 600.0f, 500.0f, 0.0f);
    MDVN_CHECK(!m.visible);

    m = CalcScrollbarMetrics(800.0f, 600.0f, 600.0f, 0.0f);
    MDVN_CHECK(!m.visible);
}

// 滚到顶部/底部时,滑块应贴视口上/下边缘。
MDVN_TEST(ScrollbarThumbAtTopAndBottom) {
    ScrollbarMetrics top = CalcScrollbarMetrics(800.0f, 600.0f, 2000.0f, 0.0f);
    MDVN_CHECK(top.visible);
    MDVN_CHECK(NearlyEqual(top.top, 0.0f));

    ScrollbarMetrics bottom = CalcScrollbarMetrics(800.0f, 600.0f, 2000.0f, 1400.0f);  // maxScrollY = 1400
    MDVN_CHECK(NearlyEqual(bottom.bottom, 600.0f));
}

// 滑块高度按视口/内容比例算,但不低于最小可点高度。
MDVN_TEST(ScrollbarThumbHeightClampedToMinimum) {
    // 内容极长:600/1000000 * 600 远小于最小高度,应被夹到 kScrollbarMinThumbHeightDip。
    ScrollbarMetrics m = CalcScrollbarMetrics(800.0f, 600.0f, 1000000.0f, 0.0f);
    MDVN_CHECK(m.visible);
    MDVN_CHECK(NearlyEqual(m.bottom - m.top, kScrollbarMinThumbHeightDip));
}

// 命中测试:点在滑块矩形内/外。
MDVN_TEST(ScrollbarHitTestThumb) {
    ScrollbarMetrics m = CalcScrollbarMetrics(800.0f, 600.0f, 2000.0f, 700.0f);
    MDVN_CHECK(m.visible);
    float centerX = (m.left + m.right) * 0.5f;
    float centerY = (m.top + m.bottom) * 0.5f;
    MDVN_CHECK(IsPointInScrollbarThumb(m, centerX, centerY));
    MDVN_CHECK(!IsPointInScrollbarThumb(m, centerX - 100.0f, centerY));  // 横坐标远离滑块
    MDVN_CHECK(!IsPointInScrollbarThumb(m, centerX, 0.0f));              // 纵坐标在滑块之外

    ScrollbarMetrics hidden{};  // visible == false
    MDVN_CHECK(!IsPointInScrollbarThumb(hidden, 0.0f, 0.0f));
}

// 拖动映射:把滑块从顶部拖到底部的最大位移量,应得到内容的最大滚动偏移。
MDVN_TEST(ScrollbarDragMapsFullRange) {
    float viewportHeight = 600.0f;
    float totalHeight = 2000.0f;
    ScrollbarMetrics m = CalcScrollbarMetrics(800.0f, viewportHeight, totalHeight, 0.0f);
    float maxThumbTop = viewportHeight - (m.bottom - m.top);

    float y = ScrollYAfterThumbDrag(0.0f, maxThumbTop, viewportHeight, totalHeight);
    MDVN_CHECK(NearlyEqual(y, totalHeight - viewportHeight, 0.5f));

    // 反向拖到负值应夹到 0。
    float y2 = ScrollYAfterThumbDrag(0.0f, -100.0f, viewportHeight, totalHeight);
    MDVN_CHECK(NearlyEqual(y2, 0.0f));
}

// 内容不超过一屏时拖动不产生任何滚动(不应除以零/崩溃)。
MDVN_TEST(ScrollbarDragNoopWhenContentFitsViewport) {
    float y = ScrollYAfterThumbDrag(0.0f, 50.0f, 600.0f, 400.0f);
    MDVN_CHECK(NearlyEqual(y, 0.0f));
}

// "鼠标是否进入滚动条区域"的悬浮判定:只看横坐标,不管纵坐标。
MDVN_TEST(ScrollbarColumnHoverIgnoresY) {
    MDVN_CHECK(IsPointInScrollbarColumn(800.0f, 794.0f));   // 轨道范围内
    MDVN_CHECK(!IsPointInScrollbarColumn(800.0f, 400.0f));  // 远离轨道
    MDVN_CHECK(!IsPointInScrollbarColumn(800.0f, 799.9f));  // 超出右边距外
}

// Track click: clicking at top/middle/bottom centers thumb at click position and maps to content scroll range.
//
// 点击轨道:点击顶部/中部/底部时,滑块中心对齐点击位置并正确映射到内容滚动范围。
MDVN_TEST(ScrollbarTrackClickTopCenterBottom) {
    float viewportHeight = 600.0f;
    float totalHeight = 2000.0f;
    float maxScrollY = totalHeight - viewportHeight;  // 1400.0f

    // Click at top (0.0f): thumb goes to top, scroll offset should be 0.
    //
    // 点击顶部(0.0f):滑块到顶,滚动偏移应为 0。
    float yTop = ScrollYAfterTrackClick(0.0f, viewportHeight, totalHeight);
    MDVN_CHECK(NearlyEqual(yTop, 0.0f));

    // Click at bottom (viewportHeight): thumb goes to bottom, scroll offset should be maxScrollY.
    //
    // 点击底部(viewportHeight):滑块到底,滚动偏移应为 maxScrollY。
    float yBottom = ScrollYAfterTrackClick(viewportHeight, viewportHeight, totalHeight);
    MDVN_CHECK(NearlyEqual(yBottom, maxScrollY, 0.5f));

    // Click at center (viewportHeight * 0.5f): thumb centered, scroll offset should be maxScrollY * 0.5f.
    //
    // 点击正中央(viewportHeight * 0.5f):滑块居中,滚动偏移应为 maxScrollY * 0.5f。
    float yCenter = ScrollYAfterTrackClick(viewportHeight * 0.5f, viewportHeight, totalHeight);
    MDVN_CHECK(NearlyEqual(yCenter, maxScrollY * 0.5f, 0.5f));
}

// Track click is no-op when content fits within viewport.
//
// 内容不超过一屏时点击轨道不产生任何滚动。
MDVN_TEST(ScrollbarTrackClickNoopWhenContentFitsViewport) {
    float y = ScrollYAfterTrackClick(200.0f, 600.0f, 400.0f);
    MDVN_CHECK(NearlyEqual(y, 0.0f));
}
