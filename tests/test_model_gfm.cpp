// T21 覆盖测试:GFM 扩展(表格/任务列表/删除线/链接/图片/自动链接/脚注)
// 落到文档模型后的结构化摘要比对,覆盖块类型序列、单元格对齐、任务勾选态、
// 脚注 id 配对。风格参考 test_model.cpp 的 SummaryBuf/AppendBlock 手法,
// 复制一份独立的辅助函数(允许少量重复,不抽公共头,见任务描述)。
#include "markair_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/hl/languages.h"

#include <cstring>
#include <cstdio>

using markair::Arena;
using markair::Block;
using markair::BlockType;
using markair::CellAlign;
using markair::Document;
using markair::Inline;
using markair::kInlineFlagAutolink;
using markair::kInlineFlagBold;
using markair::kInlineFlagCode;
using markair::kInlineFlagFootnoteRef;
using markair::kInlineFlagImage;
using markair::kInlineFlagItalic;
using markair::kInlineFlagLink;
using markair::kInlineFlagStrike;
using markair::kInvalidIndex;
using markair::kLanguageCpp;
using markair::kLanguageJs;
using markair::kLanguageNone;
using markair::kLanguagePython;
using markair::LinkTargetKind;
using markair::ParseMarkdown;
using markair::StrSlice;
using markair::u32;

#define MARKAIR_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

namespace {

// 简单的定长字符串构建器,替代 std::string(见 coding-rules.md "慎用 std::string")。
struct SummaryBuf {
    char data[8192];
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

char CellAlignCode(CellAlign a) {
    switch (a) {
        case CellAlign::Left: return 'L';
        case CellAlign::Center: return 'C';
        case CellAlign::Right: return 'R';
        case CellAlign::Default: default: return 'D';
    }
}

// 行内 flags -> 简写字符串(bicslgaf 顺序对应 bold/italic/code/strike/link/image/autolink/footnoteref)。
void AppendInlineFlags(SummaryBuf& buf, u32 flags) {
    char tmp[8];
    int i = 0;
    if (flags & kInlineFlagBold) tmp[i++] = 'b';
    if (flags & kInlineFlagItalic) tmp[i++] = 'i';
    if (flags & kInlineFlagCode) tmp[i++] = 'c';
    if (flags & kInlineFlagStrike) tmp[i++] = 's';
    if (flags & kInlineFlagLink) tmp[i++] = 'l';
    if (flags & kInlineFlagImage) tmp[i++] = 'g';
    if (flags & kInlineFlagAutolink) tmp[i++] = 'a';
    if (flags & kInlineFlagFootnoteRef) tmp[i++] = 'f';
    if (i == 0) tmp[i++] = '-';
    tmp[i] = '\0';
    buf.Append(tmp);
}

// 块类型 -> 简写字符串;表格单元格附带对齐码,列表项附带任务勾选态。
const char* BlockTypeCode(SummaryBuf& tmp, const Document& doc, const Block& b) {
    switch (b.type) {
        case BlockType::Document: return "DOC";
        case BlockType::Heading: return "H";
        case BlockType::Paragraph: return "P";
        case BlockType::BulletList: return "UL";
        case BlockType::OrderedList: return "OL";
        case BlockType::ListItem:
            if (b.detailIdx != kInvalidIndex) {
                const auto& d = doc.listItemDetails[b.detailIdx];
                tmp.Append("LI[");
                tmp.Append(d.taskChecked ? "x" : " ");
                tmp.Append("]");
            } else {
                tmp.Append("LI");
            }
            return tmp.data;
        case BlockType::BlockQuote: return "BQ";
        case BlockType::ThematicBreak: return "HR";
        case BlockType::CodeBlock: return "CODE";
        case BlockType::Table: return "TABLE";
        case BlockType::TableHead: return "THEAD";
        case BlockType::TableBody: return "TBODY";
        case BlockType::TableRow: return "TR";
        case BlockType::TableHeadCell:
        case BlockType::TableCell: {
            tmp.Append(b.type == BlockType::TableHeadCell ? "TH[" : "TD[");
            char code[2] = {'D', '\0'};
            if (b.detailIdx != kInvalidIndex) code[0] = CellAlignCode(doc.cellDetails[b.detailIdx].align);
            tmp.Append(code);
            tmp.Append("]");
            return tmp.data;
        }
        case BlockType::FootnoteDefSection: return "FNSEC";
        case BlockType::FootnoteDef: {
            char idbuf[16] = "FNDEF#";
            if (b.detailIdx != kInvalidIndex) {
                char num[8];
                snprintf(num, sizeof(num), "%u", doc.footnoteDetails[b.detailIdx].id);
                tmp.Append("FNDEF#");
                tmp.Append(num);
            } else {
                tmp.Append("FNDEF#?");
            }
            return tmp.data;
        }
    }
    return "?";
}

// 递归输出一个块及其直属子节点(跳过根节点 Document 自身的 token)。
void AppendBlock(SummaryBuf& buf, const Document& doc, u32 idx, bool isRoot) {
    const Block& b = doc.blocks[idx];

    if (!isRoot) {
        if (buf.len > 0) buf.Append(" ");
        SummaryBuf tmp;
        buf.Append(BlockTypeCode(tmp, doc, b));
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

const char* Summarize(SummaryBuf& buf, const Document& doc) {
    if (doc.blocks.Size() == 0) return buf.data;
    AppendBlock(buf, doc, 0, true);
    return buf.data;
}

// 找到第一个指定类型的块,深度优先前序遍历(与 blocks 数组存储顺序一致,
// 直接线性扫描即可,不需要真的走树)。找不到返回 kInvalidIndex。
u32 FindFirstBlockOfType(const Document& doc, BlockType type) {
    for (u32 i = 0; i < doc.blocks.Size(); ++i) {
        if (doc.blocks[i].type == type) return i;
    }
    return kInvalidIndex;
}

// 找到第一个带有指定 flag 的 Inline run。找不到返回 kInvalidIndex。
u32 FindFirstInlineWithFlag(const Document& doc, u32 flag) {
    for (u32 i = 0; i < doc.inlines.Size(); ++i) {
        if (doc.inlines[i].flags & flag) return i;
    }
    return kInvalidIndex;
}

// 比较 StrSlice 与 C 字符串是否完全相等(长度+内容),测试专用小工具。
bool SliceEquals(StrSlice s, const char* literal) {
    size_t n = strlen(literal);
    if (s.len != n) return false;
    return memcmp(s.data, literal, n) == 0;
}

} // namespace

// 样本 1:表格 + 三种显式对齐 + 默认对齐。
MARKAIR_TEST(ModelGfm_TableWithAlignments) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] =
        "| A | B | C | D |\n"
        "|:--|:-:|--:|---|\n"
        "| 1 | 2 | 3 | 4 |\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MARKAIR_CHECK_STREQ(actual,
        "TABLE THEAD TR TH[L](-) TH[C](-) TH[R](-) TH[D](-) TBODY TR TD[L](-) TD[C](-) TD[R](-) TD[D](-)");

    u32 tableIdx = FindFirstBlockOfType(doc, BlockType::Table);
    MARKAIR_CHECK(tableIdx != kInvalidIndex);
    if (tableIdx != kInvalidIndex) {
        const auto& td = doc.tableDetails[doc.blocks[tableIdx].detailIdx];
        MARKAIR_CHECK_EQ(td.colCount, 4u);
        MARKAIR_CHECK_EQ(td.headRowCount, 1u);
        MARKAIR_CHECK_EQ(td.bodyRowCount, 1u);
    }
}

// 样本 2:任务列表,勾选态各不相同。
MARKAIR_TEST(ModelGfm_TaskListChecked) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "- [x] Done\n- [ ] Todo\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MARKAIR_CHECK_STREQ(actual, "UL LI[x](-) LI[ ](-)");
}

// 样本 3:普通(非任务)列表项不应分配 ListItemDetail(detailIdx 恒为 kInvalidIndex)。
MARKAIR_TEST(ModelGfm_RegularListItemHasNoDetail) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "- Apple\n- Banana\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    u32 liIdx = FindFirstBlockOfType(doc, BlockType::ListItem);
    MARKAIR_CHECK(liIdx != kInvalidIndex);
    if (liIdx != kInvalidIndex) MARKAIR_CHECK_EQ(doc.blocks[liIdx].detailIdx, kInvalidIndex);
}

// 样本 4:删除线。
MARKAIR_TEST(ModelGfm_Strikethrough) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "~~gone~~\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MARKAIR_CHECK_STREQ(actual, "P(s)");
}

// 样本 5:普通链接(带 title),校验 linkTargets 内容与种类。
MARKAIR_TEST(ModelGfm_LinkWithTitle) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "[text](http://example.com \"Title\")\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MARKAIR_CHECK_STREQ(actual, "P(l)");

    u32 inlineIdx = FindFirstInlineWithFlag(doc, kInlineFlagLink);
    MARKAIR_CHECK(inlineIdx != kInvalidIndex);
    if (inlineIdx != kInvalidIndex) {
        u32 targetIdx = doc.inlines[inlineIdx].linkTargetIdx;
        MARKAIR_CHECK(targetIdx != kInvalidIndex);
        if (targetIdx != kInvalidIndex) {
            const auto& lt = doc.linkTargets[targetIdx];
            MARKAIR_CHECK_EQ(lt.href.len, 18u); // "http://example.com"
            MARKAIR_CHECK(memcmp(lt.href.data, "http://example.com", 18) == 0);
            MARKAIR_CHECK_EQ(lt.title.len, 5u);
            MARKAIR_CHECK(memcmp(lt.title.data, "Title", 5) == 0);
            MARKAIR_CHECK(lt.kind == LinkTargetKind::External);
        }
    }
}

// 样本 6:相对路径图片,无 title。
MARKAIR_TEST(ModelGfm_ImageRelativePath) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "![alt](./img.png)\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MARKAIR_CHECK_STREQ(actual, "P(g)");

    u32 inlineIdx = FindFirstInlineWithFlag(doc, kInlineFlagImage);
    MARKAIR_CHECK(inlineIdx != kInvalidIndex);
    if (inlineIdx != kInvalidIndex) {
        u32 targetIdx = doc.inlines[inlineIdx].linkTargetIdx;
        MARKAIR_CHECK(targetIdx != kInvalidIndex);
        if (targetIdx != kInvalidIndex) {
            const auto& lt = doc.linkTargets[targetIdx];
            MARKAIR_CHECK_EQ(lt.title.len, 0u);
            MARKAIR_CHECK(lt.kind == LinkTargetKind::RelativePath);
        }
    }
}

// 样本 7:宽松自动链接(裸 URL 直接识别为 <a>,is_autolink 置位)。
MARKAIR_TEST(ModelGfm_PermissiveAutolink) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "Visit https://example.com now.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 inlineIdx = FindFirstInlineWithFlag(doc, kInlineFlagAutolink);
    MARKAIR_CHECK(inlineIdx != kInvalidIndex);
    if (inlineIdx != kInvalidIndex) {
        MARKAIR_CHECK(doc.inlines[inlineIdx].flags & kInlineFlagLink);
        u32 targetIdx = doc.inlines[inlineIdx].linkTargetIdx;
        MARKAIR_CHECK(targetIdx != kInvalidIndex);
        if (targetIdx != kInvalidIndex) {
            MARKAIR_CHECK(doc.linkTargets[targetIdx].kind == LinkTargetKind::External);
        }
    }
}

// 样本 8:脚注引用与定义的 id 配对。
MARKAIR_TEST(ModelGfm_FootnoteRefDefPairing) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "Text[^1]\n\n[^1]: Note.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 refIdx = FindFirstInlineWithFlag(doc, kInlineFlagFootnoteRef);
    u32 defIdx = FindFirstBlockOfType(doc, BlockType::FootnoteDef);
    MARKAIR_CHECK(refIdx != kInvalidIndex);
    MARKAIR_CHECK(defIdx != kInvalidIndex);
    if (refIdx != kInvalidIndex && defIdx != kInvalidIndex) {
        u32 refFootnoteId = doc.inlines[refIdx].linkTargetIdx; // 直接是脚注 id,见 model.h 注释
        u32 defDetailIdx = doc.blocks[defIdx].detailIdx;
        MARKAIR_CHECK(defDetailIdx != kInvalidIndex);
        if (defDetailIdx != kInvalidIndex) {
            MARKAIR_CHECK_EQ(refFootnoteId, doc.footnoteDetails[defDetailIdx].id);
            MARKAIR_CHECK_EQ(doc.footnoteDetails[defDetailIdx].refCount, 1u);
        }
    }

    u32 sectionIdx = FindFirstBlockOfType(doc, BlockType::FootnoteDefSection);
    MARKAIR_CHECK(sectionIdx != kInvalidIndex);
}

// 样本 9:链接内嵌图片(alt 文本同时携带 link + image 标记,目标取最内层的图片)。
MARKAIR_TEST(ModelGfm_LinkContainingImage) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "[![alt](img.png)](http://example.com)\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    // alt 文本应同时具有 link 与 image 标记。
    for (u32 i = 0; i < doc.inlines.Size(); ++i) {
        const Inline& in = doc.inlines[i];
        if ((in.flags & kInlineFlagImage) != 0) {
            MARKAIR_CHECK((in.flags & kInlineFlagLink) != 0);
            u32 targetIdx = in.linkTargetIdx;
            MARKAIR_CHECK(targetIdx != kInvalidIndex);
            if (targetIdx != kInvalidIndex) {
                // 最内层是图片,href 应为 "img.png",而不是外层链接的 http://example.com。
                MARKAIR_CHECK_EQ(doc.linkTargets[targetIdx].href.len, 7u);
                MARKAIR_CHECK(memcmp(doc.linkTargets[targetIdx].href.data, "img.png", 7) == 0);
            }
        }
    }
    MARKAIR_CHECK_EQ(doc.linkTargets.Size(), 2u); // 外层链接 + 内层图片各一条
}

// 样本 11:缩进式代码块(4 空格起始,与围栏代码块走同一 BlockType,快照口径应一致)。
MARKAIR_TEST(ModelGfm_IndentedCodeBlock) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "    code line\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MARKAIR_CHECK_STREQ(actual, "CODE(c,c)");
}

// 样本 12:硬换行(行尾两个空格)。md4c 的 MD_TEXT_BR 落地为一个空文本 Inline
// (textOffset/textLen 归零,见 parser.cpp OnText 注释),不新增专属 flag,
// 但应作为段落内独立的一段出现,前后文本正常保留。
MARKAIR_TEST(ModelGfm_HardLineBreak) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "line1  \nline2\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MARKAIR_CHECK_STREQ(actual, "P(-,-,-)");
}

// 样本 10:链接 title 含 HTML 实体,展开后应解码。
MARKAIR_TEST(ModelGfm_LinkTitleWithEntity) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "[text](http://example.com \"a &amp; b\")\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 inlineIdx = FindFirstInlineWithFlag(doc, kInlineFlagLink);
    MARKAIR_CHECK(inlineIdx != kInvalidIndex);
    if (inlineIdx != kInvalidIndex) {
        u32 targetIdx = doc.inlines[inlineIdx].linkTargetIdx;
        MARKAIR_CHECK(targetIdx != kInvalidIndex);
        if (targetIdx != kInvalidIndex) {
            const auto& lt = doc.linkTargets[targetIdx];
            MARKAIR_CHECK_EQ(lt.title.len, 5u); // "a & b"
            if (lt.title.len == 5) MARKAIR_CHECK(memcmp(lt.title.data, "a & b", 5) == 0);
        }
    }
}

// 样本 13:无序列表嵌套无序列表(三层)。Summarize 按前序遍历展平输出,
// 括号只包裹每个块自身的行内 flags,不体现父子嵌套层级;tight 列表项的
// 文本直接作为 LI 自身的 inline(呈现为 "LI(-)"),不再套一层 P。
MARKAIR_TEST(ModelGfm_NestedBulletList) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] =
        "- a\n"
        "  - b\n"
        "    - c\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MARKAIR_CHECK_STREQ(actual,
        "UL LI(-) UL LI(-) UL LI(-)");
}

// 样本 14:有序列表嵌套无序列表,验证混合列表类型的块序列。
MARKAIR_TEST(ModelGfm_OrderedListNestedBulletList) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] =
        "1. a\n"
        "   - b\n"
        "   - c\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MARKAIR_CHECK_STREQ(actual,
        "OL LI(-) UL LI(-) LI(-)");
}

// 样本 15:列表项内段落 + 围栏代码块 + 嵌套列表并存(带空行,列表判定为
// loose,LI 自身不带 inline,段落作为独立 P 子块出现),验证同一 LI 下
// 多个子块(P/CODE/嵌套 UL)按序共存。
MARKAIR_TEST(ModelGfm_ListItemWithParagraphCodeAndNestedList) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] =
        "- a\n"
        "\n"
        "  ```\n"
        "  code\n"
        "  ```\n"
        "\n"
        "  - b\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    SummaryBuf buf;
    const char* actual = Summarize(buf, doc);
    MARKAIR_CHECK_STREQ(actual,
        "UL LI P(-) CODE(c,c) UL LI(-)");
}

// 样本 16(T50):围栏代码块 ```cpp,验证 lang 原文与归一化 languageId。
MARKAIR_TEST(ModelGfm_CodeBlockFenceLangCpp) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "```cpp\nint x;\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    u32 idx = FindFirstBlockOfType(doc, BlockType::CodeBlock);
    MARKAIR_CHECK(idx != kInvalidIndex);
    if (idx != kInvalidIndex) {
        const Block& b = doc.blocks[idx];
        MARKAIR_CHECK(b.detailIdx != kInvalidIndex);
        const auto& d = doc.codeBlockDetails[b.detailIdx];
        MARKAIR_CHECK(SliceEquals(d.lang, "cpp"));
        MARKAIR_CHECK_EQ(static_cast<int>(d.languageId), static_cast<int>(kLanguageCpp));
    }
}

// 样本 17(T50):空 lang 围栏 ```,与缩进代码块一样应归一化为 kLanguageNone。
MARKAIR_TEST(ModelGfm_CodeBlockFenceLangEmpty) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "```\nplain\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    u32 idx = FindFirstBlockOfType(doc, BlockType::CodeBlock);
    MARKAIR_CHECK(idx != kInvalidIndex);
    if (idx != kInvalidIndex) {
        const Block& b = doc.blocks[idx];
        MARKAIR_CHECK(b.detailIdx != kInvalidIndex);
        const auto& d = doc.codeBlockDetails[b.detailIdx];
        MARKAIR_CHECK_EQ(d.lang.len, 0u);
        MARKAIR_CHECK_EQ(static_cast<int>(d.languageId), static_cast<int>(kLanguageNone));
    }
}

// 样本 18(T50):缩进代码块(无围栏),lang 应为空、languageId 为 kLanguageNone。
MARKAIR_TEST(ModelGfm_CodeBlockIndentedNoLang) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "    indented code\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    u32 idx = FindFirstBlockOfType(doc, BlockType::CodeBlock);
    MARKAIR_CHECK(idx != kInvalidIndex);
    if (idx != kInvalidIndex) {
        const Block& b = doc.blocks[idx];
        MARKAIR_CHECK(b.detailIdx != kInvalidIndex);
        const auto& d = doc.codeBlockDetails[b.detailIdx];
        MARKAIR_CHECK_EQ(d.lang.len, 0u);
        MARKAIR_CHECK_EQ(static_cast<int>(d.languageId), static_cast<int>(kLanguageNone));
    }
}

// 样本 19(T50):```Python 大小写不敏感,应归一化为 kLanguagePython,
// 但 lang 原文保留原始大小写("Python")。
MARKAIR_TEST(ModelGfm_CodeBlockFenceLangPythonCase) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "```Python\nx = 1\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    u32 idx = FindFirstBlockOfType(doc, BlockType::CodeBlock);
    MARKAIR_CHECK(idx != kInvalidIndex);
    if (idx != kInvalidIndex) {
        const Block& b = doc.blocks[idx];
        MARKAIR_CHECK(b.detailIdx != kInvalidIndex);
        const auto& d = doc.codeBlockDetails[b.detailIdx];
        MARKAIR_CHECK(SliceEquals(d.lang, "Python"));
        MARKAIR_CHECK_EQ(static_cast<int>(d.languageId), static_cast<int>(kLanguagePython));
    }
}

// 样本 20(T50):```jsx,jsx 是 JS 的别名,应归一化为 kLanguageJs。
MARKAIR_TEST(ModelGfm_CodeBlockFenceLangJsx) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "```jsx\nconst a = 1;\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    u32 idx = FindFirstBlockOfType(doc, BlockType::CodeBlock);
    MARKAIR_CHECK(idx != kInvalidIndex);
    if (idx != kInvalidIndex) {
        const Block& b = doc.blocks[idx];
        MARKAIR_CHECK(b.detailIdx != kInvalidIndex);
        const auto& d = doc.codeBlockDetails[b.detailIdx];
        MARKAIR_CHECK(SliceEquals(d.lang, "jsx"));
        MARKAIR_CHECK_EQ(static_cast<int>(d.languageId), static_cast<int>(kLanguageJs));
    }
}

// 样本 21(T50):```mermaid 不在 T52 支持的 11 种语言列表里,应归一化为
// kLanguageNone,但 lang 原文仍保留 "mermaid"。
MARKAIR_TEST(ModelGfm_CodeBlockFenceLangMermaidUnsupported) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "```mermaid\ngraph TD;\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    u32 idx = FindFirstBlockOfType(doc, BlockType::CodeBlock);
    MARKAIR_CHECK(idx != kInvalidIndex);
    if (idx != kInvalidIndex) {
        const Block& b = doc.blocks[idx];
        MARKAIR_CHECK(b.detailIdx != kInvalidIndex);
        const auto& d = doc.codeBlockDetails[b.detailIdx];
        MARKAIR_CHECK(SliceEquals(d.lang, "mermaid"));
        MARKAIR_CHECK_EQ(static_cast<int>(d.languageId), static_cast<int>(kLanguageNone));
    }
}

// 样本 22(T50):```js title="a.js" 带附加参数,只取第一个空白前的词("js"),
// languageId 应归一化为 kLanguageJs。
MARKAIR_TEST(ModelGfm_CodeBlockFenceLangWithExtraArgs) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "```js title=\"a.js\"\nconst a = 1;\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    u32 idx = FindFirstBlockOfType(doc, BlockType::CodeBlock);
    MARKAIR_CHECK(idx != kInvalidIndex);
    if (idx != kInvalidIndex) {
        const Block& b = doc.blocks[idx];
        MARKAIR_CHECK(b.detailIdx != kInvalidIndex);
        const auto& d = doc.codeBlockDetails[b.detailIdx];
        MARKAIR_CHECK(SliceEquals(d.lang, "js"));
        MARKAIR_CHECK_EQ(static_cast<int>(d.languageId), static_cast<int>(kLanguageJs));
    }
}
