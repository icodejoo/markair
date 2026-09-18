#include "table.h"

namespace mdvn {

Span<float> ComputeTableColumnWidths(const u32* cellCharCounts, u32 colCount, u32 rowCount,
                                      float viewportWidth, Arena* arena, float fontScale) {
    if (colCount == 0 || arena == nullptr) return Span<float>{nullptr, 0};

    float* widths = static_cast<float*>(arena->Alloc(sizeof(float) * colCount, alignof(float)));
    if (!widths) return Span<float>{nullptr, 0};

    float safeViewport = viewportWidth > 0.0f ? viewportWidth : 0.0f;
    float safeScale = fontScale > 0.0f ? fontScale : 1.0f;
    // 单个视觉宽度单位真正要预留的宽度:平均字符宽度 × 字号缩放 × 字形安全系数。
    float unitWidth = kTableAvgCharWidthDip * safeScale * kTableGlyphWidthSafetyFactor;

    // ① 每列理想宽度:纯内容测宽,只钳下限,不钳视口相关的上限——上限是
    // "压缩时怎么分摊"的规则,不是"要不要压缩"的判断依据(见 table.h 注释)。
    float totalIdeal = 0.0f;
    for (u32 c = 0; c < colCount; ++c) {
        u32 maxChars = 0;
        if (cellCharCounts != nullptr) {
            for (u32 r = 0; r < rowCount; ++r) {
                u32 v = cellCharCounts[r * colCount + c];
                if (v > maxChars) maxChars = v;
            }
        }
        // +2*kTableCellPaddingDip:理想宽度要连内边距一起budget,否则
        // LayoutTableSubtree 减去内边距求 textWidth 时,定义该列宽度的那个
        // 最长单元格反而放不下自己(症状 2 的另一半根因,见 table.h 注释)。
        float ideal = static_cast<float>(maxChars) * unitWidth + kTableCellPaddingDip * 2.0f;
        if (ideal < kMinColumnWidthDip) ideal = kMinColumnWidthDip;
        widths[c] = ideal;
        totalIdeal += ideal;
    }

    // ② 总宽超视口才需要压缩;不超时直接用第①步的理想宽度,一个字节都不改。
    float gapTotal = kTableColumnGapDip * static_cast<float>(colCount > 0 ? colCount - 1 : 0);
    float totalWithGap = totalIdeal + gapTotal;
    if (totalWithGap > safeViewport && totalIdeal > 0.0f) {
        // ③ 真正需要压缩时,才把每列理想宽度先钳到上限(kMaxColumnWidthRatio),
        // 避免压缩后某一列依然独占绝大部分宽度,再按钳制后的比例整体等比压缩。
        float maxColWidth = safeViewport * kMaxColumnWidthRatio;
        if (maxColWidth < kMinColumnWidthDip) maxColWidth = kMinColumnWidthDip;
        for (u32 c = 0; c < colCount; ++c) {
            if (widths[c] > maxColWidth) widths[c] = maxColWidth;
        }

        float available = safeViewport - gapTotal;
        float floorTotal = kMinColumnWidthDip * static_cast<float>(colCount);
        // ④ 压到下限仍超宽:不再进一步压缩,交给调用方按裁决 #2 换行。
        if (available < floorTotal) available = floorTotal;

        // ⑤ 等比压缩,但要"避开已经躺在下限上的列":一刀切地整体等比缩放后再把
        // 结果钳回下限,被钳回来的那几个 DIP 没有任何一列买单,总宽依然超视口
        // (窄窗口下表现为表格右边几列被切出窗口外)。这里改成多轮分摊——每轮
        // 把已经到下限的列固定住,剩余预算只在还有压缩余地的列之间等比分配,
        // 直到没有新的列触底。列数有限,循环必然在 colCount 轮内收敛。
        //
        // 只压缩、不拉伸:钳完上限后总宽可能已经小于 available(比如就一列,
        // 钳到上限就直接达标),这时不应该反过来把列拉宽去"填满"可用空间
        // ——kMaxColumnWidthRatio 是硬上限,不是"尽量占满视口"的目标。
        for (u32 pass = 0; pass < colCount; ++pass) {
            float flexTotal = 0.0f;   // 还有压缩余地的列的当前总宽
            float fixedTotal = 0.0f;  // 已经躺在下限上、不再参与压缩的列
            for (u32 c = 0; c < colCount; ++c) {
                if (widths[c] <= kMinColumnWidthDip) {
                    widths[c] = kMinColumnWidthDip;
                    fixedTotal += kMinColumnWidthDip;
                } else {
                    flexTotal += widths[c];
                }
            }
            if (flexTotal <= 0.0f) break;

            float flexBudget = available - fixedTotal;
            float scale = flexBudget / flexTotal;
            if (scale >= 1.0f) break;  // 已经放得下,不再压缩,更不拉伸

            bool hitFloor = false;
            for (u32 c = 0; c < colCount; ++c) {
                if (widths[c] <= kMinColumnWidthDip) continue;
                float w = widths[c] * scale;
                if (w < kMinColumnWidthDip) {
                    w = kMinColumnWidthDip;
                    hitFloor = true;
                }
                widths[c] = w;
            }
            if (!hitFloor) break;  // 没有新的列触底,这一轮的等比分配就是最终结果
        }
    }

    return Span<float>{widths, colCount};
}

}  // namespace mdvn
