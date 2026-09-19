// Bug 1 覆盖测试:围栏代码块/软硬换行的换行符不应被吞掉。
//
// 背景:md4c 对软/硬换行(MD_TEXT_SOFTBR/MD_TEXT_BR)以及围栏/缩进代码块每行
// 结尾补的换行(type 固定是 MD_TEXT_CODE)统一传入库内部字面量 "\n" 指针,
// 不在 Document::source 范围内。修复前 parser.cpp::OnText 把这类 Inline 的
// textOffset/textLen 都归零,导致代码块里的换行结构在渲染时完全丢失,
// 多行代码被拼接成一整行。
//
// 这里在不依赖真实 IDWriteTextLayout 的前提下,直接在文档模型层面验证:
// 每个源码行之间都应该有且仅有一个带 kInlineFlagSyntheticNewline 标记、
// textLen == 1 的 Inline run,且 InlineTextBytes() 对它返回的字节确实是 '\n'。
#include "markair_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"

using markair::Arena;
using markair::Block;
using markair::BlockType;
using markair::Document;
using markair::Inline;
using markair::InlineTextBytes;
using markair::kInlineFlagSyntheticNewline;
using markair::kInlineFlagSyntheticSpaces;
using markair::ParseMarkdown;
using markair::StrSlice;
using markair::u32;

#define MARKAIR_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

namespace {

u32 FindBlockOfType(const Document& doc, BlockType type, u32 occurrence) {
    u32 seen = 0;
    for (u32 i = 0; i < doc.blocks.Size(); ++i) {
        if (doc.blocks[i].type == type) {
            if (seen == occurrence) return i;
            ++seen;
        }
    }
    return markair::kInvalidIndex;
}

// 数一数某个块的直属 inline 里,带 kInlineFlagSyntheticNewline 标记的个数。
u32 CountSyntheticNewlines(const Document& doc, const Block& b) {
    u32 count = 0;
    for (u32 i = 0; i < b.inlineCount; ++i) {
        if (doc.inlines[b.firstInlineIdx + i].flags & kInlineFlagSyntheticNewline) ++count;
    }
    return count;
}

// 把一个块的全部直属 inline 按顺序拼接成一段可读文本(经 InlineTextBytes,
// 不直接读 source),用于断言"缩进+换行结构是否原样保留"。
void AppendBlockText(const Document& doc, const Block& b, char* out, u32 cap, u32* outLen) {
    u32 pos = 0;
    for (u32 i = 0; i < b.inlineCount && pos < cap; ++i) {
        const Inline& in = doc.inlines[b.firstInlineIdx + i];
        const char* bytes = InlineTextBytes(in, doc);
        u32 n = in.textLen;
        if (pos + n > cap) n = cap - pos;
        for (u32 k = 0; k < n; ++k) out[pos + k] = bytes[k];
        pos += n;
    }
    *outLen = pos;
}

}  // namespace

// 用例 1:三行代码的围栏代码块应该产生恰好 3 个合成换行 Inline——md4c 的
// md_process_verbatim_block_contents 对每一行(含最后一行)都会补一个终止
// 换行,所以合成换行数直接等于源码行数,而不是"行数 - 1"。
// 每个合成换行 textLen == 1、InlineTextBytes 返回 '\n'。
MARKAIR_TEST(SyntheticNewline_FencedCodeBlockPreservesLineBreaks) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] =
        "```\n"
        "line one\n"
        "line two\n"
        "line three\n"
        "```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MARKAIR_CHECK(codeIdx != markair::kInvalidIndex);
    const Block& code = doc.blocks[codeIdx];

    MARKAIR_CHECK_EQ(CountSyntheticNewlines(doc, code), 3u);

    for (u32 i = 0; i < code.inlineCount; ++i) {
        const Inline& in = doc.inlines[code.firstInlineIdx + i];
        if (in.flags & kInlineFlagSyntheticNewline) {
            MARKAIR_CHECK_EQ(in.textLen, 1u);
            const char* bytes = InlineTextBytes(in, doc);
            MARKAIR_CHECK(bytes != nullptr);
            if (bytes) MARKAIR_CHECK(bytes[0] == '\n');
        }
    }
}

// 用例 2:五行代码应产生 5 个合成换行——换行数与源码行数一一对应。
MARKAIR_TEST(SyntheticNewline_FencedCodeBlockLineCountMatchesSource) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] =
        "```text\n"
        "a\n"
        "bb\n"
        "ccc\n"
        "dddd\n"
        "eeeee\n"
        "```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MARKAIR_CHECK(codeIdx != markair::kInvalidIndex);
    const Block& code = doc.blocks[codeIdx];

    MARKAIR_CHECK_EQ(CountSyntheticNewlines(doc, code), 5u);
}

// 用例 3:硬换行(行尾两个空格)在段落里同样应该合成为一个换行 Inline,
// 而不是被彻底吞掉——这不是代码块专属场景。
MARKAIR_TEST(SyntheticNewline_HardBreakInParagraphIsPreserved) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "first line  \nsecond line\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 pIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    MARKAIR_CHECK(pIdx != markair::kInvalidIndex);
    const Block& p = doc.blocks[pIdx];

    MARKAIR_CHECK_EQ(CountSyntheticNewlines(doc, p), 1u);
}

// 用例 4:单行代码块应恰好产生 1 个合成换行(该行结尾的终止换行)。
MARKAIR_TEST(SyntheticNewline_SingleLineCodeBlockHasExactlyOne) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "```\nonly one line\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MARKAIR_CHECK(codeIdx != markair::kInvalidIndex);
    const Block& code = doc.blocks[codeIdx];

    MARKAIR_CHECK_EQ(CountSyntheticNewlines(doc, code), 1u);
}

// Bug 3 覆盖测试(2026-09-17,用户实测发现):围栏代码块每行的前导缩进
// 不应被吞掉。md4c 的 md_process_verbatim_block_contents 用它自己的静态
// 字面量缓冲区(16 个空格一组)拼每行缩进,同样不指向 source——此前这类
// run 落进"不在 source 里就归零"的兜底分支,导致缩进整体丢失,多层缩进
// (如 Python 的嵌套 for 循环)全部拉平。这里重建整个代码块的文本,断言
// 缩进空格数与源码一致。
MARKAIR_TEST(SyntheticSpaces_FencedCodeBlockPreservesIndentation) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] =
        "```\n"
        "def f():\n"
        "    a = 1\n"
        "        b = 2\n"
        "    return a\n"
        "```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MARKAIR_CHECK(codeIdx != markair::kInvalidIndex);
    const Block& code = doc.blocks[codeIdx];

    // 至少应该出现带 kInlineFlagSyntheticSpaces 标记的 run(4 空格与 8 空格
    // 各出现过一次),证明缩进没有被当成"不在 source 里"直接吞掉归零。
    bool sawSyntheticSpaces = false;
    for (u32 i = 0; i < code.inlineCount; ++i) {
        if (doc.inlines[code.firstInlineIdx + i].flags & kInlineFlagSyntheticSpaces) {
            sawSyntheticSpaces = true;
            break;
        }
    }
    MARKAIR_CHECK(sawSyntheticSpaces);

    char buf[256] = {};
    u32 len = 0;
    AppendBlockText(doc, code, buf, sizeof(buf), &len);
    const char expected[] = "def f():\n    a = 1\n        b = 2\n    return a\n";
    MARKAIR_CHECK_EQ(len, static_cast<u32>(sizeof(expected) - 1));
    bool matches = len == sizeof(expected) - 1;
    if (matches) {
        for (u32 i = 0; i < len; ++i) {
            if (buf[i] != expected[i]) { matches = false; break; }
        }
    }
    MARKAIR_CHECK(matches);
}
