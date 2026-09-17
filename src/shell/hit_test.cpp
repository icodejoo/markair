#include "hit_test.h"

namespace mdvn {

namespace {

// "什么都没命中"的统一返回值。
HitResult NoHit() {
    return HitResult{HitKind::None, kInvalidIndex, kInvalidIndex, kInvalidIndex};
}

// 点是否落在一个左闭右开、上闭下开的矩形内。
bool Contains(float left, float top, float width, float height, float x, float y) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

}  // namespace

DocPoint ClientToDocument(int clientX, int clientY, float dipScale, float scrollY) {
    float scale = dipScale > 0.0f ? dipScale : 1.0f;
    return DocPoint{static_cast<float>(clientX) / scale,
                    static_cast<float>(clientY) / scale + scrollY};
}

bool IsContentBlockType(BlockType type) {
    switch (type) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::CodeBlock:
    case BlockType::ListItem:
    case BlockType::ThematicBreak:
    case BlockType::TableHeadCell:
    case BlockType::TableCell:
        return true;
    default:
        // 容器块(Document/列表/引用/表格及其分组行/脚注区)不参与命中:
        // 块之间的垂直间距属于容器,落在那里应当判为"没命中"。
        return false;
    }
}

u32 FindContentBlockAt(const BlockGeometry* geometries, u32 count, float docX, float docY) {
    if (!geometries) return kInvalidIndex;

    u32 best = kInvalidIndex;
    for (u32 i = 0; i < count; ++i) {
        const BlockGeometry& g = geometries[i];
        if (!IsContentBlockType(g.type)) continue;
        if (docY < g.top || docY >= g.bottom) continue;

        // 表格单元格:同一行的多个单元格 y 区间完全相同,必须再按列宽区分 x,
        // 否则点在第 3 列会误命中第 1 列。
        if (g.cellWidth > 0.0f) {
            if (docX < g.indent || docX >= g.indent + g.cellWidth) continue;
        }
        // 前序数组里下标更大 = 更深/更靠后,取最后一个匹配即最深的那个块。
        best = i;
    }
    return best;
}

u32 FindImageBoxAt(const ImageBox* boxes, u32 count, float docX, float docY) {
    if (!boxes) return kInvalidIndex;
    for (u32 i = 0; i < count; ++i) {
        const LayoutRect& r = boxes[i].rect;
        if (Contains(r.x, r.y, r.width, r.height, docX, docY)) return i;
    }
    return kInvalidIndex;
}

u32 LinkTargetAtTextPosition(const LinkBox* boxes, u32 count, u32 textPosition) {
    if (!boxes) return kInvalidIndex;
    for (u32 i = 0; i < count; ++i) {
        const LinkBox& lb = boxes[i];
        // 半开区间:落在 run 末尾(行尾空白常见的命中位置)不算在链接内。
        if (textPosition >= lb.textPosition && textPosition < lb.textPosition + lb.textLength) {
            return lb.linkTargetIdx;
        }
    }
    return kInvalidIndex;
}

HitResult HitTestDocument(const BlockLayoutEngine& layout, float docX, float docY) {
    u32 blockCount = layout.BlockCount();

    // 图片优先:图片/占位块是一整块独立的可点击区域,且可能挂在容器型块下,
    // 不受 IsContentBlockType 过滤影响,所以单独先扫一遍。图片数量天然很少,
    // 线性扫描足够,不为此新建空间索引。
    for (u32 i = 0; i < blockCount; ++i) {
        const BlockGeometry& g = layout.Geometry(i);
        if (g.imageBoxes.len == 0) continue;
        u32 imageIndex = FindImageBoxAt(g.imageBoxes.data, g.imageBoxes.len, docX, docY);
        if (imageIndex != kInvalidIndex) {
            return HitResult{HitKind::Image, i, g.imageBoxes[imageIndex].linkTargetIdx, imageIndex};
        }
    }

    u32 blockIndex = FindContentBlockAt(
        blockCount > 0 ? &layout.Geometry(0) : nullptr, blockCount, docX, docY);
    if (blockIndex == kInvalidIndex) return NoHit();

    const BlockGeometry& g = layout.Geometry(blockIndex);
    if (!g.textLayout || g.linkBoxes.len == 0) return NoHit();

    BOOL isTrailingHit = FALSE;
    BOOL isInside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    HRESULT hr = g.textLayout->HitTestPoint(docX - g.indent, docY - g.top,
                                             &isTrailingHit, &isInside, &metrics);
    if (FAILED(hr) || !isInside) {
        // isInside 为假 = 点落在行尾空白/行外的"最近字符"上,不算命中文本。
        return NoHit();
    }

    u32 targetIdx = LinkTargetAtTextPosition(g.linkBoxes.data, g.linkBoxes.len,
                                              metrics.textPosition);
    if (targetIdx == kInvalidIndex) return NoHit();
    return HitResult{HitKind::Link, blockIndex, targetIdx, kInvalidIndex};
}

bool ShouldUseHandCursor(const HitResult& hit) {
    return hit.kind == HitKind::Link || hit.kind == HitKind::Image;
}

}  // namespace mdvn
