// mdvn 的块级布局模块(T10):文档模型 -> 行盒/块盒几何坐标。
// 只做纯数字计算(y 坐标、缩进、矩形区域),不依赖 D2D 绘制(D2D 渲染是 T11 的事)。
//
// 核心约束(架构 §5):视口外不生成 IDWriteTextLayout——只为"可见范围 ± 1 屏"
// 生成/持有该对象,其余全部淘汰(Release + 置空),避免这个相对昂贵的对象无界增长。
#pragma once

#include <dwrite_2.h>

#include "../assets/cache.h"
#include "../doc/model.h"
#include "../text/font.h"
#include "../util/arena.h"
#include "../util/span.h"
#include "../util/types.h"
#include "table.h"

namespace mdvn {

/**
 * 一段矩形几何区域,单位 DIP(与 D2D/DirectWrite 坐标系一致)。
 * 用于引用块左侧竖线、围栏代码块背景等"块自带装饰区域"的几何描述。
 */
struct LayoutRect {
    float x;
    float y;
    float width;
    float height;
};

/**
 * 一个链接/自动链接 run 在某个块的 IDWriteTextLayout 里占据的区间(T24)。
 * 渲染时用它对着色 range 单独处理(不引入 SetDrawingEffect/自定义渲染器);
 * 命中测试可对 textPosition/textLength 调用 IDWriteTextLayout::HitTestTextRange
 * 取矩形。
 */
struct LinkBox {
    u32 blockIndex;    // 归属的块下标,对应 Document::blocks / BlockGeometry
    u32 textPosition;  // 该块 IDWriteTextLayout 里的 UTF-16 起始偏移
    u32 textLength;     // UTF-16 长度
    u32 linkTargetIdx; // 指向 Document::linkTargets 的下标
};

/**
 * 一张图片在布局里占据的矩形与渲染所需的全部信息(T33)。
 *
 * 设计上刻意做成"渲染层只看这个结构体就能画",不需要反查 Document:
 * alt 零拷贝指向文档源文本,href 零拷贝指向 LinkTarget::href(文档 Arena 上)。
 *
 * "先有尺寸再有位图":rect 的尺寸在 Relayout 阶段就定下来——缓存里已有该图
 * 尺寸就按真实尺寸(等比缩放到可用宽度内),否则用 kPlaceholderWidthDip/
 * kPlaceholderHeightDip 占位。解码完成后尺寸若与占位不同,由调用方触发一次
 * Relayout(见 BlockLayoutEngine::ImagePlacementChanged)。
 */
struct ImageBox {
    LayoutRect rect;      // 图片/占位块矩形(相对文档,DIP)
    StrSlice alt;         // 图片 alt 文本(零拷贝指向 Document::source)
    StrSlice href;        // 图片来源地址(零拷贝指向 LinkTarget::href),同时是缓存 key
    u32 blockIndex;       // 归属块下标
    u32 linkTargetIdx;    // 指向 Document::linkTargets 的下标
    LinkTargetKind kind;  // 目标种类(本地路径 / 外链 / data URI ...)
    ImageStatus status;   // 布局时已知的状态(缓存命中则取缓存状态,否则 NotLoaded)
};

/**
 * 单个块的布局几何结果,下标与 Document::blocks 一一对应。
 *
 * textLayout 字段体现虚拟化策略:只有该块当前落在"可见范围 ± 1 屏"内时才非空,
 * 由 BlockLayoutEngine::UpdateVisibleRange 统一创建/淘汰,调用方不应自行 Release——
 * 所有权始终归 BlockLayoutEngine。
 *
 * 表格相关字段(tableColWidths/tableRowTops/tableHeadRowCount)只在
 * type == BlockType::Table 时非空;cellWidth 只在 TableHeadCell/TableCell 时非零;
 * taskCheckbox 只在任务列表项(ListItem 且 ListItemDetail::isTask)时非全 0。
 */
struct BlockGeometry {
    float top;                // 块顶部 y 坐标(相对文档起始,DIP)
    float bottom;              // 块底部 y 坐标
    BlockType type;             // 块类型,直接引用 model.h 的枚举,不复制语义
    float indent;               // 左侧缩进量(DIP),见 BlockLayoutEngine 头注释的缩进规则
    LayoutRect quoteBar;         // 引用块左侧竖线矩形;非引用块恒为全 0
    LayoutRect codeBackground;   // 围栏代码块背景矩形;非代码块恒为全 0
    IDWriteTextLayout* textLayout; // 视口 ± 1 屏内才非空,其余淘汰为 nullptr,见虚拟化策略说明
    Span<LinkBox> linkBoxes;      // 该块内的链接/自动链接 run(T24),仅创建 layout 时才非空
    Span<ImageBox> imageBoxes;    // 该块内的图片/占位块(T33),Relayout 阶段即确定,不随虚拟化失效
    Span<float> tableColWidths;   // 表格各列宽度(T25/T26),仅 Table 块非空
    Span<float> tableRowTops;     // 表格每行顶部 y(相对文档,DIP),长度 = 行数 + 1(末尾是表格底边)
    u32 tableHeadRowCount;         // 表头行数,渲染表头背景用
    float cellWidth;               // 单元格排版宽度(T26),仅 TableHeadCell/TableCell 非零
    LayoutRect taskCheckbox;       // 任务列表勾选框几何(T27);非任务项恒为全 0
    bool taskChecked;              // 任务是否已勾选(仅任务项有意义)
    // T44 列表符号:仅非任务项的 ListItem 才非零(任务项已经画勾选框,二者互斥,
    // 见 taskCheckbox 注释)。无序列表时是符号本身的绘制矩形(圆点/空心圆/方块,
    // 渲染层按 listMarkerLevel 决定形状,见 renderer.cpp::DrawListMarker);有序
    // 列表时是序号文字的绘制原点 + 预留列宽(width/height 供渲染层排版参考)。
    LayoutRect listMarker;
    u8 listMarkerLevel;    // 列表嵌套层级,1-based;0 表示该 ListItem 不画列表符号
    bool listMarkerOrdered; // 是否为有序列表项(否则是无序符号)
    u32 listMarkerOrdinal;  // 有序列表当前序号,listMarkerOrdered 为真时才有意义
    char listMarkerDelim;   // 有序列表序号分隔符(如 '.' 或 ')'),listMarkerOrdered 为真时才有意义
    // 列表符号占用的水平位移量(DIP):只影响"这个 ListItem 自身"文字/图片的
    // 绘制起点(渲染层见 TextDrawLeft;命中测试见 hit_test.cpp),不叠加进
    // indent 本身——indent 保留"纯嵌套缩进"的既有语义(kListIndentUnitDip 的
    // 整数倍),兼容 M0 基线测试 Layout_NestedListIndent 对 indent 的断言。
    // 任务列表勾选框走的是老路径,直接改 g.indent(见 taskCheckbox 分支),
    // 与这个字段互斥,不会同时非零。子块(嵌套列表/段落)缩进不受影响,
    // 仍从未叠加此值的 x 起排(与 taskCheckbox 现有做法一致)。
    float listMarkerPad;
    bool smallText;                 // 脚注定义区块内文字整体小一号(T28)
    u32 footnoteId;                 // 脚注定义(FootnoteDef)的 1-based 编号,渲染 "[n]" 前缀用;非脚注定义恒为 0
    // 表格单元格(TableHeadCell/TableCell)的 IDWriteTextLayout 真实内容高度
    // (GetMetrics().height),用于渲染时把文字在"统一行高"内垂直居中——行高
    // 取该行所有单元格估算高度的最大值,单个单元格的文字实际高度往往比它小,
    // 直接从 top 起画会贴顶。仅表格单元格创建 layout 时才写入,其余类型恒为 0。
    float contentHeight;
    // 围栏代码块的内边距(DIP):文字渲染时应从 (indent, top) 起沿两个方向各
    // 平移这么多再画,让文字离代码高亮区背景的四边都有留白——右侧/底侧的
    // 留白已经体现在文本排版宽度收窄/块高度里,不需要额外几何字段。仅代码块
    // 非零,其余类型恒为 0。
    float textPad;
    // 围栏/缩进代码块右上角的"复制"按钮矩形(相对文档,DIP):画在
    // codeBackground 的右上角、四周各留一份内边距,正方形。仅 CodeBlock 非零,
    // 其余块类型恒为全 0 —— 表格/引用块等不提供这个按钮。渲染见
    // renderer.cpp::DrawCodeCopyButton,命中见 hit_test.h::FindCodeCopyButtonAt。
    LayoutRect codeCopyButton;
};

/**
 * 图片解码位图的"驻留控制器"(T32 第二次裁决):把解码位图的生死挂到已有的
 * 块级虚拟化触发点上,与 IDWriteTextLayout 走同一条创建/释放路径。
 *
 * BlockLayoutEngine::UpdateVisibleRange 在判断每个块是否落在"可见 ± 1 屏"之后,
 * 对有图片的块分别回调 EnsureResident / ReleaseResident;布局层本身不知道
 * 怎么解码、也不持有渲染目标,具体实现见 render/renderer.h 的 ImageResidencyManager。
 *
 * @example
 *   mdvn::ImageResidencyManager residency;   // 实现了本接口
 *   layout.UpdateVisibleRange(top, bottom, fonts, &residency);
 */
class ImageResidencyController {
public:
    virtual ~ImageResidencyController() {}

    /**
     * 块进入"可见 ± 1 屏":按需解码这些图片(已解码且位图仍在的应跳过)。
     * @param boxes 该块的图片数组,非空。
     * @param count 图片个数,大于 0。
     */
    virtual void EnsureResident(const ImageBox* boxes, u32 count) = 0;

    /**
     * 块离开"可见 ± 1 屏":释放这些图片的解码位图(保留尺寸信息)。
     * @param boxes 该块的图片数组,非空。
     * @param count 图片个数,大于 0。
     */
    virtual void ReleaseResident(const ImageBox* boxes, u32 count) = 0;
};

/**
 * 块级布局引擎:文档模型 + 视口宽度 -> 每个块的几何占位,并对
 * IDWriteTextLayout 做"可见范围 ± 1 屏"虚拟化管理。
 *
 * 缩进规则(列表缩进/引用缩进,自定,写死为两条常量):
 *   - 每层列表(BulletList/OrderedList)缩进 = kListIndentUnitDip(24 DIP),
 *     取值依据是"正文行高(20 DIP)的 1.2 倍",模拟常见 Markdown 渲染器
 *     list-item 的 padding-left,嵌套 N 层即 N * 24 DIP,是该基准值的整数倍。
 *   - 每层引用块(BlockQuote)缩进 = kQuoteIndentUnitDip(20 DIP),嵌套 N 层
 *     即 N * 20 DIP。
 *
 * 高度估算策略:Relayout 阶段不生成任何 IDWriteTextLayout,块高度用
 * "直属 inline 文本总字节数 / 按视口宽度估算的每行字符数" 粗略估算行数
 * 后乘以行高,这是一个只依赖数字计算的占位值;等到 UpdateVisibleRange
 * 为某块创建了真实 IDWriteTextLayout 后,本实现选择不做真实高度回填式
 * 的级联重排(会牵连后续兄弟块位置,超出 T10 范围),这是一个已知的取舍——
 * 见 BlockLayoutEngine::UpdateVisibleRange 的实现注释。
 *
 * 虚拟化策略:内部持有一份与 Document::blocks 等长的 BlockGeometry 数组,
 * UpdateVisibleRange 按"可见范围 ± 1 屏"(1 屏 = bottomY - topY)算出目标
 * 区间,区间内、还没有 textLayout 的块调用 FontSubsystem::CreateTextLayout
 * 生成;区间外、仍持有 textLayout 的块 Release 并置空——淘汰时机就是
 * "块的几何区间与目标区间不再相交"的那一刻。
 *
 * @example
 *   mdvn::Arena docArena;
 *   docArena.Init(4 * 1024 * 1024);
 *   mdvn::Document doc = mdvn::ParseMarkdown(mdvn::StrSlice{text, len}, &docArena);
 *
 *   mdvn::BlockLayoutEngine layout;
 *   layout.Relayout(doc, 760.0f);              // 视口宽度变化只重跑这一步,不重解析
 *
 *   mdvn::FontSubsystem fonts;
 *   fonts.Init();
 *   layout.UpdateVisibleRange(0.0f, 600.0f, fonts); // 只为可见 ± 1 屏生成/淘汰 layout
 *
 *   const mdvn::BlockGeometry& g0 = layout.Geometry(0);
 */
class BlockLayoutEngine {
public:
    // 构造一个空引擎,内部 Arena 延迟到首次 Relayout 才真正预留地址空间。
    BlockLayoutEngine();

    // 释放当前持有的全部 IDWriteTextLayout,再释放内部 Arena。
    ~BlockLayoutEngine();

    // 持有裸 COM 指针所有权,禁止拷贝。
    BlockLayoutEngine(const BlockLayoutEngine&) = delete;
    BlockLayoutEngine& operator=(const BlockLayoutEngine&) = delete;

    /**
     * 对整份文档重新计算块级几何占位(y 坐标、缩进、装饰矩形),不生成任何
     * IDWriteTextLayout。调用前会先淘汰(Release)上一次持有的全部 layout,
     * 保证不会跨文档/跨布局泄漏 COM 对象。
     *
     * @param doc 已解析好的文档模型(签名不接受原始文本,不会调用 ParseMarkdown——
     *            这是"视口宽度变化只重跑布局不重解析"约束的设计层保证)。
     * @param viewportWidth 视口宽度(DIP),用于估算换行行数与代码块背景宽度。
     * @param fontScale 当前字号缩放系数(T29,来自 FontSubsystem::Scale()),
     *                  用于让行高/字符宽度估算与勾选框等几何跟随缩放;默认
     *                  1.0(未缩放)。
     * @param images 可选的图片缓存(T32/T33):非空时图片按缓存里已知的真实尺寸
     *               占位("先有尺寸再有位图"),为空时一律用默认占位尺寸。
     *               只读引用,不持有所有权,生命周期须覆盖后续渲染。
     * @return 内部 Arena 首次预留失败时返回 false(极端情况,如系统拒绝虚拟内存);
     *         正常情况恒返回 true。
     * @example layout.Relayout(doc, 760.0f, fonts.Scale(), &imageCache);
     */
    bool Relayout(const Document& doc, float viewportWidth, float fontScale = 1.0f,
                  const ImageCache* images = nullptr);

    /**
     * 判断"按当前图片缓存重排一次,几何是否会发生变化"(T33 的一次性重排判据)。
     *
     * 典型用法:解码一批图片并写入缓存之后调用一次,返回 true 才 Relayout,
     * 避免每解码一张图都无谓地重排整份文档。
     *
     * @param images 当前的图片缓存。
     * @return 存在任一 ImageBox 的占位尺寸与"按缓存重算得到的尺寸"不同返回 true。
     * @example if (layout.ImagePlacementChanged(cache)) { / * 重排并按块下标恢复滚动 * / }
     */
    bool ImagePlacementChanged(const ImageCache& images) const;

    /**
     * 按"可见范围 ± 1 屏"虚拟化策略更新 IDWriteTextLayout 的生成/淘汰。
     *
     * @param topY 当前可见区域顶部 y 坐标(DIP)。
     * @param bottomY 当前可见区域底部 y 坐标(DIP),须大于 topY。
     * @param fonts 已 Init 成功的字体子系统,用于创建 IDWriteTextLayout。
     * @param images 可选的图片驻留控制器(T32):非空时,图片解码位图与
     *               IDWriteTextLayout 在同一时机被创建/释放——块进入"可见 ± 1 屏"
     *               回调 EnsureResident,离开回调 ReleaseResident。传 nullptr
     *               表示不管理图片位图(纯布局场景/单测)。
     * @example layout.UpdateVisibleRange(scrollY, scrollY + viewportHeight, fonts, &residency);
     */
    void UpdateVisibleRange(float topY, float bottomY, FontSubsystem& fonts,
                             ImageResidencyController* images = nullptr);

    // 当前布局结果的块总数(等于上一次 Relayout 时 Document::blocks 的大小)。
    u32 BlockCount() const { return geometries_.Size(); }

    // 按块下标取几何结果,调用方保证 index < BlockCount()。
    const BlockGeometry& Geometry(u32 blockIndex) const { return geometries_[blockIndex]; }

    // 整份文档估算/实测的总高度(DIP),即根节点 Document 块的 bottom。
    float TotalHeight() const { return totalHeight_; }

private:
    // 递归铺开一个块及其子树的几何占位,返回"排完这个块之后,下一个兄弟块应从哪个 y 开始"。
    // inFootnote 表示当前是否处于 FootnoteDefSection 子树内(T28,决定文字是否小一号),
    // 由调用方(Relayout 的首次调用传 false)沿递归自动传播,不需要外部关心。
    //
    // T44 新增四个列表符号相关参数,均由直接父块在下发子块时算好,本函数自己
    // 不做任何"往上找列表祖先"的反查:
    //   - listDepth:当前块所处的列表嵌套层级(0 = 不在任何列表容器内)。若本块
    //     是 BulletList/OrderedList 容器,它会在下发自己的子块(应为 ListItem)
    //     时传 listDepth + 1;其余情况原样透传,不在中途"凭空"改变层级。
    //   - orderedItem/itemOrdinal/itemDelim:仅当本块是某个 OrderedList 的直属
    //     ListItem 时才有意义(由该 OrderedList 在下发子块时算好当前序号),
    //     其余情况调用方一律传 false/0/0。
    float LayoutSubtree(u32 blockIndex, float x, float y, bool inFootnote, u32 listDepth,
                        bool orderedItem, u32 itemOrdinal, char itemDelim);

    // 叶子内容块(标题/段落/代码块/分割线)的高度估算,纯数字计算,不涉及 DirectWrite。
    float EstimateLeafHeight(const Block& b, float availableWidth) const;

    // 为指定块创建真实 IDWriteTextLayout:拼接其直属 inline 文本 -> UTF-16 -> 调用字体子系统,
    // 并按 run 应用粗体/斜体/删除线/行内代码字体/链接下划线等样式(T23),记录链接 run(T24)。
    // 表格单元格(TableHeadCell/TableCell)复用同一份实现:cellWidth 非零时
    // 用它做排版宽度,并按 CellDetail::align 设置文本对齐、表头单元格整体加粗(T26)。
    IDWriteTextLayout* CreateLayoutForBlock(u32 blockIndex, FontSubsystem& fonts);

    // 淘汰(Release + 置空)当前持有的全部 IDWriteTextLayout,用于 Relayout 前与析构时。
    void ReleaseAllLayouts();

    // 铺开一个表格(Table 块)及其整棵子树的几何:计算列宽、逐行逐格定位,
    // 表头/表体/行/单元格块的几何全部在这里写入,不再走通用的容器递归路径。
    // 返回排完整个表格之后下一个兄弟块应从哪个 y 开始。
    float LayoutTableSubtree(u32 tableBlockIndex, float x, float y);

    // 判断一个块是否携带"直属文本"(标题/段落/代码块/紧凑列表项首段/表格单元格),
    // 只有这类块才需要 IDWriteTextLayout。
    bool BlockHasOwnText(const Block& b) const;

    // 为一个块内的图片 inline 生成 ImageBox 并纵向堆叠,返回这些图片占用的总高度。
    // startY 为图片区起始 y;imageCache_ 为空时一律用默认占位尺寸。
    float LayoutImagesForBlock(u32 blockIndex, float x, float startY, float availableWidth);

    // 按缓存(可能为空)决定一张图片在布局里的显示尺寸(DIP):缓存里有真实像素
    // 尺寸就按它等比缩放到 availableWidth 内,否则用默认占位尺寸。
    void ResolveImageSize(StrSlice href, float availableWidth, float* outWidth,
                           float* outHeight, ImageStatus* outStatus) const;

    Arena geometryArena_;           // 存放几何数组/表格列宽/链接 run 等,每次 Relayout 整体 Reset 复用
    Arena scratchArena_;            // 拼接 UTF-8/UTF-16 文本的临时缓冲,每次生成 layout 前 Reset
    bool arenasReady_;               // 两个 Arena 是否已完成首次 Init
    Vec<BlockGeometry> geometries_; // 与 Document::blocks 等长的几何数组
    const Document* doc_;            // 当前布局所基于的文档(只读引用,不拥有)
    float viewportWidth_;            // 当前视口宽度(DIP)
    float totalHeight_;              // 文档总高度缓存(DIP)
    float fontScale_;                // 当前字号缩放系数(T29),参与行高/字符宽度/勾选框几何估算
    const ImageCache* imageCache_;   // 图片缓存(T33),只读、可为空,用于"先有尺寸再有位图"
};

}  // namespace mdvn
