// T8 覆盖测试:md4c 回调 -> 文档模型(Block/Inline 紧凑数组)的结构化摘要比对,
// 以及节点数上限/嵌套深度上限的安全截断验证。
#include "mdvn_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"

#include <cstring>
#include <cstdio>

using mdvn::Arena;
using mdvn::Block;
using mdvn::BlockType;
using mdvn::Document;
using mdvn::Inline;
using mdvn::kInlineFlagBold;
using mdvn::kInlineFlagCode;
using mdvn::kInlineFlagItalic;
using mdvn::kMaxNestingDepth;
using mdvn::ParseMarkdown;
using mdvn::StrSlice;
using mdvn::u32;

#define MDVN_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

namespace {

// 简单的定长字符串构建器,替代 std::string(见 coding-rules.md "慎用 std::string")。
struct SummaryBuf {
    char data[4096];
    size_t len;

    SummaryBuf() : len(0) { data[0] = '\0'; }

    void Append(const char* s) {
        size_t n = strlen(s);
        if (len + n >= sizeof(data)) n = sizeof(data) - 1 - len;
        memcpy(data + len, s, n);
        len += n;
        data[len] = '\0';
    }
};

// 行内 flags -> 简写字符串,如 "bic"(粗体+斜体+代码同时生效),无样式则是 "-"。
void AppendInlineFlags(SummaryBuf& buf, u32 flags) {
    char tmp[8];
    int i = 0;
    if (flags & kInlineFlagBold) tmp[i++] = 'b';
    if (flags & kInlineFlagItalic) tmp[i++] = 'i';
    if (flags & kInlineFlagCode) tmp[i++] = 'c';
    if (i == 0) tmp[i++] = '-';
    tmp[i] = '\0';
    buf.Append(tmp);
}

// 块类型 -> 简写字符串。
const char* BlockTypeCode(BlockType t) {
    switch (t) {
        case BlockType::Document: return "DOC";
        case BlockType::Heading: return "H";
        case BlockType::Paragraph: return "P";
        case BlockType::BulletList: return "UL";
        case BlockType::OrderedList: return "OL";
        case BlockType::ListItem: return "LI";
        case BlockType::BlockQuote: return "BQ";
        case BlockType::ThematicBreak: return "HR";
        case BlockType::CodeBlock: return "CODE";
    }
    return "?";
}

// 递归输出一个块及其直属子节点(跳过根节点 Document 自身的 token,只看内容)。
// 直属子节点的遍历方式见 model.h 里 Block 的子树语义注释。
void AppendBlock(SummaryBuf& buf, const Document& doc, u32 idx, bool isRoot) {
    const Block& b = doc.blocks[idx];

    if (!isRoot) {
        if (buf.len > 0) buf.Append(" ");
        buf.Append(BlockTypeCode(b.type));
        if (b.type == BlockType::Heading) {
            char lvl[4];
            snprintf(lvl, sizeof(lvl), "%u", static_cast<unsigned>(b.level));
            buf.Append(lvl);
        }
        if (b.inlineCount > 0) {
            buf.Append("(");
            for (u32 i = 0; i < b.inlineCount; ++i) {
                if (i > 0) buf.Append(",");
                AppendInlineFlags(buf, doc.inlines[b.firstInlineIdx + i].flags);
            }
            buf.Append(")");
        }
    }

    u32 child = b.firstChildIdx;
    u32 end = b.firstChildIdx + b.childCount;
    while (child < end) {
        AppendBlock(buf, doc, child, false);
        child = child + 1 + doc.blocks[child].childCount;
    }
}

// 把整份文档序列化成一行"块类型序列 + 行内 flag 序列"摘要,用于快照比对。
const char* Summarize(SummaryBuf& buf, const Document& doc) {
    if (doc.blocks.Size() == 0) return buf.data;
    AppendBlock(buf, doc, 0, true);
    return buf.data;
}

} // namespace

// 样本 1:标题。
MDVN_TEST(Model_Heading) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "# Hello\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MDVN_CHECK_STREQ(actual, "H1(-)");
}

// 样本 2:段落。
MDVN_TEST(Model_Paragraph) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "Just a paragraph.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MDVN_CHECK_STREQ(actual, "P(-)");
}

// 样本 3:粗体 + 斜体(嵌套/并列的行内样式 run)。
MDVN_TEST(Model_BoldItalic) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "A **bold** B *italic* C.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MDVN_CHECK_STREQ(actual, "P(-,b,-,i,-)");
}

// 样本 4:行内代码。
MDVN_TEST(Model_InlineCode) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "Use `code` here.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MDVN_CHECK_STREQ(actual, "P(-,c,-)");
}

// 样本 5:有序列表。
MDVN_TEST(Model_OrderedList) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "1. First\n2. Second\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MDVN_CHECK_STREQ(actual, "OL LI(-) LI(-)");
}

// 样本 6:无序列表。
MDVN_TEST(Model_BulletList) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "- Apple\n- Banana\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MDVN_CHECK_STREQ(actual, "UL LI(-) LI(-)");
}

// 样本 7:引用块。
MDVN_TEST(Model_BlockQuote) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "> Quoted text\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MDVN_CHECK_STREQ(actual, "BQ P(-)");
}

// 样本 8:分割线。
MDVN_TEST(Model_ThematicBreak) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "---\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MDVN_CHECK_STREQ(actual, "HR");
}

// 样本 9:围栏代码块(无高亮,M0 范围内的最后一类元素)。
MDVN_TEST(Model_FencedCodeBlock) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "```\ncode line\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MDVN_CHECK_STREQ(actual, "CODE(c,c)");
}

// 安全边界 1:嵌套深度超限(超深引用块嵌套)应被安全截断,不崩溃、不栈溢出。
MDVN_TEST(Model_NestingDepthLimit_Truncates) {
    // 构造 kMaxNestingDepth + 50 层嵌套引用块:"> > > ... x\n"。
    char src[512];
    u32 len = 0;
    const u32 depth = kMaxNestingDepth + 50;
    for (u32 i = 0; i < depth; ++i) {
        src[len++] = '>';
        src[len++] = ' ';
    }
    src[len++] = 'x';
    src[len++] = '\n';

    Arena arena;
    MDVN_CHECK(arena.Init(1 * 1024 * 1024));
    Document doc = ParseMarkdown(StrSlice{src, len}, &arena);
    // 核心验证:进程能跑到这里说明没有栈溢出/无限递归/崩溃;且截断标记必须置位。
    MDVN_CHECK(doc.truncated);
}

// 安全边界 2:节点总数超限(海量小段落)应被安全截断,不无界占用内存。
MDVN_TEST(Model_NodeCountLimit_Truncates) {
    // 用一个独立 Arena 构造超大的 Markdown 源文本:"x\n\n" 重复 15 万次,
    // 每次产生 1 个段落块 + 1 个行内 run,总计约 30 万节点,远超
    // kMaxDocumentNodeCount(20 万)。
    Arena srcArena;
    MDVN_CHECK(srcArena.Init(2 * 1024 * 1024));
    const char pattern[] = "x\n\n";
    const u32 patternLen = 3;
    const u32 iterations = 150000;
    const u32 totalLen = patternLen * iterations;

    char* buf = static_cast<char*>(srcArena.Alloc(totalLen, 1));
    MDVN_CHECK(buf != nullptr);
    if (!buf) return;
    for (u32 i = 0; i < iterations; ++i) {
        memcpy(buf + i * patternLen, pattern, patternLen);
    }

    Arena docArena;
    MDVN_CHECK(docArena.Init(64 * 1024 * 1024));
    Document doc = ParseMarkdown(StrSlice{buf, totalLen}, &docArena);
    MDVN_CHECK(doc.truncated);
}
