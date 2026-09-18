// T12 覆盖测试:滚动位置计算(src/shell/scroll.h)。
//
// 这部分被刻意抽成不依赖 HWND / windows.h 的纯函数,因此可以直接链进
// mdvn_tests.exe 单独测试;窗口过程本体(window.cpp)强依赖真实窗口与消息泵,
// 不在此编造假测试。
#include "mdvn_test.h"
#include "../src/shell/scroll.h"

namespace {

// 测试用的文档/视口尺寸:文档 1000 DIP、视口 600 DIP,故 maxScroll = 400。
constexpr float kTotalHeight = 1000.0f;
constexpr float kViewportHeight = 600.0f;
constexpr float kMaxScroll = 400.0f;

// 一次滚轮刻度对应的像素量:3 行 * 20 DIP = 60 DIP。
constexpr float kOneNotchDip = 60.0f;

}  // namespace

// 用例:最大滚动量 = max(0, 总高 - 视口高);文档比视口短或相等时为 0。
MDVN_TEST(Scroll_MaxOffsetClampsToZeroForShortDocument) {
    MDVN_CHECK(mdvn::MaxScrollOffset(kTotalHeight, kViewportHeight) == kMaxScroll);
    MDVN_CHECK(mdvn::MaxScrollOffset(300.0f, 600.0f) == 0.0f);
    MDVN_CHECK(mdvn::MaxScrollOffset(600.0f, 600.0f) == 0.0f);
}

// 用例:初始 scrollY = 0 时向下滚轮,偏移增加且不越界。
MDVN_TEST(Scroll_WheelDownIncreasesOffset) {
    float y = mdvn::ScrollByWheel(0.0f, -mdvn::kWheelDeltaUnit, kTotalHeight, kViewportHeight);
    MDVN_CHECK(y == kOneNotchDip);
    MDVN_CHECK(y > 0.0f && y <= kMaxScroll);

    // 连续两刻度累加。
    y = mdvn::ScrollByWheel(y, -mdvn::kWheelDeltaUnit, kTotalHeight, kViewportHeight);
    MDVN_CHECK(y == kOneNotchDip * 2.0f);
}

// 用例:顶部边界——向上滚动不会得到负偏移。
MDVN_TEST(Scroll_WheelUpClampsAtTop) {
    MDVN_CHECK(mdvn::ScrollByWheel(0.0f, mdvn::kWheelDeltaUnit, kTotalHeight, kViewportHeight) == 0.0f);
    MDVN_CHECK(mdvn::ScrollByWheel(30.0f, mdvn::kWheelDeltaUnit, kTotalHeight, kViewportHeight) == 0.0f);
    MDVN_CHECK(mdvn::ClampScrollOffset(-500.0f, kTotalHeight, kViewportHeight) == 0.0f);
}

// 用例:底部边界——向下滚动不会超过 maxScroll。
MDVN_TEST(Scroll_WheelDownClampsAtBottom) {
    MDVN_CHECK(mdvn::ScrollByWheel(kMaxScroll, -mdvn::kWheelDeltaUnit,
                                   kTotalHeight, kViewportHeight) == kMaxScroll);
    // 一次滚很多刻度也不越界。
    MDVN_CHECK(mdvn::ScrollByWheel(0.0f, -mdvn::kWheelDeltaUnit * 100,
                                   kTotalHeight, kViewportHeight) == kMaxScroll);
    MDVN_CHECK(mdvn::ClampScrollOffset(9999.0f, kTotalHeight, kViewportHeight) == kMaxScroll);
}

// 用例:文档比视口短(或相等)时,任何滚动操作都应保持 scrollY = 0。
MDVN_TEST(Scroll_ShortDocumentAlwaysStaysAtZero) {
    const float total = 300.0f;
    const float viewport = 600.0f;
    MDVN_CHECK(mdvn::ScrollByWheel(0.0f, -mdvn::kWheelDeltaUnit, total, viewport) == 0.0f);
    MDVN_CHECK(mdvn::ScrollByWheel(0.0f, mdvn::kWheelDeltaUnit, total, viewport) == 0.0f);
    MDVN_CHECK(mdvn::ApplyScrollCommand(0.0f, mdvn::ScrollCommand::PageDown, total, viewport) == 0.0f);
    MDVN_CHECK(mdvn::ApplyScrollCommand(0.0f, mdvn::ScrollCommand::End, total, viewport) == 0.0f);

    // 文档高度恰好等于视口高度,同样不产生滚动。
    MDVN_CHECK(mdvn::ApplyScrollCommand(0.0f, mdvn::ScrollCommand::End, 600.0f, 600.0f) == 0.0f);
}

// 用例:PageDown / PageUp 按整个视口高度跳转,并在两端夹取。
MDVN_TEST(Scroll_PageCommandsJumpByViewportHeight) {
    float y = mdvn::ApplyScrollCommand(0.0f, mdvn::ScrollCommand::PageDown,
                                       2000.0f, kViewportHeight);
    MDVN_CHECK(y == kViewportHeight);

    y = mdvn::ApplyScrollCommand(y, mdvn::ScrollCommand::PageUp, 2000.0f, kViewportHeight);
    MDVN_CHECK(y == 0.0f);

    // 底部夹取:文档只有 1000,一页下去只能到 maxScroll = 400。
    MDVN_CHECK(mdvn::ApplyScrollCommand(0.0f, mdvn::ScrollCommand::PageDown,
                                        kTotalHeight, kViewportHeight) == kMaxScroll);
    // 顶部夹取。
    MDVN_CHECK(mdvn::ApplyScrollCommand(100.0f, mdvn::ScrollCommand::PageUp,
                                        kTotalHeight, kViewportHeight) == 0.0f);
}

// 用例:Home 跳到 0,End 跳到 maxScroll。
MDVN_TEST(Scroll_HomeAndEndJumpToDocumentBounds) {
    MDVN_CHECK(mdvn::ApplyScrollCommand(321.0f, mdvn::ScrollCommand::Home,
                                        kTotalHeight, kViewportHeight) == 0.0f);
    MDVN_CHECK(mdvn::ApplyScrollCommand(0.0f, mdvn::ScrollCommand::End,
                                        kTotalHeight, kViewportHeight) == kMaxScroll);
}

// 用例:上下方向键按单行高度滚动,并在两端夹取。
MDVN_TEST(Scroll_LineCommandsMoveOneLine) {
    float y = mdvn::ApplyScrollCommand(0.0f, mdvn::ScrollCommand::LineDown,
                                       kTotalHeight, kViewportHeight);
    MDVN_CHECK(y == mdvn::kScrollLineHeightDip);
    MDVN_CHECK(mdvn::ApplyScrollCommand(y, mdvn::ScrollCommand::LineUp,
                                        kTotalHeight, kViewportHeight) == 0.0f);
    MDVN_CHECK(mdvn::ApplyScrollCommand(kMaxScroll, mdvn::ScrollCommand::LineDown,
                                        kTotalHeight, kViewportHeight) == kMaxScroll);
}

// T70:F5 重载后按块下标近似恢复位置——纯函数钳制,三种边界情况。
// 下标本来就在新块数范围内:原样返回。
MDVN_TEST(Scroll_ClampReloadTopBlockIndexWithinRangeKeepsValue) {
    MDVN_CHECK_EQ(mdvn::ClampReloadTopBlockIndex(2u, 10u), 2u);
}

// 下标 >= 新块数(文档被编辑变短了):钳到最后一块。
MDVN_TEST(Scroll_ClampReloadTopBlockIndexOutOfRangeClampsToLast) {
    MDVN_CHECK_EQ(mdvn::ClampReloadTopBlockIndex(9u, 5u), 4u);
    MDVN_CHECK_EQ(mdvn::ClampReloadTopBlockIndex(5u, 5u), 4u);
}

// 新文档 0 个块:返回 0(调用方据此滚到顶部)。
MDVN_TEST(Scroll_ClampReloadTopBlockIndexEmptyDocumentReturnsZero) {
    MDVN_CHECK_EQ(mdvn::ClampReloadTopBlockIndex(3u, 0u), 0u);
    MDVN_CHECK_EQ(mdvn::ClampReloadTopBlockIndex(0u, 0u), 0u);
}
