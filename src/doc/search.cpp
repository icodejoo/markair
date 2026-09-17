#include "search.h"

#include <cstring>

namespace mdvn {

namespace {

// ASCII 小写折叠。CJK / 其它非 ASCII 字节没有大小写之分,原样比较即可;
// UTF-8 的续字节恒 >= 0x80,不会被这里误折叠,所以按字节折叠是安全的。
char Fold(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// 十进制位数,用于算脚注引用在排版里合成的 "[n]" 占了多少个 UTF-16 code unit。
u32 DecimalDigits(u32 value) {
    u32 n = 1;
    while (value >= 10) { value /= 10; ++n; }
    return n;
}

// 某个 inline run 是否会进入正文排版(与 layout.cpp 的 CreateLayoutForBlock 同一口径):
// 图片 run 的 alt 画在图片内部、脚注引用用合成文本,两者都不是源文本本身。
bool RunEntersLayout(const Inline& in) {
    return (in.flags & (kInlineFlagImage | kInlineFlagFootnoteRef)) == 0;
}

// 该 run 在排版文本里占用的 UTF-16 长度(图片 0,脚注引用是合成的 "[n]")。
u32 RunLayoutUtf16Length(const Document& doc, const Inline& in) {
    if (in.flags & kInlineFlagFootnoteRef) return 2u + DecimalDigits(in.linkTargetIdx);
    if (in.flags & kInlineFlagImage) return 0u;
    if (in.textLen == 0) return 0u;
    return Utf16LengthOfUtf8(StrSlice{InlineTextBytes(in, doc), in.textLen});
}

// 把一个块的全部 inline run 源文本首尾相接拼进 scratch,得到本块的可搜索文本。
// 跨 run 的匹配之所以成立,就是因为搜索发生在这份拼接缓冲上。
StrSlice BuildBlockText(const Document& doc, const Block& b, Arena* scratch) {
    u32 total = 0;
    for (u32 i = 0; i < b.inlineCount; ++i) total += doc.inlines[b.firstInlineIdx + i].textLen;
    if (total == 0) return StrSlice{nullptr, 0};

    char* buf = static_cast<char*>(scratch->Alloc(total, 1));
    if (!buf) return StrSlice{nullptr, 0};  // Arena 耗尽:本块安全跳过,不崩溃

    u32 cursor = 0;
    for (u32 i = 0; i < b.inlineCount; ++i) {
        const Inline& in = doc.inlines[b.firstInlineIdx + i];
        if (in.textLen == 0) continue;
        memcpy(buf + cursor, InlineTextBytes(in, doc), in.textLen);
        cursor += in.textLen;
    }
    return StrSlice{buf, cursor};
}

}  // namespace

u32 FindSubstringNoCase(StrSlice haystack, StrSlice needle, u32 fromOffset) {
    if (!haystack.data || !needle.data) return kInvalidIndex;
    if (needle.len == 0 || needle.len > haystack.len) return kInvalidIndex;
    if (fromOffset > haystack.len - needle.len) return kInvalidIndex;

    // Boyer-Moore-Horspool 的坏字符跳转表(256 项落在栈上,不做任何分配)。
    // 关键词很短时跳转量小,行为自动退化成朴素扫描,不需要额外分支。
    u32 shift[256];
    for (u32 i = 0; i < 256; ++i) shift[i] = needle.len;
    for (u32 i = 0; i + 1 < needle.len; ++i) {
        shift[static_cast<unsigned char>(Fold(needle.data[i]))] = needle.len - 1 - i;
    }

    u32 pos = fromOffset;
    while (pos + needle.len <= haystack.len) {
        u32 j = needle.len;
        while (j > 0 && Fold(haystack.data[pos + j - 1]) == Fold(needle.data[j - 1])) --j;
        if (j == 0) return pos;
        pos += shift[static_cast<unsigned char>(Fold(haystack.data[pos + needle.len - 1]))];
    }
    return kInvalidIndex;
}

u32 SearchDocument(const Document& doc, StrSlice needle, Arena* scratch, Vec<Match>* out) {
    if (!out || !scratch || !needle.data || needle.len == 0) return 0;

    u32 found = 0;
    for (u32 blockIdx = 0; blockIdx < doc.blocks.Size(); ++blockIdx) {
        const Block& b = doc.blocks[blockIdx];
        if (b.inlineCount == 0) continue;

        // 每块用完即 Reset:整个查找过程只有"当前这一块"的拼接文本活着,
        // 不留任何常驻索引缓冲(裁决 #7 的"零常驻内存增量")。
        scratch->Reset();
        StrSlice text = BuildBlockText(doc, b, scratch);

        u32 at = 0;
        while (text.len > 0) {
            at = FindSubstringNoCase(text, needle, at);
            if (at == kInvalidIndex) break;
            if (!out->Push(Match{blockIdx, at, needle.len})) return found;
            found++;
            at += 1;  // 允许重叠命中,与常见查看器的"下一处"行为一致
            if (at + needle.len > text.len) break;
        }

        // 链接 URL / 图片 src 也参与查找(裁决 #7)。它们不出现在正文上,所以
        // 记成"不可高亮"的命中(byteOffset = kInvalidIndex),仍可跳转到该块。
        for (u32 i = 0; i < b.inlineCount; ++i) {
            const Inline& in = doc.inlines[b.firstInlineIdx + i];
            if ((in.flags & (kInlineFlagLink | kInlineFlagImage | kInlineFlagAutolink)) == 0) continue;
            if (in.linkTargetIdx == kInvalidIndex || in.linkTargetIdx >= doc.linkTargets.Size()) continue;
            const LinkTarget& target = doc.linkTargets[in.linkTargetIdx];
            if (FindSubstringNoCase(target.href, needle, 0) == kInvalidIndex) continue;
            if (!out->Push(Match{blockIdx, kInvalidIndex, 0})) return found;
            found++;
        }
    }
    scratch->Reset();
    return found;
}

bool MatchToTextRange(const Document& doc, const Match& m, u32* outPosition, u32* outLength) {
    if (!outPosition || !outLength) return false;
    if (m.byteOffset == kInvalidIndex || m.byteLen == 0) return false;
    if (m.blockIdx >= doc.blocks.Size()) return false;

    const Block& b = doc.blocks[m.blockIdx];
    u32 matchEnd = m.byteOffset + m.byteLen;
    u32 byteCursor = 0;
    u32 utf16Cursor = 0;
    u32 startUnit = kInvalidIndex;
    u32 endUnit = 0;

    for (u32 i = 0; i < b.inlineCount; ++i) {
        const Inline& in = doc.inlines[b.firstInlineIdx + i];
        u32 runBegin = byteCursor;
        u32 runEnd = byteCursor + in.textLen;

        if (in.textLen > 0 && m.byteOffset < runEnd && matchEnd > runBegin) {
            // 命中触及不进入正文排版的 run(图片 alt / 脚注引用)——没有可高亮的文字。
            if (!RunEntersLayout(in)) return false;

            const char* runText = InlineTextBytes(in, doc);
            if (startUnit == kInvalidIndex) {
                u32 within = m.byteOffset > runBegin ? m.byteOffset - runBegin : 0;
                startUnit = utf16Cursor + Utf16LengthOfUtf8(StrSlice{runText, within});
            }
            u32 withinEnd = matchEnd < runEnd ? matchEnd - runBegin : in.textLen;
            endUnit = utf16Cursor + Utf16LengthOfUtf8(StrSlice{runText, withinEnd});
        }

        byteCursor = runEnd;
        utf16Cursor += RunLayoutUtf16Length(doc, in);
    }

    if (startUnit == kInvalidIndex || endUnit <= startUnit) return false;
    *outPosition = startUnit;
    *outLength = endUnit - startUnit;
    return true;
}

}  // namespace mdvn
