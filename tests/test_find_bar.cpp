// 查找条几何(shell/find_bar.h)的纯函数覆盖测试:布局计算与箭头/关闭按钮命中测试。
#include "markair_test.h"
#include "../src/shell/find_bar.h"

using markair::ComputeFindBarLayout;
using markair::FindBarLayout;
using markair::FindBarNavHit;
using markair::FindBarNavHitTest;
using markair::kFindBarCloseButtonWidthDip;
using markair::kFindBarNavButtonWidthDip;

// 关闭按钮应排在"下一个"箭头之后,且不与其重叠。
MARKAIR_TEST(FindBarCloseButtonAfterNextArrow) {
    FindBarLayout layout = ComputeFindBarLayout(1200.0f, 0.0f);
    MARKAIR_CHECK(layout.closeLeft >= layout.nextLeft + kFindBarNavButtonWidthDip);
}

// 点击关闭按钮矩形内应命中 Close,矩形外(比如往左一点,落在"下一个"箭头上)不应命中。
MARKAIR_TEST(FindBarNavHitTestDetectsClose) {
    FindBarLayout layout = ComputeFindBarLayout(1200.0f, 0.0f);
    float cy = layout.top + layout.height * 0.5f;

    FindBarNavHit hit = FindBarNavHitTest(layout, layout.closeLeft + kFindBarCloseButtonWidthDip * 0.5f, cy);
    MARKAIR_CHECK(hit == FindBarNavHit::Close);

    FindBarNavHit missHit = FindBarNavHitTest(layout, layout.nextLeft + kFindBarNavButtonWidthDip * 0.5f, cy);
    MARKAIR_CHECK(missHit == FindBarNavHit::Next);
}

// 查找条整体宽度必须能容纳到关闭按钮的右边缘,否则关闭按钮会画出条外。
MARKAIR_TEST(FindBarWidthCoversCloseButton) {
    FindBarLayout layout = ComputeFindBarLayout(1200.0f, 0.0f);
    MARKAIR_CHECK(layout.closeLeft + kFindBarCloseButtonWidthDip <= layout.left + layout.width);
}
