// T25 覆盖测试:表格列宽算法(纯数字函数,不依赖 D2D/DirectWrite)。
#include "mdvn_test.h"
#include "../src/util/arena.h"
#include "../src/layout/table.h"

using mdvn::Arena;
using mdvn::ComputeTableColumnWidths;
using mdvn::Span;
using mdvn::kMinColumnWidthDip;
using mdvn::kMaxColumnWidthRatio;
using mdvn::u32;

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
    MDVN_CHECK_EQ(widths[0], kMinColumnWidthDip); // 5 字符 * 8 DIP = 40 < 60 下限
    MDVN_CHECK(widths[1] > widths[0]); // 8 字符 * 8 DIP = 64 > 60 下限
}

// 用例 6:colCount 为 0 时安全返回空 Span,不崩溃。
MDVN_TEST(Table_ZeroColumnsReturnsEmptySpan) {
    MDVN_MAKE_TEST_ARENA();
    Span<float> widths = ComputeTableColumnWidths(nullptr, 0, 0, 600.0f, &arena);
    MDVN_CHECK(widths.data == nullptr);
    MDVN_CHECK_EQ(widths.len, 0u);
}
