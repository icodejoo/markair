// 侧边栏纯函数(src/shell/sidebar.h)覆盖测试:挤压模式宽度计算 + 文件夹
// 侧栏"新窗口打开"按钮矩形,均为纯数字函数,不依赖真实 HWND/D2D。
#include "markair_test.h"
#include "../src/shell/sidebar.h"

using markair::SidebarSqueezeWidthDip;
using markair::SidebarFolderItemButtonLocalRectDip;
using markair::SidebarFolderItemNewWindowButtonLocalRectDip;
using markair::kSidebarCloseButtonSizeDip;
using markair::kSidebarFolderButtonGapDip;

// 挤压宽度 = 面板宽度 * 动画进度,进度为 0/1 两个边界值与中间值。
MARKAIR_TEST(Sidebar_SqueezeWidthScalesWithProgress) {
    MARKAIR_CHECK(SidebarSqueezeWidthDip(260.0f, 0.0f) == 0.0f);
    MARKAIR_CHECK(SidebarSqueezeWidthDip(260.0f, 1.0f) == 260.0f);
    MARKAIR_CHECK(SidebarSqueezeWidthDip(260.0f, 0.5f) == 130.0f);
}

// 动画进度超出 [0,1] 区间时应被钳制,不产生负宽度或超过面板宽度的结果。
MARKAIR_TEST(Sidebar_SqueezeWidthClampsProgress) {
    MARKAIR_CHECK(SidebarSqueezeWidthDip(260.0f, -0.5f) == 0.0f);
    MARKAIR_CHECK(SidebarSqueezeWidthDip(260.0f, 1.5f) == 260.0f);
}

// "新窗口打开"按钮应紧贴在"打开所在文件夹"按钮左侧,尺寸相同、纵向位置一致,
// 中间隔一个 kSidebarFolderButtonGapDip 间隙,不与之重叠。
MARKAIR_TEST(Sidebar_FolderNewWindowButtonSitsLeftOfFolderButton) {
    float panelWidth = 260.0f;
    auto folderBtn = SidebarFolderItemButtonLocalRectDip(panelWidth, 0, 0.0f);
    auto newWinBtn = SidebarFolderItemNewWindowButtonLocalRectDip(panelWidth, 0, 0.0f);

    MARKAIR_CHECK(newWinBtn.Width() == kSidebarCloseButtonSizeDip);
    MARKAIR_CHECK(newWinBtn.Height() == kSidebarCloseButtonSizeDip);
    MARKAIR_CHECK(newWinBtn.top == folderBtn.top);
    MARKAIR_CHECK(newWinBtn.bottom == folderBtn.bottom);
    MARKAIR_CHECK(newWinBtn.right == folderBtn.left - kSidebarFolderButtonGapDip);
}

// 行下标与滚动偏移变化时,两个按钮应保持同步平移(同一行内相对位置不变)。
MARKAIR_TEST(Sidebar_FolderNewWindowButtonFollowsRowAndScroll) {
    float panelWidth = 300.0f;
    auto row0 = SidebarFolderItemNewWindowButtonLocalRectDip(panelWidth, 0, 0.0f);
    auto row1 = SidebarFolderItemNewWindowButtonLocalRectDip(panelWidth, 1, 0.0f);
    MARKAIR_CHECK(row1.top > row0.top);

    auto scrolled = SidebarFolderItemNewWindowButtonLocalRectDip(panelWidth, 1, 28.0f);
    MARKAIR_CHECK(scrolled.top == row1.top - 28.0f);
}
