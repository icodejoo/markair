// 底部操作栏(2026-09-18 改版:左图标 + 右状态;2026-09-21 新增"打开文件夹"
// 按钮)覆盖测试:各按钮的固定宽度矩形几何 + 命中测试 + 文件大小格式化,
// 均为纯数字/字符串函数,不依赖真实 HWND/D2D(见 shell/bottom_bar.h 顶部注释)。
#include <cwchar>  // wcscmp

#include "markair_test.h"
#include "../src/shell/bottom_bar.h"

using markair::BottomBarButton;
using markair::BottomBarButtonRectDip;
using markair::FormatBottomBarFileSize;
using markair::HitTestBottomBar;
using markair::IsBottomBarFileListEnabled;
using markair::IsBottomBarOutlineEnabled;
using markair::IsPointInBottomBar;
using markair::kBottomBarButtonCount;
using markair::kBottomBarButtonWidthDip;
using markair::kBottomBarLeftButtonCount;
using markair::kBottomBarHeightDip;

// 8 个左侧按钮从左侧起紧密排列，History(下标 9)贴最右侧，CopyPath(下标 8)
// 紧贴其左边——CopyPath 的矩形本身不受"是否有文档打开"影响，可见性由调用方
// (HitTestBottomBar 的 hasDocument 参数/渲染层的 hasDocument 判断)另行控制。
MARKAIR_TEST(BottomBar_ButtonRectsPackedFromLeftAndRight) {
    float clientH = 600.0f;
    float clientW = 800.0f;
    for (markair::u32 i = 0; i < 8; ++i) {
        auto r = BottomBarButtonRectDip(i, clientW, clientH);
        MARKAIR_CHECK(r.left == kBottomBarButtonWidthDip * static_cast<float>(i));
        MARKAIR_CHECK(r.right == kBottomBarButtonWidthDip * static_cast<float>(i + 1));
        MARKAIR_CHECK(r.top == clientH - kBottomBarHeightDip);
        MARKAIR_CHECK(r.bottom == clientH);
    }
    auto rHistory = BottomBarButtonRectDip(static_cast<markair::u32>(BottomBarButton::History), clientW, clientH);
    MARKAIR_CHECK(rHistory.left == clientW - kBottomBarButtonWidthDip);
    MARKAIR_CHECK(rHistory.right == clientW);
    MARKAIR_CHECK(rHistory.top == clientH - kBottomBarHeightDip);
    MARKAIR_CHECK(rHistory.bottom == clientH);

    auto rCopyPath = BottomBarButtonRectDip(static_cast<markair::u32>(BottomBarButton::CopyPath), clientW, clientH);
    MARKAIR_CHECK(rCopyPath.right == rHistory.left);
    MARKAIR_CHECK(rCopyPath.left == rHistory.left - kBottomBarButtonWidthDip);
}

// 启动状态机(main.cpp)的禁用态判定:文件列表按钮只要有文档或有文件夹
// 上下文任一成立即可用;大纲按钮只看是否有文档。
MARKAIR_TEST(BottomBar_FileListEnabledRequiresDocOrFolderContext) {
    MARKAIR_CHECK(!IsBottomBarFileListEnabled(false, false));
    MARKAIR_CHECK(IsBottomBarFileListEnabled(true, false));
    MARKAIR_CHECK(IsBottomBarFileListEnabled(false, true));
    MARKAIR_CHECK(IsBottomBarFileListEnabled(true, true));
}

MARKAIR_TEST(BottomBar_OutlineEnabledRequiresDocument) {
    MARKAIR_CHECK(!IsBottomBarOutlineEnabled(false));
    MARKAIR_CHECK(IsBottomBarOutlineEnabled(true));
}

// 点落在底部栏高度带内才算命中(按钮区/状态区都算在这条带里)。
MARKAIR_TEST(BottomBar_IsPointInBottomBarChecksBandOnly) {
    float clientH = 600.0f;
    MARKAIR_CHECK(!IsPointInBottomBar(clientH, clientH - kBottomBarHeightDip - 1.0f));
    MARKAIR_CHECK(IsPointInBottomBar(clientH, clientH - kBottomBarHeightDip));
    MARKAIR_CHECK(IsPointInBottomBar(clientH, clientH - 1.0f));
}

// 按横坐标分段命中对应按钮,从左到右依次是
// FileList/Outline/OpenDoc/OpenFolder/Theme/ZoomOut/ZoomIn/Find,最右侧是 History。
// hasDocument=false:CopyPath 不存在,不占用任何区域。
MARKAIR_TEST(BottomBar_HitTestReturnsCorrectButtonByColumn) {
    float clientW = 800.0f;  // 远大于 8 * kBottomBarButtonWidthDip,中间是状态区
    float w = kBottomBarButtonWidthDip;
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, 5.0f, false)),
                  static_cast<int>(BottomBarButton::FileList));
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w + 5.0f, false)),
                  static_cast<int>(BottomBarButton::Outline));
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 2.0f + 5.0f, false)),
                  static_cast<int>(BottomBarButton::OpenDoc));
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 3.0f + 5.0f, false)),
                  static_cast<int>(BottomBarButton::OpenFolder));
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 4.0f + 5.0f, false)),
                  static_cast<int>(BottomBarButton::Theme));
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 5.0f + 5.0f, false)),
                  static_cast<int>(BottomBarButton::ZoomOut));
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 6.0f + 5.0f, false)),
                  static_cast<int>(BottomBarButton::ZoomIn));
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 7.0f + 5.0f, false)),
                  static_cast<int>(BottomBarButton::Find));
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, clientW - 5.0f, false)),
                  static_cast<int>(BottomBarButton::History));
}

// hasDocument=true:CopyPath 出现在 History 左侧一个按钮宽度处;原本落在
// 那个区域的点(hasDocument=false 时是状态区/None)现在命中 CopyPath。
MARKAIR_TEST(BottomBar_HitTestCopyPathOnlyWhenDocumentOpen) {
    float clientW = 800.0f;
    float w = kBottomBarButtonWidthDip;
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, clientW - w - 5.0f, true)),
                  static_cast<int>(BottomBarButton::CopyPath));
    // 同一个点,没有打开文档时该区域是状态区,不是按钮。
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, clientW - w - 5.0f, false)),
                  static_cast<int>(BottomBarButton::None));
    // History 不受影响,始终贴最右。
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, clientW - 5.0f, true)),
                  static_cast<int>(BottomBarButton::History));
}

// 边界:恰好落在两段交界处(第 4/5 段边界)算作右边那一段(下标用
// floor(x / btnW)),与渐进递增的分段口径一致。
MARKAIR_TEST(BottomBar_HitTestBoundaryBelongsToRightSegment) {
    float clientW = 800.0f;
    float w = kBottomBarButtonWidthDip;
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, w * 4.0f, false)),
                  static_cast<int>(BottomBarButton::Theme));
}

// 极窄/零宽度窗口不崩溃,返回 None。
MARKAIR_TEST(BottomBar_HitTestZeroWidthReturnsNone) {
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(0.0f, 5.0f, false)),
                  static_cast<int>(BottomBarButton::None));
}

// 落在左侧按钮区和右侧历史按钮之间的中间状态区必须返回 None——状态区
// 不是按钮,点击它不该触发任何按钮动作。(hasDocument=false,CopyPath 不存在)
MARKAIR_TEST(BottomBar_HitTestBetweenButtonsReturnsNoneForStatusArea) {
    float clientW = 800.0f;
    float leftButtonsWidth = kBottomBarButtonWidthDip * static_cast<float>(kBottomBarLeftButtonCount);
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, leftButtonsWidth, false)),
                  static_cast<int>(BottomBarButton::None));
    MARKAIR_CHECK_EQ(static_cast<int>(HitTestBottomBar(clientW, clientW - kBottomBarButtonWidthDip - 1.0f, false)),
                  static_cast<int>(BottomBarButton::None));
}

// 文件大小格式化:小于 1MB 显示 KB,保留 1 位小数。
MARKAIR_TEST(BottomBar_FormatFileSizeUsesKbBelow1Mb) {
    wchar_t buf[32];
    FormatBottomBarFileSize(876544, buf, 32);  // 856.0 KB
    MARKAIR_CHECK(wcscmp(buf, L"856.0 KB") == 0);
}

// 文件大小格式化:大于等于 1MB 显示 MB,保留 1 位小数(含四舍五入)。
MARKAIR_TEST(BottomBar_FormatFileSizeUsesMbAtOrAbove1Mb) {
    wchar_t buf[32];
    FormatBottomBarFileSize(1024ull * 1024ull, buf, 32);  // 恰好 1MB -> 1.0 MB
    MARKAIR_CHECK(wcscmp(buf, L"1.0 MB") == 0);

    FormatBottomBarFileSize(2415919, buf, 32);  // ~2.304MB -> 四舍五入到 2.3 MB
    MARKAIR_CHECK(wcscmp(buf, L"2.3 MB") == 0);
}

// 零字节文件:落在 KB 分支,格式化成 "0.0 KB",不崩溃。
MARKAIR_TEST(BottomBar_FormatFileSizeHandlesZeroBytes) {
    wchar_t buf[32];
    FormatBottomBarFileSize(0, buf, 32);
    MARKAIR_CHECK(wcscmp(buf, L"0.0 KB") == 0);
}

// 验证新增的 FileList 按钮下标、标签及矩形位置
MARKAIR_TEST(BottomBar_FileListButtonLabelAndIndex) {
    using markair::kBottomBarLabels;
    int fileListIndex = static_cast<int>(BottomBarButton::FileList);
    MARKAIR_CHECK(fileListIndex == 0);
    MARKAIR_CHECK(wcscmp(kBottomBarLabels[0], L"文件列表") == 0);

    float clientH = 600.0f;
    float clientW = 800.0f;
    auto r = BottomBarButtonRectDip(0, clientW, clientH);
    MARKAIR_CHECK(r.left == 0.0f);
    MARKAIR_CHECK(r.right == kBottomBarButtonWidthDip);
    MARKAIR_CHECK(r.top == clientH - kBottomBarHeightDip);
    MARKAIR_CHECK(r.bottom == clientH);
}

