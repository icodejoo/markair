// T10 覆盖测试:块级布局模块(Document -> 几何占位 + IDWriteTextLayout 虚拟化)。
#include "mdvn_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/text/font.h"
#include "../src/layout/layout.h"

#include <cstdio>
#include <cstring>

using mdvn::Arena;
using mdvn::Block;
using mdvn::BlockGeometry;
using mdvn::BlockLayoutEngine;
using mdvn::BlockType;
using mdvn::Document;
using mdvn::FontSubsystem;
using mdvn::LinkBox;
using mdvn::ParseMarkdown;
using mdvn::StrSlice;
using mdvn::u32;

#define MDVN_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

namespace {

// 在 doc.blocks 里线性找到第 occurrence 个(从 0 计数)指定类型的块下标,
// 找不到返回 kInvalidIndex。测试文档很小,线性扫描足够。
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

// 用例 1:基本布局——块按文档顺序 y 坐标递增,且每个块高度 > 0。
MDVN_TEST(Layout_BasicOrderingAndPositiveHeight) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "# Title\n"
        "Paragraph one with some reasonably long content for estimating.\n\n"
        "## Subtitle\n"
        "Paragraph two follows right after the subtitle heading here.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));
    MDVN_CHECK_EQ(layout.BlockCount(), doc.blocks.Size());

    // 这份文档是扁平结构(标题/段落都是 Document 的直属子块,不含容器),
    // 所以前序数组里从下标 1 开始就是严格按文档顺序排列的兄弟块。
    float prevBottom = -1.0f;
    for (u32 i = 1; i < layout.BlockCount(); ++i) {
        const BlockGeometry& g = layout.Geometry(i);
        MDVN_CHECK(g.bottom > g.top);  // 高度 > 0
        MDVN_CHECK(g.top >= prevBottom);
        prevBottom = g.bottom;
    }
}

// 用例 2:视口宽度变化只重跑布局(不重新解析),几何结果应随宽度变化。
MDVN_TEST(Layout_RelayoutOnWidthChangeAffectsGeometry) {
    MDVN_MAKE_TEST_ARENA();
    // 足够长的一段文字,窄视口下估算行数明显更多。
    const char src[] =
        "This paragraph contains a fairly long sentence that should wrap "
        "into a different number of lines depending on how wide the "
        "viewport currently is set to for this particular test case here.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 pIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    MDVN_CHECK(pIdx != mdvn::kInvalidIndex);

    BlockLayoutEngine layout;

    MDVN_CHECK(layout.Relayout(doc, 800.0f));
    // Relayout 前后 Document 的底层数组指针不变,佐证"没有重新解析",
    // 只是复用同一份已解析好的模型重新计算几何(签名本身也不接受原始文本)。
    const Block* blocksPtrBefore = doc.blocks.Data();
    float wideHeight = layout.Geometry(pIdx).bottom - layout.Geometry(pIdx).top;

    MDVN_CHECK(layout.Relayout(doc, 100.0f));
    const Block* blocksPtrAfter = doc.blocks.Data();
    MDVN_CHECK(blocksPtrBefore == blocksPtrAfter);
    float narrowHeight = layout.Geometry(pIdx).bottom - layout.Geometry(pIdx).top;

    MDVN_CHECK(narrowHeight > wideHeight);
}

// 用例 3:虚拟化——只有"可见范围 ± 1 屏"内的块持有 IDWriteTextLayout,
// 滚动后旧范围外的块被淘汰(Release + 置空),新范围内的块被创建。
MDVN_TEST(Layout_VirtualizationCreatesAndEvictsTextLayout) {
    FontSubsystem fonts;
    if (!fonts.Init()) {
        // 无 DirectWrite 环境(极端 CI/无显示场景),优雅跳过,不 crash。
        fprintf(stderr, "Layout_VirtualizationCreatesAndEvictsTextLayout: "
                         "FontSubsystem::Init 失败,跳过本测试\n");
        return;
    }

    // 构造一份足够长的文档:40 个短段落,总高度远超一屏。
    Arena srcArena;
    MDVN_CHECK(srcArena.Init(64 * 1024));
    char* buf = static_cast<char*>(srcArena.Alloc(4096, 1));
    MDVN_CHECK(buf != nullptr);
    if (!buf) return;
    u32 len = 0;
    const u32 kParagraphCount = 40;
    for (u32 i = 0; i < kParagraphCount; ++i) {
        int n = snprintf(buf + len, 4096 - len, "Paragraph number %u body text.\n\n", i);
        MDVN_CHECK(n > 0);
        len += static_cast<u32>(n);
    }

    Arena docArena;
    MDVN_CHECK(docArena.Init(1 * 1024 * 1024));
    Document doc = ParseMarkdown(StrSlice{buf, len}, &docArena);
    MDVN_CHECK(!doc.truncated);

    u32 firstP = FindBlockOfType(doc, BlockType::Paragraph, 0);
    u32 lastP = FindBlockOfType(doc, BlockType::Paragraph, kParagraphCount - 1);
    MDVN_CHECK(firstP != mdvn::kInvalidIndex);
    MDVN_CHECK(lastP != mdvn::kInvalidIndex);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 700.0f));
    float totalHeight = layout.TotalHeight();
    MDVN_CHECK(totalHeight > 900.0f);  // 40 段足够撑起远超一屏(300)的总高度

    // Relayout 刚跑完,不应该生成任何 layout(占位阶段不建 IDWriteTextLayout)。
    MDVN_CHECK(layout.Geometry(firstP).textLayout == nullptr);
    MDVN_CHECK(layout.Geometry(lastP).textLayout == nullptr);

    // 可见范围停在文档开头一小段:开头块应有 layout,结尾块(远超 ± 1 屏)应没有。
    layout.UpdateVisibleRange(0.0f, 300.0f, fonts);
    MDVN_CHECK(layout.Geometry(firstP).textLayout != nullptr);
    MDVN_CHECK(layout.Geometry(lastP).textLayout == nullptr);

    // 滚到文档末尾一小段:旧范围(开头)应被淘汰置空,新范围(结尾)应生成。
    layout.UpdateVisibleRange(totalHeight - 300.0f, totalHeight, fonts);
    MDVN_CHECK(layout.Geometry(firstP).textLayout == nullptr);
    MDVN_CHECK(layout.Geometry(lastP).textLayout != nullptr);
}

// 用例 4a:嵌套列表缩进——层级越深缩进越大,且是同一基准值(24 DIP)的整数倍。
MDVN_TEST(Layout_NestedListIndent) {
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
    MDVN_CHECK(li0 != mdvn::kInvalidIndex);
    MDVN_CHECK(li1 != mdvn::kInvalidIndex);
    MDVN_CHECK(li2 != mdvn::kInvalidIndex);

    float indent0 = layout.Geometry(li0).indent;
    float indent1 = layout.Geometry(li1).indent;
    float indent2 = layout.Geometry(li2).indent;

    // 基准值 24 DIP(见 layout.cpp 的 kListIndentUnitDip 注释),逐层递增。
    MDVN_CHECK_EQ(indent0, 24.0f);
    MDVN_CHECK_EQ(indent1, 48.0f);
    MDVN_CHECK_EQ(indent2, 72.0f);
    MDVN_CHECK(indent1 > indent0);
    MDVN_CHECK(indent2 > indent1);
}

// 用例 4b:嵌套引用块的左侧竖线几何——x/y/height 与该引用块的几何区间吻合,
// width 是固定小正数(3 DIP),嵌套层级越深左侧竖线的 x 越靠右。
MDVN_TEST(Layout_BlockQuoteBarGeometry) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "> > nested quote\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));

    u32 outerBq = FindBlockOfType(doc, BlockType::BlockQuote, 0);
    u32 innerBq = FindBlockOfType(doc, BlockType::BlockQuote, 1);
    MDVN_CHECK(outerBq != mdvn::kInvalidIndex);
    MDVN_CHECK(innerBq != mdvn::kInvalidIndex);

    const BlockGeometry& outerG = layout.Geometry(outerBq);
    const BlockGeometry& innerG = layout.Geometry(innerBq);

    MDVN_CHECK_EQ(outerG.quoteBar.width, 3.0f);
    MDVN_CHECK_EQ(innerG.quoteBar.width, 3.0f);
    MDVN_CHECK_EQ(outerG.quoteBar.x, outerG.indent);
    MDVN_CHECK_EQ(outerG.quoteBar.y, outerG.top);
    MDVN_CHECK_EQ(outerG.quoteBar.height, outerG.bottom - outerG.top);
    MDVN_CHECK_EQ(innerG.quoteBar.x, innerG.indent);
    MDVN_CHECK(innerG.quoteBar.x > outerG.quoteBar.x);  // 嵌套更深,缩进更大
}

// 用例 4c:围栏代码块背景矩形——覆盖该代码块自身的几何区间,宽度是视口宽度
// 减去左侧缩进与右侧留白(16 DIP)。
MDVN_TEST(Layout_CodeBlockBackgroundGeometry) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "```\ncode line one\ncode line two\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    const float viewportWidth = 760.0f;
    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, viewportWidth));

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MDVN_CHECK(codeIdx != mdvn::kInvalidIndex);

    const BlockGeometry& g = layout.Geometry(codeIdx);
    MDVN_CHECK_EQ(g.codeBackground.x, g.indent);
    MDVN_CHECK_EQ(g.codeBackground.y, g.top);
    MDVN_CHECK_EQ(g.codeBackground.height, g.bottom - g.top);
    MDVN_CHECK_EQ(g.codeBackground.width, viewportWidth - g.indent - 16.0f);
}

// 用例 5(T23):富行内样式——粗体/删除线/行内代码的 run 在同一个 IDWriteTextLayout
// 上按区间生效(SetFontWeight/SetStrikethrough/SetFontFamilyName),不新建多个
// layout 对象;用逐字符扫描断言"确实存在被标记为粗体/删除线/等宽字体的字符位置",
// 同时也存在未被标记的位置(佐证是按 run 生效而非整段一刀切)。
MDVN_TEST(Layout_RichInlineStylingAppliesPerRun) {
    FontSubsystem fonts;
    if (!fonts.Init()) {
        fprintf(stderr, "Layout_RichInlineStylingAppliesPerRun: FontSubsystem::Init 失败,跳过本测试\n");
        return;
    }

    MDVN_MAKE_TEST_ARENA();
    const char src[] = "plain **bold** and `code` text with ~~strike~~ here.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));
    layout.UpdateVisibleRange(0.0f, 300.0f, fonts);

    u32 pIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    MDVN_CHECK(pIdx != mdvn::kInvalidIndex);
    const BlockGeometry& g = layout.Geometry(pIdx);
    MDVN_CHECK(g.textLayout != nullptr);
    if (!g.textLayout) return;

    // DWRITE_TEXT_METRICS 没有 textLength 字段,用 GetClusterMetrics 的分段长度
    // 之和算出总 UTF-16 长度(256 个簇对这条极短测试文本绰绰有余)。
    DWRITE_CLUSTER_METRICS clusters[256]{};
    UINT32 clusterCount = 0;
    MDVN_CHECK(SUCCEEDED(g.textLayout->GetClusterMetrics(clusters, 256, &clusterCount)));
    UINT32 totalLength = 0;
    for (UINT32 i = 0; i < clusterCount; ++i) totalLength += clusters[i].length;

    bool sawBold = false, sawNormalWeight = false, sawStrike = false, sawMonoFamily = false;
    for (UINT32 pos = 0; pos < totalLength; ++pos) {
        DWRITE_FONT_WEIGHT weight{};
        DWRITE_TEXT_RANGE range{};
        g.textLayout->GetFontWeight(pos, &weight, &range);
        if (weight == DWRITE_FONT_WEIGHT_BOLD) sawBold = true; else sawNormalWeight = true;

        BOOL strike = FALSE;
        g.textLayout->GetStrikethrough(pos, &strike, &range);
        if (strike) sawStrike = true;

        wchar_t famName[64] = {};
        if (SUCCEEDED(g.textLayout->GetFontFamilyName(pos, famName, 64, &range)) &&
            wcscmp(famName, FontSubsystem::MonoFamilyName()) == 0) {
            sawMonoFamily = true;
        }
    }

    MDVN_CHECK(sawBold);
    MDVN_CHECK(sawNormalWeight);
    MDVN_CHECK(sawStrike);
    MDVN_CHECK(sawMonoFamily);
}

// 用例 6(T24):链接 run 被记进 BlockGeometry::linkBoxes,且 HitTestTextRange
// 拿到的矩形与该块的绘制起点(indent/top)吻合(在 1 DIP 误差内);链接 range
// 上的下划线(SetUnderline)已生效,佐证"不用 SetDrawingEffect"的实现路径。
MDVN_TEST(Layout_LinkRunsRecordedWithHitTestableRange) {
    FontSubsystem fonts;
    if (!fonts.Init()) {
        fprintf(stderr, "Layout_LinkRunsRecordedWithHitTestableRange: FontSubsystem::Init 失败,跳过本测试\n");
        return;
    }

    MDVN_MAKE_TEST_ARENA();
    const char src[] = "see [example](https://example.com) site.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    MDVN_CHECK(doc.linkTargets.Size() >= 1);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));
    layout.UpdateVisibleRange(0.0f, 300.0f, fonts);

    u32 pIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    const BlockGeometry& g = layout.Geometry(pIdx);
    MDVN_CHECK(g.textLayout != nullptr);
    MDVN_CHECK(g.linkBoxes.len >= 1);
    if (g.linkBoxes.len == 0 || !g.textLayout) return;

    const LinkBox& lb = g.linkBoxes[0];
    MDVN_CHECK_EQ(lb.blockIndex, pIdx);
    MDVN_CHECK(lb.textLength > 0);
    MDVN_CHECK(lb.linkTargetIdx < doc.linkTargets.Size());

    DWRITE_HIT_TEST_METRICS hitMetrics[4]{};
    UINT32 actualCount = 0;
    HRESULT hr = g.textLayout->HitTestTextRange(lb.textPosition, lb.textLength, g.indent, g.top,
                                                 hitMetrics, 4, &actualCount);
    MDVN_CHECK(SUCCEEDED(hr));
    MDVN_CHECK(actualCount >= 1);
    if (actualCount >= 1) {
        MDVN_CHECK(hitMetrics[0].left >= g.indent - 1.0f);
        MDVN_CHECK(hitMetrics[0].width > 0.0f);
    }

    BOOL underline = FALSE;
    DWRITE_TEXT_RANGE outRange{};
    g.textLayout->GetUnderline(lb.textPosition, &underline, &outRange);
    MDVN_CHECK(underline == TRUE);
}

// 用例 7(T27):任务列表勾选框几何——正方形、随 fontScale 等比缩放、
// 勾选状态与 ListItemDetail::taskChecked 一致,且自身文本缩进让出了勾选框空间。
MDVN_TEST(Layout_TaskCheckboxGeometryScalesWithFontScale) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "- [ ] todo one\n- [x] todo two\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f, 1.0f));

    u32 li0 = FindBlockOfType(doc, BlockType::ListItem, 0);
    u32 li1 = FindBlockOfType(doc, BlockType::ListItem, 1);
    MDVN_CHECK(li0 != mdvn::kInvalidIndex);
    MDVN_CHECK(li1 != mdvn::kInvalidIndex);

    const BlockGeometry& g0 = layout.Geometry(li0);
    const BlockGeometry& g1 = layout.Geometry(li1);
    MDVN_CHECK(g0.taskCheckbox.width > 0.0f);
    MDVN_CHECK_EQ(g0.taskCheckbox.height, g0.taskCheckbox.width);
    MDVN_CHECK(!g0.taskChecked);
    MDVN_CHECK(g1.taskChecked);
    MDVN_CHECK(g0.indent > g0.taskCheckbox.x);

    float unscaledSize = g0.taskCheckbox.width;

    BlockLayoutEngine layout2;
    MDVN_CHECK(layout2.Relayout(doc, 760.0f, 2.0f));
    const BlockGeometry& g0Scaled = layout2.Geometry(li0);
    MDVN_CHECK_EQ(g0Scaled.taskCheckbox.width, unscaledSize * 2.0f);
}

// 用例 8(T26):表格布局——列宽数组长度与列数一致、每行有独立的行边界、
// 单元格几何按列顺序水平排列且不重叠,表头行数与 TableDetail 一致。
MDVN_TEST(Layout_TableColumnsAndRowsLayoutSideBySide) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "| a | bb | ccc |\n"
        "|---|----|-----|\n"
        "| 1 | 2  | 3   |\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));

    u32 tableIdx = FindBlockOfType(doc, BlockType::Table, 0);
    MDVN_CHECK(tableIdx != mdvn::kInvalidIndex);
    const BlockGeometry& tg = layout.Geometry(tableIdx);
    MDVN_CHECK_EQ(tg.tableColWidths.len, 3u);
    MDVN_CHECK_EQ(tg.tableRowTops.len, 3u);  // 2 行(1 表头 + 1 表体)+ 1 个结尾边界
    MDVN_CHECK_EQ(tg.tableHeadRowCount, 1u);

    u32 headCell0 = FindBlockOfType(doc, BlockType::TableHeadCell, 0);
    u32 headCell1 = FindBlockOfType(doc, BlockType::TableHeadCell, 1);
    MDVN_CHECK(headCell0 != mdvn::kInvalidIndex);
    MDVN_CHECK(headCell1 != mdvn::kInvalidIndex);
    const BlockGeometry& hc0 = layout.Geometry(headCell0);
    const BlockGeometry& hc1 = layout.Geometry(headCell1);
    MDVN_CHECK_EQ(hc0.top, hc1.top);           // 同一行,顶部对齐
    MDVN_CHECK(hc1.indent > hc0.indent);        // 第二列在第一列右侧
    MDVN_CHECK(hc0.cellWidth > 0.0f);

    u32 bodyCell0 = FindBlockOfType(doc, BlockType::TableCell, 0);
    MDVN_CHECK(bodyCell0 != mdvn::kInvalidIndex);
    const BlockGeometry& bc0 = layout.Geometry(bodyCell0);
    MDVN_CHECK(bc0.top > hc0.top);  // 表体行在表头行下方
}

// 用例 9(T28):正文里的脚注引用合成出可见的 "[n]" 上标文本(源 span 本身没有
// 真实文本);脚注定义区块(及其内部段落)整体标记为小字号(smallText)。
MDVN_TEST(Layout_FootnoteRefSynthesizesTextAndDefSectionIsSmall) {
    FontSubsystem fonts;
    if (!fonts.Init()) {
        fprintf(stderr, "Layout_FootnoteRefSynthesizesTextAndDefSectionIsSmall: "
                         "FontSubsystem::Init 失败,跳过本测试\n");
        return;
    }

    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "See note[^1] for details.\n\n"
        "[^1]: This is the footnote body.\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);
    MDVN_CHECK(doc.footnoteDetails.Size() >= 1);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));
    layout.UpdateVisibleRange(0.0f, 2000.0f, fonts);

    u32 pIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    MDVN_CHECK(pIdx != mdvn::kInvalidIndex);
    const BlockGeometry& g = layout.Geometry(pIdx);
    MDVN_CHECK(g.textLayout != nullptr);
    if (g.textLayout) {
        // "See note for details." 本身约 21 个字符,合成的 "[1]" 应让总长度更长,
        // 佐证脚注引用确实产出了可见文本(而不是源里的空 span)。
        DWRITE_CLUSTER_METRICS clusters[256]{};
        UINT32 clusterCount = 0;
        MDVN_CHECK(SUCCEEDED(g.textLayout->GetClusterMetrics(clusters, 256, &clusterCount)));
        UINT32 totalLength = 0;
        for (UINT32 i = 0; i < clusterCount; ++i) totalLength += clusters[i].length;
        MDVN_CHECK(totalLength > 21u);
    }

    u32 sectionIdx = FindBlockOfType(doc, BlockType::FootnoteDefSection, 0);
    MDVN_CHECK(sectionIdx != mdvn::kInvalidIndex);
    if (sectionIdx != mdvn::kInvalidIndex) {
        MDVN_CHECK(layout.Geometry(sectionIdx).smallText);
    }

    u32 defIdx = FindBlockOfType(doc, BlockType::FootnoteDef, 0);
    MDVN_CHECK(defIdx != mdvn::kInvalidIndex);

    // 脚注定义内部的段落(若 md4c 把正文包成 Paragraph 子块)也应继承小字号标记。
    u32 footnoteParagraph = FindBlockOfType(doc, BlockType::Paragraph, 1);
    if (footnoteParagraph != mdvn::kInvalidIndex) {
        MDVN_CHECK(layout.Geometry(footnoteParagraph).smallText);
    }
}

// ---- T33:图片布局与占位块 ----

namespace {

// 在布局结果里找到第一个 ImageBox;没有返回 nullptr。
const mdvn::ImageBox* FirstImageBox(const BlockLayoutEngine& layout) {
    for (u32 i = 0; i < layout.BlockCount(); ++i) {
        const BlockGeometry& g = layout.Geometry(i);
        if (g.imageBoxes.len > 0) return &g.imageBoxes[0];
    }
    return nullptr;
}

}  // namespace

// 用例:图片 inline 会产出 ImageBox;尺寸未知时用默认占位尺寸,
// alt/href 均为零拷贝切片,状态为"未加载"。
MDVN_TEST(Layout_ImageProducesPlaceholderBox) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "文字段落\n\n![示例图](images/a.png)\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));  // 不传缓存:一律走默认占位尺寸

    const mdvn::ImageBox* box = FirstImageBox(layout);
    MDVN_CHECK(box != nullptr);
    if (!box) return;

    MDVN_CHECK(box->status == mdvn::ImageStatus::NotLoaded);
    MDVN_CHECK(box->kind == mdvn::LinkTargetKind::RelativePath);
    MDVN_CHECK_EQ(box->rect.width, mdvn::kPlaceholderWidthDip);
    MDVN_CHECK_EQ(box->rect.height, mdvn::kPlaceholderHeightDip);
    MDVN_CHECK_EQ(box->href.len, 12u);  // "images/a.png"
    MDVN_CHECK(box->href.data != nullptr);
}

// 用例:缓存里已有真实像素尺寸时,布局按真实尺寸占位("先有尺寸再有位图")。
MDVN_TEST(Layout_ImageUsesCachedSize) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "![](pic.png)\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    mdvn::ImageCache cache;
    MDVN_CHECK(cache.Init(&arena));
    cache.Put(StrSlice{"pic.png", 7}, nullptr, 200, 120, mdvn::ImageStatus::Ok, false);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f, 1.0f, &cache));

    const mdvn::ImageBox* box = FirstImageBox(layout);
    MDVN_CHECK(box != nullptr);
    if (!box) return;
    MDVN_CHECK_EQ(box->rect.width, 200.0f);
    MDVN_CHECK_EQ(box->rect.height, 120.0f);
    MDVN_CHECK(box->status == mdvn::ImageStatus::Ok);
}

// 用例:超过可用宽度的图片等比缩小到视口内,不撑破版面。
MDVN_TEST(Layout_ImageScaledDownToViewportWidth) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "![](wide.png)\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    mdvn::ImageCache cache;
    MDVN_CHECK(cache.Init(&arena));
    cache.Put(StrSlice{"wide.png", 8}, nullptr, 1000, 500, mdvn::ImageStatus::Ok, false);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 400.0f, 1.0f, &cache));

    const mdvn::ImageBox* box = FirstImageBox(layout);
    MDVN_CHECK(box != nullptr);
    if (!box) return;
    MDVN_CHECK(box->rect.width <= 400.0f);
    // 等比:1000x500 压到宽 400 -> 高约 200。
    MDVN_CHECK(box->rect.height > 190.0f && box->rect.height < 210.0f);
}

// 用例(T33 核心):解码出真实尺寸后只触发"一次"重排 ——
// ImagePlacementChanged 在尺寸变化时为 true,按新缓存重排之后立刻变回 false。
MDVN_TEST(Layout_ImagePlacementChangedFiresOnlyOnce) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "![](p.png)\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    mdvn::ImageCache cache;
    MDVN_CHECK(cache.Init(&arena));

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f, 1.0f, &cache));
    // 缓存为空时按占位尺寸排,此时"按当前缓存重排"不会有变化。
    MDVN_CHECK(!layout.ImagePlacementChanged(cache));

    // 模拟解码完成:真实尺寸与占位尺寸不同 -> 需要一次重排。
    cache.Put(StrSlice{"p.png", 5}, nullptr, 320, 64, mdvn::ImageStatus::Ok, false);
    MDVN_CHECK(layout.ImagePlacementChanged(cache));

    MDVN_CHECK(layout.Relayout(doc, 760.0f, 1.0f, &cache));
    // 重排之后不再变化 —— 不会每帧都重排。
    MDVN_CHECK(!layout.ImagePlacementChanged(cache));
}

// 用例(T33 核心):位图被释放(块滚出可见范围)之后,尺寸信息仍在缓存里,
// 因此"按当前缓存重排"不会有任何几何变化 —— 这是"滚出再滚回不跳动"的静态保证。
MDVN_TEST(Layout_ImageGeometryStableAfterBitmapRelease) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "![](q.png)\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    mdvn::ImageCache cache;
    MDVN_CHECK(cache.Init(&arena));
    cache.Put(StrSlice{"q.png", 5}, nullptr, 256, 128, mdvn::ImageStatus::Ok, false);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f, 1.0f, &cache));
    float heightBefore = layout.TotalHeight();

    cache.ReleaseBitmap(StrSlice{"q.png", 5});  // 模拟滚出可见 ± 1 屏
    MDVN_CHECK(!layout.ImagePlacementChanged(cache));

    MDVN_CHECK(layout.Relayout(doc, 760.0f, 1.0f, &cache));
    MDVN_CHECK_EQ(layout.TotalHeight(), heightBefore);
}

// 用例(T33 核心):图片尺寸变化只影响它自己及其之后的块,
// 位于图片之前的块 top 完全不变 —— 用户停在图片上方时不会看到内容跳动。
MDVN_TEST(Layout_ImageResizeDoesNotMoveEarlierBlocks) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "# 标题\n\n"
        "第一段文字。\n\n"
        "第二段文字。\n\n"
        "![](later.png)\n\n"
        "结尾段落。\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    mdvn::ImageCache cache;
    MDVN_CHECK(cache.Init(&arena));

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f, 1.0f, &cache));

    // 记下图片所在块之前所有块的 top。
    u32 imageBlock = mdvn::kInvalidIndex;
    for (u32 i = 0; i < layout.BlockCount(); ++i) {
        if (layout.Geometry(i).imageBoxes.len > 0) { imageBlock = i; break; }
    }
    MDVN_CHECK(imageBlock != mdvn::kInvalidIndex);
    if (imageBlock == mdvn::kInvalidIndex) return;

    float tops[64];
    u32 n = imageBlock < 64 ? imageBlock : 64;
    for (u32 i = 0; i < n; ++i) tops[i] = layout.Geometry(i).top;

    // 解码完成,真实尺寸远大于占位尺寸 -> 一次重排。
    cache.Put(StrSlice{"later.png", 9}, nullptr, 480, 400, mdvn::ImageStatus::Ok, false);
    MDVN_CHECK(layout.ImagePlacementChanged(cache));
    MDVN_CHECK(layout.Relayout(doc, 760.0f, 1.0f, &cache));

    for (u32 i = 0; i < n; ++i) {
        MDVN_CHECK_EQ(layout.Geometry(i).top, tops[i]);
    }
}

// 用例:一个段落里的多张图片各自成框,纵向堆叠不重叠。
MDVN_TEST(Layout_MultipleImagesStackVertically) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "![a](1.png) ![b](2.png) ![c](3.png)\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));

    u32 total = 0;
    for (u32 i = 0; i < layout.BlockCount(); ++i) {
        const BlockGeometry& g = layout.Geometry(i);
        for (u32 k = 0; k + 1 < g.imageBoxes.len; ++k) {
            // 下一张图的顶边不得高于上一张图的底边。
            MDVN_CHECK(g.imageBoxes[k + 1].rect.y >=
                       g.imageBoxes[k].rect.y + g.imageBoxes[k].rect.height);
        }
        total += g.imageBoxes.len;
    }
    MDVN_CHECK_EQ(total, 3u);
}

// 用例:没有图片的文档不产生任何 ImageBox(不为无图文档付出额外内存)。
MDVN_TEST(Layout_NoImagesMeansNoImageBoxes) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "# 标题\n\n普通段落,含 [链接](https://example.com)。\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));
    for (u32 i = 0; i < layout.BlockCount(); ++i) {
        MDVN_CHECK_EQ(layout.Geometry(i).imageBoxes.len, 0u);
    }
}
