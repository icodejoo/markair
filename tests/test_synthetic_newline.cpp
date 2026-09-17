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
#include "mdvn_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"

using mdvn::Arena;
using mdvn::Block;
using mdvn::BlockType;
using mdvn::Document;
using mdvn::Inline;
using mdvn::InlineTextBytes;
using mdvn::kInlineFlagSyntheticNewline;
using mdvn::ParseMarkdown;
using mdvn::StrSlice;
using mdvn::u32;

#define MDVN_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

namespace {

u32 FindBlockOfType(const Document& doc, BlockType type, u32 occurrence) {
    u32 seen = 0;
    for (u32 i = 0; i < doc.blocks.Size(); ++i) {
        if (doc.blocks[i].type == type) {
            if (seen == occurrence) return i;
            ++seen;
        }
    }
    return mdvn::kInvalidIndex;
}

// 数一数某个块的直属 inline 里,带 kInlineFlagSyntheticNewline 标记的个数。
u32 CountSyntheticNewlines(const Document& doc, const Block& b) {
    u32 count = 0;
    for (u32 i = 0; i < b.inlineCount; ++i) {
        if (doc.inlines[b.firstInlineIdx + i].flags & kInlineFlagSyntheticNewline) ++count;
    }
    return count;
}

}  // namespace

// 用例 1:三行代码的围栏代码块应该产生恰好 3 个合成换行 Inline——md4c 的
// md_process_verbatim_block_contents 对每一行(含最后一行)都会补一个终止
// 换行,所以合成换行数直接等于源码行数,而不是"行数 - 1"。
// 每个合成换行 textLen == 1、InlineTextBytes 返回 '\n'。
MDVN_TEST(SyntheticNewline_FencedCodeBlockPreservesLineBreaks) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "```\n"
        "line one\n"
        "line two\n"
        "line three\n"
        "```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MDVN_CHECK(codeIdx != mdvn::kInvalidIndex);
    const Block& code = doc.blocks[codeIdx];

    MDVN_CHECK_EQ(CountSyntheticNewlines(doc, code), 3u);

    for (u32 i = 0; i < code.inlineCount; ++i) {
        const Inline& in = doc.inlines[code.firstInlineIdx + i];
        if (in.flags & kInlineFlagSyntheticNewline) {
            MDVN_CHECK_EQ(in.textLen, 1u);
            const char* bytes = InlineTextBytes(in, doc);
            MDVN_CHECK(bytes != nullptr);
            if (bytes) MDVN_CHECK(bytes[0] == '\n');
        }
    }
}

// 用例 2:五行代码应产生 5 个合成换行——换行数与源码行数一一对应。
MDVN_TEST(SyntheticNewline_FencedCodeBlockLineCountMatchesSource) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "```text\n"
        "a\n"
        "bb\n"
        "ccc\n"
        "dddd\n"
        "eeeee\n"
        "```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MDVN_CHECK(codeIdx != mdvn::kInvalidIndex);
    const Block& code = doc.blocks[codeIdx];

    MDVN_CHECK_EQ(CountSyntheticNewlines(doc, code), 5u);
}

// 用例 3:硬换行(行尾两个空格)在段落里同样应该合成为一个换行 Inline,
// 而不是被彻底吞掉——这不是代码块专属场景。
MDVN_TEST(SyntheticNewline_HardBreakInParagraphIsPreserved) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "first line  \nsecond line\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 pIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    MDVN_CHECK(pIdx != mdvn::kInvalidIndex);
    const Block& p = doc.blocks[pIdx];

    MDVN_CHECK_EQ(CountSyntheticNewlines(doc, p), 1u);
}

// 用例 4:单行代码块应恰好产生 1 个合成换行(该行结尾的终止换行)。
MDVN_TEST(SyntheticNewline_SingleLineCodeBlockHasExactlyOne) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "```\nonly one line\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MDVN_CHECK(codeIdx != mdvn::kInvalidIndex);
    const Block& code = doc.blocks[codeIdx];

    MDVN_CHECK_EQ(CountSyntheticNewlines(doc, code), 1u);
}
