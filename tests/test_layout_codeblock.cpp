// Bug 1(代码块高度估算)与 Bug 2(表格单元格垂直居中)的布局层覆盖测试。
//
// 不复用/不修改 tests/test_layout.cpp(M0 基线文件),单独起一个新文件。
#include "mdvn_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/text/font.h"
#include "../src/layout/layout.h"

using mdvn::Arena;
using mdvn::Block;
using mdvn::BlockGeometry;
using mdvn::BlockLayoutEngine;
using mdvn::BlockType;
using mdvn::Document;
using mdvn::FontSubsystem;
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

}  // namespace

// 用例 1(Bug 1 修复 B):多行围栏代码块 + 紧跟的段落,估算高度不应该让二者
// 重叠——修复前"字符总数 / 平均每行字符数"的线性估算会把换行吞掉后的一整行
// 字符数按平均宽度重新拆分,与真实的"每个源码行各自一行"严重不符,预留高度
// 明显偏小,段落会画进代码块区域(g.top < 代码块 g.bottom)。
MDVN_TEST(Layout_CodeBlockHeightDoesNotOverlapNextParagraph) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "```python\n"
        "def add(a, b):\n"
        "    return a + b\n"
        "\n"
        "def sub(a, b):\n"
        "    return a - b\n"
        "\n"
        "result = add(1, 2)\n"
        "print(result)\n"
        "```\n"
        "\n"
        "Paragraph right after the code block.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    u32 pIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    MDVN_CHECK(codeIdx != mdvn::kInvalidIndex);
    MDVN_CHECK(pIdx != mdvn::kInvalidIndex);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));

    const BlockGeometry& codeGeom = layout.Geometry(codeIdx);
    const BlockGeometry& pGeom = layout.Geometry(pIdx);

    // 段落顶部不能早于代码块底部——不重叠是这个修复的核心验收标准。
    MDVN_CHECK(pGeom.top >= codeGeom.bottom);

    // 代码块本身有 8 个源码行(含两个空行),估算高度应至少覆盖 8 行,
    // 不能因为换行被当成普通字符而被严重低估。
    float lineHeight = 18.0f;  // kMonoLineHeightDip,未缩放
    MDVN_CHECK(codeGeom.bottom - codeGeom.top >= lineHeight * 7.0f);
}

// 用例 2:代码块行数估算应该基本随"真实源码行数"线性增长,而不是随
// "总字符数"这种和折行宽度绑死的量线性增长——用两份"总字符数相近但行数
// 差异很大"的代码块对比高度,验证高度差主要来自行数而不是字符数。
MDVN_TEST(Layout_CodeBlockHeightTracksLineCountNotCharCount) {
    MDVN_MAKE_TEST_ARENA();
    // 10 行,每行 1 个字符,总字符数 10。
    const char manyLines[] =
        "```\na\nb\nc\nd\ne\nf\ng\nh\ni\nj\n```\n";
    // 1 行,10 个字符,总字符数同样是 10。
    const char oneLine[] = "```\nabcdefghij\n```\n";

    Arena arenaA, arenaB;
    arenaA.Init(1 * 1024 * 1024);
    arenaB.Init(1 * 1024 * 1024);

    Document docA = ParseMarkdown(StrSlice{manyLines, sizeof(manyLines) - 1}, &arenaA);
    Document docB = ParseMarkdown(StrSlice{oneLine, sizeof(oneLine) - 1}, &arenaB);
    MDVN_CHECK(!docA.truncated);
    MDVN_CHECK(!docB.truncated);

    BlockLayoutEngine layoutA, layoutB;
    MDVN_CHECK(layoutA.Relayout(docA, 760.0f));
    MDVN_CHECK(layoutB.Relayout(docB, 760.0f));

    u32 idxA = FindBlockOfType(docA, BlockType::CodeBlock, 0);
    u32 idxB = FindBlockOfType(docB, BlockType::CodeBlock, 0);
    MDVN_CHECK(idxA != mdvn::kInvalidIndex);
    MDVN_CHECK(idxB != mdvn::kInvalidIndex);

    float heightA = layoutA.Geometry(idxA).bottom - layoutA.Geometry(idxA).top;
    float heightB = layoutB.Geometry(idxB).bottom - layoutB.Geometry(idxB).top;

    // 总字符数相同,但 10 行的代码块高度必须明显大于 1 行的代码块——
    // 若还是按字符总数估算(旧算法),两者会算出几乎相同的高度(都是 10 字符,
    // 折成同样的行数),这里的差异断言正是回归防护。
    MDVN_CHECK(heightA > heightB * 3.0f);
}

// 用例 3(Bug 2):表格单元格文字应在"该行统一行高"内垂直居中,而不是贴顶。
// 构造一张两列表格,一列内容很短(单行),另一列内容很长(需要折成多行),
// 短的那一格所在行的行高会被撑高,短内容格的 contentHeight 应明显小于行高,
// 从而 (rowHeight - contentHeight) > 0,渲染层才需要做居中偏移。
MDVN_TEST(Layout_TableCellContentHeightRecordedForCentering) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "| Short | Long |\n"
        "| --- | --- |\n"
        "| a | This cell has a lot more text in it so it wraps across several lines within its narrow column width |\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 shortCellIdx = mdvn::kInvalidIndex;
    for (u32 i = 0; i < doc.blocks.Size(); ++i) {
        if (doc.blocks[i].type == BlockType::TableCell) {
            shortCellIdx = i;
            break;
        }
    }
    MDVN_CHECK(shortCellIdx != mdvn::kInvalidIndex);

    BlockLayoutEngine layout;
    FontSubsystem fonts;
    MDVN_CHECK(fonts.Init());
    MDVN_CHECK(layout.Relayout(doc, 400.0f));
    // 让虚拟化范围覆盖全文档,强制为可见块创建真实 IDWriteTextLayout,
    // 这样 BlockGeometry::contentHeight 才会被 CreateLayoutForBlock 回填。
    layout.UpdateVisibleRange(0.0f, layout.TotalHeight() + 1000.0f, fonts);

    const BlockGeometry& shortCell = layout.Geometry(shortCellIdx);
    MDVN_CHECK(shortCell.textLayout != nullptr);
    // 真实内容高度应该被记录下来(> 0),且明显小于该行的统一行高——
    // 否则渲染层没有依据可以做垂直居中。
    MDVN_CHECK(shortCell.contentHeight > 0.0f);
    MDVN_CHECK(shortCell.bottom - shortCell.top > shortCell.contentHeight);
}
