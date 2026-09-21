#include "layout.h"

#include <cstring>

#include "../hl/lexer.h"

namespace markair {

namespace {

// 几何数组用的 Arena 预留大小:按 kMaxDocumentNodeCount(20 万)量级的块数估算,
// 每个 BlockGeometry 约 60 字节,Vec 扩容翻倍最多多占一倍,32MB 留有充分余量。
// M1 新增的表格列宽/行边界/链接 run 侧数组也从这个 Arena 分配,量级远小于
// 几何数组本身,不需要单独扩大预留。
constexpr size_t kGeometryArenaReserveBytes = 32u * 1024u * 1024u;

// 拼接单个块文本用的临时 Arena,每次生成 layout 前 Reset 复用,不需要很大。
constexpr size_t kScratchArenaReserveBytes = 1u * 1024u * 1024u;

// 每层列表缩进量:正文行高(20 DIP)的 1.2 倍,模拟常见 Markdown 渲染器
// list-item 的 padding-left。
constexpr float kListIndentUnitDip = 24.0f;

// 每层引用块缩进量。
constexpr float kQuoteIndentUnitDip = 20.0f;

// 引用块左侧竖线宽度(固定小正数)。
constexpr float kQuoteBarWidthDip = 3.0f;

// 围栏代码块背景矩形右侧留白,不铺满到视口最右边。
constexpr float kCodeBlockRightMarginDip = 16.0f;

// 代码高亮区内边距,对齐 GitHub 代码块的 16px 留白(GitHub Primer 规范),
// 文字四周离背景边框 16 DIP;圆角半径见 renderer.cpp 的 kCodeBlockCornerRadiusDip。
// 与 kCodeBlockRightMarginDip/kTableCellPaddingDip 同一惯例:固定留白不随
// fontScale_ 缩放。
constexpr float kCodeBlockPaddingDip = 16.0f;

// 代码块右上角"复制"按钮的边长(DIP,未缩放前),随 fontScale_ 缩放;按钮与
// 背景矩形四边的留白直接复用 kCodeBlockPaddingDip(与代码文字同一档内边距,
// 视觉上按钮与首行文字上沿对齐)。
constexpr float kCodeCopyButtonSizeDip = 20.0f;

// 块与块之间的垂直间距。
constexpr float kBlockVerticalGapDip = 8.0f;

// 叶子内容块估算高度时额外附加的上下留白。
constexpr float kLeafVerticalPaddingDip = 4.0f;

// 表格单元格行数估算的安全系数:直接复用列宽算法的同一个常量,两处口径必须
// 一致——列宽按它放宽、行数不按它折算的话,恰好定义该列宽度的那个单元格会被
// 估成两行,行高凭空翻倍;反过来行数估得比真实少 1 行,单元格就会和下一行
// 表格内容重叠(表格行与行之间紧贴,没有 kBlockVerticalGapDip 兜底)。
constexpr float kTableLineEstimateSafetyFactor = kTableGlyphWidthSafetyFactor;

// 正文行高与平均字符宽度估算值(占位用,真实值等真实 IDWriteTextLayout 生成后才知道)。
// kAvgCharWidthDip 这里的"字符"指 Utf8VisualWidth 的视觉宽度单位(ASCII 记 1、
// CJK 等宽字符记 2),不是字节数——用视觉宽度而非字节数才能让 CJK/西文混排
// 场景下"总视觉宽度 ÷ 平均字符宽度"估算出的折行行数与真实渲染基本吻合。
constexpr float kBaseLineHeightDip = 20.0f;
constexpr float kAvgCharWidthDip = 8.0f;

// 等宽字体(围栏代码块)的行高/字符宽度估算值,略窄一些的行高、略宽一些的字符宽度。
constexpr float kMonoLineHeightDip = 18.0f;
constexpr float kMonoAvgCharWidthDip = 9.0f;

// 分割线固定高度(含上下留白)。
constexpr float kThematicBreakHeightDip = 24.0f;

// 创建 IDWriteTextLayout 时给的排版框最大高度,只是给 DWrite 一个足够大的框,
// 不会截断文本,真实高度以 GetMetrics() 为准(本实现暂不做该值的回填,见头文件说明)。
constexpr float kMaxTextLayoutHeightDip = 100000.0f;

// 标题各级别(1-6)相对正文的字号/行高放大系数,索引 0 不使用。
constexpr float kHeadingScale[7] = {1.0f, 1.8f, 1.5f, 1.35f, 1.2f, 1.1f, 1.0f};

// T27 任务列表勾选框:边长与文字之间的间隙(DIP,未缩放前),随 fontScale_ 缩放。
constexpr float kCheckboxSizeDip = 14.0f;
constexpr float kCheckboxGapDip = 6.0f;

// T44 列表符号(无序列表用几何图元,有序列表用数字):圆点/空心圆直径、方块边长
// (DIP,未缩放前);符号与文字之间复用 kCheckboxGapDip 同一档间隙,视觉上与
// 任务列表勾选框保持一致的"图元 + 间隙 + 文字"节奏。
constexpr float kListMarkerDotDiameterDip = 6.0f;
constexpr float kListMarkerSquareDip = 5.0f;
// 有序列表序号预留列宽:按两位数字("12.")在正文字号下的量级估算,不精确测量
// 实际文字宽度(和任务列表勾选框一样,布局阶段不为此创建 IDWriteTextLayout)。
constexpr float kOrderedMarkerColumnDip = 26.0f;

// T28 脚注区块内文字整体缩小的比例(相对当前正文字号)。
constexpr float kFootnoteFontRatio = 0.85f;

// T28 正文里脚注引用上标 "[n]" 的缩小比例。
constexpr float kFootnoteRefFontRatio = 0.7f;

// T28 脚注定义 "[n]" 编号标签预留的高度(DIP,未缩放前),独占一行,
// 具体文字由渲染层用小号一次性 layout 画出(见 renderer.cpp)。
constexpr float kFootnoteLabelHeightDip = 18.0f;

// T33 图片之间、以及图片与同块文本之间的垂直间距(DIP,未缩放前)。
constexpr float kImageVerticalGapDip = 6.0f;

// 把一个 u32 格式化成十进制 wchar_t 数字写入 buf(不含符号/千分位),返回写入长度。
// 手写实现(脚注引用只需要合成形如 "12" 的极短数字文本),避免为此引入
// swprintf 格式串解析的开销与依赖。
u32 FormatDecimalW(u32 value, wchar_t* buf, u32 cap) {
    wchar_t tmp[10];
    u32 n = 0;
    if (value == 0) {
        tmp[n++] = L'0';
    } else {
        while (value > 0 && n < 10) {
            tmp[n++] = static_cast<wchar_t>(L'0' + (value % 10));
            value /= 10;
        }
    }
    u32 len = n < cap ? n : cap;
    for (u32 i = 0; i < len; ++i) buf[i] = tmp[len - 1 - i];
    return len;
}

// T53:围栏代码块语法着色——一个 inline 段在"拼接后的代码文本"里的字节区间,
// 与它在最终 UTF-16 buf 里对应的起始偏移(长度可由下一段的 byteStart 或
// 总字节数推出,这里不单独存,用不上)。用于把词法器产出的字节偏移换算成
// IDWriteTextLayout 认识的 UTF-16 偏移。
struct CodeSeg {
    u32 byteStart;
    u32 byteLen;
    u32 utf16Offset;
};

// 把"拼接后的代码文本"里的字节偏移换算成 UTF-16 偏移:线性定位偏移落在哪个
// inline 段,再对段内前缀调用 Utf16LengthOfUtf8 得到段内的 UTF-16 增量。
// segCount 通常很小(代码块的 inline 段数,一般等于源码行数量级),线性扫描
// 足够快,不需要二分。
u32 CodeByteOffsetToUtf16(const CodeSeg* segs, u32 segCount, const char* codeBuf,
                            u32 byteOffset, u32 fallbackUtf16) {
    for (u32 s = 0; s < segCount; ++s) {
        const CodeSeg& seg = segs[s];
        if (byteOffset >= seg.byteStart && byteOffset <= seg.byteStart + seg.byteLen) {
            u32 intra = byteOffset - seg.byteStart;
            return seg.utf16Offset + Utf16LengthOfUtf8(StrSlice{codeBuf + seg.byteStart, intra});
        }
    }
    return fallbackUtf16;  // 落在末尾之后(极端情况),钳到缓冲区末尾
}

}  // namespace

// 构造一个空引擎,Arena 延迟到首次 Relayout 才真正预留地址空间。
BlockLayoutEngine::BlockLayoutEngine()
    : geometryArena_(),
      scratchArena_(),
      arenasReady_(false),
      geometries_(&geometryArena_),
      doc_(nullptr),
      viewportWidth_(0.0f),
      totalHeight_(0.0f),
      fontScale_(1.0f),
      imageCache_(nullptr),
      relayoutCallCount_(0) {}

// 先淘汰所有仍持有的 IDWriteTextLayout,再让 Arena 析构释放虚拟地址空间。
BlockLayoutEngine::~BlockLayoutEngine() { ReleaseAllLayouts(); }

void BlockLayoutEngine::ReleaseAllLayouts() {
    u32 count = geometries_.Size();
    for (u32 i = 0; i < count; ++i) {
        IDWriteTextLayout*& layout = geometries_[i].textLayout;
        if (layout) {
            layout->Release();
            layout = nullptr;
        }
    }
}

bool BlockLayoutEngine::BlockHasOwnText(const Block& b) const {
    // 只有携带直属文本的块才需要 layout:标题/段落/代码块/表格单元格恒有文本
    // (空单元格 inlineCount 为 0,下面按此过滤);紧凑列表项(ListItem)可能
    // 携带自己的首段文本;纯容器块(文档根/列表/引用块/表格分组行/无文本的
    // 列表项)永远没有直属文本,不生成 layout。
    if (b.type == BlockType::Heading || b.type == BlockType::Paragraph ||
        b.type == BlockType::CodeBlock) {
        return true;
    }
    if (b.type == BlockType::TableHeadCell || b.type == BlockType::TableCell) {
        return b.inlineCount > 0;
    }
    if (b.type == BlockType::ListItem) {
        return b.inlineCount > 0;
    }
    return false;
}

bool BlockLayoutEngine::Relayout(const Document& doc, float viewportWidth, float fontScale,
                                  const ImageCache* images) {
    ++relayoutCallCount_;  // T49 测试桩:仅计数,不影响布局逻辑
    // 先淘汰上一次布局持有的全部 layout,避免跨文档/跨布局泄漏 COM 对象。
    ReleaseAllLayouts();

    if (!arenasReady_) {
        if (!geometryArena_.Init(kGeometryArenaReserveBytes)) return false;
        if (!scratchArena_.Init(kScratchArenaReserveBytes)) return false;
        arenasReady_ = true;
    } else {
        geometryArena_.Reset();
        scratchArena_.Reset();
    }

    // Vec 本身是"绑定到某个 Arena 的追加数组"值语义,重新绑定即可让它在
    // Reset 后的 Arena 上重新从零开始追加,不需要额外的"清空"接口。
    geometries_ = Vec<BlockGeometry>(&geometryArena_);
    doc_ = &doc;
    viewportWidth_ = viewportWidth;
    fontScale_ = fontScale > 0.0f ? fontScale : 1.0f;
    imageCache_ = images;
    totalHeight_ = 0.0f;

    u32 blockCount = doc.blocks.Size();
    // 提前精确预留,避免 Push 翻倍扩容在 Arena 中留下大量废弃旧块(见 P0 内存优化)。
    if (!geometries_.Reserve(blockCount)) {
        return false;  // Arena 空间耗尽,安全放弃,不崩溃
    }
    for (u32 i = 0; i < blockCount; ++i) {
        if (!geometries_.Push(BlockGeometry{})) {
            return false;  // Arena 空间耗尽,安全放弃,不崩溃
        }
    }
    if (blockCount == 0) return true;

    LayoutSubtree(0, 0.0f, 0.0f, false, 0, false, 0, 0);
    totalHeight_ = geometries_[0].bottom;
    return true;
}

float BlockLayoutEngine::LayoutSubtree(u32 blockIndex, float x, float y, bool inFootnote,
                                        u32 listDepth, bool orderedItem, u32 itemOrdinal,
                                        char itemDelim) {
    const Block& b = doc_->blocks[blockIndex];
    BlockGeometry& g = geometries_[blockIndex];

    g.type = b.type;
    g.top = y;
    g.indent = x;
    g.textLayout = nullptr;
    g.quoteBar = LayoutRect{0, 0, 0, 0};
    g.codeBackground = LayoutRect{0, 0, 0, 0};
    g.codeCopyButton = LayoutRect{0, 0, 0, 0};
    g.linkBoxes = Span<LinkBox>{nullptr, 0};
    g.codeHighlights = Span<CodeHighlightRun>{nullptr, 0};
    g.sideDataReady = false;
    g.imageBoxes = Span<ImageBox>{nullptr, 0};
    g.tableColWidths = Span<float>{nullptr, 0};
    g.tableRowTops = Span<float>{nullptr, 0};
    g.tableHeadRowCount = 0;
    g.cellWidth = 0.0f;
    g.taskCheckbox = LayoutRect{0, 0, 0, 0};
    g.taskChecked = false;
    g.listMarker = LayoutRect{0, 0, 0, 0};
    g.listMarkerLevel = 0;
    g.listMarkerOrdered = false;
    g.listMarkerOrdinal = 0;
    g.listMarkerDelim = 0;
    g.listMarkerPad = 0.0f;
    g.smallText = inFootnote || b.type == BlockType::FootnoteDefSection;
    g.footnoteId = 0;
    // 代码高亮区内边距:文字离背景四边各留 8 DIP,但 g.indent/g.codeBackground
    // 的既有几何口径不能变(会影响下面 codeBackground 的计算)。这里只记一份
    // textPad,渲染时(见 renderer.cpp 的 TextDrawLeft/TextDrawTop)把文字整体
    // 从 (g.indent, g.top) 平移 (pad, pad) 画;右侧/底部内边距体现为下面
    // cursor/maxWidth 计算时预留等量空间,不需要额外几何字段。
    bool isCodeBlock = b.type == BlockType::CodeBlock;
    g.textPad = isCodeBlock ? kCodeBlockPaddingDip : 0.0f;

    bool isOrderedContainer = b.type == BlockType::OrderedList;
    bool isListContainer = b.type == BlockType::BulletList || isOrderedContainer;
    bool isQuote = b.type == BlockType::BlockQuote;
    float childX = x;
    if (isListContainer) childX = x + kListIndentUnitDip * fontScale_;
    if (isQuote) childX = x + kQuoteIndentUnitDip * fontScale_;

    bool nextInFootnote = inFootnote || b.type == BlockType::FootnoteDefSection;

    // T27:任务列表勾选框——检测到 ListItemDetail 侧表条目即认定为任务项
    // (parser.cpp 只在 is_task 为真时才 Push 该侧表,detailIdx 非法即非任务项)。
    // 勾选框画在原始左边界 x 处,自身文字整体右移让出空间;子块(嵌套列表/
    // 段落)缩进不受影响,仍从 childX(未右移的 x)起排。
    // 松散列表(项间有空行)时,ListItem 自身没有直属行内内容,文字改由子
    // Paragraph 块承载(见下方 hasOwnLeafContent 的判据)。勾选框/列表符号
    // 占用的水平空间(下面写进 g.indent 或 g.listMarkerPad)只作用于"本块自己
    // 画文字"这一种情况——子块默认仍从未偏移的 childX 起排,否则松散列表的
    // 首段文字会紧贴在符号右边,毫无间距(T44 之后发现的真实 bug)。这里额外
    // 记一份 listItemOwnContentPad,子块递归那一步按"是不是嵌套列表容器"分流:
    // 嵌套列表(BulletList/OrderedList)保持不偏移(嵌套缩进只看 kListIndentUnitDip,
    // 不应该再叠加父项符号的宽度,否则会破坏既有的嵌套缩进验收);其余子块
    // (松散列表的延续段落、代码块等)按这份 pad 右移,对齐"紧凑列表本该有"的
    // 文字起点。
    float listItemOwnContentPad = 0.0f;

    bool isTaskItem = b.type == BlockType::ListItem && b.detailIdx != kInvalidIndex;
    if (isTaskItem) {
        const ListItemDetail& lid = doc_->listItemDetails[b.detailIdx];
        float size = kCheckboxSizeDip * fontScale_;
        float lineHeight = kBaseLineHeightDip * fontScale_;
        float boxY = y + (lineHeight - size) * 0.5f;
        if (boxY < y) boxY = y;
        g.taskCheckbox = LayoutRect{x, boxY, size, size};
        g.taskChecked = lid.taskChecked;
        g.indent = x + size + kCheckboxGapDip * fontScale_;
        listItemOwnContentPad = size + kCheckboxGapDip * fontScale_;
    }

    // T44:列表符号——非任务列表项的 ListItem 才画(任务列表项已经在上面画了
    // 勾选框,不重复画符号,二者互斥,判据同样是 isTaskItem)。listDepth 由
    // 直接父块(BulletList/OrderedList)下发,>0 才说明确实处于某个列表容器内
    // (理论上非 ListItem 块不会带非 0 listDepth 进这里,但仍以 b.type 判断为准)。
    bool isListItem = b.type == BlockType::ListItem;
    if (isListItem && !isTaskItem && listDepth > 0) {
        float lineHeight = kBaseLineHeightDip * fontScale_;
        g.listMarkerLevel = static_cast<u8>(listDepth);
        g.listMarkerOrdered = orderedItem;
        if (orderedItem) {
            // 有序列表:所有层级统一用"阿拉伯数字 + 分隔符"(如 "1." "2)"),不切换
            // 字母/罗马数字——这与常见 GFM 渲染器(如 GitHub)嵌套有序列表的实际
            // 观感一致:层级只体现在缩进上,序号风格不变。
            g.listMarkerOrdinal = itemOrdinal;
            g.listMarkerDelim = itemDelim;
            float columnWidth = kOrderedMarkerColumnDip * fontScale_;
            g.listMarker = LayoutRect{x, y, columnWidth, lineHeight};
            g.listMarkerPad = columnWidth + kCheckboxGapDip * fontScale_;
            listItemOwnContentPad = g.listMarkerPad;
        } else {
            // 无序列表:三档循环——第 1 层实心圆点,第 2 层空心圆,第 3 层(及之后
            // 再循环)实心方块,全部用 D2D 几何图元画,不依赖任何字体字形。
            u32 cyc = (listDepth - 1) % 3;
            float shapeSize = (cyc == 2 ? kListMarkerSquareDip : kListMarkerDotDiameterDip) * fontScale_;
            float boxY = y + (lineHeight - shapeSize) * 0.5f;
            if (boxY < y) boxY = y;
            g.listMarker = LayoutRect{x, boxY, shapeSize, shapeSize};
            g.listMarkerPad = shapeSize + kCheckboxGapDip * fontScale_;
            listItemOwnContentPad = g.listMarkerPad;
        }
    }

    // 紧凑列表项(ListItem)可能直属一段行内文本(紧凑列表首段内容),
    // 这段文本按叶子内容块估算高度;标题/段落/代码块/分割线也是同样的
    // 叶子内容块,没有子块。文档根/列表/引用块永远没有直属行内内容
    // (model.h 注释:容器类块恒为 0),只靠子块堆叠出高度。
    bool hasOwnLeafContent =
        b.type == BlockType::Heading || b.type == BlockType::Paragraph ||
        b.type == BlockType::CodeBlock || b.type == BlockType::ThematicBreak ||
        (b.type == BlockType::ListItem && b.inlineCount > 0);

    // T28:脚注定义(FootnoteDef)本身是容器块(正文走子 Paragraph),但需要在
    // 顶部独占一行画 "[n]" 编号标签,这里给它预留一行高度,渲染层据此画标签、
    // 子块(正文段落)则照常从预留高度之后开始排。
    bool isFootnoteDefLabel = b.type == BlockType::FootnoteDef && b.detailIdx != kInvalidIndex;
    if (isFootnoteDefLabel) {
        g.footnoteId = doc_->footnoteDetails[b.detailIdx].id;
    }

    float cursor = y;
    bool wroteSomething = false;
    if (hasOwnLeafContent) {
        float availableWidth = viewportWidth_ - g.indent - g.listMarkerPad;
        // 代码高亮区文字左右各留 8 DIP 内边距(注意 g.indent 本身不变,背景矩形
        // 仍按 g.indent 起算,和 M0 既有的 Layout_CodeBlockBackgroundGeometry
        // 断言口径保持一致),这里只是收窄"文字可用宽度",与 kCodeBlockRightMarginDip
        // 一起决定真正留给文字折行的宽度;上下各 8 DIP 内边距体现为 cursor
        // 前后各加一份 kCodeBlockPaddingDip。
        if (isCodeBlock) availableWidth -= kCodeBlockRightMarginDip + kCodeBlockPaddingDip * 2.0f;
        if (availableWidth < 1.0f) availableWidth = 1.0f;
        cursor = y + g.textPad + EstimateLeafHeight(b, availableWidth) + g.textPad;
        wroteSomething = true;
    } else if (isFootnoteDefLabel) {
        cursor = y + kFootnoteLabelHeightDip * fontScale_;
        wroteSomething = true;
    }

    // T33:图片 inline 参与块高度计算——在本块直属文本之下纵向堆叠图片/占位块。
    // 尺寸在这一步就定下来("先有尺寸再有位图"),渲染层只负责往矩形里画。
    if (b.inlineCount > 0) {
        float imageLeft = g.indent + g.listMarkerPad;
        float imageAvailWidth = viewportWidth_ - imageLeft;
        float imagesHeight = LayoutImagesForBlock(blockIndex, imageLeft, cursor, imageAvailWidth);
        if (imagesHeight > 0.0f) {
            cursor += imagesHeight;
            wroteSomething = true;
        }
    }

    // T44:本块若是列表容器,给直属子块(应为 ListItem)算好列表符号参数——
    // 层级在容器这一层 +1,有序列表的序号从 OrderedListDetail::start 起递增。
    // 非列表容器时,listDepth 原样透传给子块(既不增也不是重置为 0),
    // 让"容器套容器"之间的中间层(引用块/脚注定义等)不影响列表嵌套计数。
    u32 childListDepth = isListContainer ? (listDepth + 1) : listDepth;
    u32 orderedOrdinalCounter = 1;
    char orderedDelim = '.';
    if (isOrderedContainer && b.detailIdx != kInvalidIndex) {
        const OrderedListDetail& od = doc_->orderedListDetails[b.detailIdx];
        orderedOrdinalCounter = od.start;
        orderedDelim = od.markDelimiter != 0 ? od.markDelimiter : '.';
    }

    u32 child = b.firstChildIdx;
    u32 end = b.firstChildIdx + b.childCount;
    while (child < end) {
        if (wroteSomething) cursor += kBlockVerticalGapDip * fontScale_;  // 兄弟块/自身内容之间留一份间距
        // 表格(T25/T26)整棵子树(TableHead/TableBody/TableRow/单元格)都由
        // LayoutTableSubtree 直接铺开几何,不再走这里的通用容器递归路径。
        // 松散列表项的延续段落等"非嵌套列表"子块要按 listItemOwnContentPad
        // 右移,对齐符号/勾选框让出的文字起点;嵌套列表容器(BulletList/
        // OrderedList)保持不偏移,嵌套缩进只叠加 kListIndentUnitDip 这一份,
        // 不重复叠加父项的符号宽度(否则会破坏既有嵌套缩进的验收断言)。
        bool childIsListContainer = doc_->blocks[child].type == BlockType::BulletList ||
                                     doc_->blocks[child].type == BlockType::OrderedList;
        float thisChildX = (isListItem && listItemOwnContentPad > 0.0f && !childIsListContainer)
                                ? childX + listItemOwnContentPad
                                : childX;

        if (doc_->blocks[child].type == BlockType::Table) {
            cursor = LayoutTableSubtree(child, thisChildX, cursor);
        } else {
            bool childOrdered = false;
            u32 childOrdinal = 0;
            char childDelim = 0;
            if (isListContainer && doc_->blocks[child].type == BlockType::ListItem) {
                childOrdered = isOrderedContainer;
                if (isOrderedContainer) {
                    childOrdinal = orderedOrdinalCounter;
                    childDelim = orderedDelim;
                    orderedOrdinalCounter++;
                }
            }
            cursor = LayoutSubtree(child, thisChildX, cursor, nextInFootnote, childListDepth,
                                   childOrdered, childOrdinal, childDelim);
        }
        wroteSomething = true;
        child = child + 1 + doc_->blocks[child].childCount;
    }

    g.bottom = wroteSomething ? cursor : y;

    if (isQuote) {
        g.quoteBar = LayoutRect{x, g.top, kQuoteBarWidthDip, g.bottom - g.top};
    }
    if (b.type == BlockType::CodeBlock) {
        float bgWidth = viewportWidth_ - x - kCodeBlockRightMarginDip;
        if (bgWidth < 0.0f) bgWidth = 0.0f;
        g.codeBackground = LayoutRect{x, g.top, bgWidth, g.bottom - g.top};

        // 复制按钮:贴在背景矩形右上角,离右边/上边各一份 kCodeBlockPaddingDip。
        // 背景太窄(窗口被拖得极窄)时把按钮夹回背景左边界内,不画到背景外面去。
        float buttonSize = kCodeCopyButtonSizeDip * fontScale_;
        float buttonX = g.codeBackground.x + g.codeBackground.width - kCodeBlockPaddingDip - buttonSize;
        if (buttonX < g.codeBackground.x) buttonX = g.codeBackground.x;
        g.codeCopyButton =
            LayoutRect{buttonX, g.top + kCodeBlockPaddingDip, buttonSize, buttonSize};
    }

    return g.bottom;
}

// 叶子内容块的行数估算:凡是块内出现强制换行(kInlineFlagSyntheticNewline)的
// 地方都必须另起一行,不能按"字符总数 / 平均每行字符数"这种韵文式折行去反推
// 行数(那是给会自动折行的纯文本准备的近似)。否则强制换行被当成 1 个普通
// 字符,估算行数远小于真实渲染行数,预留高度不够,下一个块的 y 会算早,
// 视觉上与本块重叠。
//
// 这个口径对**所有**叶子块类型都成立,不只代码块:
//   - 围栏/缩进代码块:每个源码行各自一行;
//   - 普通段落:软换行(SOFTBR)/硬换行(BR)以及按纯文本退化的 HTML 块
//     (裁决 #1,解析时兜底成 Paragraph)内部的换行,同样是强制换行;
//   - 标题:行尾硬换行同理。
// 单行字符数超过可用宽度时,该行再退化成按字符数估算会被 DirectWrite 二次
// 折行的行数——单行超长仍是近似值,但"多行、每行不太长"这个最常见场景能与
// 真实渲染基本吻合。
//
// 行数与换行个数的关系交给"末尾是否还剩未被换行终止的内容"自然处理,两类
// 块因此共用同一套逻辑:md4c 的 md_process_verbatim_block_contents 对代码块
// **每一行**(含最后一行)都补一个终止换行,所以合成换行个数恰等于源码行数,
// 末尾没有剩余内容、不再补行(否则会多算一行空行);段落里的软/硬换行是
// 行"分隔符",最后一行之后没有换行,末尾剩余内容正好补上那一行。
//
// 图片 run 的文本是 alt,画在图片块内部(T33),不参与正文排版;脚注引用的
// 可见文本是合成的 "[n]",按 4 字符量级估算。这两类 flag 在代码块里不会出现,
// 因此对代码块的行为与改动前完全一致。
static u32 CountLeafLines(const Document& doc, const Block& b, u32 charsPerLine) {
    u32 totalLines = 0;
    u32 curLineChars = 0;
    for (u32 i = 0; i < b.inlineCount; ++i) {
        const Inline& in = doc.inlines[b.firstInlineIdx + i];
        if (in.flags & kInlineFlagSyntheticNewline) {
            u32 wrapped = curLineChars == 0 ? 1 : (curLineChars + charsPerLine - 1) / charsPerLine;
            totalLines += wrapped;
            curLineChars = 0;
        } else if (in.flags & kInlineFlagImage) {
            continue;
        } else if (in.flags & kInlineFlagFootnoteRef) {
            curLineChars += 4;
        } else {
            curLineChars += Utf8VisualWidth(StrSlice{InlineTextBytes(in, doc), in.textLen});
        }
    }
    if (curLineChars > 0) {
        totalLines += (curLineChars + charsPerLine - 1) / charsPerLine;
    }
    return totalLines > 0 ? totalLines : 1;
}

// 可用宽度能放下几个"视觉宽度单位"。加一道极小容差再取整:表格列宽正好是
// "单元格视觉宽度 × 单位宽度 + 两侧内边距",调用方减回内边距求 textWidth 时
// 浮点会掉最后一两个 ulp,不补容差就会把 32.0 个单位算成 31 个,让恰好定义
// 该列宽度的那个单元格凭空多估一行。
static u32 CharsPerLine(float availableWidth, float unitWidth) {
    constexpr float kRoundingTolerance = 0.001f;
    if (unitWidth <= 0.0f) return 1;
    u32 n = static_cast<u32>(availableWidth / unitWidth + kRoundingTolerance);
    return n > 0 ? n : 1;
}

float BlockLayoutEngine::EstimateLeafHeight(const Block& b, float availableWidth,
                                             float lineEstimateSafetyFactor) const {
    if (b.type == BlockType::ThematicBreak) return kThematicBreakHeightDip * fontScale_;

    if (b.type == BlockType::CodeBlock) {
        float lineHeight = kMonoLineHeightDip * fontScale_;
        float avgCharWidth = kMonoAvgCharWidthDip * fontScale_;
        float safeWidth = availableWidth > avgCharWidth ? availableWidth : avgCharWidth;
        u32 charsPerLine = CharsPerLine(safeWidth, avgCharWidth * lineEstimateSafetyFactor);
        u32 lines = CountLeafLines(*doc_, b, charsPerLine);
        return static_cast<float>(lines) * lineHeight + kLeafVerticalPaddingDip * fontScale_;
    }

    u32 totalChars = 0;
    bool hasImage = false;
    for (u32 i = 0; i < b.inlineCount; ++i) {
        const Inline& in = doc_->inlines[b.firstInlineIdx + i];
        if (in.flags & kInlineFlagImage) {
            // 图片 run 的文本是 alt,画在图片/占位块内部(T33),不参与正文排版,
            // 因此也不计入正文高度 —— 否则图片上方会多出一行空白。
            hasImage = true;
            continue;
        }
        if (in.flags & kInlineFlagFootnoteRef) {
            totalChars += 4;  // 合成的可见文本 "[n]" 量级很小,固定按 4 字符估算即可
            continue;
        }
        totalChars += Utf8VisualWidth(StrSlice{InlineTextBytes(in, *doc_), in.textLen});
    }
    // 纯图片段落没有任何正文文字,正文部分高度为 0,整块高度全由图片贡献。
    if (totalChars == 0 && hasImage) return 0.0f;
    // 无文字且无图片的空块:CountLeafLines 下限就是 1 行,这里不需要再补 totalChars。

    float lineHeight = kBaseLineHeightDip * fontScale_;
    float avgCharWidth = kAvgCharWidthDip * fontScale_;
    if (b.type == BlockType::Heading) {
        u32 level = (b.level >= 1 && b.level <= 6) ? b.level : 6;
        float scale = kHeadingScale[level];
        lineHeight *= scale;
        avgCharWidth *= scale;
    }

    float safeWidth = availableWidth > avgCharWidth ? availableWidth : avgCharWidth;
    u32 charsPerLine = CharsPerLine(safeWidth, avgCharWidth * lineEstimateSafetyFactor);
    // 与代码块共用同一个行数口径:块内的强制换行(段落的软/硬换行、按纯文本
    // 退化的 HTML 块内部换行)都会各自另起一行。块内没有任何强制换行时,
    // 这个函数退化成原来的 ceil(总字符数 / 每行字符数),行为不变。
    u32 lines = CountLeafLines(*doc_, b, charsPerLine);
    return static_cast<float>(lines) * lineHeight + kLeafVerticalPaddingDip * fontScale_;
}

void BlockLayoutEngine::ResolveImageSize(StrSlice href, float availableWidth, float* outWidth,
                                          float* outHeight, ImageStatus* outStatus) const {
    float maxWidth = availableWidth > 1.0f ? availableWidth : 1.0f;

    const ImageCacheEntry* entry = imageCache_ ? imageCache_->Find(href) : nullptr;
    if (entry && entry->status == ImageStatus::Ok && entry->width > 0 && entry->height > 0) {
        // 已知真实(降采样后)像素尺寸:1 像素 = 1 DIP,超过可用宽度时等比缩小。
        float w = static_cast<float>(entry->width);
        float h = static_cast<float>(entry->height);
        if (w > maxWidth) {
            h = h * (maxWidth / w);
            w = maxWidth;
        }
        *outWidth = w;
        *outHeight = h > 1.0f ? h : 1.0f;
        *outStatus = ImageStatus::Ok;
        return;
    }

    // 尺寸未知(尚未解码 / 解码失败 / SVG / 网络未加载):用固定占位尺寸,
    // 这样"位图还没来"的阶段几何也是确定的,不会因为解码时机不同而抖动。
    float w = kPlaceholderWidthDip * fontScale_;
    if (w > maxWidth) w = maxWidth;
    *outWidth = w;
    *outHeight = kPlaceholderHeightDip * fontScale_;
    *outStatus = entry ? entry->status : ImageStatus::NotLoaded;
}

float BlockLayoutEngine::LayoutImagesForBlock(u32 blockIndex, float x, float startY,
                                               float availableWidth) {
    const Block& b = doc_->blocks[blockIndex];

    // 先数一遍本块有几张图片,没有就直接返回,不碰 Arena。
    u32 imageCount = 0;
    for (u32 i = 0; i < b.inlineCount; ++i) {
        const Inline& in = doc_->inlines[b.firstInlineIdx + i];
        if ((in.flags & kInlineFlagImage) != 0 && in.linkTargetIdx != kInvalidIndex) imageCount++;
    }
    if (imageCount == 0) return 0.0f;

    ImageBox* boxes = static_cast<ImageBox*>(
        geometryArena_.Alloc(sizeof(ImageBox) * imageCount, alignof(ImageBox)));
    if (!boxes) return 0.0f;  // Arena 耗尽:安全退化为"不画图片",不崩溃

    float gap = kImageVerticalGapDip * fontScale_;
    float cursor = startY;
    u32 slot = 0;
    for (u32 i = 0; i < b.inlineCount && slot < imageCount; ++i) {
        const Inline& in = doc_->inlines[b.firstInlineIdx + i];
        if ((in.flags & kInlineFlagImage) == 0 || in.linkTargetIdx == kInvalidIndex) continue;
        if (in.linkTargetIdx >= doc_->linkTargets.Size()) continue;

        const LinkTarget& target = doc_->linkTargets[in.linkTargetIdx];
        float w = 0.0f, h = 0.0f;
        ImageStatus status = ImageStatus::NotLoaded;
        ResolveImageSize(target.href, availableWidth, &w, &h, &status);

        cursor += gap;
        ImageBox& box = boxes[slot];
        box.rect = LayoutRect{x, cursor, w, h};
        box.alt = StrSlice{in.textLen > 0 ? InlineTextBytes(in, *doc_) : nullptr, in.textLen};
        box.href = target.href;
        box.blockIndex = blockIndex;
        box.linkTargetIdx = in.linkTargetIdx;
        box.kind = target.kind;
        box.status = status;
        cursor += h;
        slot++;
    }

    geometries_[blockIndex].imageBoxes = Span<ImageBox>{boxes, slot};
    return slot > 0 ? (cursor - startY) : 0.0f;
}

bool BlockLayoutEngine::ImagePlacementChanged(const ImageCache& images) const {
    const ImageCache* saved = imageCache_;
    // 临时借用传入的缓存做一次"如果现在重排会得到什么尺寸"的试算,
    // 不改变任何已落地的几何(ResolveImageSize 是纯读函数)。
    const_cast<BlockLayoutEngine*>(this)->imageCache_ = &images;

    bool changed = false;
    for (u32 i = 0; i < geometries_.Size() && !changed; ++i) {
        const BlockGeometry& g = geometries_[i];
        for (u32 k = 0; k < g.imageBoxes.len; ++k) {
            const ImageBox& box = g.imageBoxes[k];
            float availableWidth = viewportWidth_ - g.indent;
            float w = 0.0f, h = 0.0f;
            ImageStatus status = ImageStatus::NotLoaded;
            ResolveImageSize(box.href, availableWidth, &w, &h, &status);
            // 尺寸差半个 DIP 以内视为未变化,避免浮点噪声触发无谓重排。
            float dw = w - box.rect.width;
            float dh = h - box.rect.height;
            if (dw < 0.0f) dw = -dw;
            if (dh < 0.0f) dh = -dh;
            if (dw > 0.5f || dh > 0.5f) { changed = true; break; }
        }
    }

    const_cast<BlockLayoutEngine*>(this)->imageCache_ = saved;
    return changed;
}

float BlockLayoutEngine::LayoutTableSubtree(u32 tableBlockIndex, float x, float y) {
    const Block& tableBlock = doc_->blocks[tableBlockIndex];
    BlockGeometry& tg = geometries_[tableBlockIndex];
    tg.type = tableBlock.type;
    tg.indent = x;
    tg.top = y;
    tg.textLayout = nullptr;

    if (tableBlock.detailIdx == kInvalidIndex) {
        tg.bottom = y;
        return y;
    }
    const TableDetail& td = doc_->tableDetails[tableBlock.detailIdx];
    u32 colCount = td.colCount;
    u32 totalRows = td.headRowCount + td.bodyRowCount;
    if (colCount == 0 || totalRows == 0) {
        tg.bottom = y;
        return y;
    }

    // 用临时 Arena 收集"每行每列的单元格块下标 + 字符数估算",这一次遍历结果
    // 同时供 T25 的列宽算法与后面逐格定位复用,避免重复遍历文档树。
    u32* charCounts = static_cast<u32*>(
        scratchArena_.Alloc(sizeof(u32) * static_cast<size_t>(colCount) * totalRows, alignof(u32)));
    u32* rowBlockIdx =
        static_cast<u32*>(scratchArena_.Alloc(sizeof(u32) * totalRows, alignof(u32)));
    u32* cellBlockIdx = static_cast<u32*>(
        scratchArena_.Alloc(sizeof(u32) * static_cast<size_t>(colCount) * totalRows, alignof(u32)));
    if (!charCounts || !rowBlockIdx || !cellBlockIdx) {
        tg.bottom = y;
        return y;
    }
    for (u32 i = 0; i < colCount * totalRows; ++i) {
        charCounts[i] = 0;
        cellBlockIdx[i] = kInvalidIndex;
    }
    for (u32 i = 0; i < totalRows; ++i) rowBlockIdx[i] = kInvalidIndex;

    u32 rowSlot = 0;
    u32 child = tableBlock.firstChildIdx;
    u32 end = tableBlock.firstChildIdx + tableBlock.childCount;
    while (child < end) {
        const Block& group = doc_->blocks[child];  // TableHead 或 TableBody
        u32 groupEnd = child + 1 + group.childCount;
        u32 row = child + 1;
        while (row < groupEnd) {
            const Block& rowBlock = doc_->blocks[row];
            if (rowBlock.type == BlockType::TableRow && rowSlot < totalRows) {
                rowBlockIdx[rowSlot] = row;
                u32 col = 0;
                u32 cell = row + 1;
                u32 rowEnd = row + 1 + rowBlock.childCount;
                while (cell < rowEnd && col < colCount) {
                    const Block& cellBlock = doc_->blocks[cell];
                    cellBlockIdx[rowSlot * colCount + col] = cell;
                    u32 chars = 0;
                    for (u32 k = 0; k < cellBlock.inlineCount; ++k) {
                        const Inline& cellIn = doc_->inlines[cellBlock.firstInlineIdx + k];
                        // 按视觉宽度而非字节数估算(见 Utf8VisualWidth),避免 CJK 字符
                        // 按 UTF-8 字节数(3 字节/字)虚高,挤压同一行里纯英文列的宽度。
                        chars += Utf8VisualWidth(StrSlice{InlineTextBytes(cellIn, *doc_), cellIn.textLen});
                    }
                    charCounts[rowSlot * colCount + col] = chars;
                    col++;
                    cell = cell + 1 + cellBlock.childCount;
                }
                rowSlot++;
            }
            row = row + 1 + rowBlock.childCount;
        }
        child = groupEnd;
    }

    float availableWidth = viewportWidth_ - x;
    Span<float> colWidths =
        ComputeTableColumnWidths(charCounts, colCount, totalRows, availableWidth, &geometryArena_,
                                 fontScale_);
    tg.tableColWidths = colWidths;
    tg.tableHeadRowCount = td.headRowCount;
    if (colWidths.data == nullptr) {
        tg.bottom = y;
        return y;
    }

    float* rowTops = static_cast<float*>(
        geometryArena_.Alloc(sizeof(float) * (static_cast<size_t>(totalRows) + 1), alignof(float)));
    if (!rowTops) {
        tg.bottom = y;
        return y;
    }

    float rowY = y;
    for (u32 r = 0; r < totalRows; ++r) {
        rowTops[r] = rowY;
        u32 rIdx = rowBlockIdx[r];

        // 该行高度取行内各单元格按其列宽换行后估算高度的最大值。EstimateLeafHeight
        // 内置的是普通段落用的通用块间距 kLeafVerticalPaddingDip,这里替换成表格
        // 专用的 kTableCellVerticalPaddingDip(与横向 kTableCellPaddingDip 对称),
        // 不改动 EstimateLeafHeight 本身(它是段落/代码块共用的通用估算函数)。
        constexpr float kVerticalPaddingDelta =
            kTableCellVerticalPaddingDip - kLeafVerticalPaddingDip;
        float rowHeight = kTableRowMinHeightDip * fontScale_;
        for (u32 c = 0; c < colCount; ++c) {
            u32 cIdx = cellBlockIdx[r * colCount + c];
            if (cIdx == kInvalidIndex) continue;
            float textWidth = colWidths[c] - kTableCellPaddingDip * 2.0f;
            if (textWidth < 1.0f) textWidth = 1.0f;
            float h = EstimateLeafHeight(doc_->blocks[cIdx], textWidth, kTableLineEstimateSafetyFactor) +
                      kVerticalPaddingDelta * fontScale_;
            if (h > rowHeight) rowHeight = h;
        }

        if (rIdx != kInvalidIndex) {
            BlockGeometry& rowG = geometries_[rIdx];
            rowG.type = BlockType::TableRow;
            rowG.top = rowY;
            rowG.bottom = rowY + rowHeight;
            rowG.indent = x;
        }

        float colX = x;
        for (u32 c = 0; c < colCount; ++c) {
            float colWidth = colWidths[c];
            u32 cIdx = cellBlockIdx[r * colCount + c];
            if (cIdx != kInvalidIndex) {
                BlockGeometry& cellG = geometries_[cIdx];
                cellG.type = doc_->blocks[cIdx].type;
                cellG.top = rowY;
                cellG.bottom = rowY + rowHeight;
                cellG.indent = colX + kTableCellPaddingDip;
                cellG.textLayout = nullptr;
                cellG.cellWidth = colWidth - kTableCellPaddingDip * 2.0f;
                if (cellG.cellWidth < 1.0f) cellG.cellWidth = 1.0f;
            }
            colX += colWidth;
        }

        rowY += rowHeight;
    }
    rowTops[totalRows] = rowY;
    tg.tableRowTops = Span<float>{rowTops, totalRows + 1};

    // 表头/表体分组块(TableHead/TableBody)几何:覆盖各自所含行的范围。
    child = tableBlock.firstChildIdx;
    u32 groupRowCursor = 0;
    while (child < end) {
        const Block& group = doc_->blocks[child];
        BlockGeometry& groupG = geometries_[child];
        groupG.type = group.type;
        groupG.indent = x;

        u32 rowsInGroup = 0;
        u32 row = child + 1;
        u32 groupEnd = child + 1 + group.childCount;
        while (row < groupEnd) {
            if (doc_->blocks[row].type == BlockType::TableRow) rowsInGroup++;
            row = row + 1 + doc_->blocks[row].childCount;
        }

        groupG.top = rowTops[groupRowCursor];
        groupRowCursor += rowsInGroup;
        groupG.bottom = rowTops[groupRowCursor];
        child = groupEnd;
    }

    tg.bottom = rowY;
    return rowY;
}

void BlockLayoutEngine::UpdateVisibleRange(float topY, float bottomY, FontSubsystem& fonts,
                                            ImageResidencyController* images) {
    // "可见范围 ± 1 屏"(架构 §5):1 屏 = 视口高度,上下各多留一屏的缓冲区,
    // 目的是滚动时提前/滞后一点淘汰,避免每次微小滚动都抖动式创建/释放。
    float viewportHeight = bottomY - topY;
    if (viewportHeight < 0.0f) viewportHeight = 0.0f;
    float extendedTop = topY - viewportHeight;
    float extendedBottom = bottomY + viewportHeight;

    u32 count = geometries_.Size();
    for (u32 i = 0; i < count; ++i) {
        BlockGeometry& g = geometries_[i];
        const Block& srcBlock = doc_->blocks[i];
        bool inRange = g.bottom > extendedTop && g.top < extendedBottom;

        // T32:图片解码位图与 IDWriteTextLayout 共用同一个进入/离开判据与触发点,
        // 不另起一套虚拟化逻辑。图片块未必携带直属文本,所以放在 BlockHasOwnText
        // 过滤之前处理。
        if (images && g.imageBoxes.len > 0) {
            if (inRange) {
                images->EnsureResident(g.imageBoxes.data, g.imageBoxes.len);
            } else {
                images->ReleaseResident(g.imageBoxes.data, g.imageBoxes.len);
            }
        }

        if (!BlockHasOwnText(srcBlock)) continue;

        if (inRange) {
            if (!g.textLayout) {
                g.textLayout = CreateLayoutForBlock(i, fonts);
            }
        } else if (g.textLayout) {
            // 淘汰时机:块的几何区间与"可见 ± 1 屏"目标区间不再相交。
            g.textLayout->Release();
            g.textLayout = nullptr;
            // linkBoxes/codeHighlights 指向 geometryArena(活到下一次 Relayout),
            // 这里**故意不清空**:清空只会让块再次滚进来时重新分配一份同样的
            // 数据,而 geometryArena 在 Relayout 之间不回收,来回滚动就会单调
            // 增长。读取方(renderer/hit_test)都先判 textLayout 非空,保留这两
            // 个 Span 不会让它们读到本该失效的数据。
        }
    }
}

IDWriteTextLayout* BlockLayoutEngine::CreateLayoutForBlock(u32 blockIndex, FontSubsystem& fonts) {
    const Block& b = doc_->blocks[blockIndex];
    if (b.inlineCount == 0) return nullptr;

    // IDWriteTextLayout 创建时会把文本内容拷贝进内部,调用方缓冲区不需要
    // 在创建之后继续存活,所以这里用一块"每次用前 Reset"的小型临时 Arena。
    scratchArena_.Reset();

    // 第一步:逐 run 转换 UTF-8 -> UTF-16(脚注引用 run 是自包含 span,没有
    // 真实源文本,合成可见的 "[n]" 文本),记下每个 run 对应哪个源 Inline。
    struct RunSlot {
        const wchar_t* text;
        u32 len;
        u32 inlineIndex;
        bool footnoteRef;
    };
    Vec<RunSlot> runs(&scratchArena_);

    u32 totalUtf16 = 0;
    for (u32 i = 0; i < b.inlineCount; ++i) {
        const Inline& in = doc_->inlines[b.firstInlineIdx + i];
        if (in.flags & kInlineFlagFootnoteRef) {
            wchar_t* digits = static_cast<wchar_t*>(
                scratchArena_.Alloc(sizeof(wchar_t) * 12, alignof(wchar_t)));
            if (!digits) continue;
            u32 dn = 0;
            digits[dn++] = L'[';
            dn += FormatDecimalW(in.linkTargetIdx, digits + dn, 10);
            digits[dn++] = L']';
            runs.Push(RunSlot{digits, dn, i, true});
            totalUtf16 += dn;
            continue;
        }
        if (in.flags & kInlineFlagImage) {
            // T33:图片 run 的文本是 alt,由 ImageBox 在图片/占位块内部绘制,
            // 不进正文 layout(否则 alt 会在图片上方重复显示一遍)。
            runs.Push(RunSlot{nullptr, 0, i, false});
            continue;
        }
        if (in.textLen == 0) {
            runs.Push(RunSlot{nullptr, 0, i, false});
            continue;
        }
        Utf16Slice wide =
            Utf8ToUtf16(StrSlice{InlineTextBytes(in, *doc_), in.textLen}, &scratchArena_);
        runs.Push(RunSlot{wide.data, wide.len, i, false});
        totalUtf16 += wide.len;
    }
    if (totalUtf16 == 0) return nullptr;

    // 第二步:拼接进最终缓冲区,同时记下每个 run 在缓冲区里的 [offset, offset+len)
    // 区间,供下面按 run 应用样式(T23)。
    wchar_t* buf = static_cast<wchar_t*>(
        scratchArena_.Alloc(sizeof(wchar_t) * (totalUtf16 + 1), alignof(wchar_t)));
    if (!buf) return nullptr;

    struct RunRange {
        u32 offset;
        u32 len;
        u32 inlineIndex;
        bool footnoteRef;
    };
    Vec<RunRange> ranges(&scratchArena_);
    u32 cursor = 0;
    for (u32 i = 0; i < runs.Size(); ++i) {
        const RunSlot& r = runs[i];
        if (r.len > 0) memcpy(buf + cursor, r.text, sizeof(wchar_t) * r.len);
        ranges.Push(RunRange{cursor, r.len, r.inlineIndex, r.footnoteRef});
        cursor += r.len;
    }
    buf[cursor] = 0;

    BlockGeometry& g = geometries_[blockIndex];
    FontRole role = (b.type == BlockType::CodeBlock) ? FontRole::Mono : FontRole::Body;
    float maxWidth = (g.cellWidth > 0.0f) ? g.cellWidth : (viewportWidth_ - g.indent - g.listMarkerPad);
    // 代码高亮区文字左右各留 8 DIP 内边距,还要扣掉背景本身的右侧留白
    // (kCodeBlockRightMarginDip),与 LayoutSubtree 里 EstimateLeafHeight 用的
    // availableWidth 保持同一份计算口径,避免估算高度和真实折行宽度不一致。
    if (b.type == BlockType::CodeBlock) {
        maxWidth -= kCodeBlockRightMarginDip + kCodeBlockPaddingDip * 2.0f;
    }
    if (maxWidth < 1.0f) maxWidth = 1.0f;

    IDWriteTextLayout* layout = fonts.CreateTextLayout(buf, cursor, role, maxWidth, kMaxTextLayoutHeightDip);
    if (!layout) return nullptr;

    // T53:围栏代码块语法着色——只有语言被识别(languageId != kLanguageNone)才跑
    // 词法器;未识别语言(含缩进代码块,detailIdx 可能为 kInvalidIndex)一行都不
    // 执行,codeHighlights 保持初始的空 Span,渲染层走原有纯色路径。
    // sideDataReady 为 true 说明这个块本轮 Relayout 内已经算过一次,直接沿用
    // 上次的 Span,不重复扫描、也不在 geometryArena 上再占一份空间。
    if (!g.sideDataReady && b.type == BlockType::CodeBlock && b.detailIdx != kInvalidIndex) {
        const CodeBlockDetail& cbd = doc_->codeBlockDetails[b.detailIdx];
        LanguageId langId = static_cast<LanguageId>(cbd.languageId);
        if (langId != kLanguageNone) {
            // 按 ranges 的顺序把各 inline 的原始 UTF-8 字节拼成一份连续的代码
            // 文本喂给词法器,同时记下每段在这份代码文本里的字节区间与它在
            // buf 里对应的 UTF-16 起始偏移(ranges 已经算好),供词法器产出的
            // 字节偏移换算回 UTF-16 偏移。footnote 引用/图片 run 不会出现在
            // 代码块里,这里仍按同样条件过滤以防御性对齐 RunSlot 的构造逻辑。
            Vec<CodeSeg> segs(&scratchArena_);
            u32 totalCodeBytes = 0;
            for (u32 i = 0; i < ranges.Size(); ++i) {
                const RunRange& rr = ranges[i];
                if (rr.footnoteRef || rr.len == 0) continue;
                const Inline& in = doc_->inlines[b.firstInlineIdx + rr.inlineIndex];
                if (in.flags & kInlineFlagImage) continue;
                totalCodeBytes += in.textLen;
            }

            if (totalCodeBytes > 0) {
                char* codeBuf =
                    static_cast<char*>(scratchArena_.Alloc(totalCodeBytes, alignof(char)));
                if (codeBuf) {
                    u32 byteCursor = 0;
                    for (u32 i = 0; i < ranges.Size(); ++i) {
                        const RunRange& rr = ranges[i];
                        if (rr.footnoteRef || rr.len == 0) continue;
                        const Inline& in = doc_->inlines[b.firstInlineIdx + rr.inlineIndex];
                        if (in.flags & kInlineFlagImage) continue;
                        memcpy(codeBuf + byteCursor, InlineTextBytes(in, *doc_), in.textLen);
                        segs.Push(CodeSeg{byteCursor, in.textLen, rr.offset});
                        byteCursor += in.textLen;
                    }

                    const LanguageRule& rule = GetLanguageRule(langId);
                    LexResult lex =
                        LexCodeBlock(StrSlice{codeBuf, byteCursor}, rule, &scratchArena_);

                    Vec<CodeHighlightRun> tempHighlights(&scratchArena_);
                    for (u32 t = 0; t < lex.tokens.len; ++t) {
                        const Token& tok = lex.tokens[t];
                        u32 u16Start = CodeByteOffsetToUtf16(segs.Data(), segs.Size(), codeBuf,
                                                              tok.offset, cursor);
                        u32 u16End = CodeByteOffsetToUtf16(segs.Data(), segs.Size(), codeBuf,
                                                            tok.offset + tok.len, cursor);
                        if (u16End > u16Start) {
                            tempHighlights.Push(CodeHighlightRun{u16Start, u16End - u16Start, tok.type});
                        }
                    }

                    if (tempHighlights.Size() > 0) {
                        CodeHighlightRun* stored = static_cast<CodeHighlightRun*>(
                            geometryArena_.Alloc(sizeof(CodeHighlightRun) * tempHighlights.Size(),
                                                  alignof(CodeHighlightRun)));
                        if (stored) {
                            for (u32 i = 0; i < tempHighlights.Size(); ++i) stored[i] = tempHighlights[i];
                            g.codeHighlights = Span<CodeHighlightRun>{stored, tempHighlights.Size()};
                        }
                    }
                }
            }
        }
    }

    // Bug 2 修复:表格单元格的行高是"该行所有单元格里最高的那个"(见
    // LayoutTableSubtree),单个单元格的文字真实高度往往比它小,记下真实
    // 内容高度供渲染时算垂直居中偏移(布局框本身给了 kMaxTextLayoutHeightDip
    // 那么大的高度,GetMetrics().height 返回的是紧贴文字的真实高度,不是
    // 布局框高度,SetParagraphAlignment 在这个尺寸的布局框内没有实际效果)。
    if (b.type == BlockType::TableHeadCell || b.type == BlockType::TableCell) {
        DWRITE_TEXT_METRICS metrics{};
        g.contentHeight = SUCCEEDED(layout->GetMetrics(&metrics)) ? metrics.height : 0.0f;
    }

    // T26:表头单元格整体加粗;单元格按 CellDetail::align 设置文本对齐。
    if (b.type == BlockType::TableHeadCell) {
        layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_RANGE{0, cursor});
    }
    if ((b.type == BlockType::TableHeadCell || b.type == BlockType::TableCell) &&
        b.detailIdx != kInvalidIndex) {
        CellAlign align = doc_->cellDetails[b.detailIdx].align;
        DWRITE_TEXT_ALIGNMENT ta = DWRITE_TEXT_ALIGNMENT_LEADING;
        if (align == CellAlign::Center) ta = DWRITE_TEXT_ALIGNMENT_CENTER;
        else if (align == CellAlign::Right) ta = DWRITE_TEXT_ALIGNMENT_TRAILING;
        layout->SetTextAlignment(ta);
    }

    IDWriteTextFormat* fmt = fonts.GetTextFormat(role);
    float baseFontSize = fmt ? fmt->GetFontSize() : (16.0f * fontScale_);

    // 标题:按 kHeadingScale 放大字号 + 整体加粗(常见 GFM 渲染惯例,如 GitHub
    // 网页版)。此前只有 EstimateLeafHeight 用 kHeadingScale 算预留高度,真正
    // 绘制用的 IDWriteTextLayout 从未跟着放大/加粗,导致标题视觉上和正文段落
    // 没有任何区别(用户实测发现的真实缺失,不是设计裁决)。
    if (b.type == BlockType::Heading) {
        u32 level = (b.level >= 1 && b.level <= 6) ? b.level : 6;
        float scale = kHeadingScale[level];
        DWRITE_TEXT_RANGE fullRange{0, cursor};
        layout->SetFontSize(baseFontSize * scale, fullRange);
        layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, fullRange);
    }

    // T28:脚注定义区块内文字整体小一号。
    if (g.smallText) {
        layout->SetFontSize(baseFontSize * kFootnoteFontRatio, DWRITE_TEXT_RANGE{0, cursor});
    }

    // T23/T24/T28:按 run 应用粗体/斜体/删除线/行内代码字体/链接下划线/
    // 脚注引用上标,并记录链接 run 供渲染时着色与命中测试共用。
    Vec<LinkBox> tempLinkBoxes(&scratchArena_);
    for (u32 i = 0; i < ranges.Size(); ++i) {
        const RunRange& rr = ranges[i];
        if (rr.len == 0) continue;
        DWRITE_TEXT_RANGE range{rr.offset, rr.len};

        if (rr.footnoteRef) {
            layout->SetFontSize(baseFontSize * kFootnoteRefFontRatio, range);
            IDWriteFactory* factory = fonts.Factory();
            if (factory) {
                IDWriteTypography* typo = nullptr;
                if (SUCCEEDED(factory->CreateTypography(&typo)) && typo) {
                    DWRITE_FONT_FEATURE feature{DWRITE_FONT_FEATURE_TAG_SUPERSCRIPT, 1};
                    typo->AddFontFeature(feature);
                    layout->SetTypography(typo, range);
                    typo->Release();
                }
            }
            continue;  // 合成的可见文本没有对应的粗体/斜体等源样式
        }

        const Inline& in = doc_->inlines[b.firstInlineIdx + rr.inlineIndex];
        if (in.flags & kInlineFlagBold) layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        if (in.flags & kInlineFlagItalic) layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
        if (in.flags & kInlineFlagStrike) layout->SetStrikethrough(TRUE, range);
        if ((in.flags & kInlineFlagCode) && b.type != BlockType::CodeBlock) {
            layout->SetFontFamilyName(fonts.MonoFamily(), range);
        }
        if (in.flags & (kInlineFlagLink | kInlineFlagAutolink)) {
            layout->SetUnderline(TRUE, range);
            if (in.linkTargetIdx != kInvalidIndex) {
                tempLinkBoxes.Push(LinkBox{blockIndex, rr.offset, rr.len, in.linkTargetIdx});
            }
        }
    }

    // 同 codeHighlights:本轮 Relayout 内只落盘一次,块反复滚进滚出时沿用。
    if (!g.sideDataReady && tempLinkBoxes.Size() > 0) {
        LinkBox* stored = static_cast<LinkBox*>(
            geometryArena_.Alloc(sizeof(LinkBox) * tempLinkBoxes.Size(), alignof(LinkBox)));
        if (stored) {
            for (u32 i = 0; i < tempLinkBoxes.Size(); ++i) stored[i] = tempLinkBoxes[i];
            g.linkBoxes = Span<LinkBox>{stored, tempLinkBoxes.Size()};
        }
    }
    g.sideDataReady = true;

    return layout;
}

}  // namespace markair
