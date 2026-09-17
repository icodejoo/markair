// mdvn 的表格列宽算法(T25)。
//
// 取舍说明:理想宽度本可以用 IDWriteTextLayout::DetermineMinWidth 之类的真实
// 排版度量,但那样会让本模块依赖 DirectWrite/字体子系统,违背"纯数字函数,
// 可脱离 D2D 单测"的验收要求;这里改用"字符数 × 平均字符宽度"的估算口径,
// 与 src/layout/layout.cpp 里块高度估算的取舍完全一致(同一份代码风格/口径),
// 差之毫厘不影响"内容测宽 + 上限约束"这个算法方向本身。
#pragma once

#include "../util/arena.h"
#include "../util/span.h"
#include "../util/types.h"

namespace mdvn {

// 单列宽度上限相对视口宽度的比例(04 已定方向:内容测宽 + 上限约束)。
constexpr float kMaxColumnWidthRatio = 0.6f;

// 单列宽度下限(DIP):等比压缩不会把列压得比这个值还窄;仍超宽时改为
// 单元格内文本换行(裁决 #2),不再继续压缩。
constexpr float kMinColumnWidthDip = 60.0f;

// 列与列之间预留的间距/网格线宽度(DIP),参与总宽是否超视口的判断。
constexpr float kTableColumnGapDip = 1.0f;

// 表格单元格内文本左右各留的内边距(DIP,T26)。ComputeTableColumnWidths 算
// 理想宽度时要把它算进去(见 kTableAvgCharWidthDip 旁的说明),否则列宽只够
// "内容本身"、不够"内容 + 两侧内边距",定义该列宽度的那个最长单元格在
// LayoutTableSubtree 里减去内边距求 textWidth 后反而放不下自己,被错误地
// 估算成需要换行——两处口径必须一致,内边距只在一个地方定义、两处共用。
constexpr float kTableCellPaddingDip = 6.0f;

// 估算单元格内容理想宽度用的平均字符宽度(DIP),与 layout.cpp 的
// kAvgCharWidthDip 取相同口径,保持两处估算风格一致。这里的"字符"指
// Utf8VisualWidth 输出的视觉宽度单位(ASCII 记 1、CJK 等宽字符记 2),
// 不是字节数,否则中英文混排时中文按 3 字节算会把列宽估得偏宽,
// 挤压同一行里纯英文列的宽度。
constexpr float kTableAvgCharWidthDip = 8.0f;

/**
 * 计算一个表格各列的宽度(DIP)。
 *
 * 算法(04 已定方向,见文件头注释的度量取舍;2026-09-17 用户真机反馈修复
 * "拖动窗口时表格明明离右边界还有大片空白却仍触发换行重排"的问题后调整
 * 为"只在真正需要压缩时才生效"的两段式):
 *   1. 每列理想宽度 = 该列所有单元格"视觉宽度估算"(见 Utf8VisualWidth)的
 *      最大值 × 平均字符宽度 + 两侧内边距(kTableCellPaddingDip * 2),
 *      只钳制下限 kMinColumnWidthDip,**不**在这一步钳制上限——上限
 *      (kMaxColumnWidthRatio)是"压缩时怎么分摊"的规则,不是"要不要压缩"
 *      的判断依据,提前钳到视口比例会导致表格整体明明还有富余空间,仅因为
 *      某一列内容很长就被强制换行,且这个上限本身随视口变化,窗口一拖动
 *      该列就跟着持续重排,即使表格右边界离窗口右边界仍有大片空白。
 *   2. 若各列理想宽度之和(含列间距)未超过视口宽度:直接使用第 1 步的
 *      理想宽度,不做任何进一步处理(哪怕某一列因此占了视口的绝大部分)——
 *      表格不需要收缩,就不该有任何视觉变化。
 *   3. 若超过视口宽度(表格才真正"贴到"可用宽度的边界):先把每列理想宽度
 *      钳到上限 [kMinColumnWidthDip, 视口宽度 * kMaxColumnWidthRatio],
 *      再按钳制后的比例整体等比压缩,压缩下限为 kMinColumnWidthDip。
 *   4. 压到下限后仍超宽,不再进一步压缩(由调用方对单元格文本换行,
 *      裁决 #2:表格变高但不丢信息)。
 *
 * @param cellCharCounts 按行主序展开的单元格"视觉宽度估算"(Utf8VisualWidth
 *                       的输出,不是字节数也不是字符数)二维数组,
 *                       长度须为 colCount * rowCount(含表头行)。
 * @param colCount 列数,0 时直接返回空 Span。
 * @param rowCount 行数(含表头),用于遍历 cellCharCounts。
 * @param viewportWidth 当前视口宽度(DIP)。
 * @param arena 输出数组所在的 Arena,按裁决 §6"列数 × 4 字节"存放。
 * @return colCount 个元素的列宽数组;Arena 分配失败时返回空 Span(data=nullptr,len=0)。
 * @example
 *   u32 chars[] = {3, 10,   2, 20}; // 2 列 2 行:第 0 列最大视觉宽度 3,第 1 列 20
 *   mdvn::Span<float> widths = mdvn::ComputeTableColumnWidths(chars, 2, 2, 600.0f, &arena);
 */
Span<float> ComputeTableColumnWidths(const u32* cellCharCounts, u32 colCount, u32 rowCount,
                                      float viewportWidth, Arena* arena);

}  // namespace mdvn
