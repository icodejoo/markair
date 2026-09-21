// 统一按钮抽象(shell/button.h)的纯几何/命中测试/悬浮提示定位函数覆盖。
#include "markair_test.h"
#include "../src/shell/button.h"

using markair::Button;
using markair::ButtonRectDip;
using markair::ButtonRectFromCenterDip;
using markair::ClampTooltipLeftDip;
using markair::IconButton;
using markair::IconButtonRectDip;
using markair::PointInButtonRectDip;
using markair::PointInRectDip;
using markair::PointInRectDipInclusive;
using markair::TooltipBubbleVerticalDip;

// 半开区间命中测试:含左上边界,不含右下边界。
MARKAIR_TEST(Button_PointInRectDip_HalfOpen) {
    MARKAIR_CHECK(PointInRectDip(0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f));
    MARKAIR_CHECK(PointInRectDip(0.0f, 0.0f, 10.0f, 10.0f, 5.0f, 5.0f));
    MARKAIR_CHECK(!PointInRectDip(0.0f, 0.0f, 10.0f, 10.0f, 10.0f, 5.0f));
    MARKAIR_CHECK(!PointInRectDip(0.0f, 0.0f, 10.0f, 10.0f, 5.0f, 10.0f));
    MARKAIR_CHECK(!PointInRectDip(0.0f, 0.0f, 10.0f, 10.0f, -1.0f, 5.0f));
}

// 闭区间命中测试:含全部四条边界。
MARKAIR_TEST(Button_PointInRectDipInclusive_Closed) {
    MARKAIR_CHECK(PointInRectDipInclusive(0.0f, 0.0f, 10.0f, 10.0f, 10.0f, 10.0f));
    MARKAIR_CHECK(PointInRectDipInclusive(0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f));
    MARKAIR_CHECK(!PointInRectDipInclusive(0.0f, 0.0f, 10.0f, 10.0f, 10.01f, 5.0f));
}

// ButtonRectDip 版本与拆字段版本结果一致。
MARKAIR_TEST(Button_PointInButtonRectDip_MatchesFields) {
    ButtonRectDip rect{2.0f, 3.0f, 12.0f, 13.0f};
    MARKAIR_CHECK(PointInButtonRectDip(rect, 5.0f, 5.0f));
    MARKAIR_CHECK(!PointInButtonRectDip(rect, 12.0f, 5.0f));
}

// Button 按宽高与几何中心算出的矩形,中心点应回落在矩形正中。
MARKAIR_TEST(Button_ButtonRectFromCenterDip_CentersOnPoint) {
    Button btn{140.0f, 32.0f, 4.0f, L"打开文件", L"打开文件", nullptr, nullptr};
    ButtonRectDip r = ButtonRectFromCenterDip(btn, 100.0f, 50.0f);
    MARKAIR_CHECK(r.Width() == 140.0f);
    MARKAIR_CHECK(r.Height() == 32.0f);
    MARKAIR_CHECK((r.left + r.right) * 0.5f == 100.0f);
    MARKAIR_CHECK((r.top + r.bottom) * 0.5f == 50.0f);
}

// IconButton 用 size 同时充当宽高,算出的矩形必为正方形。
MARKAIR_TEST(Button_IconButtonRectDip_IsSquare) {
    IconButton btn{24.0f, 4.0f, L"查找", nullptr, nullptr};
    ButtonRectDip r = IconButtonRectDip(btn, 60.0f, 80.0f);
    MARKAIR_CHECK(r.Width() == 24.0f);
    MARKAIR_CHECK(r.Height() == 24.0f);
    MARKAIR_CHECK((r.left + r.right) * 0.5f == 60.0f);
    MARKAIR_CHECK((r.top + r.bottom) * 0.5f == 80.0f);
}

// 悬浮提示水平位置:居中于锚点、超出屏幕范围会被钳制在边距内。
MARKAIR_TEST(Button_ClampTooltipLeftDip_ClampsToScreen) {
    // 正常情况不需要钳制。
    MARKAIR_CHECK(ClampTooltipLeftDip(100.0f, 80.0f, 800.0f, 4.0f) == 100.0f);
    // 太靠左,钳到 marginDip。
    MARKAIR_CHECK(ClampTooltipLeftDip(-10.0f, 80.0f, 800.0f, 4.0f) == 4.0f);
    // 太靠右,钳到 clientWidthDip - marginDip - bubbleWidth。
    MARKAIR_CHECK(ClampTooltipLeftDip(790.0f, 80.0f, 800.0f, 4.0f) == 800.0f - 4.0f - 80.0f);
}

// 悬浮提示竖直位置:优先显示在锚点上方,与锚点上边留 gap 间隙。
MARKAIR_TEST(Button_TooltipBubbleVerticalDip_PrefersAbove) {
    auto v = TooltipBubbleVerticalDip(100.0f, 124.0f, 22.0f, 4.0f, 0.0f);
    MARKAIR_CHECK(v.bottom == 100.0f - 4.0f);
    MARKAIR_CHECK(v.top == v.bottom - 22.0f);
}

// 上方放不下(minTopDip 卡住)时退化到锚点下方。
MARKAIR_TEST(Button_TooltipBubbleVerticalDip_FallsBackBelowWhenNoRoomAbove) {
    auto v = TooltipBubbleVerticalDip(10.0f, 34.0f, 22.0f, 4.0f, 5.0f);
    // 上方算出的 top = 10 - 4 - 22 = -16,小于 minTopDip(5),应退化到下方。
    MARKAIR_CHECK(v.top == 34.0f + 4.0f);
    MARKAIR_CHECK(v.bottom == v.top + 22.0f);
}

// 下方超出 maxBottomDip 时整体上移贴住 maxBottomDip。
MARKAIR_TEST(Button_TooltipBubbleVerticalDip_ClampsToMaxBottom) {
    auto v = TooltipBubbleVerticalDip(10.0f, 34.0f, 22.0f, 4.0f, 5.0f, 50.0f);
    // 退化到下方后 bottom = 34+4+22 = 60,超出 maxBottomDip(50),应贴住 50。
    MARKAIR_CHECK(v.bottom == 50.0f);
    MARKAIR_CHECK(v.top == 50.0f - 22.0f);
}

// IconButton::ToButton() 转换后宽高均等于 size,label 为空。
MARKAIR_TEST(Button_IconButtonToButton_WidthEqualsHeight) {
    IconButton icon{20.0f, 2.0f, L"提示", nullptr, nullptr};
    Button b = icon.ToButton();
    MARKAIR_CHECK(b.width == 20.0f);
    MARKAIR_CHECK(b.height == 20.0f);
    MARKAIR_CHECK(b.label == nullptr);
    MARKAIR_CHECK(b.title != nullptr);
}
