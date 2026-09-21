// 文件夹扫描与穿透模块单元测试。
#include "markair_test.h"
#include "../src/shell/folder_scan.h"
#include "../src/shell/sidebar.h"
#include "../src/util/arena.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>

using markair::Arena;
using markair::FolderEntry;
using markair::IsIgnoredDirectoryName;
using markair::IsMarkdownFileExtension;
using markair::ScanMarkdownFolder;
using markair::Vec;
using markair::u32;

// 测试 1：Markdown 扩展名有效性判定。
MARKAIR_TEST(FolderScan_ExtensionCheck) {
    MARKAIR_CHECK(IsMarkdownFileExtension(L"readme.md"));
    MARKAIR_CHECK(IsMarkdownFileExtension(L"README.MD"));
    MARKAIR_CHECK(IsMarkdownFileExtension(L"doc.markdown"));
    MARKAIR_CHECK(IsMarkdownFileExtension(L"note.mdown"));
    MARKAIR_CHECK(IsMarkdownFileExtension(L"file.mkd"));
    MARKAIR_CHECK(IsMarkdownFileExtension(L"post.mdtext"));

    // 无效扩展名
    MARKAIR_CHECK(!IsMarkdownFileExtension(L"main.cpp"));
    MARKAIR_CHECK(!IsMarkdownFileExtension(L"image.png"));
    MARKAIR_CHECK(!IsMarkdownFileExtension(L"document.txt"));
    MARKAIR_CHECK(!IsMarkdownFileExtension(L"noextension"));
    MARKAIR_CHECK(!IsMarkdownFileExtension(nullptr));
}

// 测试 2：忽略噪音目录判定。
MARKAIR_TEST(FolderScan_IgnoredDirectoryCheck) {
    MARKAIR_CHECK(IsIgnoredDirectoryName(L".git"));
    MARKAIR_CHECK(IsIgnoredDirectoryName(L".svn"));
    MARKAIR_CHECK(IsIgnoredDirectoryName(L".vscode"));
    MARKAIR_CHECK(IsIgnoredDirectoryName(L".idea"));
    MARKAIR_CHECK(IsIgnoredDirectoryName(L"node_modules"));
    MARKAIR_CHECK(IsIgnoredDirectoryName(L"build"));
    MARKAIR_CHECK(IsIgnoredDirectoryName(L"dist"));

    // 正常目录不应被忽略
    MARKAIR_CHECK(!IsIgnoredDirectoryName(L"docs"));
    MARKAIR_CHECK(!IsIgnoredDirectoryName(L"articles"));
    MARKAIR_CHECK(!IsIgnoredDirectoryName(L"content"));
}

// 测试 3：实际扫描目录。
MARKAIR_TEST(FolderScan_ScanDirectory) {
    Arena arena;
    arena.Init(4 * 1024 * 1024);

    Vec<FolderEntry> entries;
    // 扫描仓库中的 bench 目录（内含 BENCH-A.md, BENCH-B.md, EMPTY.md 等）
    wchar_t repoRoot[MAX_PATH];
    swprintf_s(repoRoot, L"%hs", MARKAIR_REPO_ROOT_DIR);

    wchar_t benchDir[MAX_PATH];
    swprintf_s(benchDir, L"%s\\bench", repoRoot);

    u32 count = ScanMarkdownFolder(benchDir, &arena, &entries);
    MARKAIR_CHECK(count >= 3); // 至少包含 BENCH-A.md, BENCH-B.md, EMPTY.md

    // 检查排序及相对路径
    bool foundEmpty = false;
    for (markair::u32 i = 0; i < entries.Size(); ++i) {
        if (wcscmp(entries[i].fileName, L"EMPTY.md") == 0) {
            foundEmpty = true;
            MARKAIR_CHECK(entries[i].sizeBytes > 0);
            MARKAIR_CHECK(wcslen(entries[i].relPath) > 0);
        }
    }
    MARKAIR_CHECK(foundEmpty);
}

// 测试 4：文件夹侧栏顶部第一行根目录按钮几何计算。
MARKAIR_TEST(FolderSidebar_HeaderButtonGeometry) {
    float panelWidth = 260.0f;
    markair::SidebarRectDip r = markair::SidebarFolderHeaderButtonLocalRectDip(panelWidth);
    MARKAIR_CHECK_EQ(r.right, panelWidth - markair::kSidebarPanelPaddingDip);
    MARKAIR_CHECK_EQ(r.left, r.right - 24.0f);
    MARKAIR_CHECK_EQ(r.top, (markair::kFolderHeaderRow1HeightDip - 24.0f) * 0.5f);
    MARKAIR_CHECK_EQ(r.bottom, r.top + 24.0f);
}

// 测试 5：文件夹侧栏顶部第二行过滤输入框容器几何计算。
MARKAIR_TEST(FolderSidebar_FilterInputGeometry) {
    float panelWidth = 260.0f;
    markair::SidebarRectDip r = markair::SidebarFolderFilterInputLocalRectDip(panelWidth);
    // 需求：过滤框左边紧贴面板边缘，不再复用 kSidebarPanelPaddingDip；
    // 右边要让出滚动条列宽度(10dip)，不能真正贴到面板边缘(实测发现
    // 贴边会导致过滤框背景盖住滚动条)。
    MARKAIR_CHECK_EQ(r.left, 0.0f);
    MARKAIR_CHECK_EQ(r.right, panelWidth - 10.0f);
    // 改为下划线样式后输入框紧贴第一行底边，不再居中留间隙
    MARKAIR_CHECK_EQ(r.top, markair::kFolderHeaderRow1HeightDip);
    // 视觉容器高度撑满第二行，底边与列表分割线重合，不再留 10px 空隙
    MARKAIR_CHECK_EQ(r.bottom, r.top + markair::kFolderHeaderRow2HeightDip);
}

// 测试 5b：原生 EDIT 控件矩形——左右沿用容器，高度就等于第二行高度(24dip)，
// 铺满整行不留间隙(裁决：行高本身收窄到跟控件一样高，不靠"控件矮+居中"
// 掩盖间距)。这里仍按通用居中公式断言，行高与控件高度相等时偏移量为 0，
// 数学上与"铺满"等价，无需分两条断言。
MARKAIR_TEST(FolderSidebar_FilterEditGeometry) {
    float panelWidth = 260.0f;
    markair::SidebarRectDip container = markair::SidebarFolderFilterInputLocalRectDip(panelWidth);
    markair::SidebarRectDip r = markair::SidebarFolderFilterEditLocalRectDip(panelWidth);
    MARKAIR_CHECK_EQ(r.left, container.left);
    MARKAIR_CHECK_EQ(r.right, container.right);
    MARKAIR_CHECK_EQ(r.bottom - r.top, markair::kFolderFilterEditHeightDip);
    float rowHeight = container.bottom - container.top;
    MARKAIR_CHECK_EQ(r.top, container.top + (rowHeight - markair::kFolderFilterEditHeightDip) * 0.5f);
    MARKAIR_CHECK_EQ(r.bottom, container.bottom - (rowHeight - markair::kFolderFilterEditHeightDip) * 0.5f);
}

// 测试 6：文件夹侧栏列表项 Hover 文件夹按钮几何计算与双行头部命中测试。
MARKAIR_TEST(FolderSidebar_ItemButtonGeometryAndHitTest) {
    float panelWidth = 260.0f;
    u32 itemCount = 5;
    float scrollY = 0.0f;

    // 第 0 行的项文件夹按钮
    markair::SidebarRectDip r0 = markair::SidebarFolderItemButtonLocalRectDip(panelWidth, 0, scrollY);
    MARKAIR_CHECK_EQ(r0.right, panelWidth - markair::kSidebarCloseButtonMarginDip);
    MARKAIR_CHECK_EQ(r0.left, r0.right - markair::kSidebarCloseButtonSizeDip);
    float row0Top = markair::kFolderSidebarHeaderHeightDip;
    MARKAIR_CHECK_EQ(r0.top, row0Top + (markair::kSidebarRowHeightDip - markair::kSidebarCloseButtonSizeDip) * 0.5f);
    MARKAIR_CHECK_EQ(r0.bottom, r0.top + markair::kSidebarCloseButtonSizeDip);

    // 双行头部高度下的命中测试：头部内部点击不命中项（返回 -1）
    markair::i32 hitHeader = markair::SidebarHitTestItem(
        markair::SidebarDirection::Left, 800.0f, 600.0f, panelWidth, itemCount, scrollY,
        50.0f, 30.0f, markair::kSidebarRowHeightDip, markair::kFolderSidebarHeaderHeightDip);
    MARKAIR_CHECK_EQ(hitHeader, -1);

    // 第一项（y: 72 ~ 100）内部点击命中第 0 项
    markair::i32 hitItem0 = markair::SidebarHitTestItem(
        markair::SidebarDirection::Left, 800.0f, 600.0f, panelWidth, itemCount, scrollY,
        50.0f, 80.0f, markair::kSidebarRowHeightDip, markair::kFolderSidebarHeaderHeightDip);
    MARKAIR_CHECK_EQ(hitItem0, 0);

    // 第二项（y: 100 ~ 128）内部点击命中第 1 项
    markair::i32 hitItem1 = markair::SidebarHitTestItem(
        markair::SidebarDirection::Left, 800.0f, 600.0f, panelWidth, itemCount, scrollY,
        50.0f, 110.0f, markair::kSidebarRowHeightDip, markair::kFolderSidebarHeaderHeightDip);
    MARKAIR_CHECK_EQ(hitItem1, 1);
}

// 测试 7：侧栏项通用按钮槽位几何计算（Slot 0 最右侧，Slot 1 次右侧）。
MARKAIR_TEST(Sidebar_ItemButtonSlotGeometry) {
    float panelWidth = 260.0f;
    u32 itemIndex = 2;
    float scrollY = 10.0f;

    // Slot 0 针对历史记录（默认单行高度与默认标题栏高度）
    markair::SidebarRectDip slot0 = markair::SidebarItemButtonSlotRectDip(panelWidth, itemIndex, 0, scrollY);
    markair::SidebarRectDip closeExpected = markair::SidebarCloseButtonLocalRectDip(panelWidth, itemIndex, scrollY);
    MARKAIR_CHECK_EQ(slot0.left, closeExpected.left);
    MARKAIR_CHECK_EQ(slot0.top, closeExpected.top);
    MARKAIR_CHECK_EQ(slot0.right, closeExpected.right);
    MARKAIR_CHECK_EQ(slot0.bottom, closeExpected.bottom);

    // Slot 1 紧挨 Slot 0 左侧，间距应等于 kSidebarFolderButtonGapDip
    markair::SidebarRectDip slot1 = markair::SidebarItemButtonSlotRectDip(panelWidth, itemIndex, 1, scrollY);
    markair::SidebarRectDip folderExpected = markair::SidebarFolderButtonLocalRectDip(panelWidth, itemIndex, scrollY);
    MARKAIR_CHECK_EQ(slot1.left, folderExpected.left);
    MARKAIR_CHECK_EQ(slot1.top, folderExpected.top);
    MARKAIR_CHECK_EQ(slot1.right, folderExpected.right);
    MARKAIR_CHECK_EQ(slot1.bottom, folderExpected.bottom);

    // 验证两槽位按钮尺寸与间距公式
    MARKAIR_CHECK_EQ(slot0.right - slot0.left, markair::kSidebarCloseButtonSizeDip);
    MARKAIR_CHECK_EQ(slot1.right - slot1.left, markair::kSidebarCloseButtonSizeDip);
    MARKAIR_CHECK_EQ(slot0.left - slot1.right, markair::kSidebarFolderButtonGapDip);

    // 验证针对自定义标题栏高度（如文件夹侧栏双行头部）
    markair::SidebarRectDip folderItemSlot0 = markair::SidebarItemButtonSlotRectDip(
        panelWidth, itemIndex, 0, scrollY, markair::kSidebarRowHeightDip, markair::kFolderSidebarHeaderHeightDip);
    markair::SidebarRectDip folderItemExpected = markair::SidebarFolderItemButtonLocalRectDip(panelWidth, itemIndex, scrollY);
    MARKAIR_CHECK_EQ(folderItemSlot0.left, folderItemExpected.left);
    MARKAIR_CHECK_EQ(folderItemSlot0.top, folderItemExpected.top);
    MARKAIR_CHECK_EQ(folderItemSlot0.right, folderItemExpected.right);
    MARKAIR_CHECK_EQ(folderItemSlot0.bottom, folderItemExpected.bottom);
}

