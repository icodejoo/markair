// 底部操作栏(2026-09-18 改版:左图标 + 右状态)覆盖测试:5 个按钮的固定
// 宽度矩形几何 + 命中测试 + 文件大小格式化,均为纯数字/字符串函数,不依赖
// 真实 HWND/D2D(见 shell/bottom_bar.h 顶部注释)。
#include <cwchar>  // wcscmp

#include "mdvn_test.h"
#include "../src/shell/bottom_bar.h"

using mdvn::BottomBarButton;
using mdvn::BottomBarButtonRectDip;
using mdvn::FormatBottomBarFileSize;
using mdvn::HitTestBottomBar;
using mdvn::IsPointInBottomBar;
using mdvn::kBottomBarButtonCount;
using mdvn::kBottomBarButtonWidthDip;
using mdvn::kBottomBarHeightDip;

// 5 个按钮从左侧起紧密排列,固定宽度 = 栏高度,纵向铺满栏高度。
MDVN_TEST(BottomBar_ButtonRectsPackedFromLeftWithFixedWidth) {
    float clientH = 600.0f;
    for (mdvn::u32 i = 0; i < kBottomBarButtonCount; ++i) {
        auto r = BottomBarButtonRectDip(i, clientH);
        MDVN_CHECK(r.left == kBottomBarButtonWidthDip * static_cast<float>(i));
        MDVN_CHECK(r.right == kBottomBarButtonWidthDip * static_cast<float>(i + 1));
        MDVN_CHECK(r.top == clientH - kBottomBarHeightDip);
        MDVN_CHECK(r.bottom == clientH);
    }
}

// 点落在底部栏高度带内才算命中(按钮区/状态区都算在这条带里)。
MDVN_TEST(BottomBar_IsPointInBottomBarChecksBandOnly) {
    float clientH = 600.0f;
    MDVN_CHECK(!IsPointInBottomBar(clientH, clientH - kBottomBarHeightDip - 1.0f));
    MDVN_CHECK(IsPointInBottomBar(clientH, clientH - kBottomBarHeightDip));
    MDVN_CHECK(IsPointInBottomBar(clientH, clientH - 1.0f));
}

// 按横坐标分段命中对应按钮,从左到右依次是 ZoomIn/ZoomOut/Theme/OpenDoc/Outline,
// 每段固定宽度 kBottomBarButtonWidthDip。
MDVN_TEST(BottomBar_HitTestReturnsCorrectButtonByColumn) {
    float clientW = 800.0f;  // 远大于 5 * kBottomBarButtonWidthDip,右侧是状态区
    float w = kBottomBarButtonWidthDip;
    MDVN_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, 5.0f)),
                  static_cast<int>(BottomBarButton::ZoomIn));
    MDVN_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w + 5.0f)),
                  static_cast<int>(BottomBarButton::ZoomOut));
    MDVN_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 2.0f + 5.0f)),
                  static_cast<int>(BottomBarButton::Theme));
    MDVN_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 3.0f + 5.0f)),
                  static_cast<int>(BottomBarButton::OpenDoc));
    MDVN_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 4.0f + 5.0f)),
                  static_cast<int>(BottomBarButton::Outline));
}

// 边界:恰好落在两段交界处(第 2/3 段边界)算作右边那一段(下标用
// floor(x / btnW)),与渐进递增的分段口径一致。
MDVN_TEST(BottomBar_HitTestBoundaryBelongsToRightSegment) {
    float clientW = 800.0f;
    float w = kBottomBarButtonWidthDip;
    MDVN_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 2.0f)),
                  static_cast<int>(BottomBarButton::Theme));
}

// 极窄/零宽度窗口不崩溃,返回 None。
MDVN_TEST(BottomBar_HitTestZeroWidthReturnsNone) {
    MDVN_CHECK_EQ(static_cast<int>(HitTestBottomBar(0.0f, 5.0f)),
                  static_cast<int>(BottomBarButton::None));
}

// 落在按钮区右侧(状态区)不再钳制成最后一个按钮,必须返回 None——状态区
// 不是按钮,点击它不该触发任何按钮动作。
MDVN_TEST(BottomBar_HitTestBeyondButtonsReturnsNoneForStatusArea) {
    float clientW = 800.0f;
    float buttonsWidth = kBottomBarButtonWidthDip * static_cast<float>(kBottomBarButtonCount);
    MDVN_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, buttonsWidth)),
                  static_cast<int>(BottomBarButton::None));
    MDVN_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, clientW - 1.0f)),
                  static_cast<int>(BottomBarButton::None));
}

// 文件大小格式化:小于 1MB 显示 KB,保留 1 位小数。
MDVN_TEST(BottomBar_FormatFileSizeUsesKbBelow1Mb) {
    wchar_t buf[32];
    FormatBottomBarFileSize(876544, buf, 32);  // 856.0 KB
    MDVN_CHECK(wcscmp(buf, L"856.0 KB") == 0);
}

// 文件大小格式化:大于等于 1MB 显示 MB,保留 1 位小数(含四舍五入)。
MDVN_TEST(BottomBar_FormatFileSizeUsesMbAtOrAbove1Mb) {
    wchar_t buf[32];
    FormatBottomBarFileSize(1024ull * 1024ull, buf, 32);  // 恰好 1MB -> 1.0 MB
    MDVN_CHECK(wcscmp(buf, L"1.0 MB") == 0);

    FormatBottomBarFileSize(2415919, buf, 32);  // ~2.304MB -> 四舍五入到 2.3 MB
    MDVN_CHECK(wcscmp(buf, L"2.3 MB") == 0);
}

// 零字节文件:落在 KB 分支,格式化成 "0.0 KB",不崩溃。
MDVN_TEST(BottomBar_FormatFileSizeHandlesZeroBytes) {
    wchar_t buf[32];
    FormatBottomBarFileSize(0, buf, 32);
    MDVN_CHECK(wcscmp(buf, L"0.0 KB") == 0);
}
