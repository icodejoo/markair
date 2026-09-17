#include "table.h"

namespace mdvn {

Span<float> ComputeTableColumnWidths(const u32* cellCharCounts, u32 colCount, u32 rowCount,
                                      float viewportWidth, Arena* arena) {
    if (colCount == 0 || arena == nullptr) return Span<float>{nullptr, 0};

    float* widths = static_cast<float*>(arena->Alloc(sizeof(float) * colCount, alignof(float)));
    if (!widths) return Span<float>{nullptr, 0};

    float safeViewport = viewportWidth > 0.0f ? viewportWidth : 0.0f;
    float maxColWidth = safeViewport * kMaxColumnWidthRatio;
    if (maxColWidth < kMinColumnWidthDip) maxColWidth = kMinColumnWidthDip;

    // ① 每列理想宽度:内容测宽 + 上限约束。
    float totalIdeal = 0.0f;
    for (u32 c = 0; c < colCount; ++c) {
        u32 maxChars = 0;
        if (cellCharCounts != nullptr) {
            for (u32 r = 0; r < rowCount; ++r) {
                u32 v = cellCharCounts[r * colCount + c];
                if (v > maxChars) maxChars = v;
            }
        }
        float ideal = static_cast<float>(maxChars) * kTableAvgCharWidthDip;
        if (ideal < kMinColumnWidthDip) ideal = kMinColumnWidthDip;
        if (ideal > maxColWidth) ideal = maxColWidth;
        widths[c] = ideal;
        totalIdeal += ideal;
    }

    // ② 总宽超视口时按理想宽度比例整体等比压缩。
    float gapTotal = kTableColumnGapDip * static_cast<float>(colCount > 0 ? colCount - 1 : 0);
    float totalWithGap = totalIdeal + gapTotal;
    if (totalWithGap > safeViewport && totalIdeal > 0.0f) {
        float available = safeViewport - gapTotal;
        float floorTotal = kMinColumnWidthDip * static_cast<float>(colCount);
        // ③ 压到下限仍超宽:不再进一步压缩,交给调用方按裁决 #2 换行。
        if (available < floorTotal) available = floorTotal;

        float scale = available / totalIdeal;
        for (u32 c = 0; c < colCount; ++c) {
            float w = widths[c] * scale;
            if (w < kMinColumnWidthDip) w = kMinColumnWidthDip;
            widths[c] = w;
        }
    }

    return Span<float>{widths, colCount};
}

}  // namespace mdvn
