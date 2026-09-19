// 欢迎屏(未打开任何文档时的静态引导页)几何/命中测试覆盖:"打开文件"
// 按钮的矩形计算与点命中判断,均为纯数字函数,不依赖真实 HWND/D2D
// (见 shell/welcome_screen.h 顶部注释)。
#include "markair_test.h"
#include "../src/shell/welcome_screen.h"

using markair::IsPointInWelcomeButton;
using markair::kWelcomeButtonHeightDip;
using markair::kWelcomeButtonTopRatio;
using markair::kWelcomeButtonWidthDip;
using markair::WelcomeButtonRectDip;

// 按钮水平居中:左右两侧到客户区边缘的距离相等,宽度恒为 kWelcomeButtonWidthDip。
MARKAIR_TEST(WelcomeScreen_ButtonRectIsHorizontallyCentered) {
    float clientW = 800.0f;
    float clientH = 600.0f;
    auto rect = WelcomeButtonRectDip(clientW, clientH);
    MARKAIR_CHECK(rect.right - rect.left == kWelcomeButtonWidthDip);
    float leftGap = rect.left;
    float rightGap = clientW - rect.right;
    MARKAIR_CHECK(leftGap == rightGap);
}

// 按钮垂直位置按客户区高度的固定比例计算,高度恒为 kWelcomeButtonHeightDip。
MARKAIR_TEST(WelcomeScreen_ButtonRectTopFollowsHeightRatio) {
    float clientH = 600.0f;
    auto rect = WelcomeButtonRectDip(800.0f, clientH);
    MARKAIR_CHECK(rect.top == clientH * kWelcomeButtonTopRatio);
    MARKAIR_CHECK(rect.bottom - rect.top == kWelcomeButtonHeightDip);
}

// 客户区变宽/变高后按钮矩形应跟着重新居中/重新定位,不是钉死的绝对像素。
MARKAIR_TEST(WelcomeScreen_ButtonRectTracksClientResize) {
    auto small = WelcomeButtonRectDip(800.0f, 600.0f);
    auto large = WelcomeButtonRectDip(1600.0f, 1200.0f);
    MARKAIR_CHECK(large.left != small.left);
    MARKAIR_CHECK(large.top != small.top);
}

// 命中测试:矩形内部(含左上边界、不含右下边界,半开区间)命中,矩形外不命中。
MARKAIR_TEST(WelcomeScreen_HitTestChecksRectBoundsHalfOpen) {
    auto rect = WelcomeButtonRectDip(800.0f, 600.0f);
    MARKAIR_CHECK(IsPointInWelcomeButton(rect, rect.left, rect.top));
    MARKAIR_CHECK(IsPointInWelcomeButton(
        rect, (rect.left + rect.right) * 0.5f, (rect.top + rect.bottom) * 0.5f));
    MARKAIR_CHECK(!IsPointInWelcomeButton(rect, rect.right, rect.top));   // 右边界不含
    MARKAIR_CHECK(!IsPointInWelcomeButton(rect, rect.left, rect.bottom)); // 下边界不含
    MARKAIR_CHECK(!IsPointInWelcomeButton(rect, rect.left - 1.0f, rect.top));
    MARKAIR_CHECK(!IsPointInWelcomeButton(rect, rect.left, rect.top - 1.0f));
}
