// T44 覆盖测试:列表符号(无序列表圆点/空心圆/方块三档循环 + 有序列表序号)。
// 覆盖两层:
//   - 模型层(parser.cpp):OrderedListDetail::start/markDelimiter 是否正确
//     消费了 md4c 的 MD_BLOCK_OL_DETAIL(默认起始 1、显式起始 N、'.'/')' 分隔符)。
//   - 布局层(layout.cpp):BlockGeometry::listMarker* 系列字段——嵌套层级、
//     有序序号递增、任务列表项不重复画符号(与 taskCheckbox 互斥)、
//     listMarkerPad 只影响文字绘制起点、不改变 indent 本身(兼容 M0 基线
//     测试 tests/test_layout.cpp::Layout_NestedListIndent 对 indent 的断言)。
//
// 不复用/不修改 tests/test_layout.cpp、tests/test_model.cpp(M0 基线文件),
// 单独起一个新文件,风格参考 test_layout_codeblock.cpp / test_model_gfm.cpp。
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
using mdvn::kInvalidIndex;
using mdvn::OrderedListDetail;
using mdvn::ParseMarkdown;
using mdvn::StrSlice;
using mdvn::u32;

#define MDVN_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

namespace {

// 按出现顺序找第 occurrence 个(0-based)指定类型的块,找不到返回 kInvalidIndex。
u32 FindBlockOfType(const Document& doc, BlockType type, u32 occurrence) {
    u32 seen = 0;
    for (u32 i = 0; i < doc.blocks.Size(); ++i) {
        if (doc.blocks[i].type == type) {
            if (seen == occurrence) return i;
            ++seen;
        }
    }
    return kInvalidIndex;
}

}  // namespace

// ---------- 模型层 ----------

// 用例 1:普通无序列表(- a\n- b)不应该产生任何 OrderedListDetail 记录。
MDVN_TEST(Model_BulletListHasNoOrderedDetail) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "- a\n- b\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    MDVN_CHECK_EQ(doc.orderedListDetails.Size(), static_cast<u32>(0));

    u32 ul = FindBlockOfType(doc, BlockType::BulletList, 0);
    MDVN_CHECK(ul != kInvalidIndex);
    MDVN_CHECK_EQ(doc.blocks[ul].detailIdx, kInvalidIndex);
}

// 用例 2:默认从 1 开始的有序列表,start == 1、分隔符 '.'。
MDVN_TEST(Model_OrderedListDefaultStart) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "1. a\n2. b\n3. c\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 ol = FindBlockOfType(doc, BlockType::OrderedList, 0);
    MDVN_CHECK(ol != kInvalidIndex);
    MDVN_CHECK(doc.blocks[ol].detailIdx != kInvalidIndex);

    const OrderedListDetail& od = doc.orderedListDetails[doc.blocks[ol].detailIdx];
    MDVN_CHECK_EQ(od.start, static_cast<u32>(1));
    MDVN_CHECK_EQ(static_cast<int>(od.markDelimiter), static_cast<int>('.'));
}

// 用例 3(需求里明确要求的场景):"5. foo" 这种显式指定非 1 起始序号的写法,
// start 必须原样保留成 5,不能被凭空归零/归一。
MDVN_TEST(Model_OrderedListExplicitStart) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "5. foo\n6. bar\n7. baz\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 ol = FindBlockOfType(doc, BlockType::OrderedList, 0);
    MDVN_CHECK(ol != kInvalidIndex);
    MDVN_CHECK(doc.blocks[ol].detailIdx != kInvalidIndex);

    const OrderedListDetail& od = doc.orderedListDetails[doc.blocks[ol].detailIdx];
    MDVN_CHECK_EQ(od.start, static_cast<u32>(5));
}

// 用例 4:分隔符为 ')' 的有序列表写法("1) foo")应保留 ')' 而非硬编码成 '.'。
MDVN_TEST(Model_OrderedListParenDelimiter) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "1) foo\n2) bar\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 ol = FindBlockOfType(doc, BlockType::OrderedList, 0);
    MDVN_CHECK(ol != kInvalidIndex);
    MDVN_CHECK(doc.blocks[ol].detailIdx != kInvalidIndex);

    const OrderedListDetail& od = doc.orderedListDetails[doc.blocks[ol].detailIdx];
    MDVN_CHECK_EQ(static_cast<int>(od.markDelimiter), static_cast<int>(')'));
}

// ---------- 布局层 ----------

// 用例 5:三层嵌套无序列表——每层 ListItem 的 listMarkerLevel 依次是 1/2/3,
// 均为无序符号(listMarkerOrdered == false),且都分配了非空的绘制矩形。
MDVN_TEST(Layout_NestedBulletListMarkerLevels) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "- a\n"
        "  - b\n"
        "    - c\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));

    u32 li0 = FindBlockOfType(doc, BlockType::ListItem, 0);
    u32 li1 = FindBlockOfType(doc, BlockType::ListItem, 1);
    u32 li2 = FindBlockOfType(doc, BlockType::ListItem, 2);
    MDVN_CHECK(li0 != kInvalidIndex);
    MDVN_CHECK(li1 != kInvalidIndex);
    MDVN_CHECK(li2 != kInvalidIndex);

    const BlockGeometry& g0 = layout.Geometry(li0);
    const BlockGeometry& g1 = layout.Geometry(li1);
    const BlockGeometry& g2 = layout.Geometry(li2);

    MDVN_CHECK_EQ(static_cast<u32>(g0.listMarkerLevel), static_cast<u32>(1));
    MDVN_CHECK_EQ(static_cast<u32>(g1.listMarkerLevel), static_cast<u32>(2));
    MDVN_CHECK_EQ(static_cast<u32>(g2.listMarkerLevel), static_cast<u32>(3));

    MDVN_CHECK(!g0.listMarkerOrdered);
    MDVN_CHECK(!g1.listMarkerOrdered);
    MDVN_CHECK(!g2.listMarkerOrdered);

    MDVN_CHECK(g0.listMarker.width > 0.0f);
    MDVN_CHECK(g1.listMarker.width > 0.0f);
    MDVN_CHECK(g2.listMarker.width > 0.0f);

    // indent 本身必须保持 M0 既有的"纯嵌套缩进"语义(24/48/72),符号占用的
    // 空间只体现在 listMarkerPad 里——这是本次任务与 M0 基线测试
    // Layout_NestedListIndent 保持兼容的关键设计点。
    MDVN_CHECK_EQ(g0.indent, 24.0f);
    MDVN_CHECK_EQ(g1.indent, 48.0f);
    MDVN_CHECK_EQ(g2.indent, 72.0f);
    MDVN_CHECK(g0.listMarkerPad > 0.0f);
}

// 用例 6:有序列表序号从显式起始值递增(对应需求里的"5. foo"验收场景),
// 混入一层嵌套无序列表,确认嵌套不打断有序计数、也不串到无序层级上。
MDVN_TEST(Layout_OrderedListOrdinalStartsFromParsedStart) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "5. foo\n"
        "6. bar\n"
        "   - nested\n"
        "7. baz\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));

    u32 li0 = FindBlockOfType(doc, BlockType::ListItem, 0);  // foo
    u32 li1 = FindBlockOfType(doc, BlockType::ListItem, 1);  // bar
    u32 li2 = FindBlockOfType(doc, BlockType::ListItem, 2);  // nested (无序,嵌套一层)
    u32 li3 = FindBlockOfType(doc, BlockType::ListItem, 3);  // baz
    MDVN_CHECK(li0 != kInvalidIndex);
    MDVN_CHECK(li1 != kInvalidIndex);
    MDVN_CHECK(li2 != kInvalidIndex);
    MDVN_CHECK(li3 != kInvalidIndex);

    const BlockGeometry& g0 = layout.Geometry(li0);
    const BlockGeometry& g1 = layout.Geometry(li1);
    const BlockGeometry& g2 = layout.Geometry(li2);
    const BlockGeometry& g3 = layout.Geometry(li3);

    MDVN_CHECK(g0.listMarkerOrdered);
    MDVN_CHECK(g1.listMarkerOrdered);
    MDVN_CHECK(!g2.listMarkerOrdered);  // 嵌套的无序列表项不应该被误标成有序
    MDVN_CHECK(g3.listMarkerOrdered);

    MDVN_CHECK_EQ(g0.listMarkerOrdinal, static_cast<u32>(5));
    MDVN_CHECK_EQ(g1.listMarkerOrdinal, static_cast<u32>(6));
    MDVN_CHECK_EQ(g3.listMarkerOrdinal, static_cast<u32>(7));  // 不因为中间插了一层嵌套列表而跳号

    MDVN_CHECK_EQ(static_cast<u32>(g2.listMarkerLevel), static_cast<u32>(2));  // 嵌套一层
}

// 用例 7(本次任务重点回归):任务列表项只画勾选框,不应该同时分配列表符号
// 几何——taskCheckbox 与 listMarker 互斥。
MDVN_TEST(Layout_TaskListItemDoesNotGetListMarker) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "- [ ] todo\n"
        "- [x] done\n"
        "- plain\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));

    u32 li0 = FindBlockOfType(doc, BlockType::ListItem, 0);  // todo(任务项)
    u32 li1 = FindBlockOfType(doc, BlockType::ListItem, 1);  // done(任务项,已勾选)
    u32 li2 = FindBlockOfType(doc, BlockType::ListItem, 2);  // plain(普通项)
    MDVN_CHECK(li0 != kInvalidIndex);
    MDVN_CHECK(li1 != kInvalidIndex);
    MDVN_CHECK(li2 != kInvalidIndex);

    const BlockGeometry& g0 = layout.Geometry(li0);
    const BlockGeometry& g1 = layout.Geometry(li1);
    const BlockGeometry& g2 = layout.Geometry(li2);

    // 任务项:勾选框非空,列表符号必须是"不画"状态(width <= 0、level == 0)。
    MDVN_CHECK(g0.taskCheckbox.width > 0.0f);
    MDVN_CHECK(g0.listMarker.width <= 0.0f);
    MDVN_CHECK_EQ(static_cast<u32>(g0.listMarkerLevel), static_cast<u32>(0));
    MDVN_CHECK(g1.taskCheckbox.width > 0.0f);
    MDVN_CHECK(g1.listMarker.width <= 0.0f);

    // 普通项:反过来,只画列表符号,不分配勾选框。
    MDVN_CHECK(g2.taskCheckbox.width <= 0.0f);
    MDVN_CHECK(g2.listMarker.width > 0.0f);
    MDVN_CHECK(!g2.listMarkerOrdered);
}

// 用例 8:无序列表嵌套有序列表——外层无序符号(level 1),内层有序项 level 2
// 且序号从 1 开始(内层是独立的 OrderedList,start 未显式指定,默认 1)。
MDVN_TEST(Layout_MixedNestingBulletThenOrdered) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "- outer\n"
        "  1. inner-a\n"
        "  2. inner-b\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));

    u32 outer = FindBlockOfType(doc, BlockType::ListItem, 0);
    u32 innerA = FindBlockOfType(doc, BlockType::ListItem, 1);
    u32 innerB = FindBlockOfType(doc, BlockType::ListItem, 2);
    MDVN_CHECK(outer != kInvalidIndex);
    MDVN_CHECK(innerA != kInvalidIndex);
    MDVN_CHECK(innerB != kInvalidIndex);

    const BlockGeometry& gOuter = layout.Geometry(outer);
    const BlockGeometry& gA = layout.Geometry(innerA);
    const BlockGeometry& gB = layout.Geometry(innerB);

    MDVN_CHECK(!gOuter.listMarkerOrdered);
    MDVN_CHECK_EQ(static_cast<u32>(gOuter.listMarkerLevel), static_cast<u32>(1));

    MDVN_CHECK(gA.listMarkerOrdered);
    MDVN_CHECK(gB.listMarkerOrdered);
    MDVN_CHECK_EQ(static_cast<u32>(gA.listMarkerLevel), static_cast<u32>(2));
    MDVN_CHECK_EQ(static_cast<u32>(gB.listMarkerLevel), static_cast<u32>(2));
    MDVN_CHECK_EQ(gA.listMarkerOrdinal, static_cast<u32>(1));
    MDVN_CHECK_EQ(gB.listMarkerOrdinal, static_cast<u32>(2));
}
