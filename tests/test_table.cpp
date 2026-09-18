// T25 覆盖测试:表格列宽算法(纯数字函数,不依赖 D2D/DirectWrite)。
// 另含"表格列宽/行高估算改用视觉宽度而非 UTF-8 字节数"这次修复的回归测试:
// Utf8VisualWidth 本身的纯函数断言,以及 CJK/英文混排场景下列宽分配、
// 单行 CJK 行高估算不再被字节数口径带偏的集成测试。
#include "mdvn_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/layout/table.h"
#include "../src/layout/layout.h"

using mdvn::Arena;
using mdvn::Block;
using mdvn::BlockGeometry;
using mdvn::BlockLayoutEngine;
using mdvn::BlockType;
using mdvn::ComputeTableColumnWidths;
using mdvn::Document;
using mdvn::ParseMarkdown;
using mdvn::Span;
using mdvn::StrSlice;
using mdvn::Utf8VisualWidth;
using mdvn::kMinColumnWidthDip;
using mdvn::kMaxColumnWidthRatio;
using mdvn::kTableAvgCharWidthDip;
using mdvn::kTableCellPaddingDip;
using mdvn::kTableGlyphWidthSafetyFactor;
using mdvn::u32;

namespace {
// 在 doc.blocks 里线性找到第 occurrence 个(从 0 计数)指定类型的块下标,
// 找不到返回 kInvalidIndex。与 test_layout.cpp 里的同名辅助函数保持相同写法。
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

#define MDVN_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

// 用例 1:单列超长内容——理想宽度被钳制在视口宽度 * kMaxColumnWidthRatio 以内。
MDVN_TEST(Table_SingleColumnVeryLongContentClampedToRatio) {
    MDVN_MAKE_TEST_ARENA();
    u32 chars[] = {500}; // 1 列 1 行,极长内容
    Span<float> widths = ComputeTableColumnWidths(chars, 1, 1, 600.0f, &arena);
    MDVN_CHECK(widths.data != nullptr);
    MDVN_CHECK_EQ(widths.len, 1u);
    MDVN_CHECK(widths[0] <= 600.0f * kMaxColumnWidthRatio + 0.01f);
}

// 用例 2:空表头(字符数全 0)——理想宽度钳制到下限,不产生 0 宽列。
MDVN_TEST(Table_EmptyHeaderFallsBackToMinWidth) {
    MDVN_MAKE_TEST_ARENA();
    u32 chars[] = {0, 0, 0, 0}; // 2 列 2 行,全空
    Span<float> widths = ComputeTableColumnWidths(chars, 2, 2, 600.0f, &arena);
    MDVN_CHECK_EQ(widths.len, 2u);
    MDVN_CHECK_EQ(widths[0], kMinColumnWidthDip);
    MDVN_CHECK_EQ(widths[1], kMinColumnWidthDip);
}

// 用例 3:列数不齐(某些行缺数据按 0 补齐)——仍能正确取每列最大值。
MDVN_TEST(Table_RaggedRowsStillTakePerColumnMax) {
    MDVN_MAKE_TEST_ARENA();
    // 3 列 3 行,第三行只有第 0 列有数据(缺的补 0);数值都选得明显超过
    // 下限(kMinColumnWidthDip / kTableAvgCharWidthDip = 7.5 字符),确保
    // 三列的理想宽度互不相同、不会同时被下限钳制到同一个值。
    u32 chars[] = {
        12, 10, 8,
        14,  9, 0,
        20,  0, 0,
    };
    Span<float> widths = ComputeTableColumnWidths(chars, 3, 3, 900.0f, &arena);
    MDVN_CHECK_EQ(widths.len, 3u);
    // 第 0 列最大字符数是 9,理应比第 1/2 列(最大 5/3)更宽。
    MDVN_CHECK(widths[0] > widths[1]);
    MDVN_CHECK(widths[1] > widths[2]);
}

// 用例 4:100 列宽表——总宽远超视口时整体等比压缩,不出现小于下限的列,
// 且压缩后各列仍保持原有的相对比例顺序。
MDVN_TEST(Table_HundredColumnsCompressProportionally) {
    Arena bigArena;
    MDVN_CHECK(bigArena.Init(1 * 1024 * 1024));

    const u32 kCols = 100;
    u32 chars[kCols];
    for (u32 c = 0; c < kCols; ++c) chars[c] = 20; // 每列理想宽度相同
    Span<float> widths = ComputeTableColumnWidths(chars, kCols, 1, 800.0f, &bigArena);
    MDVN_CHECK_EQ(widths.len, kCols);

    for (u32 c = 0; c < kCols; ++c) {
        MDVN_CHECK(widths[c] >= kMinColumnWidthDip - 0.01f);
    }
    // 100 列同宽,压缩后应仍然彼此相等(等比压缩不改变相对比例)。
    for (u32 c = 1; c < kCols; ++c) {
        MDVN_CHECK(widths[c] == widths[0]);
    }
}

// 用例 5:总宽不超视口时不触发压缩,列宽就是各自的理想宽度(钳制到下限之后)。
MDVN_TEST(Table_NoCompressionWhenFitsViewport) {
    MDVN_MAKE_TEST_ARENA();
    u32 chars[] = {5, 8}; // 2 列 1 行,理想宽度均远小于视口
    Span<float> widths = ComputeTableColumnWidths(chars, 2, 1, 2000.0f, &arena);
    MDVN_CHECK_EQ(widths.len, 2u);
    // 理想宽度现在含两侧内边距预算(kTableCellPaddingDip*2=12,见 table.h):
    // 5*8+12=52 < 60 下限,8*8+12=76 > 60 下限。
    MDVN_CHECK_EQ(widths[0], kMinColumnWidthDip);
    MDVN_CHECK(widths[1] > widths[0]);
}

// 用例 5b(2026-09-17 用户真机反馈的回归):总宽不超视口时,即使某一列内容
// 长到理应超过 kMaxColumnWidthRatio 的比例,也不应该被钳到那个比例——
// 上限只在"确实需要压缩"时才生效,不能在表格明明还有大片富余空间时,
// 仅因为单列内容长就强制换行(否则拖动窗口会出现"表格离右边界还有大片
// 空白却仍然触发重排"的问题,见 table.h 文件头注释)。
MDVN_TEST(Table_LongColumnNotClampedWhenTableFitsViewport) {
    MDVN_MAKE_TEST_ARENA();
    // 1 列内容视觉宽度 150,理想宽度 = 150*8*1.2+12=1452,超过 viewport*0.6=1200
    // (若按旧逻辑无条件钳到这个比例就会被强制换行),但总宽(1452)本身小于
    // 视口(2000),不应该压缩。
    u32 chars[] = {150};
    Span<float> widths = ComputeTableColumnWidths(chars, 1, 1, 2000.0f, &arena);
    MDVN_CHECK_EQ(widths.len, 1u);
    MDVN_CHECK_EQ(widths[0],
                  150.0f * kTableAvgCharWidthDip * kTableGlyphWidthSafetyFactor +
                      kTableCellPaddingDip * 2.0f);
    MDVN_CHECK(widths[0] > 2000.0f * kMaxColumnWidthRatio);
}

// 用例 5c:同样的单列超长内容,若视口本身较窄导致总宽确实超视口,验证
// 用例 1 的"钳到上限"行为不受用例 5b 改动影响——上限仍然只在压缩时生效,
// 但压缩时不会反过来把已经钳到上限的列"拉伸"去填满剩余空间(裁决:上限
// 是硬约束,不是"尽量占满视口"的目标)。
MDVN_TEST(Table_CappedColumnDuringCompressionIsNotStretchedBack) {
    MDVN_MAKE_TEST_ARENA();
    u32 chars[] = {500}; // 与用例 1 相同,理想宽度远超视口
    Span<float> widths = ComputeTableColumnWidths(chars, 1, 1, 600.0f, &arena);
    MDVN_CHECK_EQ(widths.len, 1u);
    MDVN_CHECK_EQ(widths[0], 600.0f * kMaxColumnWidthRatio);
}

// 用例 6:colCount 为 0 时安全返回空 Span,不崩溃。
MDVN_TEST(Table_ZeroColumnsReturnsEmptySpan) {
    MDVN_MAKE_TEST_ARENA();
    Span<float> widths = ComputeTableColumnWidths(nullptr, 0, 0, 600.0f, &arena);
    MDVN_CHECK(widths.data == nullptr);
    MDVN_CHECK_EQ(widths.len, 0u);
}

// ---- Utf8VisualWidth:纯函数,验证宽字符/窄字符/混排的视觉宽度计数 ----

// 用例 7:纯 ASCII——每个字节 1 个视觉宽度单位,与字符数相等。
MDVN_TEST(VisualWidth_AsciiCountsOnePerChar) {
    MDVN_CHECK_EQ(Utf8VisualWidth(StrSlice{"Gamma", 5}), 5u);
}

// 用例 8:纯 CJK——"网关服务" 4 个汉字,每个记 2 个单位,共 8;
// UTF-8 下这 4 个汉字占 12 字节,验证结果是视觉宽度而不是字节数。
MDVN_TEST(VisualWidth_CjkCountsTwoPerChar) {
    const char text[] = "\xe7\xbd\x91\xe5\x85\xb3\xe6\x9c\x8d\xe5\x8a\xa1"; // 网关服务
    MDVN_CHECK_EQ(Utf8VisualWidth(StrSlice{text, sizeof(text) - 1}), 8u);
}

// 用例 9:中英混排——"a你b" = 1(a) + 2(你) + 1(b) = 4,不是字节数 5,也不是字符数 3。
MDVN_TEST(VisualWidth_MixedCjkAndAsciiSumsPerCharWidth) {
    const char text[] = "a\xe4\xbd\xa0" "b"; // a 你 b
    MDVN_CHECK_EQ(Utf8VisualWidth(StrSlice{text, sizeof(text) - 1}), 4u);
}

// 用例 10:空串视觉宽度为 0。
MDVN_TEST(VisualWidth_EmptyStringIsZero) {
    MDVN_CHECK_EQ(Utf8VisualWidth(StrSlice{"", 0}), 0u);
}

// 用例 11:韩文谚文音节区(2026-09-17 扩充覆盖范围)——"안녕"(你好)2 个音节,
// 每个记 2 个单位,共 4;UTF-8 下占 6 字节,验证走的是谚文音节区分支而不是
// 退化成默认的 1 个单位。
MDVN_TEST(VisualWidth_HangulSyllableCountsTwoPerChar) {
    const char text[] = "\xec\x95\x88\xeb\x85\x95"; // 안녕
    MDVN_CHECK_EQ(Utf8VisualWidth(StrSlice{text, sizeof(text) - 1}), 4u);
}

// ---- 集成测试:表格列宽/行高估算改用视觉宽度后,不再被字节数口径带偏 ----

// 用例 11(症状 1 回归):中英混排宽表格里,同一列内既有纯英文单词又有中文
// 内容时,纯英文单元格的估算宽度不应该被"中文按字节数虚高"挤压变窄——
// 用视觉宽度口径,"Gamma"(5)应比"网关服务"(视觉宽度 8,字节数 12)窄,
// 但差距只应体现视觉宽度的 5:8,而不是字节数的 5:12 那么悬殊。
MDVN_TEST(Table_CjkAndAsciiMixedColumnDoesNotSquashAsciiWidth) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "| Name | Desc |\n"
        "|------|------|\n"
        "| Gamma | 网关服务说明文字 |\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));

    u32 tableIdx = FindBlockOfType(doc, BlockType::Table, 0);
    MDVN_CHECK(tableIdx != mdvn::kInvalidIndex);
    const BlockGeometry& tg = layout.Geometry(tableIdx);
    MDVN_CHECK_EQ(tg.tableColWidths.len, 2u);

    // "Gamma" 视觉宽度 5,"网关服务说明文字" 8 个汉字视觉宽度 16——
    // 字节数口径下分别是 5 字节 vs 24 字节,比例悬殊得多(1:4.8);
    // 视觉宽度口径下比例是 1:3.2,列宽差距应体现更温和的比例,
    // 且第 0 列(纯英文)不应被压到刚好等于下限(证明没有被过度压缩)。
    MDVN_CHECK(tg.tableColWidths[0] >= kMinColumnWidthDip);
    float ratio = tg.tableColWidths[1] / tg.tableColWidths[0];
    MDVN_CHECK(ratio < 4.0f);  // 明显小于字节数口径下会出现的比例
}

// 用例 12(症状 2 回归):单元格内容是能在估算列宽下一行放下的中文短语时,
// 行高不应该被多算成 2 行——用视觉宽度而不是字节数估算折行,配合按视觉
// 宽度分配的列宽,两者口径一致,单行内容就应该估算成 1 行的高度。
MDVN_TEST(Table_SingleLineCjkCellDoesNotOverestimateRowHeight) {
    MDVN_MAKE_TEST_ARENA();
    // "短" 和 "中等长度内容" 都很短,给足够宽的视口,列宽应该远大于这两列
    // 内容的视觉宽度,不会触发折行。
    const char src[] =
        "| 左对齐 | 居中对齐 | 右对齐 |\n"
        "|:---|:---:|---:|\n"
        "| 短 | 中等长度内容 | 1 |\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 900.0f));

    u32 headRow = FindBlockOfType(doc, BlockType::TableRow, 0);
    u32 bodyRow = FindBlockOfType(doc, BlockType::TableRow, 1);
    MDVN_CHECK(headRow != mdvn::kInvalidIndex);
    MDVN_CHECK(bodyRow != mdvn::kInvalidIndex);

    float headHeight = layout.Geometry(headRow).bottom - layout.Geometry(headRow).top;
    float bodyHeight = layout.Geometry(bodyRow).bottom - layout.Geometry(bodyRow).top;
    // 两行都只有单行文字,不应该因为估算口径不一致被判成两行——高度应当相等
    // (允许极小的浮点误差)。
    MDVN_CHECK(headHeight > 0.0f);
    MDVN_CHECK(bodyHeight > 0.0f);
    float diff = headHeight > bodyHeight ? headHeight - bodyHeight : bodyHeight - headHeight;
    MDVN_CHECK(diff < 0.5f);
}


// ---- 回归:理想宽度必须覆盖真实字形宽度(2026-09-18 用户真机反馈) ----

// 用例 13:理想宽度要在"平均字符宽度"之上乘 kTableGlyphWidthSafetyFactor。
// 真机实测 Segoe UI 16dip 下阿拉伯数字约为平均值的 1.08 倍,按平均值分配的
// 列宽连自己最长的那个单元格都放不下,表现为"窗口明明够宽,单元格还在折行"。
MDVN_TEST(Table_IdealWidthReservesGlyphSafetyMargin) {
    MDVN_MAKE_TEST_ARENA();
    u32 chars[] = {10};  // "1234567890":视觉宽度 10,真实排版约 86.2dip
    Span<float> widths = ComputeTableColumnWidths(chars, 1, 1, 2000.0f, &arena);
    MDVN_CHECK_EQ(widths.len, 1u);
    float textWidth = widths[0] - kTableCellPaddingDip * 2.0f;
    MDVN_CHECK(textWidth > 10.0f * kTableAvgCharWidthDip);
    MDVN_CHECK(textWidth >= 90.0f);  // 覆盖真实的 86.2dip,留有余量
}

// 用例 14(核心回归):字号缩放必须参与理想宽度计算。此前列宽恒按 1.0 档的
// 8dip/单位算,放大字号后真实字形同比变宽而列宽纹丝不动,于是无论把窗口拉多宽
// 表格都不变宽、单元格全部折行。
MDVN_TEST(Table_IdealWidthScalesWithFontScale) {
    MDVN_MAKE_TEST_ARENA();
    u32 chars[] = {30, 10};  // 2 列 1 行,理想宽度都远小于视口
    Span<float> base = ComputeTableColumnWidths(chars, 2, 1, 4000.0f, &arena, 1.0f);
    Span<float> zoomed = ComputeTableColumnWidths(chars, 2, 1, 4000.0f, &arena, 1.5f);
    MDVN_CHECK_EQ(base.len, 2u);
    MDVN_CHECK_EQ(zoomed.len, 2u);
    // 内容部分(扣掉不随字号缩放的内边距)必须正好放大 1.5 倍。
    float baseText = base[0] - kTableCellPaddingDip * 2.0f;
    float zoomedText = zoomed[0] - kTableCellPaddingDip * 2.0f;
    float diff = zoomedText - baseText * 1.5f;
    if (diff < 0.0f) diff = -diff;
    MDVN_CHECK(diff < 0.01f);
    MDVN_CHECK(zoomed[1] > base[1]);
}

// 用例 15:fontScale 非法值(0 或负数)退化成 1.0,不产生 0 宽/负宽列。
MDVN_TEST(Table_NonPositiveFontScaleFallsBackToOne) {
    MDVN_MAKE_TEST_ARENA();
    u32 chars[] = {20};
    Span<float> normal = ComputeTableColumnWidths(chars, 1, 1, 2000.0f, &arena, 1.0f);
    Span<float> zero = ComputeTableColumnWidths(chars, 1, 1, 2000.0f, &arena, 0.0f);
    Span<float> negative = ComputeTableColumnWidths(chars, 1, 1, 2000.0f, &arena, -2.0f);
    MDVN_CHECK_EQ(zero[0], normal[0]);
    MDVN_CHECK_EQ(negative[0], normal[0]);
}

// 用例 16(端到端回归):table_check.md 那张 7 列宽表,在足够宽的视口下
// 每一列的排版宽度都要放得下本列最长单元格的内容,且整表不被压缩——
// 这正是"窗口拉宽后表格应该一行展示更多内容"的验收点。
MDVN_TEST(Table_WideViewportFitsEveryCellOnOneLine) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "| 列一 | 列二 | 列三 | 列四 | 列五 | 列六 | 列七 |\n"
        "|---|---|---|---|---|---|---|\n"
        "| 这是一段比较长的中文内容测试超宽 | short | data | 1234567890 | more content here | x | y |\n"
        "| a | b | c | d | e | f | g |\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 1600.0f));

    u32 tableIdx = FindBlockOfType(doc, BlockType::Table, 0);
    MDVN_CHECK(tableIdx != mdvn::kInvalidIndex);
    const BlockGeometry& tg = layout.Geometry(tableIdx);
    MDVN_CHECK_EQ(tg.tableColWidths.len, 7u);

    // 每个单元格的排版宽度都要 >= 本单元格内容的估算宽度(含安全系数),
    // 即真实渲染时不需要折行。
    for (u32 i = 0; i < doc.blocks.Size(); ++i) {
        BlockType t = doc.blocks[i].type;
        if (t != BlockType::TableCell && t != BlockType::TableHeadCell) continue;
        u32 visual = 0;
        for (u32 k = 0; k < doc.blocks[i].inlineCount; ++k) {
            const mdvn::Inline& in = doc.inlines[doc.blocks[i].firstInlineIdx + k];
            visual += Utf8VisualWidth(StrSlice{mdvn::InlineTextBytes(in, doc), in.textLen});
        }
        float need = static_cast<float>(visual) * kTableAvgCharWidthDip * kTableGlyphWidthSafetyFactor;
        MDVN_CHECK(layout.Geometry(i).cellWidth + 0.01f >= need);
    }

    // 所有表格行等高:没有任何一格被估算成两行(与上面的"都放得下"互为印证)。
    float firstRowHeight = -1.0f;
    for (u32 i = 0; i < doc.blocks.Size(); ++i) {
        if (doc.blocks[i].type != BlockType::TableRow) continue;
        float h = layout.Geometry(i).bottom - layout.Geometry(i).top;
        if (firstRowHeight < 0.0f) firstRowHeight = h;
        float diff = h > firstRowHeight ? h - firstRowHeight : firstRowHeight - h;
        MDVN_CHECK(diff < 0.5f);
    }
    MDVN_CHECK(firstRowHeight > 0.0f);
}

// 用例 17:同一张表,视口越宽表格越宽——直到不再需要压缩为止;之后继续拉宽
// 不再变化(内容本身不多就不强行拉宽,见 table.h 算法注释第 2 步)。
MDVN_TEST(Table_WidensWithViewportUntilNoCompressionNeeded) {
    MDVN_MAKE_TEST_ARENA();
    u32 chars[] = {40, 30, 20};  // 3 列 1 行
    float narrow = 0.0f, mid = 0.0f, wide = 0.0f, wider = 0.0f;
    float viewports[] = {400.0f, 700.0f, 1200.0f, 3000.0f};
    float* outs[] = {&narrow, &mid, &wide, &wider};
    for (u32 i = 0; i < 4; ++i) {
        Span<float> w = ComputeTableColumnWidths(chars, 3, 1, viewports[i], &arena);
        float total = 0.0f;
        for (u32 c = 0; c < 3; ++c) total += w[c];
        *outs[i] = total;
    }
    MDVN_CHECK(narrow < mid);
    MDVN_CHECK(mid < wide);
    // 1200 已经足够放下全部理想宽度((40+30+20)*8*1.2+3*12=900),继续拉宽不再变化。
    MDVN_CHECK_EQ(wide, wider);
}

// 用例 18(回归):压缩时"被下限抬回来"的宽度必须由还有余地的列买单。
// 此前是一刀切等比缩放后再把结果钳回下限,被钳回来的那几个 DIP 没人买单,
// 总宽依然超视口——窄窗口下表现为表格右边几列被切到窗口外面。
MDVN_TEST(Table_CompressionRespectsViewportWhenSomeColumnsHitFloor) {
    MDVN_MAKE_TEST_ARENA();
    // 1 列内容很长 + 6 列内容极短:短列会立刻触底,长列必须替它们让出宽度。
    u32 chars[] = {32, 5, 4, 10, 17, 1, 1};
    const float kViewport = 700.0f;
    Span<float> widths = ComputeTableColumnWidths(chars, 7, 1, kViewport, &arena);
    MDVN_CHECK_EQ(widths.len, 7u);

    float total = 0.0f;
    for (u32 c = 0; c < 7u; ++c) {
        MDVN_CHECK(widths[c] >= kMinColumnWidthDip - 0.01f);
        total += widths[c];
    }
    // 7 个下限列(420)远小于视口,属于"压得下"的情形,总宽必须落在视口内。
    MDVN_CHECK(total <= kViewport + 0.01f);
    // 且不能压过头:还有余地的那一列应当把剩下的空间吃满。
    MDVN_CHECK(total > kViewport - kMinColumnWidthDip);
}
