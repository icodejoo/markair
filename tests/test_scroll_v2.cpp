// 2026 内边距 + 原生滚动条改造的覆盖测试(src/shell/scroll.h 新增部分)。
//
// 按裁决:不往 test_scroll.cpp(M0 8 个基线测试文件之一)里加东西,新增内容
// 单独放这个文件——覆盖内容内边距的宽高收窄计算,以及 WM_VSCROLL 请求码到
// ScrollCommand 的映射。同样是不依赖 HWND / windows.h 的纯函数测试。
#include "markair_test.h"
#include "../src/shell/scroll.h"

// 用例:换行宽度 = 客户区宽度 - 2 * 内边距,正常尺寸下的基本计算。
MARKAIR_TEST(ScrollV2_ContentWidthDipSubtractsBothSidePadding) {
    MARKAIR_CHECK(markair::ContentWidthDip(800.0f) == 800.0f - 2.0f * markair::kContentPaddingDip);
    MARKAIR_CHECK(markair::ContentWidthDip(24.0f) == 0.0f);  // 恰好等于两倍内边距
}

// 用例:极端小窗口(客户区宽度小于两倍内边距)夹到 0,不产生负数换行宽度。
MARKAIR_TEST(ScrollV2_ContentWidthDipClampsToZeroForTinyWindow) {
    MARKAIR_CHECK(markair::ContentWidthDip(10.0f) == 0.0f);
    MARKAIR_CHECK(markair::ContentWidthDip(0.0f) == 0.0f);
    MARKAIR_CHECK(markair::ContentWidthDip(-5.0f) == 0.0f);
}

// 用例:可用视口高度 = 客户区高度 - 2 * 内边距,口径与 ContentWidthDip 一致。
MARKAIR_TEST(ScrollV2_UsableViewportHeightDipSubtractsBothSidePadding) {
    MARKAIR_CHECK(markair::UsableViewportHeightDip(600.0f) == 600.0f - 2.0f * markair::kContentPaddingDip);
}

// 用例:极端小窗口(客户区高度小于两倍内边距)同样夹到 0。
MARKAIR_TEST(ScrollV2_UsableViewportHeightDipClampsToZeroForTinyWindow) {
    MARKAIR_CHECK(markair::UsableViewportHeightDip(20.0f) == 0.0f);
    MARKAIR_CHECK(markair::UsableViewportHeightDip(0.0f) == 0.0f);
    MARKAIR_CHECK(markair::UsableViewportHeightDip(-100.0f) == 0.0f);
}

// 用例:内边距收窄后的可用视口高度喂给 ClampScrollOffset/MaxScrollOffset,
// 会比用原始视口高度算出更大的滚动上限——这就是"滚到底留白"的数值来源。
MARKAIR_TEST(ScrollV2_UsableViewportHeightYieldsLargerMaxScroll) {
    constexpr float totalHeight = 1000.0f;
    constexpr float rawViewport = 600.0f;
    float usable = markair::UsableViewportHeightDip(rawViewport);
    float maxWithPadding = markair::MaxScrollOffset(totalHeight, usable);
    float maxWithoutPadding = markair::MaxScrollOffset(totalHeight, rawViewport);
    MARKAIR_CHECK(maxWithPadding > maxWithoutPadding);
    MARKAIR_CHECK(maxWithPadding - maxWithoutPadding == 2.0f * markair::kContentPaddingDip);
}

// 用例:WM_VSCROLL 的普通请求码(非拖动)逐一映射到对应的 ScrollCommand。
MARKAIR_TEST(ScrollV2_ScrollCommandFromScrollBarCodeMapsLineAndPageAndEnds) {
    markair::ScrollCommand cmd = markair::ScrollCommand::Home;

    MARKAIR_CHECK(markair::ScrollCommandFromScrollBarCode(markair::kSbLineUp, &cmd));
    MARKAIR_CHECK(cmd == markair::ScrollCommand::LineUp);

    MARKAIR_CHECK(markair::ScrollCommandFromScrollBarCode(markair::kSbLineDown, &cmd));
    MARKAIR_CHECK(cmd == markair::ScrollCommand::LineDown);

    MARKAIR_CHECK(markair::ScrollCommandFromScrollBarCode(markair::kSbPageUp, &cmd));
    MARKAIR_CHECK(cmd == markair::ScrollCommand::PageUp);

    MARKAIR_CHECK(markair::ScrollCommandFromScrollBarCode(markair::kSbPageDown, &cmd));
    MARKAIR_CHECK(cmd == markair::ScrollCommand::PageDown);

    MARKAIR_CHECK(markair::ScrollCommandFromScrollBarCode(markair::kSbTop, &cmd));
    MARKAIR_CHECK(cmd == markair::ScrollCommand::Home);

    MARKAIR_CHECK(markair::ScrollCommandFromScrollBarCode(markair::kSbBottom, &cmd));
    MARKAIR_CHECK(cmd == markair::ScrollCommand::End);
}

// 用例:拖动滑块的两个请求码(THUMBTRACK/THUMBPOSITION)不映射到任何
// ScrollCommand——它们携带的是绝对位置,不是相对动作。
MARKAIR_TEST(ScrollV2_ScrollCommandFromScrollBarCodeRejectsThumbCodes) {
    markair::ScrollCommand cmd = markair::ScrollCommand::Home;
    MARKAIR_CHECK(!markair::ScrollCommandFromScrollBarCode(markair::kSbThumbTrack, &cmd));
    MARKAIR_CHECK(!markair::ScrollCommandFromScrollBarCode(markair::kSbThumbPosition, &cmd));
}

// 用例:未知/不处理的请求码(如 SB_ENDSCROLL = 8)返回 false,不产生滚动。
MARKAIR_TEST(ScrollV2_ScrollCommandFromScrollBarCodeRejectsUnknownCode) {
    markair::ScrollCommand cmd = markair::ScrollCommand::Home;
    MARKAIR_CHECK(!markair::ScrollCommandFromScrollBarCode(8, &cmd));
}

// 用例:IsThumbScrollCode 精确识别 THUMBTRACK/THUMBPOSITION,其余请求码都不是。
MARKAIR_TEST(ScrollV2_IsThumbScrollCodeIdentifiesDragCodesOnly) {
    MARKAIR_CHECK(markair::IsThumbScrollCode(markair::kSbThumbTrack));
    MARKAIR_CHECK(markair::IsThumbScrollCode(markair::kSbThumbPosition));
    MARKAIR_CHECK(!markair::IsThumbScrollCode(markair::kSbLineUp));
    MARKAIR_CHECK(!markair::IsThumbScrollCode(markair::kSbPageDown));
    MARKAIR_CHECK(!markair::IsThumbScrollCode(markair::kSbTop));
    MARKAIR_CHECK(!markair::IsThumbScrollCode(markair::kSbBottom));
}
