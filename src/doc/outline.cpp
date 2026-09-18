#include "outline.h"

namespace mdvn {

u32 ExtractOutline(const Document& doc, Vec<OutlineItem>* out) {
    u32 found = 0;
    for (u32 i = 0; i < doc.blocks.Size(); ++i) {
        const Block& b = doc.blocks[i];
        // level 恒在 1-6 内(见 model.h Block::level 注释),这里仍做一次边界
        // 检查,防御未来 md4c 版本/上游数据异常带来的越界 level。
        if (b.type == BlockType::Heading && b.level >= 1 && b.level <= 6) {
            out->Push(OutlineItem{i, b.level});
            ++found;
        }
    }
    return found;
}

} // namespace mdvn
