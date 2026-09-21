#include "hit_test.h"

namespace markair {

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

u32 FindCodeCopyButtonAt(const BlockGeometry* geometries, u32 count, float docX, float docY) {
    if (!geometries) return kInvalidIndex;
    for (u32 i = 0; i < count; ++i) {
        const LayoutRect& r = geometries[i].codeCopyButton;
        // 非代码块的 codeCopyButton 恒为全 0,width <= 0 直接跳过,不必再判块类型。
        if (r.width <= 0.0f) continue;
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
    const BlockGeometry* geometries = blockCount > 0 ? &layout.Geometry(0) : nullptr;

    // T45 代码块复制按钮最优先:它在视觉上浮在代码块背景/文字之上,点它就该
    // 是"复制",不能被下面的文本命中抢走。
    u32 copyBlock = FindCodeCopyButtonAt(geometries, blockCount, docX, docY);
    if (copyBlock != kInvalidIndex) {
        return HitResult{HitKind::CodeCopyButton, copyBlock, kInvalidIndex, kInvalidIndex};
    }

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

    u32 blockIndex = FindContentBlockAt(geometries, blockCount, docX, docY);
    if (blockIndex == kInvalidIndex) return NoHit();

    const BlockGeometry& g = layout.Geometry(blockIndex);
    if (!g.textLayout || g.linkBoxes.len == 0) return NoHit();

    BOOL isTrailingHit = FALSE;
    BOOL isInside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    // T44/真实 bug 修复:命中坐标必须与渲染层 TextDrawLeft/TextDrawTop 用
    // 同一份公式(现共享定义在 layout.h),否则围栏代码块(textPad 非零)会
    // 整体错位一份内边距——错位量接近一行高,足以让点在第一行的鼠标被当成
    // 点在第二行(真实 bug,见 layout.h::TextDrawTop 头注释)。
    HRESULT hr = g.textLayout->HitTestPoint(docX - TextDrawLeft(g), docY - TextDrawTop(g),
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

DocTextHit HitTestTextPosition(const BlockLayoutEngine& layout, float docX, float docY) {
    u32 blockCount = layout.BlockCount();

    // 先按几何直接命中一个内容块(与 HitTestDocument 同一条纯几何判定)。
    u32 blockIndex = FindContentBlockAt(blockCount > 0 ? &layout.Geometry(0) : nullptr,
                                        blockCount, docX, docY);

    // 落在块间隙/文档上下边界外:找竖直方向最近的一个有文本的内容块,
    // 点在其上方就钳到开头,在其下方就钳到末尾——这样拖选可以从文档任意
    // 空白处开始/结束,行为贴近浏览器。
    if (blockIndex == kInvalidIndex) {
        u32 nearest = kInvalidIndex;
        float nearestDist = 0.0f;
        bool nearestAbove = false;  // docY 是否落在 nearest 块的上方
        for (u32 i = 0; i < blockCount; ++i) {
            const BlockGeometry& g = layout.Geometry(i);
            if (!g.textLayout) continue;
            float dist;
            bool above;
            if (docY < g.top) {
                dist = g.top - docY;
                above = true;
            } else if (docY >= g.bottom) {
                dist = docY - g.bottom;
                above = false;
            } else {
                dist = 0.0f;
                above = false;
            }
            if (nearest == kInvalidIndex || dist < nearestDist) {
                nearest = i;
                nearestDist = dist;
                nearestAbove = above;
            }
        }
        if (nearest == kInvalidIndex) return DocTextHit{false, kInvalidIndex, 0};
        const BlockGeometry& g = layout.Geometry(nearest);
        u32 endOffset = 0;
        if (!nearestAbove) {
            // DWRITE_TEXT_METRICS 没有直接的"文本总长度"字段,借道
            // HitTestPoint 打在远超文本范围的坐标上(必落在最后一个字符的
            // 尾随边),取 textPosition + length 反推出整块文本的 UTF-16 长度。
            BOOL trailing = FALSE;
            BOOL inside = FALSE;
            DWRITE_HIT_TEST_METRICS endMetrics{};
            if (SUCCEEDED(g.textLayout->HitTestPoint(1.0e6f, 1.0e6f, &trailing, &inside,
                                                       &endMetrics))) {
                endOffset = endMetrics.textPosition + endMetrics.length;
            }
        }
        return DocTextHit{true, nearest, nearestAbove ? 0u : endOffset};
    }

    const BlockGeometry& g = layout.Geometry(blockIndex);
    if (!g.textLayout) return DocTextHit{false, kInvalidIndex, 0};

    BOOL isTrailingHit = FALSE;
    BOOL isInside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    // 同上:必须用 TextDrawLeft/TextDrawTop,不能只减 g.indent/g.top(真实 bug,
    // 见 HitTestDocument 里的同一处修复注释)。
    HRESULT hr = g.textLayout->HitTestPoint(docX - TextDrawLeft(g), docY - TextDrawTop(g),
                                             &isTrailingHit, &isInside, &metrics);
    if (FAILED(hr)) return DocTextHit{false, kInvalidIndex, 0};

    u32 offset = metrics.textPosition + (isTrailingHit ? 1u : 0u);
    return DocTextHit{true, blockIndex, offset};
}

bool IsPointInOutlinePanel(int clientX, float dipScale, float panelWidthDip) {
    float scale = dipScale > 0.0f ? dipScale : 1.0f;
    float x = static_cast<float>(clientX) / scale;
    return x >= 0.0f && x < panelWidthDip;
}

bool IsPointInOutlineOverlayMask(int clientX, float dipScale, float panelWidthDip) {
    return !IsPointInOutlinePanel(clientX, dipScale, panelWidthDip);
}

bool IsPointInOutlinePanelRect(int clientX, int clientY, float dipScale, float panelWidthDip,
                                float clientHeightDip) {
    if (!IsPointInOutlinePanel(clientX, dipScale, panelWidthDip)) return false;
    float scale = dipScale > 0.0f ? dipScale : 1.0f;
    float y = static_cast<float>(clientY) / scale;
    return y >= 0.0f && y < clientHeightDip;
}

bool IsPointInOutlinePanelResizeHandle(int clientX, float dipScale, float panelWidthDip) {
    float scale = dipScale > 0.0f ? dipScale : 1.0f;
    float x = static_cast<float>(clientX) / scale;
    float half = kOutlinePanelResizeHandleWidthDip * 0.5f;
    return x >= panelWidthDip - half && x <= panelWidthDip + half;
}

bool ShouldUseHandCursor(const HitResult& hit) {
    return hit.kind == HitKind::Link || hit.kind == HitKind::Image ||
           hit.kind == HitKind::CodeCopyButton;
}

}  // namespace markair
