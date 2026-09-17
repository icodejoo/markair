// 2026 内边距 + 原生滚动条改造的覆盖测试(src/shell/scroll.h 新增部分)。
//
// 按裁决:不往 test_scroll.cpp(M0 8 个基线测试文件之一)里加东西,新增内容
// 单独放这个文件——覆盖内容内边距的宽高收窄计算,以及 WM_VSCROLL 请求码到
// ScrollCommand 的映射。同样是不依赖 HWND / windows.h 的纯函数测试。
#include "mdvn_test.h"
#include "../src/shell/scroll.h"

// 用例:换行宽度 = 客户区宽度 - 2 * 内边距,正常尺寸下的基本计算。
MDVN_TEST(ScrollV2_ContentWidthDipSubtractsBothSidePadding) {
    MDVN_CHECK(mdvn::ContentWidthDip(800.0f) == 800.0f - 2.0f * mdvn::kContentPaddingDip);
    MDVN_CHECK(mdvn::ContentWidthDip(24.0f) == 0.0f);  // 恰好等于两倍内边距
}

// 用例:极端小窗口(客户区宽度小于两倍内边距)夹到 0,不产生负数换行宽度。
MDVN_TEST(ScrollV2_ContentWidthDipClampsToZeroForTinyWindow) {
    MDVN_CHECK(mdvn::ContentWidthDip(10.0f) == 0.0f);
    MDVN_CHECK(mdvn::ContentWidthDip(0.0f) == 0.0f);
    MDVN_CHECK(mdvn::ContentWidthDip(-5.0f) == 0.0f);
}

// 用例:可用视口高度 = 客户区高度 - 2 * 内边距,口径与 ContentWidthDip 一致。
MDVN_TEST(ScrollV2_UsableViewportHeightDipSubtractsBothSidePadding) {
    MDVN_CHECK(mdvn::UsableViewportHeightDip(600.0f) == 600.0f - 2.0f * mdvn::kContentPaddingDip);
}

// 用例:极端小窗口(客户区高度小于两倍内边距)同样夹到 0。
MDVN_TEST(ScrollV2_UsableViewportHeightDipClampsToZeroForTinyWindow) {
    MDVN_CHECK(mdvn::UsableViewportHeightDip(20.0f) == 0.0f);
    MDVN_CHECK(mdvn::UsableViewportHeightDip(0.0f) == 0.0f);
    MDVN_CHECK(mdvn::UsableViewportHeightDip(-100.0f) == 0.0f);
}

// 用例:内边距收窄后的可用视口高度喂给 ClampScrollOffset/MaxScrollOffset,
// 会比用原始视口高度算出更大的滚动上限——这就是"滚到底留白"的数值来源。
MDVN_TEST(ScrollV2_UsableViewportHeightYieldsLargerMaxScroll) {
    constexpr float totalHeight = 1000.0f;
    constexpr float rawViewport = 600.0f;
    float usable = mdvn::UsableViewportHeightDip(rawViewport);
    float maxWithPadding = mdvn::MaxScrollOffset(totalHeight, usable);
    float maxWithoutPadding = mdvn::MaxScrollOffset(totalHeight, rawViewport);
    MDVN_CHECK(maxWithPadding > maxWithoutPadding);
    MDVN_CHECK(maxWithPadding - maxWithoutPadding == 2.0f * mdvn::kContentPaddingDip);
}

// 用例:WM_VSCROLL 的普通请求码(非拖动)逐一映射到对应的 ScrollCommand。
MDVN_TEST(ScrollV2_ScrollCommandFromScrollBarCodeMapsLineAndPageAndEnds) {
    mdvn::ScrollCommand cmd = mdvn::ScrollCommand::Home;

    MDVN_CHECK(mdvn::ScrollCommandFromScrollBarCode(mdvn::kSbLineUp, &cmd));
    MDVN_CHECK(cmd == mdvn::ScrollCommand::LineUp);

    MDVN_CHECK(mdvn::ScrollCommandFromScrollBarCode(mdvn::kSbLineDown, &cmd));
    MDVN_CHECK(cmd == mdvn::ScrollCommand::LineDown);

    MDVN_CHECK(mdvn::ScrollCommandFromScrollBarCode(mdvn::kSbPageUp, &cmd));
    MDVN_CHECK(cmd == mdvn::ScrollCommand::PageUp);

    MDVN_CHECK(mdvn::ScrollCommandFromScrollBarCode(mdvn::kSbPageDown, &cmd));
    MDVN_CHECK(cmd == mdvn::ScrollCommand::PageDown);

    MDVN_CHECK(mdvn::ScrollCommandFromScrollBarCode(mdvn::kSbTop, &cmd));
    MDVN_CHECK(cmd == mdvn::ScrollCommand::Home);

    MDVN_CHECK(mdvn::ScrollCommandFromScrollBarCode(mdvn::kSbBottom, &cmd));
    MDVN_CHECK(cmd == mdvn::ScrollCommand::End);
}

// 用例:拖动滑块的两个请求码(THUMBTRACK/THUMBPOSITION)不映射到任何
// ScrollCommand——它们携带的是绝对位置,不是相对动作。
MDVN_TEST(ScrollV2_ScrollCommandFromScrollBarCodeRejectsThumbCodes) {
    mdvn::ScrollCommand cmd = mdvn::ScrollCommand::Home;
    MDVN_CHECK(!mdvn::ScrollCommandFromScrollBarCode(mdvn::kSbThumbTrack, &cmd));
    MDVN_CHECK(!mdvn::ScrollCommandFromScrollBarCode(mdvn::kSbThumbPosition, &cmd));
}

// 用例:未知/不处理的请求码(如 SB_ENDSCROLL = 8)返回 false,不产生滚动。
MDVN_TEST(ScrollV2_ScrollCommandFromScrollBarCodeRejectsUnknownCode) {
    mdvn::ScrollCommand cmd = mdvn::ScrollCommand::Home;
    MDVN_CHECK(!mdvn::ScrollCommandFromScrollBarCode(8, &cmd));
}

// 用例:IsThumbScrollCode 精确识别 THUMBTRACK/THUMBPOSITION,其余请求码都不是。
MDVN_TEST(ScrollV2_IsThumbScrollCodeIdentifiesDragCodesOnly) {
    MDVN_CHECK(mdvn::IsThumbScrollCode(mdvn::kSbThumbTrack));
    MDVN_CHECK(mdvn::IsThumbScrollCode(mdvn::kSbThumbPosition));
    MDVN_CHECK(!mdvn::IsThumbScrollCode(mdvn::kSbLineUp));
    MDVN_CHECK(!mdvn::IsThumbScrollCode(mdvn::kSbPageDown));
    MDVN_CHECK(!mdvn::IsThumbScrollCode(mdvn::kSbTop));
    MDVN_CHECK(!mdvn::IsThumbScrollCode(mdvn::kSbBottom));
}
