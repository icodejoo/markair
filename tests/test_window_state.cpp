// T56 覆盖测试:窗口状态记忆的纯几何逻辑(越界钳制 + 多开层叠偏移)。
// 全部用桩显示器矩形,不碰任何真实 Win32 API,可在无图形环境下跑。
#include "mdvn_test.h"
#include "../src/shell/window_state.h"

using mdvn::ApplyCascadeOffset;
using mdvn::ClampMinimumSize;
using mdvn::ClampWindowRectToMonitors;
using mdvn::FindMonitorContaining;
using mdvn::kCascadeOffsetDip;
using mdvn::i32;
using mdvn::kMinWindowHeight;
using mdvn::kMinWindowWidth;
using mdvn::MonitorRect;
using mdvn::RectI;
using mdvn::RectsOverlap;
using mdvn::SameOrigin;
using mdvn::u32;

namespace {

// 单屏场景常用的一台 1920x1080 显示器(工作区去掉底部 40px 任务栏)。
MonitorRect SinglePrimaryMonitor() {
    return MonitorRect{RectI{0, 0, 1920, 1080}, RectI{0, 0, 1920, 1040}};
}

bool RectEquals(const RectI& a, const RectI& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

}  // namespace

// 用例:单屏正常恢复 —— 矩形完全落在唯一显示器内,原样保留。
MDVN_TEST(WindowState_SingleMonitorRestoresUnchanged) {
    MonitorRect mon = SinglePrimaryMonitor();
    RectI saved{100, 100, 900, 700};
    RectI result = ClampWindowRectToMonitors(saved, &mon, 1, 0);
    MDVN_CHECK(RectEquals(result, saved));
}

// 用例:副屏被拔掉 —— 保存时的矩形落在原来的副屏(已不存在)上,
// 恢复时只有主屏这一个显示器,越界必须钳到主屏工作区中心。
MDVN_TEST(WindowState_SecondaryMonitorRemovedClampsToPrimaryCenter) {
    MonitorRect mon = SinglePrimaryMonitor();
    RectI saved{2000, 100, 2800, 700};  // 原本在副屏(1920 起)上,现在副屏不存在
    RectI result = ClampWindowRectToMonitors(saved, &mon, 1, 0);

    MDVN_CHECK(FindMonitorContaining(result, &mon, 1) == 0);
    // 居中:工作区宽 1920、高 1040,窗口宽 800、高 600。
    i32 expectedLeft = 0 + (1920 - 800) / 2;
    i32 expectedTop = 0 + (1040 - 600) / 2;
    MDVN_CHECK_EQ(result.left, expectedLeft);
    MDVN_CHECK_EQ(result.top, expectedTop);
    MDVN_CHECK_EQ(result.Width(), 800);
    MDVN_CHECK_EQ(result.Height(), 600);
}

// 用例:副屏在主屏左侧(负坐标)—— 合法情形,不能被当成越界。
MDVN_TEST(WindowState_NegativeCoordinatesOnLeftMonitorAreLegal) {
    MonitorRect mons[] = {
        SinglePrimaryMonitor(),
        MonitorRect{RectI{-1920, 0, 0, 1080}, RectI{-1920, 0, 0, 1040}},  // 副屏在主屏左侧
    };
    RectI saved{-1800, 100, -1000, 700};  // 落在副屏(负坐标)上
    RectI result = ClampWindowRectToMonitors(saved, mons, 2, 0);
    MDVN_CHECK(RectEquals(result, saved));  // 负坐标本身合法,原样保留
    MDVN_CHECK(FindMonitorContaining(result, mons, 2) == 1);
}

// 用例:矩形部分越界 —— 只要与某个显示器还有重叠(标题栏可见)就算合法,
// 不触发居中钳制。
MDVN_TEST(WindowState_PartiallyOffscreenIsAllowed) {
    MonitorRect mon = SinglePrimaryMonitor();
    RectI saved{1800, 100, 2600, 700};  // 右边界超出 1920,但左边界还在屏内
    RectI result = ClampWindowRectToMonitors(saved, &mon, 1, 0);
    MDVN_CHECK(RectEquals(result, saved));  // 部分越界不钳制位置
    MDVN_CHECK(FindMonitorContaining(result, &mon, 1) == 0);
}

// 用例:矩形完全越界 —— 与任何显示器都没有重叠,必须钳到主屏工作区中心。
MDVN_TEST(WindowState_FullyOffscreenClampsToPrimaryCenter) {
    MonitorRect mon = SinglePrimaryMonitor();
    RectI saved{5000, 5000, 5800, 5600};  // 与唯一显示器毫无重叠
    RectI result = ClampWindowRectToMonitors(saved, &mon, 1, 0);
    MDVN_CHECK(FindMonitorContaining(result, &mon, 1) == 0);
    MDVN_CHECK_EQ(result.left, (1920 - 800) / 2);
    MDVN_CHECK_EQ(result.top, (1040 - 600) / 2);
}

// 用例:尺寸小于下限 —— 无论是否越界,尺寸都要先被钳到 kMinWindowWidth/Height。
MDVN_TEST(WindowState_TinySizeClampedToMinimum) {
    MonitorRect mon = SinglePrimaryMonitor();
    RectI tiny{100, 100, 101, 101};  // 1x1
    RectI result = ClampWindowRectToMonitors(tiny, &mon, 1, 0);
    MDVN_CHECK_EQ(result.Width(), kMinWindowWidth);
    MDVN_CHECK_EQ(result.Height(), kMinWindowHeight);
    MDVN_CHECK_EQ(result.left, 100);  // 原点不动,只扩右/下边界
    MDVN_CHECK_EQ(result.top, 100);

    // 单独验证 ClampMinimumSize 本身(供 window.cpp 复用的独立子步骤)。
    RectI sized = ClampMinimumSize(RectI{0, 0, 10, 10}, kMinWindowWidth, kMinWindowHeight);
    MDVN_CHECK_EQ(sized.Width(), kMinWindowWidth);
    MDVN_CHECK_EQ(sized.Height(), kMinWindowHeight);
}

// 用例:最大化态恢复 —— 越界钳制作用在"还原态矩形"(rcNormalPosition)上,
// 与是否最大化这个布尔标记完全独立;这里验证同一份矩形不受 maximized
// 语义影响,钳制结果与非最大化场景一致(window.cpp 负责在拿到钳制结果后
// 决定要不要额外调 SW_MAXIMIZE,不属于本纯函数的职责)。
MDVN_TEST(WindowState_MaximizedRestoreUsesNormalRectClamping) {
    MonitorRect mon = SinglePrimaryMonitor();
    // 保存时窗口是最大化的,rcNormalPosition 记录的是还原后的正常矩形,
    // 可能仍然越界(比如切换过分辨率),同样要走越界钳制。
    RectI normalRect{2000, 100, 2800, 700};
    RectI result = ClampWindowRectToMonitors(normalRect, &mon, 1, 0);
    MDVN_CHECK(FindMonitorContaining(result, &mon, 1) == 0);
    MDVN_CHECK_EQ(result.Width(), 800);
    MDVN_CHECK_EQ(result.Height(), 600);
}

// ---------------------------------------------------------------------------
// 层叠偏移(裁决 #8)。
// ---------------------------------------------------------------------------

// 用例:无同位窗口时不偏移。
MDVN_TEST(WindowState_CascadeNoOffsetWithoutExistingWindow) {
    RectI base{100, 100, 900, 700};
    RectI workArea{0, 0, 1920, 1040};
    RectI result = ApplyCascadeOffset(base, nullptr, 0, workArea, 24);
    MDVN_CHECK(RectEquals(result, base));
}

// 用例:有 1 个同位窗口时偏移 24px(单测用像素直接验证,真实调用方会把
// kCascadeOffsetDip 按 DPI 换算成物理像素再传入)。
MDVN_TEST(WindowState_CascadeOffsetsBy24WhenOneWindowAtSamePosition) {
    RectI base{100, 100, 900, 700};
    RectI existing[] = {base};  // 同一个原点已经有一个窗口
    RectI workArea{0, 0, 1920, 1040};
    RectI result = ApplyCascadeOffset(base, existing, 1, workArea,
                                       static_cast<mdvn::i32>(kCascadeOffsetDip));
    MDVN_CHECK_EQ(result.left, base.left + 24);
    MDVN_CHECK_EQ(result.top, base.top + 24);
    MDVN_CHECK_EQ(result.Width(), base.Width());  // 尺寸不变,只偏移原点
    MDVN_CHECK_EQ(result.Height(), base.Height());
    MDVN_CHECK(!SameOrigin(result, base));
}

// 用例:连续层叠到边界时回卷到起始位置。
MDVN_TEST(WindowState_CascadeWrapsAroundAtWorkAreaBoundary) {
    // 窗口 800x600,恰好贴着工作区右/下边界(自身合法,未越界),
    // 但只要再偏移一次 24px 就会超出边界。
    RectI base{1100, 440, 1900, 1040};
    RectI workArea{0, 0, 1920, 1040};

    RectI existing[] = {base};
    RectI result = ApplyCascadeOffset(base, existing, 1, workArea, 24);
    // 唯一一次候选偏移 {1824,924,...} 就已经超出 workArea.right(2624>1920)/
    // bottom(1524>1040),回卷到起始位置;起始位置又与 existing[0] 同位,
    // 于是在 guard 上限内保持回卷到 base——用例断言"不会被错误地推到界外"。
    MDVN_CHECK(RectEquals(result, base));

    // 多个已有窗口占住了"起点"与"一步偏移后"两个位置,第三个新窗口必须
    // 先尝试偏移、发现越界、回卷,最终仍然只能落在其中一个已占位置上
    // (退化场景,断言的是"矩形不会跑出工作区之外",而不是具体重合与否)。
    RectI stepOffset{base.left + 24, base.top + 24, base.right + 24, base.bottom + 24};
    RectI existingTwo[] = {base, stepOffset};
    RectI result2 = ApplyCascadeOffset(base, existingTwo, 2, workArea, 24);
    MDVN_CHECK(result2.right <= workArea.right || RectEquals(result2, base));
    MDVN_CHECK(result2.bottom <= workArea.bottom || RectEquals(result2, base));
}

// 用例:RectsOverlap / FindMonitorContaining 的基本边界行为(边缘相接不算重叠)。
MDVN_TEST(WindowState_RectsOverlapEdgeTouchingIsNotOverlap) {
    RectI a{0, 0, 100, 100};
    RectI b{100, 0, 200, 100};  // 恰好在 a 的右边缘相接,不重叠
    MDVN_CHECK(!RectsOverlap(a, b));

    RectI c{99, 0, 200, 100};  // 有 1px 真实重叠
    MDVN_CHECK(RectsOverlap(a, c));
}
