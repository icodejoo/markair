#pragma once

#include "../util/types.h"
#include "button.h"

namespace markair {

// Minimum and maximum allowable sidebar width in DIPs.
//
// 侧边栏允许调整的最小与最大宽度（DIP）。
constexpr float kSidebarMinResizeWidthDip = 180.0f;
constexpr float kSidebarMaxResizeWidthDip = 600.0f;

// Default sidebar width when opening.
//
// 侧边栏展开时的默认宽度（DIP）。
constexpr float kSidebarDefaultWidthDip = 260.0f;

// Width of the edge resize handle area in DIPs.
//
// 侧边栏边缘拖拽调宽区域的感应宽度（DIP）。
constexpr float kSidebarResizeHandleWidthDip = 6.0f;

// Default height of a sidebar list item row in DIPs.
//
// 侧边栏列表单行项的默认高度（DIP）。
constexpr float kSidebarRowHeightDip = 28.0f;

// Height of sidebar top header area in DIPs.
//
// 侧边栏顶部标题栏区域的高度（DIP）。
constexpr float kSidebarHeaderHeightDip = 40.0f;

// 侧边栏面板左右内边距（DIP）。
constexpr float kSidebarPanelPaddingDip = 10.0f;

// 侧边栏列表行文字字号（DIP）。
constexpr float kSidebarRowFontSizeDip = 13.0f;

// 侧边栏标题栏文字字号（DIP）。
constexpr float kSidebarHeaderFontSizeDip = 14.0f;

// Size (width and height, it's square) of the per-row close/delete button in DIPs.
//
// 每行右侧关闭/删除按钮的边长（正方形，DIP）。
constexpr float kSidebarCloseButtonSizeDip = 20.0f;

// Right margin from panel edge to the close/delete button in DIPs.
//
// 关闭/删除按钮距面板右边缘的间距（DIP）。
constexpr float kSidebarCloseButtonMarginDip = 6.0f;

// Gap between the "open containing folder" button and the close/delete
// button (the former sits immediately to the left of the latter) in DIPs.
//
// "打开所在文件夹"按钮与关闭/删除按钮之间的间隙（前者紧贴在后者左侧）（DIP）。
constexpr float kSidebarFolderButtonGapDip = 4.0f;

// 按钮图标相对按钮矩形的内边距（DIP）。
constexpr float kSidebarCloseButtonGlyphPaddingDip = 6.0f;

// Total animation duration for sliding drawer in milliseconds.
//
// 抽屉式侧边栏滑动动画的总持续时长（毫秒）。
constexpr u32 kSidebarAnimDurationMs = 180;

/**
 * Slide-in direction for drawer sidebar widget.
 *
 * 抽屉侧边栏的滑出方向。
 */
enum class SidebarDirection {
    Left,   // Slides from left edge (e.g. Outline / 大纲侧栏)
    Right,  // Slides from right edge (e.g. History / 历史记录侧栏)
};

/**
 * Animation state machine phases for a sliding drawer sidebar. Shared by
 * both drawer instances in this codebase (history sidebar here, and the
 * outline sidebar via outline_panel.h's `OutlineAnimState` alias) — they
 * are the same 4-state open/close shape, not two independent state
 * machines that happen to look alike.
 *
 * 侧边栏抽屉的动画状态机阶段。本代码库两处抽屉侧栏共用同一套(历史记录
 * 侧栏直接用这个类型;大纲侧栏通过 outline_panel.h 的 `OutlineAnimState`
 * 别名复用)——两者是同一套展开/收起动画形状,不是两套凑巧长得像的
 * 独立状态机。
 */
enum class SidebarAnimState {
    Closed,   // Completely closed, 0% visible, not interactive
    Opening,  // Animating from closed/current towards fully open
    Open,     // Fully open at 100%, interactive
    Closing,  // Animating from open/current towards completely closed
};

/**
 * Result of hit-testing mouse point against sidebar and mask overlay.
 *
 * 鼠标在侧边栏与蒙层区域上的命中测试结果分类。
 */
enum class SidebarHitArea {
    None,          // Outside interactive area
    InsideDrawer,  // Inside sidebar content area
    ResizeHandle,  // On sidebar resizing edge handle
    Mask,          // On background translucent mask overlay
};

/**
 * 左侧栏容器当前显示的视图(2026-09-22 起,文件列表与大纲共用同一个可拖拽
 * 调宽、可收起的挤压式容器,同一时刻只显示其中一个)。历史记录侧栏不纳入
 * 本容器,继续保持独立的悬浮抽屉,不受此枚举影响。
 *
 * 容器自身的宽度/展开收起动画复用 WindowState 里原文件夹侧栏那套字段
 * (folderAnimState/folderPanelWidthDip 等),这个枚举只负责回答"当前该画
 * 文件列表还是大纲、当前该由谁接管点击"。
 */
enum class SidebarContainerView : u8 {
    FileList,  // 显示文件列表(原文件夹穿透侧栏)
    Outline,   // 显示大纲
};

/**
 * Rectangle coordinates in DIP.
 *
 * DIP 物理坐标矩形。
 */
struct SidebarRectDip {
    float left;
    float top;
    float right;
    float bottom;

    float Width() const { return right - left; }
    float Height() const { return bottom - top; }
};

/**
 * Standard cubic ease-out curve function: f(t) = 1 - (1 - t)^3. Pure function.
 *
 * 标准三次方缓出动画曲线函数：f(t) = 1 - (1 - t)^3。纯函数。
 *
 * @param t Normalized linear time progress in [0.0f, 1.0f].
 *
 *   归一化线性时间进度，范围 [0.0f, 1.0f]。
 *
 * @return Eased animation progress in [0.0f, 1.0f].
 *
 *   经过缓动计算后的动画进度，范围 [0.0f, 1.0f]。
 */
inline float EaseOutCubic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

/**
 * Clamp requested sidebar width within valid bounds and window limits. Pure function.
 *
 * 将请求的侧边栏宽度钳制在合法区间与当前窗口宽度内。纯函数。
 *
 * @param requestedWidth Requested width in DIP.
 *
 *   请求的侧边栏宽度（DIP）。
 *
 * @param clientWidthDip Current window client width in DIP.
 *
 *   当前窗口客户区宽度（DIP）。
 *
 * @return Clamped width in DIP.
 *
 *   钳制后的侧边栏宽度（DIP）。
 */
inline float ClampSidebarWidth(float requestedWidth, float clientWidthDip) {
    float maxAllowed = clientWidthDip * 0.75f;
    if (maxAllowed > kSidebarMaxResizeWidthDip) maxAllowed = kSidebarMaxResizeWidthDip;
    if (maxAllowed < kSidebarMinResizeWidthDip) maxAllowed = kSidebarMinResizeWidthDip;

    if (requestedWidth < kSidebarMinResizeWidthDip) return kSidebarMinResizeWidthDip;
    if (requestedWidth > maxAllowed) return maxAllowed;
    return requestedWidth;
}

/**
 * 计算"挤压模式"侧栏当前应从正文可用宽度中占用的宽度（DIP）。纯函数。
 * 悬浮模式的侧栏（大纲/历史）不应调用本函数——它们的正文宽度不受侧栏影响；
 * 只有选择挤压模式的侧栏（如文件夹侧栏）需要把返回值从正文可用宽度里减去。
 *
 * @param panelWidthDip 侧栏配置宽度（DIP）。
 * @param progress 动画进度 [0.0f, 1.0f]，超出区间会被钳制。
 * @return `panelWidthDip * clamp(progress, 0, 1)`。
 * @example float squeeze = markair::SidebarSqueezeWidthDip(260.0f, 0.5f); // 得 130
 */
inline float SidebarSqueezeWidthDip(float panelWidthDip, float progress) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    return panelWidthDip * progress;
}

/**
 * Calculate drawer panel rectangle in DIP based on direction, panel width and progress. Pure function.
 *
 * 根据方向、面板宽度与当前动画进度计算抽屉面板矩形（DIP）。纯函数。
 *
 * @param dir Sidebar slide direction (Left or Right).
 *
 *   侧边栏滑出方向（左或右）。
 *
 * @param clientWidthDip Client width in DIP.
 *
 *   客户区宽度（DIP）。
 *
 * @param clientHeightDip Client height in DIP (excluding bottom bar if applicable).
 *
 *   客户区高度（DIP，扣除底部栏后）。
 *
 * @param panelWidthDip Configured panel width in DIP.
 *
 *   配置的侧边栏面板宽度（DIP）。
 *
 * @param progress Animation progress in [0.0f, 1.0f].
 *
 *   动画进度，范围 [0.0f, 1.0f]。
 *
 * @return Drawer rectangle in DIP.
 *
 *   抽屉面板矩形（DIP）。
 */
inline SidebarRectDip SidebarDrawerRectDip(SidebarDirection dir, float clientWidthDip,
                                           float clientHeightDip, float panelWidthDip,
                                           float progress) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    if (dir == SidebarDirection::Left) {
        float right = panelWidthDip * progress;
        float left = right - panelWidthDip;
        return SidebarRectDip{left, 0.0f, right, clientHeightDip};
    } else {
        float left = clientWidthDip - panelWidthDip * progress;
        float right = left + panelWidthDip;
        return SidebarRectDip{left, 0.0f, right, clientHeightDip};
    }
}

/**
 * Calculate drawer resize handle rectangle in DIP. Pure function.
 *
 * 计算侧边栏边缘拖拽调宽手柄的矩形区域（DIP）。纯函数。
 *
 * @param dir Sidebar slide direction (Left or Right).
 *
 *   侧边栏滑出方向（左或右）。
 *
 * @param clientWidthDip Client width in DIP.
 *
 *   客户区宽度（DIP）。
 *
 * @param clientHeightDip Client height in DIP.
 *
 *   客户区高度（DIP）。
 *
 * @param panelWidthDip Configured panel width in DIP.
 *
 *   配置的侧边栏面板宽度（DIP）。
 *
 * @param progress Animation progress in [0.0f, 1.0f].
 *
 *   动画进度，范围 [0.0f, 1.0f]。
 *
 * @return Resize handle rectangle in DIP.
 *
 *   调宽手柄矩形（DIP）。
 */
inline SidebarRectDip SidebarResizeHandleRectDip(SidebarDirection dir, float clientWidthDip,
                                                 float clientHeightDip, float panelWidthDip,
                                                 float progress) {
    SidebarRectDip drawer =
        SidebarDrawerRectDip(dir, clientWidthDip, clientHeightDip, panelWidthDip, progress);
    if (dir == SidebarDirection::Left) {
        return SidebarRectDip{drawer.right - kSidebarResizeHandleWidthDip, drawer.top,
                              drawer.right, drawer.bottom};
    } else {
        return SidebarRectDip{drawer.left, drawer.top,
                              drawer.left + kSidebarResizeHandleWidthDip, drawer.bottom};
    }
}

/**
 * Hit-test mouse coordinate against sidebar drawer and background mask. Pure function.
 *
 * 鼠标坐标在侧边栏抽屉与背景蒙层上的命中测试。纯函数。
 *
 * @param dir Sidebar slide direction.
 *
 *   侧边栏方向。
 *
 * @param clientWidthDip Client width in DIP.
 *
 *   客户区宽度（DIP）。
 *
 * @param clientHeightDip Client height in DIP.
 *
 *   客户区高度（DIP）。
 *
 * @param panelWidthDip Configured panel width in DIP.
 *
 *   侧边栏面板宽度（DIP）。
 *
 * @param progress Animation progress in [0.0f, 1.0f].
 *
 *   动画进度，范围 [0.0f, 1.0f]。
 *
 * @param pointXDip Mouse X in DIP.
 *
 *   鼠标 X 坐标（DIP）。
 *
 * @param pointYDip Mouse Y in DIP.
 *
 *   鼠标 Y 坐标（DIP）。
 *
 * @return HitArea enum indicating target area.
 *
 *   命中的区域分类枚举。
 */
inline SidebarHitArea SidebarHitTest(SidebarDirection dir, float clientWidthDip,
                                     float clientHeightDip, float panelWidthDip,
                                     float progress, float pointXDip, float pointYDip) {
    if (progress <= 0.0f) return SidebarHitArea::None;
    if (pointYDip < 0.0f || pointYDip >= clientHeightDip) return SidebarHitArea::None;
    if (pointXDip < 0.0f || pointXDip >= clientWidthDip) return SidebarHitArea::None;

    SidebarRectDip drawer =
        SidebarDrawerRectDip(dir, clientWidthDip, clientHeightDip, panelWidthDip, progress);
    bool insideDrawer = (pointXDip >= drawer.left && pointXDip < drawer.right);

    if (insideDrawer) {
        if (progress >= 0.99f) {
            SidebarRectDip handle =
                SidebarResizeHandleRectDip(dir, clientWidthDip, clientHeightDip, panelWidthDip, progress);
            if (pointXDip >= handle.left && pointXDip < handle.right) {
                return SidebarHitArea::ResizeHandle;
            }
        }
        return SidebarHitArea::InsideDrawer;
    }

    return SidebarHitArea::Mask;
}

/**
 * Hit-test item row index inside a sidebar drawer. Pure function.
 *
 * 命中测试侧边栏抽屉内部的列表项行下标。纯函数。
 *
 * @param dir Sidebar slide direction.
 *
 *   侧边栏方向。
 *
 * @param clientWidthDip Client width in DIP.
 *
 *   客户区宽度（DIP）。
 *
 * @param clientHeightDip Client height in DIP.
 *
 *   客户区高度（DIP）。
 *
 * @param panelWidthDip Configured panel width in DIP.
 *
 *   侧边栏面板宽度（DIP）。
 *
 * @param itemCount Total number of items in list.
 *
 *   列表中项的总数。
 *
 * @param scrollY Current vertical scroll offset in DIP.
 *
 *   当前纵向滚动偏移量（DIP）。
 *
 * @param pointXDip Mouse X in DIP.
 *
 *   鼠标 X 坐标（DIP）。
 *
 * @param pointYDip Mouse Y in DIP.
 *
 *   鼠标 Y 坐标（DIP）。
 *
 * @param rowHeightDip Height of each row in DIP.
 *
 *   单行项高度（DIP）。
 *
 * @param headerHeightDip Height of top header in DIP.
 *
 *   顶部标题栏高度（DIP）。
 *
 * @return Zero-based item index, or -1 if no item is hit.
 *
 *   命中的项下标（从零起始），若未命中任何项返回 -1。
 */
inline i32 SidebarHitTestItem(SidebarDirection dir, float clientWidthDip,
                              float clientHeightDip, float panelWidthDip,
                              u32 itemCount, float scrollY, float pointXDip,
                              float pointYDip, float rowHeightDip = kSidebarRowHeightDip,
                              float headerHeightDip = kSidebarHeaderHeightDip) {
    if (itemCount == 0) return -1;
    SidebarRectDip drawer =
        SidebarDrawerRectDip(dir, clientWidthDip, clientHeightDip, panelWidthDip, 1.0f);
    if (pointXDip < drawer.left || pointXDip >= drawer.right) return -1;
    if (pointYDip < headerHeightDip || pointYDip >= clientHeightDip) return -1;

    float relativeY = (pointYDip - headerHeightDip) + scrollY;
    if (relativeY < 0.0f) return -1;

    i32 index = static_cast<i32>(relativeY / rowHeightDip);
    if (index >= 0 && index < static_cast<i32>(itemCount)) {
        return index;
    }
    return -1;
}

/**
 * 计算侧栏列表项中从右往左第 slotIndex 个动作按钮的矩形（DIP）。纯函数。
 * slotIndex = 0 表示最右侧按钮槽位；1 表示紧邻其左侧的槽位。
 *
 * @param panelWidthDip 侧边栏面板宽度（DIP）。
 * @param itemIndex 行下标（从零起始）。
 * @param slotIndex 槽位下标（0 为最右侧，1 为次右侧）。
 * @param scrollY 当前纵向滚动偏移量（DIP）。
 * @param rowHeightDip 单行项高度（DIP）。
 * @param headerHeightDip 顶部标题栏高度（DIP）。
 * @return 按钮在面板局部坐标系下的矩形。
 * @example SidebarRectDip r = SidebarItemButtonSlotRectDip(260.0f, 0, 0, 0.0f);
 */
inline SidebarRectDip SidebarItemButtonSlotRectDip(
    float panelWidthDip, u32 itemIndex, u32 slotIndex, float scrollY,
    float rowHeightDip = kSidebarRowHeightDip,
    float headerHeightDip = kSidebarHeaderHeightDip) {
    // 排布算法(第 slotIndex 个槽位的几何中心在哪)是本系统自己的"从右向左
    // 分槽位"公式,允许跟其它 4 套按钮系统的排布不一样;算出中心点之后,
    // 统一交给 IconButtonRectDip 从中心点+正方形边长构造最终矩形,不再自己
    // 手写 left = right - size 这一步。
    float rowTop = headerHeightDip + rowHeightDip * static_cast<float>(itemIndex) - scrollY;
    float right = panelWidthDip - kSidebarCloseButtonMarginDip -
                  static_cast<float>(slotIndex) * (kSidebarCloseButtonSizeDip + kSidebarFolderButtonGapDip);
    float centerX = right - kSidebarCloseButtonSizeDip * 0.5f;
    float centerY = rowTop + rowHeightDip * 0.5f;
    IconButton btn{kSidebarCloseButtonSizeDip, 0.0f, nullptr, nullptr, nullptr};
    ButtonRectDip r = IconButtonRectDip(btn, centerX, centerY);
    return SidebarRectDip{r.left, r.top, r.right, r.bottom};
}

/**
 * Calculate the per-row close/delete button rectangle in DIP, in panel-local
 * coordinates (X relative to the drawer's own left edge, i.e. same coordinate
 * system SidebarHitTestItem's caller must convert into before calling this).
 * Pure function.
 *
 * 计算某一行右侧关闭/删除按钮的矩形（DIP），面板局部坐标系（X 相对抽屉自身
 * 左边缘，即调用方在调用本函数前需转换到的坐标系）。纯函数。
 *
 * @param panelWidthDip Configured panel width in DIP.
 *
 *   侧边栏面板宽度（DIP）。
 *
 * @param itemIndex Zero-based row index whose close button rect is wanted.
 *
 *   要计算关闭按钮矩形的行下标（从零起始）。
 *
 * @param scrollY Current vertical scroll offset in DIP.
 *
 *   当前纵向滚动偏移量（DIP）。
 *
 * @param rowHeightDip Height of each row in DIP.
 *
 *   单行项高度（DIP）。
 *
 * @param headerHeightDip Height of top header in DIP.
 *
 *   顶部标题栏高度（DIP）。
 *
 * @return Close button rectangle in panel-local DIP coordinates.
 *
 *   面板局部坐标系下的关闭按钮矩形（DIP）。
 */
inline SidebarRectDip SidebarCloseButtonLocalRectDip(
    float panelWidthDip, u32 itemIndex, float scrollY,
    float rowHeightDip = kSidebarRowHeightDip,
    float headerHeightDip = kSidebarHeaderHeightDip) {
    return SidebarItemButtonSlotRectDip(panelWidthDip, itemIndex, 0, scrollY, rowHeightDip, headerHeightDip);
}

/**
 * Calculate the per-row "open containing folder" button rectangle in DIP, in
 * panel-local coordinates. Sits immediately to the left of the close/delete
 * button (see SidebarCloseButtonLocalRectDip), same size, same vertical
 * position. Pure function.
 *
 * 计算某一行"打开所在文件夹"按钮的矩形（DIP），面板局部坐标系。紧贴在关闭/
 * 删除按钮（见 SidebarCloseButtonLocalRectDip）左侧，尺寸与纵向位置相同。
 * 纯函数。
 *
 * @param panelWidthDip Configured panel width in DIP.
 *
 *   侧边栏面板宽度（DIP）。
 *
 * @param itemIndex Zero-based row index whose folder button rect is wanted.
 *
 *   要计算文件夹按钮矩形的行下标（从零起始）。
 *
 * @param scrollY Current vertical scroll offset in DIP.
 *
 *   当前纵向滚动偏移量（DIP）。
 *
 * @param rowHeightDip Height of each row in DIP.
 *
 *   单行项高度（DIP）。
 *
 * @param headerHeightDip Height of top header in DIP.
 *
 *   顶部标题栏高度（DIP）。
 *
 * @return Folder button rectangle in panel-local DIP coordinates.
 *
 *   面板局部坐标系下的文件夹按钮矩形（DIP）。
 */
inline SidebarRectDip SidebarFolderButtonLocalRectDip(
    float panelWidthDip, u32 itemIndex, float scrollY,
    float rowHeightDip = kSidebarRowHeightDip,
    float headerHeightDip = kSidebarHeaderHeightDip) {
    return SidebarItemButtonSlotRectDip(panelWidthDip, itemIndex, 1, scrollY, rowHeightDip, headerHeightDip);
}

// 文件夹侧栏专属常量：顶部工具栏分两行（第一行标题+打开目录按钮，第二行过滤输入框）
// 第二行高度不再用 36dip 撑满(那会导致过滤框要么留出明显的上下间距，要么
// 撑高原生 EDIT 控件让光标不垂直居中)——改为按输入框自身 24dip 的高度收窄
// 整个第二行，输入框铺满这一行、不留间隙，头部总高度跟着从 72 降到 60。
constexpr float kFolderHeaderRow1HeightDip = 36.0f;
constexpr float kFolderHeaderRow2HeightDip = 24.0f;
constexpr float kFolderSidebarHeaderHeightDip =
    kFolderHeaderRow1HeightDip + kFolderHeaderRow2HeightDip;

/**
 * 计算文件夹侧栏顶部第一行右侧"打开根目录"文件夹按钮的矩形（DIP）。
 *
 * @param panelWidthDip 侧边栏面板宽度（DIP）。
 * @return 按钮在面板局部坐标系下的矩形。
 */
// "打开根目录"文件夹按钮的正方形边长(DIP)——出现 3 次以上,提成命名常量。
constexpr float kSidebarFolderHeaderButtonSizeDip = 24.0f;

inline SidebarRectDip SidebarFolderHeaderButtonLocalRectDip(float panelWidthDip) {
    float centerX = panelWidthDip - kSidebarPanelPaddingDip - kSidebarFolderHeaderButtonSizeDip * 0.5f;
    float centerY = kFolderHeaderRow1HeightDip * 0.5f;
    IconButton btn{kSidebarFolderHeaderButtonSizeDip, 0.0f, L"打开根目录文件夹", nullptr, nullptr};
    ButtonRectDip r = IconButtonRectDip(btn, centerX, centerY);
    return SidebarRectDip{r.left, r.top, r.right, r.bottom};
}

/**
 * 计算文件夹侧栏顶部第二行过滤输入框的容器矩形（DIP）。
 *
 * @param panelWidthDip 侧边栏面板宽度（DIP）。
 * @return 输入框容器在面板局部坐标系下的矩形。
 */
inline SidebarRectDip SidebarFolderFilterInputLocalRectDip(float panelWidthDip) {
    // 需求：过滤框左边紧贴侧栏面板边缘，不复用 kSidebarPanelPaddingDip
    // (该常量仍给标题行文字/空态提示等其它场景用)。
    float left = 0.0f;
    // 右边不能真正贴到面板边缘——列表自绘滚动条(renderer.cpp DrawScrollbar)
    // 的轨道从 y=0 铺到 targetHeight,横向覆盖整个头部两行，右边界必须让出
    // 滚动条列宽度，否则过滤框背景会盖住滚动条(实测截图发现的真实 bug，
    // 已修正)。数值等于 scrollbar.h 里 kScrollbarMarginDip(4) +
    // kScrollbarWidthDip(6)，这里不直接 #include scrollbar.h 是因为它和
    // renderer.cpp 匿名命名空间里的同名副本会产生二义性符号冲突。
    constexpr float kFolderFilterRightScrollbarGapDip = 10.0f;
    float right = panelWidthDip - kFolderFilterRightScrollbarGapDip;
    // 改为下划线样式后紧贴第一行底边，不再居中留间隙
    float top = kFolderHeaderRow1HeightDip;
    // 这是"视觉容器"矩形(第二行整块区域)，用于 D2D 背景/占位文字绘制和
    // 点击热区判定；底边跟列表内容区分割线(kFolderSidebarHeaderHeightDip
    // 处)重合，不再留 10px 空隙。原生 EDIT 控件的实际高度不用这个矩形，
    // 见 SidebarFolderFilterEditLocalRectDip。
    return SidebarRectDip{left, top, right, top + kFolderHeaderRow2HeightDip};
}

// 原生 EDIT 控件的实际窗口矩形(DIP)：高度就是 kFolderHeaderRow2HeightDip
// 本身(24dip)，铺满整个第二行、上下不留间隙——裁决(实测截图+用户拍板)：
// 不要用"控件矮+在更高的行内居中"这种方式掩盖间距，而是把行高本身收窄到
// 跟控件一样高。左右沿用视觉容器的 left/right(紧贴面板边缘，右边让出
// 滚动条列)。
constexpr float kFolderFilterEditHeightDip = kFolderHeaderRow2HeightDip;
inline SidebarRectDip SidebarFolderFilterEditLocalRectDip(float panelWidthDip) {
    SidebarRectDip container = SidebarFolderFilterInputLocalRectDip(panelWidthDip);
    return container;
}

/**
 * 计算文件夹侧栏中某一项最右侧"打开所在文件夹"按钮的矩形（DIP）。
 *
 * @param panelWidthDip 侧边栏面板宽度（DIP）。
 * @param itemIndex 要计算的行下标（从零起始）。
 * @param scrollY 当前纵向滚动偏移量（DIP）。
 * @param rowHeightDip 单行项高度（DIP）。
 * @param headerHeightDip 顶部两行标题栏总高度（DIP）。
 * @return 按钮在面板局部坐标系下的矩形。
 */
inline SidebarRectDip SidebarFolderItemButtonLocalRectDip(
    float panelWidthDip, u32 itemIndex, float scrollY,
    float rowHeightDip = kSidebarRowHeightDip,
    float headerHeightDip = kFolderSidebarHeaderHeightDip) {
    return SidebarItemButtonSlotRectDip(panelWidthDip, itemIndex, 0, scrollY, rowHeightDip, headerHeightDip);
}

/**
 * 计算文件夹侧栏中某一项"新窗口打开"按钮的矩形（DIP）。紧贴在"打开所在
 * 文件夹"按钮（见 SidebarFolderItemButtonLocalRectDip，槽位 0）左侧，
 * 尺寸与纵向位置相同。纯函数。
 *
 * @param panelWidthDip 侧边栏面板宽度（DIP）。
 * @param itemIndex 要计算的行下标（从零起始）。
 * @param scrollY 当前纵向滚动偏移量（DIP）。
 * @param rowHeightDip 单行项高度（DIP）。
 * @param headerHeightDip 顶部两行标题栏总高度（DIP）。
 * @return 按钮在面板局部坐标系下的矩形。
 * @example SidebarRectDip r = SidebarFolderItemNewWindowButtonLocalRectDip(260.0f, 0, 0.0f);
 */
inline SidebarRectDip SidebarFolderItemNewWindowButtonLocalRectDip(
    float panelWidthDip, u32 itemIndex, float scrollY,
    float rowHeightDip = kSidebarRowHeightDip,
    float headerHeightDip = kFolderSidebarHeaderHeightDip) {
    return SidebarItemButtonSlotRectDip(panelWidthDip, itemIndex, 1, scrollY, rowHeightDip, headerHeightDip);
}

}  // namespace markair
