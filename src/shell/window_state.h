// markair 窗口状态记忆(T56):恢复/钳制窗口矩形 + 多开层叠偏移的纯几何逻辑。
// 裁决 #8 定死"全局一份 + 层叠偏移":state.ini 只存一份窗口矩形,新窗口默认
// 出现在上次记住的位置;同位置已有本程序窗口时按 kCascadeOffsetDip 层叠。
//
// 本文件不 include <windows.h>、不碰 GetWindowPlacement/MonitorFromRect 等
// 任何 Win32 API,只处理矩形数字——这样才能脱离图形环境单测多显示器越界
// 场景(见 tests/test_window_state.cpp)。真正调用 Win32 取显示器/已有窗口
// 矩形、调用 SetWindowPlacement 的部分在 src/shell/window.cpp。
#pragma once

#include "../util/types.h"

namespace markair {

// 窗口尺寸下限(物理像素),防止存进去一个几乎不可见的窗口再也打不开。
constexpr i32 kMinWindowWidth = 300;
constexpr i32 kMinWindowHeight = 200;

// 多开层叠偏移量(DIP,随 DPI 缩放;调用方在换算成物理像素后再喂给
// `ApplyCascadeOffset`)。裁决 #8 定死的数值,不得散落成字面量。
constexpr float kCascadeOffsetDip = 24.0f;

/**
 * 整数矩形(物理像素),字段语义与 Win32 `RECT` 一致:`right`/`bottom` 不含在内。
 * 纯 POD,不含任何有副作用的构造函数。
 */
struct RectI {
    i32 left;
    i32 top;
    i32 right;
    i32 bottom;

    i32 Width() const { return right - left; }
    i32 Height() const { return bottom - top; }
};

/**
 * 判断两个矩形是否有重叠(边缘相接不算重叠,与 `MonitorFromRect` 的语义
 * 口径一致——`MONITOR_DEFAULTTONULL` 要求矩形与显示器边界有实际交集)。
 * @param a 矩形 A。
 * @param b 矩形 B。
 * @return 有非空重叠区域返回 true。
 * @example markair::RectsOverlap({0,0,100,100}, {50,50,150,150});  // true
 */
inline bool RectsOverlap(const RectI& a, const RectI& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

/**
 * 一台显示器的完整边界(用于 `MonitorFromRect` 式的越界判定)与工作区
 * (用于居中钳制,不含任务栏)。两者在多显示器/任务栏不同位置的场景下
 * 通常不同。
 */
struct MonitorRect {
    RectI bounds;     // 显示器完整边界(对应 `MONITORINFO::rcMonitor`)
    RectI workArea;   // 显示器工作区(对应 `MONITORINFO::rcWork`,不含任务栏)
};

/**
 * 按 `MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)` 的语义,找出 `rect` 落在
 * 哪个当前存在的显示器上(与显示器**完整边界**有重叠即算,矩形可以只有
 * 标题栏可见——"部分越界"是合法情形,不算落不到任何显示器上)。
 * @param rect 待判定矩形(物理像素)。
 * @param monitors 当前系统的显示器列表。
 * @param monitorCount 列表长度。
 * @return 命中的显示器下标;一个都不重叠(显示器被拔掉/分辨率变小等)返回 -1。
 * @example
 *   markair::MonitorRect mons[] = {{{0,0,1920,1080}, {0,0,1920,1040}}};
 *   markair::FindMonitorContaining({100,100,900,700}, mons, 1);  // 0
 */
inline i32 FindMonitorContaining(const RectI& rect, const MonitorRect* monitors, u32 monitorCount) {
    for (u32 i = 0; i < monitorCount; ++i) {
        if (RectsOverlap(rect, monitors[i].bounds)) return static_cast<i32>(i);
    }
    return -1;
}

/**
 * 把矩形尺寸钳到不小于下限,原点(左上角)保持不动。
 * @param rect 待钳制矩形。
 * @param minWidth 宽度下限。
 * @param minHeight 高度下限。
 * @return 尺寸不低于下限的矩形。
 * @example markair::ClampMinimumSize({0,0,10,10}, 300, 200);  // {0,0,300,200}
 */
inline RectI ClampMinimumSize(const RectI& rect, i32 minWidth, i32 minHeight) {
    RectI out = rect;
    if (out.Width() < minWidth) out.right = out.left + minWidth;
    if (out.Height() < minHeight) out.bottom = out.top + minHeight;
    return out;
}

/**
 * 窗口恢复矩形的多显示器越界保护(本任务的核心)。
 *
 * 规则:① 先把尺寸钳到 `kMinWindowWidth`/`kMinWindowHeight` 下限;
 * ② 用 `FindMonitorContaining` 判断钳过尺寸的矩形是否仍落在某个当前存在的
 * 显示器上——落得到(哪怕只有标题栏可见,即"部分越界")原样保留位置,只是
 * 尺寸已经钳过;③ 一个显示器都落不到(显示器被拔掉/分辨率变小/从外接屏
 * 回到笔记本屏)时,钳制到主显示器工作区**中心**,尺寸同时钳到不超过该
 * 工作区。
 *
 * @param rect 待恢复的矩形(物理像素,来自上次保存的 `win_x/y/w/h`)。
 * @param monitors 当前系统的显示器列表,不可为空(除非 `monitorCount == 0`)。
 * @param monitorCount 列表长度。
 * @param primaryIndex 主显示器在 `monitors` 里的下标(越界保护钳制到它)。
 * @return 钳制后的合法窗口矩形。`monitorCount == 0` 时只做最小尺寸钳制,
 *         原样返回位置(极端情况下的兜底,不应在真实系统上发生)。
 * @example
 *   markair::MonitorRect mons[] = {{{0,0,1920,1080}, {0,0,1920,1040}}};
 *   // 副屏被拔掉后,原矩形整体落在 (2000,0) 之外的地方 -> 钳到主屏中心
 *   markair::ClampWindowRectToMonitors({2000,0,2800,600}, mons, 1, 0);
 */
inline RectI ClampWindowRectToMonitors(const RectI& rect, const MonitorRect* monitors,
                                        u32 monitorCount, u32 primaryIndex) {
    RectI sized = ClampMinimumSize(rect, kMinWindowWidth, kMinWindowHeight);
    if (monitorCount == 0) return sized;

    if (FindMonitorContaining(sized, monitors, monitorCount) >= 0) return sized;

    u32 primary = primaryIndex < monitorCount ? primaryIndex : 0;
    const RectI& work = monitors[primary].workArea;
    i32 w = sized.Width();
    if (w > work.Width()) w = work.Width();
    i32 h = sized.Height();
    if (h > work.Height()) h = work.Height();

    i32 left = work.left + (work.Width() - w) / 2;
    i32 top = work.top + (work.Height() - h) / 2;
    return RectI{left, top, left + w, top + h};
}

/**
 * 两个矩形的原点(左上角)是否相同——用来判定"新窗口默认位置上是否已经
 * 有一个本程序窗口"(裁决 #8:同位置判定看原点,不看整块矩形是否完全相等,
 * 因为层叠链上后面的窗口尺寸可能因越界钳制而与最初保存的尺寸略有不同)。
 * @param a 矩形 A。
 * @param b 矩形 B。
 * @return 左上角坐标完全相同返回 true。
 */
inline bool SameOrigin(const RectI& a, const RectI& b) {
    return a.left == b.left && a.top == b.top;
}

/**
 * 多开层叠偏移(裁决 #8)。若 `baseRect` 的原点上已经有一个本程序窗口,
 * 依次按 `offset`(物理像素,调用方已把 `kCascadeOffsetDip` 按 DPI 换算过)
 * 向右下偏移,直到找到一个原点不与任何已有窗口重合的位置;若连续偏移导致
 * 矩形右/下边界超出 `workArea`,回卷到 `baseRect` 的原始位置重新开始找。
 *
 * 迭代次数以 `existingCount + 1` 为上限,保证在"已有窗口清单本身就是一条
 * 完整链"的最坏情况下也一定会终止(找不到空位时返回最后一次尝试的位置,
 * 属于极端退化场景,不影响正确性——层叠位置本来就只是体验优化)。
 *
 * @param baseRect 候选的默认位置(通常就是越界钳制后的恢复矩形)。
 * @param existingOrigins 当前已存在的本程序窗口的矩形原点列表
 *        (`FindWindowExW` 按窗口类名枚举后取矩形;只看原点,尺寸不参与比较)。
 * @param existingCount 列表长度。
 * @param workArea 目标显示器的工作区(用于判断层叠是否碰到右/下边界)。
 * @param offset 单次层叠的偏移量(物理像素,水平/垂直相同)。
 * @return 层叠后的矩形;无同位窗口时原样返回 `baseRect`。
 * @example
 *   markair::RectI existing[] = {{100,100,900,700}};
 *   markair::RectI cascaded = markair::ApplyCascadeOffset(
 *       {100,100,900,700}, existing, 1, {0,0,1920,1040}, 24);
 *   // cascaded == {124,124,924,724}
 */
inline RectI ApplyCascadeOffset(const RectI& baseRect, const RectI* existingOrigins,
                                 u32 existingCount, const RectI& workArea, i32 offset) {
    RectI candidate = baseRect;
    for (u32 guard = 0; guard <= existingCount; ++guard) {
        bool collides = false;
        for (u32 i = 0; i < existingCount; ++i) {
            if (SameOrigin(candidate, existingOrigins[i])) {
                collides = true;
                break;
            }
        }
        if (!collides) return candidate;

        i32 w = candidate.Width();
        i32 h = candidate.Height();
        RectI next{candidate.left + offset, candidate.top + offset, 0, 0};
        next.right = next.left + w;
        next.bottom = next.top + h;

        if (next.right > workArea.right || next.bottom > workArea.bottom) {
            // 碰到工作区右/下边界:回卷到起始位置重新开始,而不是继续无限往外偏移。
            candidate = baseRect;
        } else {
            candidate = next;
        }
    }
    return candidate;
}

}  // namespace markair
