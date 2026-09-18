#include "selection.h"

#include "../util/str.h"

namespace mdvn {

namespace {

// 累加写入(与 clipboard.cpp::CodeBlockPlainTextUtf8 同一惯用手法):
// out 为空时只计数,非空时按 cap 截断写入,total 始终是完整长度。
void AppendBytes(const char* bytes, u32 len, char* out, u32 cap, u32* total) {
    for (u32 k = 0; k < len; ++k) {
        if (out && *total < cap) out[*total] = bytes[k];
        ++*total;
    }
}

}  // namespace

u32 BlockOwnPlainTextUtf8(const Document& doc, u32 blockIndex, char* out, u32 cap) {
    if (blockIndex >= doc.blocks.Size()) return 0;
    const Block& b = doc.blocks[blockIndex];

    u32 total = 0;
    for (u32 i = 0; i < b.inlineCount; ++i) {
        const Inline& in = doc.inlines[b.firstInlineIdx + i];
        // 必须走 InlineTextBytes:合成换行/合成缩进的 run 不指向 source,
        // 直接读 source + textOffset 会拿到错的字节(见 model.h 的标记说明)。
        const char* bytes = InlineTextBytes(in, doc);
        AppendBytes(bytes, in.textLen, out, cap, &total);
    }
    return total;
}

StrSlice SelectionPlainTextUtf8(const Document& doc, const SelectionRange& range, Arena* arena) {
    if (!arena) return StrSlice{nullptr, 0};
    if (SelectionEmpty(range)) return StrSlice{nullptr, 0};
    if (range.start.blockIndex >= doc.blocks.Size()) return StrSlice{nullptr, 0};

    arena->Reset();

    // 第一遍只算上界(纯数字计算,不分配):每块裁剪后的字节数不会超过该块
    // 自身完整文本的字节数,块间分隔符最多 (块数 - 1) 个 '\n'。据此先分配好
    // 一块足够大且**位置固定**的输出缓冲区,后续的 UTF-8/UTF-16 往返转换
    // 临时分配都排在它后面,不会互相覆盖(全程只 Reset 一次,见函数头注释)。
    u32 upperBound = 0;
    u32 blockCountInRange = 0;
    for (u32 blockIndex = range.start.blockIndex;
         blockIndex <= range.end.blockIndex && blockIndex < doc.blocks.Size(); ++blockIndex) {
        u32 len = BlockOwnPlainTextUtf8(doc, blockIndex, nullptr, 0);
        if (len == 0) continue;
        upperBound += len;
        ++blockCountInRange;
    }
    if (blockCountInRange > 1) upperBound += blockCountInRange - 1;  // 块间分隔符
    if (upperBound == 0) return StrSlice{nullptr, 0};

    char* outBuf = static_cast<char*>(arena->Alloc(upperBound, 1));
    if (!outBuf) return StrSlice{nullptr, 0};

    u32 written = 0;
    bool wroteAny = false;

    for (u32 blockIndex = range.start.blockIndex;
         blockIndex <= range.end.blockIndex && blockIndex < doc.blocks.Size(); ++blockIndex) {
        u32 utf8Len = BlockOwnPlainTextUtf8(doc, blockIndex, nullptr, 0);
        if (utf8Len == 0) continue;  // 容器块/空块,跨选区时天然跳过

        char* utf8Buf = static_cast<char*>(arena->Alloc(utf8Len, 1));
        if (!utf8Buf) break;  // 临时 Arena 耗尽,已写出的部分仍然有效
        BlockOwnPlainTextUtf8(doc, blockIndex, utf8Buf, utf8Len);

        Utf16Slice wide = Utf8ToUtf16(StrSlice{utf8Buf, utf8Len}, arena);
        if (!wide.data) break;

        u32 sliceStart = (blockIndex == range.start.blockIndex) ? range.start.charOffset : 0;
        u32 sliceEnd = (blockIndex == range.end.blockIndex) ? range.end.charOffset : wide.len;
        if (sliceStart > wide.len) sliceStart = wide.len;
        if (sliceEnd > wide.len) sliceEnd = wide.len;
        if (sliceStart >= sliceEnd) continue;  // 该块落在选区外的那一侧,没有内容

        StrSlice segment =
            Utf16ToUtf8(Utf16Slice{wide.data + sliceStart, sliceEnd - sliceStart}, arena);
        if (!segment.data) break;

        if (wroteAny && written < upperBound) outBuf[written++] = '\n';
        for (u32 k = 0; k < segment.len && written < upperBound; ++k) outBuf[written++] = segment.data[k];
        wroteAny = true;
    }

    return StrSlice{outBuf, written};
}

}  // namespace mdvn
