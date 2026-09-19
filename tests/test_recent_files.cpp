// Tests for recent files management and sidebar drawer hit testing.
//
// 最近打开历史记录管理与抽屉侧栏命中测试用例。

#include <cwchar>
#include <cstring>

#include "markair_test.h"
#include "../src/util/types.h"
#include "../src/util/recent_files.h"
#include "../src/shell/sidebar.h"

using markair::RecentFileEntry;
using markair::RecentFiles;
using markair::InitRecentFiles;
using markair::ParseRecentFiles;
using markair::FormatRecentFiles;
using markair::AddRecentFile;
using markair::RemoveRecentFileAt;
using markair::SidebarDirection;
using markair::SidebarHitArea;
using markair::SidebarHitTest;
using markair::SidebarHitTestItem;
using markair::SidebarCloseButtonLocalRectDip;
using markair::SidebarRectDip;
using markair::ClampSidebarWidth;
using markair::kSidebarMinResizeWidthDip;
using markair::kSidebarMaxResizeWidthDip;
using markair::kSidebarRowHeightDip;
using markair::kSidebarHeaderHeightDip;
using markair::kSidebarCloseButtonSizeDip;
using markair::kSidebarCloseButtonMarginDip;
using markair::u32;

// Test initialization of empty recent files.
//
// 测试空历史记录结构体初始化。
MARKAIR_TEST(RecentFiles_InitIsEmpty) {
    RecentFiles rf;
    InitRecentFiles(&rf);
    MARKAIR_CHECK_EQ(rf.count, 0u);
}

// Test adding entries: newest becomes first (index 0).
//
// 测试添加条目：最新添加的条目排在最前（下标 0）。
MARKAIR_TEST(RecentFiles_AddRecentFileNewestFirst) {
    RecentFiles rf;
    InitRecentFiles(&rf);

    AddRecentFile(&rf, L"C:\\doc1.md");
    MARKAIR_CHECK_EQ(rf.count, 1u);
    MARKAIR_CHECK(wcscmp(rf.entries[0].path, L"C:\\doc1.md") == 0);

    AddRecentFile(&rf, L"C:\\doc2.md");
    MARKAIR_CHECK_EQ(rf.count, 2u);
    MARKAIR_CHECK(wcscmp(rf.entries[0].path, L"C:\\doc2.md") == 0);
    MARKAIR_CHECK(wcscmp(rf.entries[1].path, L"C:\\doc1.md") == 0);
}

// Test deduplication: re-adding an existing entry moves it to index 0 without increasing count.
//
// 测试去重：重复添加已有条目会将其提升至下标 0，总数不增加。
MARKAIR_TEST(RecentFiles_AddRecentFileDeduplicatesAndPromotes) {
    RecentFiles rf;
    InitRecentFiles(&rf);

    AddRecentFile(&rf, L"C:\\folder\\fileA.md");
    AddRecentFile(&rf, L"C:\\folder\\fileB.md");
    AddRecentFile(&rf, L"C:\\folder\\fileC.md");
    MARKAIR_CHECK_EQ(rf.count, 3u);
    MARKAIR_CHECK(wcscmp(rf.entries[0].path, L"C:\\folder\\fileC.md") == 0);

    // Re-add fileA with different case -> case-insensitive match on Windows
    AddRecentFile(&rf, L"C:\\Folder\\FileA.md");
    MARKAIR_CHECK_EQ(rf.count, 3u);
    MARKAIR_CHECK(wcscmp(rf.entries[0].path, L"C:\\Folder\\FileA.md") == 0);
    MARKAIR_CHECK(wcscmp(rf.entries[1].path, L"C:\\folder\\fileC.md") == 0);
    MARKAIR_CHECK(wcscmp(rf.entries[2].path, L"C:\\folder\\fileB.md") == 0);
}

// Test removing entry at index.
//
// 测试按指定下标删除条目。
MARKAIR_TEST(RecentFiles_RemoveRecentFileAt) {
    RecentFiles rf;
    InitRecentFiles(&rf);

    AddRecentFile(&rf, L"C:\\file1.md");
    AddRecentFile(&rf, L"C:\\file2.md");
    AddRecentFile(&rf, L"C:\\file3.md");

    // Remove middle entry (file2 at index 1)
    RemoveRecentFileAt(&rf, 1);
    MARKAIR_CHECK_EQ(rf.count, 2u);
    MARKAIR_CHECK(wcscmp(rf.entries[0].path, L"C:\\file3.md") == 0);
    MARKAIR_CHECK(wcscmp(rf.entries[1].path, L"C:\\file1.md") == 0);

    // Remove first entry (file3 at index 0)
    RemoveRecentFileAt(&rf, 0);
    MARKAIR_CHECK_EQ(rf.count, 1u);
    MARKAIR_CHECK(wcscmp(rf.entries[0].path, L"C:\\file1.md") == 0);

    // Out of bounds remove is safe no-op
    RemoveRecentFileAt(&rf, 99);
    MARKAIR_CHECK_EQ(rf.count, 1u);
}

// Test formatting and parsing UTF-8 serialization.
//
// 测试 UTF-8 文本序列化与反序列化解析。
MARKAIR_TEST(RecentFiles_FormatAndParseRoundtrip) {
    RecentFiles rf;
    InitRecentFiles(&rf);

    AddRecentFile(&rf, L"D:\\docs\\readme.md");
    AddRecentFile(&rf, L"D:\\docs\\中文测试.md");

    char buffer[4096]{};
    u32 written = FormatRecentFiles(rf, buffer, sizeof(buffer));
    MARKAIR_CHECK(written > 0);

    RecentFiles parsed;
    InitRecentFiles(&parsed);
    ParseRecentFiles(markair::StrSlice{buffer, written}, &parsed);

    MARKAIR_CHECK_EQ(parsed.count, 2u);
    MARKAIR_CHECK(wcscmp(parsed.entries[0].path, L"D:\\docs\\中文测试.md") == 0);
    MARKAIR_CHECK(wcscmp(parsed.entries[1].path, L"D:\\docs\\readme.md") == 0);
}

// Test sidebar drawer hit testing for Right drawer.
//
// 测试右侧抽屉侧栏的命中区域检测。
MARKAIR_TEST(Sidebar_RightDrawerHitTesting) {
    float clientW = 800.0f;
    float clientH = 600.0f;
    float panelW = 300.0f;
    float animProgress = 1.0f;  // fully open, drawer from 500.0 to 800.0

    // Inside drawer
    SidebarHitArea hit1 = SidebarHitTest(
        SidebarDirection::Right, clientW, clientH, panelW, animProgress, 600.0f, 300.0f);
    MARKAIR_CHECK_EQ(static_cast<int>(hit1), static_cast<int>(SidebarHitArea::InsideDrawer));

    // On resize handle (left edge of right drawer: 500.0 +/- 4.0)
    SidebarHitArea hit2 = SidebarHitTest(
        SidebarDirection::Right, clientW, clientH, panelW, animProgress, 501.0f, 300.0f);
    MARKAIR_CHECK_EQ(static_cast<int>(hit2), static_cast<int>(SidebarHitArea::ResizeHandle));

    // In mask area (left of drawer)
    SidebarHitArea hit3 = SidebarHitTest(
        SidebarDirection::Right, clientW, clientH, panelW, animProgress, 200.0f, 300.0f);
    MARKAIR_CHECK_EQ(static_cast<int>(hit3), static_cast<int>(SidebarHitArea::Mask));
}

// Test sidebar hit testing item calculation by row height.
//
// 测试侧栏条目行下标命中计算。
MARKAIR_TEST(Sidebar_HitTestItemByRowHeight) {
    float clientW = 800.0f;
    float clientH = 600.0f;
    float panelW = 300.0f;
    float scrollY = 0.0f;
    u32 count = 10;

    // Inside drawer (X = 600.0f), Y = 40.0f (header = 32.0f, row = 0)
    MARKAIR_CHECK_EQ(SidebarHitTestItem(SidebarDirection::Right, clientW, clientH, panelW, count, scrollY, 600.0f, 40.0f), 0);
    // Y = 70.0f (row 1)
    MARKAIR_CHECK_EQ(SidebarHitTestItem(SidebarDirection::Right, clientW, clientH, panelW, count, scrollY, 600.0f, 70.0f), 1);

    // Outside drawer (X = 200.0f)
    MARKAIR_CHECK_EQ(SidebarHitTestItem(SidebarDirection::Right, clientW, clientH, panelW, count, scrollY, 200.0f, 40.0f), -1);

    // Header area (Y = 10.0f)
    MARKAIR_CHECK_EQ(SidebarHitTestItem(SidebarDirection::Right, clientW, clientH, panelW, count, scrollY, 600.0f, 10.0f), -1);

    // Below all items
    MARKAIR_CHECK_EQ(SidebarHitTestItem(SidebarDirection::Right, clientW, clientH, panelW, count, scrollY, 600.0f, 500.0f), -1);
}

// Test per-row close/delete button rect stays inside the panel, right-aligned,
// and moves down by exactly one row height per item index — this is the
// geometry the hover-close-button hit test in window.cpp relies on.
//
// 测试每行关闭/删除按钮矩形落在面板内、贴右对齐，且每加一个下标恰好整体
// 下移一个行高——这是 window.cpp 里悬浮关闭按钮命中测试依赖的几何关系。
MARKAIR_TEST(Sidebar_CloseButtonRectDip) {
    float panelW = 300.0f;

    SidebarRectDip rect0 = SidebarCloseButtonLocalRectDip(panelW, /*itemIndex=*/0, /*scrollY=*/0.0f);
    MARKAIR_CHECK(rect0.Width() == kSidebarCloseButtonSizeDip);
    MARKAIR_CHECK(rect0.Height() == kSidebarCloseButtonSizeDip);
    MARKAIR_CHECK(rect0.right == panelW - kSidebarCloseButtonMarginDip);
    MARKAIR_CHECK(rect0.top >= kSidebarHeaderHeightDip);
    MARKAIR_CHECK(rect0.bottom <= kSidebarHeaderHeightDip + kSidebarRowHeightDip);

    SidebarRectDip rect1 = SidebarCloseButtonLocalRectDip(panelW, /*itemIndex=*/1, /*scrollY=*/0.0f);
    MARKAIR_CHECK(rect1.top - rect0.top == kSidebarRowHeightDip);
    MARKAIR_CHECK(rect1.left == rect0.left);

    // Scrolling down shifts the button rect up by the same amount, matching
    // SidebarHitTestItem's own scrollY handling.
    SidebarRectDip rectScrolled = SidebarCloseButtonLocalRectDip(panelW, /*itemIndex=*/0, /*scrollY=*/10.0f);
    MARKAIR_CHECK(rect0.top - rectScrolled.top == 10.0f);
}

// Test clamping sidebar width.
//
// 测试侧栏宽度范围限制。
MARKAIR_TEST(Sidebar_ClampSidebarWidth) {
    float clientW = 800.0f;
    MARKAIR_CHECK_EQ(ClampSidebarWidth(100.0f, clientW), kSidebarMinResizeWidthDip);
    MARKAIR_CHECK_EQ(ClampSidebarWidth(1000.0f, clientW), kSidebarMaxResizeWidthDip);
    MARKAIR_CHECK_EQ(ClampSidebarWidth(350.0f, clientW), 350.0f);
}
