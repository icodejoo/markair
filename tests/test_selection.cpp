// T80 鼠标拖选文本 + Ctrl+C 复制:纯逻辑层测试。
//
// 覆盖三部分,全部不依赖真实 Win32/HWND(与 test_history.cpp/test_find.cpp
// 同一口径):
// 1) DocTextPos 先后顺序判定 / SelectionRange 归一化。
// 2) 跨块纯文本提取(SelectionPlainTextUtf8)——单块内部、跨越多个块
//    (含表格单元格/代码块等不同块类型)、选区覆盖整篇文档等边界情况。
// 3) SelectionState 状态机(Begin/Update/End/Clear/HasSelection)。
#include "markair_test.h"
#include "../src/util/arena.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/shell/selection.h"

using markair::Arena;
using markair::Block;
using markair::BlockType;
using markair::Document;
using markair::DocTextPos;
using markair::NormalizeSelection;
using markair::ParseMarkdown;
using markair::PositionLess;
using markair::SelectionEmpty;
using markair::SelectionPlainTextUtf8;
using markair::SelectionRange;
using markair::SelectionState;
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

bool TextEquals(StrSlice s, const char* expected) {
    u32 expectedLen = 0;
    while (expected[expectedLen] != 0) ++expectedLen;
    if (s.len != expectedLen) return false;
    for (u32 i = 0; i < s.len; ++i) {
        if (s.data[i] != expected[i]) return false;
    }
    return true;
}

}  // namespace

// ---- 1. 位置先后顺序 / 归一化 ----

MARKAIR_TEST(Selection_PositionLessComparesBlockThenOffset) {
    MARKAIR_CHECK(PositionLess(DocTextPos{2, 5}, DocTextPos{3, 0}));
    MARKAIR_CHECK(!PositionLess(DocTextPos{3, 0}, DocTextPos{2, 5}));
    MARKAIR_CHECK(PositionLess(DocTextPos{1, 1}, DocTextPos{1, 2}));
    MARKAIR_CHECK(!PositionLess(DocTextPos{1, 2}, DocTextPos{1, 2}));  // 相等不算"早于"
}

MARKAIR_TEST(Selection_NormalizeSwapsReversedDrag) {
    // 从后往前拖(松开位置早于按下位置)也要能排成 start <= end。
    DocTextPos anchor{5, 2};
    DocTextPos focus{1, 0};
    SelectionRange r = NormalizeSelection(anchor, focus);
    MARKAIR_CHECK(r.start.blockIndex == 1 && r.start.charOffset == 0);
    MARKAIR_CHECK(r.end.blockIndex == 5 && r.end.charOffset == 2);
}

MARKAIR_TEST(Selection_EmptyWhenStartEqualsEnd) {
    SelectionRange r = NormalizeSelection(DocTextPos{3, 4}, DocTextPos{3, 4});
    MARKAIR_CHECK(SelectionEmpty(r));
    SelectionRange r2 = NormalizeSelection(DocTextPos{3, 4}, DocTextPos{3, 5});
    MARKAIR_CHECK(!SelectionEmpty(r2));
}

// ---- 2. 跨块纯文本提取 ----

// 同一个块内部:只取选区覆盖的那一段字符,不多不少。
MARKAIR_TEST(Selection_ExtractWithinSingleBlock) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "hello world\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    u32 paraIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    MARKAIR_CHECK(paraIdx != markair::kInvalidIndex);

    Arena scratch;
    scratch.Init(1 * 1024 * 1024);
    // "hello world" -> 取下标 [6, 11) = "world"。
    SelectionRange range{{paraIdx, 6}, {paraIdx, 11}};
    StrSlice text = SelectionPlainTextUtf8(doc, range, &scratch);
    MARKAIR_CHECK(TextEquals(text, "world"));
}

// 跨越三个块(标题 -> 段落 -> 段落):中间块取全部文本,起止块按偏移裁剪。
MARKAIR_TEST(Selection_ExtractAcrossThreeBlocks) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] =
        "# Title Here\n"
        "\n"
        "middle paragraph\n"
        "\n"
        "final paragraph\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 headingIdx = FindBlockOfType(doc, BlockType::Heading, 0);
    u32 middleIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    u32 finalIdx = FindBlockOfType(doc, BlockType::Paragraph, 1);
    MARKAIR_CHECK(headingIdx != markair::kInvalidIndex);
    MARKAIR_CHECK(middleIdx != markair::kInvalidIndex);
    MARKAIR_CHECK(finalIdx != markair::kInvalidIndex);

    Arena scratch;
    scratch.Init(1 * 1024 * 1024);
    // 从标题 "Title Here" 的 "Here"(下标 6)开始,到 "final paragraph" 的
    // "final"(下标 [0,5),含)结束——半开区间的终点不含 charOffset 本身,
    // 因此终点块取到的是 [0, 5) == "final"。
    SelectionRange range{{headingIdx, 6}, {finalIdx, 5}};
    StrSlice text = SelectionPlainTextUtf8(doc, range, &scratch);
    MARKAIR_CHECK(TextEquals(text, "Here\nmiddle paragraph\nfinal"));
}

// 跨块选区中,不同的块类型(代码块/表格单元格)都要能正确取出各自的纯文本。
MARKAIR_TEST(Selection_ExtractAcrossCodeBlockAndTable) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] =
        "```\n"
        "code line\n"
        "```\n"
        "\n"
        "| a | b |\n"
        "| - | - |\n"
        "| x | y |\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    u32 cellIdx = FindBlockOfType(doc, BlockType::TableCell, 0);  // 表体第一个单元格 "x"
    MARKAIR_CHECK(codeIdx != markair::kInvalidIndex);
    MARKAIR_CHECK(cellIdx != markair::kInvalidIndex);
    MARKAIR_CHECK(codeIdx < cellIdx);  // 前序遍历,代码块理应排在表格单元格之前

    Arena scratch;
    scratch.Init(1 * 1024 * 1024);
    // 整个代码块文本("code line\n")到表体第一个单元格文本("x")结束;途中
    // 会经过表头两个单元格("a"/"b"),它们同样是携带直属文本的内容块,
    // 应按文档顺序一并计入(容器块 Table/TableHead/TableRow/TableBody 才是
    // 被跳过的那一类)。
    SelectionRange range{{codeIdx, 0}, {cellIdx, 1}};
    StrSlice text = SelectionPlainTextUtf8(doc, range, &scratch);
    MARKAIR_CHECK(TextEquals(text, "code line\n\na\nb\nx"));
}

// 选区跨越整篇文档:从第一个内容块开头到最后一个内容块末尾,应等价于把
// 每个直属文本块的内容按顺序拼接。
MARKAIR_TEST(Selection_ExtractWholeDocument) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "first\n\nsecond\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 firstIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    u32 secondIdx = FindBlockOfType(doc, BlockType::Paragraph, 1);
    MARKAIR_CHECK(firstIdx != markair::kInvalidIndex);
    MARKAIR_CHECK(secondIdx != markair::kInvalidIndex);

    Arena scratch;
    scratch.Init(1 * 1024 * 1024);
    SelectionRange range{{firstIdx, 0}, {secondIdx, 6}};
    StrSlice text = SelectionPlainTextUtf8(doc, range, &scratch);
    MARKAIR_CHECK(TextEquals(text, "first\nsecond"));
}

// 边界情况:空选区(起止点完全相同)应返回空结果,不崩溃。
MARKAIR_TEST(Selection_ExtractEmptyRangeReturnsEmpty) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] = "just one paragraph\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);
    u32 paraIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);

    Arena scratch;
    scratch.Init(1 * 1024 * 1024);
    SelectionRange range{{paraIdx, 3}, {paraIdx, 3}};
    StrSlice text = SelectionPlainTextUtf8(doc, range, &scratch);
    MARKAIR_CHECK(text.data == nullptr);
    MARKAIR_CHECK_EQ(text.len, 0u);
}

// 边界情况:容器块(如 BlockQuote 自身)夹在选区中间时应被自动跳过,
// 只取其内部真正携带文本的子块。
MARKAIR_TEST(Selection_ContainerBlocksInsideRangeAreSkipped) {
    MARKAIR_MAKE_TEST_ARENA();
    const char src[] =
        "before\n"
        "\n"
        "> quoted line\n"
        "\n"
        "after\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MARKAIR_CHECK(!doc.truncated);

    u32 beforeIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    u32 afterIdx = FindBlockOfType(doc, BlockType::Paragraph, 2);  // quote 内部的段落是 occurrence 1
    MARKAIR_CHECK(beforeIdx != markair::kInvalidIndex);
    MARKAIR_CHECK(afterIdx != markair::kInvalidIndex);

    Arena scratch;
    scratch.Init(1 * 1024 * 1024);
    SelectionRange range{{beforeIdx, 0}, {afterIdx, 5}};
    StrSlice text = SelectionPlainTextUtf8(doc, range, &scratch);
    MARKAIR_CHECK(TextEquals(text, "before\nquoted line\nafter"));
}

// ---- 3. SelectionState 状态机 ----

MARKAIR_TEST(Selection_StateBeginUpdateEndHasSelection) {
    SelectionState sel;
    MARKAIR_CHECK(!sel.HasSelection());

    sel.Begin(DocTextPos{0, 3});
    MARKAIR_CHECK(!sel.HasSelection());  // 刚按下,还没拖动
    MARKAIR_CHECK(sel.IsDragging());

    sel.Update(DocTextPos{2, 1});
    MARKAIR_CHECK(sel.HasSelection());

    sel.End();
    MARKAIR_CHECK(!sel.IsDragging());
    MARKAIR_CHECK(sel.HasSelection());  // 松开后选区保留

    SelectionRange r = sel.Range();
    MARKAIR_CHECK(r.start.blockIndex == 0 && r.start.charOffset == 3);
    MARKAIR_CHECK(r.end.blockIndex == 2 && r.end.charOffset == 1);
}

MARKAIR_TEST(Selection_ClearRemovesSelection) {
    SelectionState sel;
    sel.Begin(DocTextPos{1, 0});
    sel.Update(DocTextPos{1, 5});
    sel.End();
    MARKAIR_CHECK(sel.HasSelection());

    sel.Clear();
    MARKAIR_CHECK(!sel.HasSelection());
    MARKAIR_CHECK(!sel.IsDragging());
}

MARKAIR_TEST(Selection_BeginAgainClearsPreviousSelection) {
    // 模拟"点击文档任意位置清除已有选区":重新 Begin 应覆盖旧选区。
    SelectionState sel;
    sel.Begin(DocTextPos{0, 0});
    sel.Update(DocTextPos{3, 9});
    sel.End();
    MARKAIR_CHECK(sel.HasSelection());

    sel.Begin(DocTextPos{5, 2});  // 单击别处,未拖动
    sel.End();
    MARKAIR_CHECK(!sel.HasSelection());
}

MARKAIR_TEST(Selection_UpdateBeforeBeginIsNoOp) {
    SelectionState sel;
    sel.Update(DocTextPos{9, 9});  // 未 Begin 时调用 Update,应无效果
    MARKAIR_CHECK(!sel.HasSelection());
    MARKAIR_CHECK(!sel.IsDragging());
}
