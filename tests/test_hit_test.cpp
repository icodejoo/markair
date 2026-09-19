// T35 覆盖测试:命中测试的纯函数部分(坐标换算 / 块定位 / 图片矩形 / 链接 run 查表)。
//
// 这里刻意只测 hit_test.h 里"不依赖 Win32、不依赖真实 IDWriteTextLayout"的那一层,
// 边界条件(块间隙、行尾空白、滚动后的坐标)全部落在这一层上;需要真实 DirectWrite
// 对象的 HitTestDocument 走真实窗口人工验证,不在单测里构造。
#include "markair_test.h"
#include "../src/doc/model.h"
#include "../src/layout/layout.h"
#include "../src/shell/hit_test.h"

using markair::BlockGeometry;
using markair::BlockType;
using markair::ClientToDocument;
using markair::DocPoint;
using markair::FindContentBlockAt;
using markair::FindImageBoxAt;
using markair::HitKind;
using markair::HitResult;
using markair::ImageBox;
using markair::IsContentBlockType;
using markair::IsPointInOutlineOverlayMask;
using markair::IsPointInOutlinePanel;
using markair::IsPointInOutlinePanelRect;
using markair::kInvalidIndex;
using markair::LayoutRect;
using markair::LinkBox;
using markair::LinkTargetAtTextPosition;
using markair::ShouldUseHandCursor;
using markair::u32;

namespace {

// 造一个只填了命中测试关心字段的块几何。
BlockGeometry MakeGeometry(BlockType type, float top, float bottom, float indent = 0.0f,
                            float cellWidth = 0.0f) {
    BlockGeometry g{};
    g.type = type;
    g.top = top;
    g.bottom = bottom;
    g.indent = indent;
    g.cellWidth = cellWidth;
    return g;
}

}  // namespace

// 用例:客户区像素 -> 文档坐标,DPI 缩放与滚动偏移都要正确参与换算。
MARKAIR_TEST(HitTest_ClientToDocumentAppliesScaleAndScroll) {
    DocPoint p = ClientToDocument(200, 100, 2.0f, 300.0f);
    MARKAIR_CHECK(p.x == 100.0f);
    MARKAIR_CHECK(p.y == 350.0f);  // 100 / 2 + 300

    // 未缩放、未滚动时是恒等变换。
    DocPoint q = ClientToDocument(40, 60, 1.0f, 0.0f);
    MARKAIR_CHECK(q.x == 40.0f);
    MARKAIR_CHECK(q.y == 60.0f);

    // 非法 DPI 缩放退化为 1.0,不产生除零/负坐标。
    DocPoint r = ClientToDocument(10, 20, 0.0f, 5.0f);
    MARKAIR_CHECK(r.x == 10.0f);
    MARKAIR_CHECK(r.y == 25.0f);
}

// 用例:容器块不参与命中,内容块参与 —— 这是"块间隙判为没命中"的前提。
MARKAIR_TEST(HitTest_ContentBlockTypeClassification) {
    MARKAIR_CHECK(IsContentBlockType(BlockType::Paragraph));
    MARKAIR_CHECK(IsContentBlockType(BlockType::Heading));
    MARKAIR_CHECK(IsContentBlockType(BlockType::CodeBlock));
    MARKAIR_CHECK(IsContentBlockType(BlockType::TableCell));
    MARKAIR_CHECK(!IsContentBlockType(BlockType::Document));
    MARKAIR_CHECK(!IsContentBlockType(BlockType::BulletList));
    MARKAIR_CHECK(!IsContentBlockType(BlockType::BlockQuote));
    MARKAIR_CHECK(!IsContentBlockType(BlockType::Table));
    MARKAIR_CHECK(!IsContentBlockType(BlockType::TableRow));
}

// 用例(边界条件:块间隙):两个段落之间留 8 DIP 的空隙,落在空隙里的点
// 不应该命中任何一个段落,而不是"就近吸附"到其中一个。
MARKAIR_TEST(HitTest_GapBetweenBlocksHitsNothing) {
    BlockGeometry geoms[3] = {
        MakeGeometry(BlockType::Document, 0.0f, 108.0f),  // 容器块覆盖整段区间
        MakeGeometry(BlockType::Paragraph, 0.0f, 50.0f),
        MakeGeometry(BlockType::Paragraph, 58.0f, 108.0f),
    };

    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 10.0f, 10.0f), 1u);
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 10.0f, 60.0f), 2u);
    // 50 ~ 58 是块间距,属于容器块 —— 容器不参与命中,所以什么都没命中。
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 10.0f, 54.0f), kInvalidIndex);
    // 半开区间:恰好等于 bottom 的那一行像素算下一个块/间隙,不算上一个块。
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 10.0f, 50.0f), kInvalidIndex);
    // 文档之外(上方/下方)同样什么都没命中。
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 10.0f, -1.0f), kInvalidIndex);
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 10.0f, 999.0f), kInvalidIndex);
}

// 用例:嵌套时取最深的那个块(前序数组里下标更大者)。
MARKAIR_TEST(HitTest_NestedBlocksPickDeepest) {
    BlockGeometry geoms[3] = {
        MakeGeometry(BlockType::Document, 0.0f, 100.0f),
        MakeGeometry(BlockType::BulletList, 0.0f, 100.0f),   // 容器
        MakeGeometry(BlockType::ListItem, 10.0f, 40.0f),      // 内容
    };
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 5.0f, 20.0f), 2u);
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 5.0f, 60.0f), kInvalidIndex);
}

// 用例:同一行的表格单元格 y 区间完全相同,必须按 x 与列宽区分,不能互相误命中。
MARKAIR_TEST(HitTest_TableCellsDistinguishedByColumn) {
    BlockGeometry geoms[3] = {
        MakeGeometry(BlockType::Table, 0.0f, 40.0f),
        MakeGeometry(BlockType::TableCell, 0.0f, 40.0f, 6.0f, 100.0f),    // 第 1 列
        MakeGeometry(BlockType::TableCell, 0.0f, 40.0f, 126.0f, 100.0f),  // 第 2 列
    };
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 50.0f, 20.0f), 1u);
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 180.0f, 20.0f), 2u);
    // 落在两列之间的单元格内边距/网格线上:不属于任何一个单元格。
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, 115.0f, 20.0f), kInvalidIndex);
}

// 用例(边界条件:滚动后的坐标):同一个客户区像素点,在不同滚动偏移下
// 命中的块必须跟着变。
MARKAIR_TEST(HitTest_ScrolledCoordinatesHitDifferentBlocks) {
    BlockGeometry geoms[3] = {
        MakeGeometry(BlockType::Document, 0.0f, 1000.0f),
        MakeGeometry(BlockType::Paragraph, 0.0f, 100.0f),
        MakeGeometry(BlockType::Paragraph, 500.0f, 600.0f),
    };

    DocPoint top = ClientToDocument(10, 50, 1.0f, 0.0f);
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, top.x, top.y), 1u);

    // 向下滚 500 DIP 之后,同一个客户区坐标落到了第二个段落上。
    DocPoint scrolled = ClientToDocument(10, 50, 1.0f, 500.0f);
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, scrolled.x, scrolled.y), 2u);

    // 滚到两块之间的空白处:仍然什么都没命中。
    DocPoint between = ClientToDocument(10, 50, 1.0f, 200.0f);
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 3, between.x, between.y), kInvalidIndex);
}

// 用例:图片矩形命中,含左闭右开的边界语义。
MARKAIR_TEST(HitTest_ImageBoxRectangles) {
    ImageBox boxes[2]{};
    boxes[0].rect = LayoutRect{10.0f, 20.0f, 100.0f, 80.0f};
    boxes[0].linkTargetIdx = 7;
    boxes[1].rect = LayoutRect{10.0f, 120.0f, 100.0f, 80.0f};
    boxes[1].linkTargetIdx = 8;

    MARKAIR_CHECK_EQ(FindImageBoxAt(boxes, 2, 50.0f, 50.0f), 0u);
    MARKAIR_CHECK_EQ(FindImageBoxAt(boxes, 2, 50.0f, 150.0f), 1u);
    MARKAIR_CHECK_EQ(FindImageBoxAt(boxes, 2, 10.0f, 20.0f), 0u);    // 左上角在内
    MARKAIR_CHECK_EQ(FindImageBoxAt(boxes, 2, 110.0f, 50.0f), kInvalidIndex);  // 右边界在外
    MARKAIR_CHECK_EQ(FindImageBoxAt(boxes, 2, 50.0f, 105.0f), kInvalidIndex);  // 两张图之间
    MARKAIR_CHECK_EQ(FindImageBoxAt(nullptr, 0, 0.0f, 0.0f), kInvalidIndex);
}

// 用例(边界条件:行尾空白):链接 run 是半开区间,正好落在 run 末尾之后的位置
// (点在链接文字右侧的行尾空白上)不应该判为命中链接。
MARKAIR_TEST(HitTest_LinkRunIsHalfOpenRange) {
    LinkBox boxes[2] = {
        LinkBox{0u, 5u, 4u, 11u},   // [5, 9)
        LinkBox{0u, 20u, 3u, 22u},  // [20, 23)
    };

    MARKAIR_CHECK_EQ(LinkTargetAtTextPosition(boxes, 2, 5u), 11u);
    MARKAIR_CHECK_EQ(LinkTargetAtTextPosition(boxes, 2, 8u), 11u);
    MARKAIR_CHECK_EQ(LinkTargetAtTextPosition(boxes, 2, 9u), kInvalidIndex);  // 行尾空白
    MARKAIR_CHECK_EQ(LinkTargetAtTextPosition(boxes, 2, 4u), kInvalidIndex);
    MARKAIR_CHECK_EQ(LinkTargetAtTextPosition(boxes, 2, 22u), 22u);
    MARKAIR_CHECK_EQ(LinkTargetAtTextPosition(boxes, 2, 23u), kInvalidIndex);
    MARKAIR_CHECK_EQ(LinkTargetAtTextPosition(nullptr, 0, 0u), kInvalidIndex);
}

// 用例(T64):大纲侧栏区域判定 —— 只按横坐标,DPI 缩放要参与换算。
MARKAIR_TEST(HitTest_OutlinePanelRegionByXOnly) {
    // 未缩放:侧栏宽度 220 DIP 内命中,边界(等于宽度)不命中(半开区间)。
    MARKAIR_CHECK(IsPointInOutlinePanel(0, 1.0f, 220.0f));
    MARKAIR_CHECK(IsPointInOutlinePanel(219, 1.0f, 220.0f));
    MARKAIR_CHECK(!IsPointInOutlinePanel(220, 1.0f, 220.0f));
    MARKAIR_CHECK(!IsPointInOutlinePanel(500, 1.0f, 220.0f));

    // 2x DPI 缩放下,侧栏在物理像素上占 440px,越过这条线就不算侧栏。
    MARKAIR_CHECK(IsPointInOutlinePanel(439, 2.0f, 220.0f));
    MARKAIR_CHECK(!IsPointInOutlinePanel(440, 2.0f, 220.0f));

    // 非法 DPI 缩放退化为 1.0。
    MARKAIR_CHECK(IsPointInOutlinePanel(100, 0.0f, 220.0f));
    MARKAIR_CHECK(!IsPointInOutlinePanel(300, 0.0f, 220.0f));

    // 负坐标不算侧栏内(理论上不会发生,防御性验证)。
    MARKAIR_CHECK(!IsPointInOutlinePanel(-1, 1.0f, 220.0f));
}

// 蒙层区域判定:与 IsPointInOutlinePanel 互补——不在侧栏内就在蒙层内。
MARKAIR_TEST(HitTest_OutlineOverlayMaskIsComplementOfPanel) {
    MARKAIR_CHECK(!IsPointInOutlineOverlayMask(0, 1.0f, 220.0f));
    MARKAIR_CHECK(!IsPointInOutlineOverlayMask(219, 1.0f, 220.0f));
    MARKAIR_CHECK(IsPointInOutlineOverlayMask(220, 1.0f, 220.0f));
    MARKAIR_CHECK(IsPointInOutlineOverlayMask(500, 1.0f, 220.0f));

    // 2x DPI 缩放下同样互补。
    MARKAIR_CHECK(!IsPointInOutlineOverlayMask(439, 2.0f, 220.0f));
    MARKAIR_CHECK(IsPointInOutlineOverlayMask(440, 2.0f, 220.0f));

    // 负坐标:不在侧栏内(IsPointInOutlinePanel 返回 false),因此判定为蒙层。
    MARKAIR_CHECK(IsPointInOutlineOverlayMask(-1, 1.0f, 220.0f));
}

// 侧栏矩形判定(供 WM_MOUSEWHEEL 使用):横坐标沿用 IsPointInOutlinePanel
// 的口径,纵坐标额外要求落在 [0, clientHeightDip) 内。
MARKAIR_TEST(HitTest_OutlinePanelRectChecksBothAxes) {
    // 横坐标在侧栏范围内,纵坐标也在客户区高度范围内 -> 命中。
    MARKAIR_CHECK(IsPointInOutlinePanelRect(100, 300, 1.0f, 220.0f, 600.0f));
    // 横坐标在侧栏范围内,但纵坐标越出客户区高度 -> 不命中。
    MARKAIR_CHECK(!IsPointInOutlinePanelRect(100, 600, 1.0f, 220.0f, 600.0f));
    MARKAIR_CHECK(!IsPointInOutlinePanelRect(100, 999, 1.0f, 220.0f, 600.0f));
    // 纵坐标为负 -> 不命中。
    MARKAIR_CHECK(!IsPointInOutlinePanelRect(100, -1, 1.0f, 220.0f, 600.0f));
    // 横坐标越出侧栏范围时,纵坐标合法与否都不命中。
    MARKAIR_CHECK(!IsPointInOutlinePanelRect(300, 300, 1.0f, 220.0f, 600.0f));
    // DPI 缩放同时影响横纵两个方向的换算。
    MARKAIR_CHECK(IsPointInOutlinePanelRect(439, 1199, 2.0f, 220.0f, 600.0f));
    MARKAIR_CHECK(!IsPointInOutlinePanelRect(440, 1199, 2.0f, 220.0f, 600.0f));
    MARKAIR_CHECK(!IsPointInOutlinePanelRect(439, 1200, 2.0f, 220.0f, 600.0f));
}

// 用例(T64,验收项):鼠标落在侧栏区域内的坐标不应命中正文任何元素——
// 即便正文在该 x 范围内恰好摆了一个链接/图片,侧栏区域的判定本身也必须
// 在命中测试正文之前短路返回,不依赖"正文碰巧没东西"这种巧合。
// 这里直接验证短路判据本身:侧栏区域内的坐标满足 IsPointInOutlinePanel,
// 调用方(window.cpp)据此在调用 HitTestDocument 之前就 return,因此正文的
// 链接/图片/复制按钮命中测试根本不会跑到这个坐标上。
MARKAIR_TEST(HitTest_SidebarRegionShortCircuitsBeforeBodyHitTest) {
    // 正文里恰好有一个横跨侧栏宽度的段落(模拟"侧栏悬浮盖住的正文内容"),
    // 以及一个同样落在这个 x 范围内的图片。
    BlockGeometry geoms[2] = {
        MakeGeometry(BlockType::Document, 0.0f, 200.0f),
        MakeGeometry(BlockType::Paragraph, 0.0f, 200.0f),
    };
    ImageBox boxes[1]{};
    boxes[0].rect = LayoutRect{0.0f, 0.0f, 220.0f, 200.0f};  // 覆盖整个侧栏宽度

    constexpr float kPanelWidthDip = 220.0f;
    int clientX = 100;  // 落在侧栏区域内
    float scale = 1.5f;

    MARKAIR_CHECK(IsPointInOutlinePanel(clientX, scale, kPanelWidthDip));

    // 即便正文在同一坐标上确实摆了内容块和图片,命中测试本身仍然会命中——
    // 真正的"不干扰"由调用方在命中前的短路保证,这条用例证明短路判据本身
    // 覆盖了正文有内容的这种情况,不是靠"正文那块恰好是空的"这种巧合成立。
    float docX = static_cast<float>(clientX) / scale;
    MARKAIR_CHECK_EQ(FindContentBlockAt(geoms, 2, docX, 50.0f), 1u);
    MARKAIR_CHECK_EQ(FindImageBoxAt(boxes, 1, docX, 50.0f), 0u);
}

// 用例:光标形状判定 —— 链接与图片给手型,其余给默认箭头。
MARKAIR_TEST(HitTest_HandCursorOnlyForInteractive) {
    HitResult none{HitKind::None, kInvalidIndex, kInvalidIndex, kInvalidIndex};
    HitResult link{HitKind::Link, 3u, 1u, kInvalidIndex};
    HitResult image{HitKind::Image, 3u, 1u, 0u};

    MARKAIR_CHECK(!ShouldUseHandCursor(none));
    MARKAIR_CHECK(ShouldUseHandCursor(link));
    MARKAIR_CHECK(ShouldUseHandCursor(image));
}
