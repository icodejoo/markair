// mdvn 的文档模型:md4c 回调解析结果落地成的 Block/Inline 紧凑数组([架构 §7])。
// 不建指针链表 AST,全部数据经由 Arena 分配,换文档时随 Arena 整体 reset。
#pragma once

#include "../util/arena.h"
#include "../util/span.h"
#include "../util/str.h"
#include "../util/types.h"
#include "detail.h"

namespace mdvn {

/** 块级节点类型,只覆盖 M0 范围内的 Markdown 元素(见 05-m0-tasks.md "M0 的边界")。 */
enum class BlockType : u8 {
    Document,      // 文档根节点,始终是 blocks[0]
    Heading,       // 标题,层级见 Block::level(1-6)
    Paragraph,     // 段落
    BulletList,    // 无序列表(<ul>)
    OrderedList,   // 有序列表(<ol>)
    ListItem,      // 列表项(<li>)
    BlockQuote,    // 引用块
    ThematicBreak, // 分割线(<hr>)
    CodeBlock,     // 围栏/缩进代码块
    // 以下为 M1 新增(GFM 表格 + 脚注定义),detail 见 Block::detailIdx。
    Table,              // 表格容器
    TableHead,          // 表头分组(<thead>)
    TableBody,          // 表体分组(<tbody>)
    TableRow,           // 表格行(<tr>)
    TableHeadCell,      // 表头单元格(<th>)
    TableCell,          // 普通单元格(<td>)
    FootnoteDefSection, // 脚注定义区块容器
    FootnoteDef,        // 单条脚注定义
};

/** 行内样式标记位,可组合(如"粗体+行内代码"同时生效时两个位都置 1)。 */
enum InlineFlag : u32 {
    kInlineFlagNone = 0,
    kInlineFlagBold = 1u << 0,   // 粗体(<strong>)
    kInlineFlagItalic = 1u << 1, // 斜体(<em>)
    kInlineFlagCode = 1u << 2,   // 行内代码或代码块内文本
    // 以下为 M1 新增。
    kInlineFlagStrike = 1u << 3,      // 删除线(~~text~~)
    kInlineFlagLink = 1u << 4,        // 链接文本,目标见 Inline::linkTargetIdx
    kInlineFlagImage = 1u << 5,       // 图片替代文本,目标见 Inline::linkTargetIdx
    kInlineFlagAutolink = 1u << 6,    // 自动识别的链接(URL/WWW/邮箱)
    kInlineFlagFootnoteRef = 1u << 7, // 脚注引用([^label])
    // md4c 对软/硬换行(MD_TEXT_SOFTBR/MD_TEXT_BR,含代码块行拼接场景)传入
    // 的是库内部字面量指针 "\n",不在 Document::source 范围内,不能按
    // textOffset 读取。带此标记的 Inline 固定 textLen == 1,渲染/统计时
    // 应视作一个字面 '\n' 字符,但绝不能从 source 读取对应字节
    // (见 parser.cpp::OnText 与 InlineTextBytes)。
    kInlineFlagSyntheticNewline = 1u << 8,
};

// 无效索引哨兵值,表示"不指向任何节点"(M0 未实现链接,linkTargetIdx 始终取此值)。
constexpr u32 kInvalidIndex = 0xFFFFFFFFu;

/**
 * 块级节点,按深度优先前序连续存放于 Document::blocks。
 *
 * 子树语义:[firstChildIdx, firstChildIdx + childCount) 是该节点子树内
 * **全部后代节点**(不含自身)所占的连续区间,而不仅是直属子节点个数。
 * 这是前序扁平数组天然具备的性质:一个节点的所有后代必然紧跟在它自己
 * 后面、在它下一个兄弟节点之前。
 *
 * 遍历"直属子节点"的方法:从 firstChildIdx 开始;处理完当前子节点后,
 * 用 `next = child + 1 + blocks[child].childCount` 跳到下一个兄弟节点,
 * 直到跳出 [firstChildIdx, firstChildIdx + childCount) 区间。
 */
struct Block {
    BlockType type;
    u8 level;            // 标题级别 1-6;其余类型无意义,取 0
    u32 firstInlineIdx;   // 该块直属的行内 run 起始下标;inlineCount 为 0 时无意义
    u32 inlineCount;      // 直属行内 run 个数(容器类块恒为 0,如列表/引用/文档根)
    u32 firstChildIdx;    // 子树起始下标,见上方子树语义说明
    u32 childCount;       // 子树内后代节点总数,见上方子树语义说明
    // M1 新增:指向按类型分表的 detail 侧表下标(表格/单元格对齐/任务项/脚注等),
    // kInvalidIndex 表示该块无 detail。刻意放在 layoutCache 之前,借用其前面
    // 因指针对齐产生的既有 padding,不增大 sizeof(Block)。
    u32 detailIdx = kInvalidIndex;
    void* layoutCache;    // 供后续布局阶段(T10+)缓存 IDWriteTextLayout 等句柄,本任务始终为 nullptr
};

/**
 * 行内样式 run:一段拥有相同样式标记的连续文本。
 * 文本本身零拷贝,textOffset/textLen 是相对 Document::source 的字节偏移,
 * 不复制原文,符合"文本切片直接指向映射内存"的内存策略([架构 §3])。
 */
struct Inline {
    u32 flags;         // InlineFlag 组合
    u32 textOffset;     // 相对 Document::source.data 的字节偏移
    u32 textLen;        // 字节长度
    // 链接目标索引,语义按 flags 区分:
    //  - flags 含 kInlineFlagLink/kInlineFlagImage 时,指向 Document::linkTargets 下标;
    //  - flags 含 kInlineFlagFootnoteRef 时,直接是被引用脚注的 1-based id
    //    (与 FootnoteDetail::id 配对,脚注引用是自包含 span,无需侧表下标);
    //  - 都不含时取 kInvalidIndex。
    u32 linkTargetIdx;
};

/** 链接/图片目标的种类,用于渲染时决定跳转行为(纯 mdvn 自定义,不依赖 md4c)。 */
enum class LinkTargetKind : u8 {
    External,     // 完整 URL(http/https 或其它带 scheme 的外部链接)
    RelativePath, // 相对路径(本地文件/相对链接)
    Anchor,       // 页内锚点(# 开头)
    DataUri,      // data: URI
    Unknown,      // 空 href 或无法判断
};

/** 链接/图片目标表条目(Document::linkTargets),href/title 均为 arena 拥有的展开文本。 */
struct LinkTarget {
    StrSlice href;      // 链接地址或图片 src(已完成实体解码)
    StrSlice title;      // 可选标题,无标题时 len 为 0
    LinkTargetKind kind; // 目标种类
};

/**
 * 一份 Markdown 文档的解析结果:Block/Inline 紧凑数组 + 原始文本引用。
 * 所有数组内存均来自构造时传入的 Arena,不做任何独立堆分配。
 *
 * @example
 *   mdvn::Arena arena;
 *   arena.Init(4 * 1024 * 1024);
 *   mdvn::Document doc = mdvn::ParseMarkdown(mdvn::StrSlice{text, len}, &arena);
 *   if (doc.truncated) { / * 提示"文档过大/过深,已截断" * / }
 */
struct Document {
    Vec<Block> blocks;
    Vec<Inline> inlines;
    // 以下为 M1 新增的按类型侧表,Block::detailIdx / Inline::linkTargetIdx 指向其中的下标。
    Vec<LinkTarget> linkTargets;         // 链接/图片目标(T20)
    Vec<TableDetail> tableDetails;       // 表格附加信息(BlockType::Table)
    Vec<CellDetail> cellDetails;         // 单元格对齐(TableHeadCell/TableCell)
    Vec<ListItemDetail> listItemDetails; // 任务列表项(ListItem,仅 is_task 时分配)
    Vec<OrderedListDetail> orderedListDetails; // 有序列表起始序号/分隔符(T44,BlockType::OrderedList)
    Vec<FootnoteDetail> footnoteDetails; // 脚注定义(FootnoteDef)
    StrSlice source;  // 原始 Markdown 字节,零拷贝引用调用方缓冲区,生命周期由调用方保证
    bool truncated;   // true 表示因超出节点数/嵌套深度上限被安全截断

    // 构造一个空文档,所有数组均绑定到指定 Arena。
    explicit Document(Arena* arena)
        : blocks(arena), inlines(arena), linkTargets(arena), tableDetails(arena),
          cellDetails(arena), listItemDetails(arena), orderedListDetails(arena),
          footnoteDetails(arena), source{nullptr, 0}, truncated(false) {}
};

// sizeof(Block) == 32、sizeof(Inline) == 16(M1 新增 detailIdx 借用了
// layoutCache 前既有的对齐 padding,两者大小相比 M0 均未变化)。

/**
 * 取一个 Inline run 用于渲染/拼接/统计的字节起点。
 *
 * 合成换行(kInlineFlagSyntheticNewline)固定返回指向静态字面量 "\n" 的指针,
 * 绝不读取 Document::source——这类 run 本来就不对应 source 里的任何字节。
 * 其余情况按 textOffset 正常指向 source。任何要按 textLen 拷贝/统计字节的
 * 代码都必须经这个函数取指针,不要直接写 `source.data + textOffset`。
 *
 * @param in 目标 Inline run。
 * @param doc 该 run 所属的文档。
 * @return 长度至少为 in.textLen 的只读字节起点。
 * @example const char* p = InlineTextBytes(in, doc); memcpy(dst, p, in.textLen);
 */
inline const char* InlineTextBytes(const Inline& in, const Document& doc) {
    return (in.flags & kInlineFlagSyntheticNewline) ? "\n" : (doc.source.data + in.textOffset);
}

} // namespace mdvn
