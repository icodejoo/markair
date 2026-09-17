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

// 估算单元格内容理想宽度用的平均字符宽度(DIP),与 layout.cpp 的
// kAvgCharWidthDip 取相同口径,保持两处估算风格一致。
constexpr float kTableAvgCharWidthDip = 8.0f;

/**
 * 计算一个表格各列的宽度(DIP)。
 *
 * 算法(04 已定方向,见文件头注释的度量取舍):
 *   1. 每列理想宽度 = 该列所有单元格"字符数估算"的最大值 × 平均字符宽度,
 *      钳制到 [kMinColumnWidthDip, 视口宽度 * kMaxColumnWidthRatio] 区间;
 *   2. 若各列理想宽度之和(含列间距)超过视口宽度,按各列理想宽度的比例
 *      整体等比压缩,压缩下限为 kMinColumnWidthDip;
 *   3. 压到下限后仍超宽,不再进一步压缩(由调用方对单元格文本换行,
 *      裁决 #2:表格变高但不丢信息)。
 *
 * @param cellCharCounts 按行主序展开的单元格"字符数估算"二维数组,
 *                       长度须为 colCount * rowCount(含表头行)。
 * @param colCount 列数,0 时直接返回空 Span。
 * @param rowCount 行数(含表头),用于遍历 cellCharCounts。
 * @param viewportWidth 当前视口宽度(DIP)。
 * @param arena 输出数组所在的 Arena,按裁决 §6"列数 × 4 字节"存放。
 * @return colCount 个元素的列宽数组;Arena 分配失败时返回空 Span(data=nullptr,len=0)。
 * @example
 *   u32 chars[] = {3, 10,   2, 20}; // 2 列 2 行:第 0 列最长 3 字符,第 1 列最长 20 字符
 *   mdvn::Span<float> widths = mdvn::ComputeTableColumnWidths(chars, 2, 2, 600.0f, &arena);
 */
Span<float> ComputeTableColumnWidths(const u32* cellCharCounts, u32 colCount, u32 rowCount,
                                      float viewportWidth, Arena* arena);

}  // namespace mdvn
