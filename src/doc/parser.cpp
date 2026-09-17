// mdvn 的 md4c SAX 回调 -> 文档模型转换器(T8,M1 扩展 GFM 表格/脚注/链接)实现。
#include "parser.h"

#include "attr.h"

#include "../../third_party/md4c/md4c.h"

namespace mdvn {

namespace {

// 解析过程中的可变状态,通过 userdata 指针在各回调间传递。
// 生命周期只覆盖单次 ParseMarkdown 调用,不持有任何需要显式释放的资源。
struct ParseContext {
    Document* doc;
    Arena* arena;   // 与 doc 内各 Vec 共用同一个 Arena,供 ExpandAttribute 使用
    u32 blockStack[kMaxNestingDepth + 1]; // 当前"块"祖先链的下标栈,深度受 kMaxNestingDepth 约束
    u32 blockStackSize;
    // 当前处于多少层链接/图片 span 之内(link/image 可以互相嵌套,如链接内嵌图片),
    // 与 linkTargetStack 配合使用:targetStackSize 始终等于 linkNest + imageNest。
    u32 targetStack[kMaxNestingDepth + 1]; // 当前生效的链接/图片目标下标栈(Document::linkTargets)
    u32 targetStackSize;
    u32 depth;      // 当前块级 + 行内 span 合计嵌套深度
    u32 boldNest;    // 当前处于多少层 <strong> 之内
    u32 italicNest;  // 当前处于多少层 <em> 之内
    u32 codeNest;    // 当前处于多少层行内代码 span 之内
    u32 strikeNest;  // 当前处于多少层删除线 <del> 之内
    u32 linkNest;    // 当前处于多少层链接 <a> 之内
    u32 imageNest;   // 当前处于多少层图片 span 之内
    u32 autolinkNest; // 当前处于多少层"自动识别链接"之内(is_autolink 的 <a>)
    u32 nodeCount;   // 已写入的 Block + Inline 节点总数
    // 进入 MD_SPAN_IMG 时的 inlines 总数快照:离开该 span 时如果一个 Inline 都没多出来,
    // 说明这张图**没有 alt 文本**(md4c 只在有 alt 时才回调 MD_TEXT),需要在 leave 里
    // 补一个零长度的图片 Inline,否则 T33 的布局层看不到这张图。有 alt 的图片走原路径,
    // 行为与 M1 之前完全一致(不改动任何既有快照)。
    u32 imgEnterInlineCount;
    bool aborted;    // 已触发截断,后续回调直接返回非零让 md4c 停止解析

    ParseContext(Document* d, Arena* a)
        : doc(d), arena(a), blockStackSize(0), targetStackSize(0), depth(0), boldNest(0),
          italicNest(0), codeNest(0), strikeNest(0), linkNest(0), imageNest(0), autolinkNest(0),
          nodeCount(0), imgEnterInlineCount(0), aborted(false) {}

    // 当前生效的行内样式标记(可同时组合生效)。
    u32 CurrentFlags() const {
        u32 f = kInlineFlagNone;
        if (boldNest > 0) f |= kInlineFlagBold;
        if (italicNest > 0) f |= kInlineFlagItalic;
        if (codeNest > 0) f |= kInlineFlagCode;
        if (strikeNest > 0) f |= kInlineFlagStrike;
        if (linkNest > 0) f |= kInlineFlagLink;
        if (imageNest > 0) f |= kInlineFlagImage;
        if (autolinkNest > 0) f |= kInlineFlagAutolink;
        return f;
    }

    // 当前生效的链接/图片目标下标(最内层的 <a>/img),都不在其中时为 kInvalidIndex。
    u32 CurrentTargetIdx() const {
        return targetStackSize > 0 ? targetStack[targetStackSize - 1] : kInvalidIndex;
    }

    // 节点数是否已达上限(在真正写入一个新节点之前调用)。
    bool AtNodeLimit() const { return nodeCount >= kMaxDocumentNodeCount; }

    // 统一的"触发截断"处理:置位标记,后续回调据此提前返回。
    void Truncate() {
        doc->truncated = true;
        aborted = true;
    }
};

// md4c 的 MD_ALIGN -> mdvn::CellAlign 映射。
CellAlign ToCellAlign(MD_ALIGN a) {
    switch (a) {
        case MD_ALIGN_LEFT: return CellAlign::Left;
        case MD_ALIGN_CENTER: return CellAlign::Center;
        case MD_ALIGN_RIGHT: return CellAlign::Right;
        case MD_ALIGN_DEFAULT:
        default: return CellAlign::Default;
    }
}

// md4c 的 MD_BLOCKTYPE -> mdvn::BlockType 映射;level 仅 MD_BLOCK_H 有意义,由调用方另行填充。
BlockType ToBlockType(MD_BLOCKTYPE t) {
    switch (t) {
        case MD_BLOCK_DOC: return BlockType::Document;
        case MD_BLOCK_QUOTE: return BlockType::BlockQuote;
        case MD_BLOCK_UL: return BlockType::BulletList;
        case MD_BLOCK_OL: return BlockType::OrderedList;
        case MD_BLOCK_LI: return BlockType::ListItem;
        case MD_BLOCK_HR: return BlockType::ThematicBreak;
        case MD_BLOCK_H: return BlockType::Heading;
        case MD_BLOCK_CODE: return BlockType::CodeBlock;
        case MD_BLOCK_P: return BlockType::Paragraph;
        case MD_BLOCK_TABLE: return BlockType::Table;
        case MD_BLOCK_THEAD: return BlockType::TableHead;
        case MD_BLOCK_TBODY: return BlockType::TableBody;
        case MD_BLOCK_TR: return BlockType::TableRow;
        case MD_BLOCK_TH: return BlockType::TableHeadCell;
        case MD_BLOCK_TD: return BlockType::TableCell;
        case MD_BLOCK_FOOTNOTE_DEF_SECTION: return BlockType::FootnoteDefSection;
        case MD_BLOCK_FOOTNOTE_DEF: return BlockType::FootnoteDef;
        default:
            // HTML 块(裁决 #1 继续不启用直通渲染)等未覆盖类型,兜底按段落处理,
            // 不崩溃、不丢数据。
            return BlockType::Paragraph;
    }
}

// enter_block 回调:压入一个新 Block 节点,检查深度/节点数上限。
int OnEnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
    ParseContext* ctx = static_cast<ParseContext*>(userdata);
    if (ctx->aborted) return 1;

    ctx->depth++;
    if (ctx->depth > kMaxNestingDepth || ctx->AtNodeLimit()) {
        ctx->Truncate();
        return 1; // 非零返回值让 md4c 中止后续解析
    }

    Block b{};
    b.type = ToBlockType(type);
    b.level = 0;
    if (type == MD_BLOCK_H && detail != nullptr) {
        b.level = static_cast<u8>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
    }
    b.firstInlineIdx = 0; // 容器块恒为 0;有直属文本的块在首次 OnText 时会被改写
    b.inlineCount = 0;
    b.firstChildIdx = ctx->doc->blocks.Size() + 1; // 自身入栈后紧邻的下一个位置就是首个子节点(若有)
    b.childCount = 0;
    b.layoutCache = nullptr;
    // b.detailIdx 已在 Block 的默认成员初始化里取 kInvalidIndex,下面按类型选择性覆写。

    // M1:表格/单元格对齐/任务列表/脚注定义,把 md4c 的 detail 结构体落地到按类型侧表,
    // 下标记到 Block::detailIdx。这里发生在通用的"压入 Block"路径之前,与其它块类型
    // 共享同一套深度/节点数上限检查(上面已经做过),截断逻辑天然覆盖这些新类型。
    switch (type) {
        case MD_BLOCK_TABLE:
            if (detail != nullptr) {
                auto* d = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
                TableDetail td{static_cast<u32>(d->col_count), static_cast<u32>(d->head_row_count),
                                static_cast<u32>(d->body_row_count)};
                b.detailIdx = ctx->doc->tableDetails.Size();
                if (!ctx->doc->tableDetails.Push(td)) {
                    ctx->Truncate();
                    return 1;
                }
            }
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            if (detail != nullptr) {
                auto* d = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
                CellDetail cd{ToCellAlign(d->align)};
                b.detailIdx = ctx->doc->cellDetails.Size();
                if (!ctx->doc->cellDetails.Push(cd)) {
                    ctx->Truncate();
                    return 1;
                }
            }
            break;
        case MD_BLOCK_OL:
            if (detail != nullptr) {
                auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
                OrderedListDetail old{static_cast<u32>(d->start), d->mark_delimiter};
                b.detailIdx = ctx->doc->orderedListDetails.Size();
                if (!ctx->doc->orderedListDetails.Push(old)) {
                    ctx->Truncate();
                    return 1;
                }
            }
            break;
        case MD_BLOCK_LI:
            if (detail != nullptr) {
                auto* d = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
                if (d->is_task) {
                    ListItemDetail lid{true, d->task_mark == 'x' || d->task_mark == 'X'};
                    b.detailIdx = ctx->doc->listItemDetails.Size();
                    if (!ctx->doc->listItemDetails.Push(lid)) {
                        ctx->Truncate();
                        return 1;
                    }
                }
            }
            break;
        case MD_BLOCK_FOOTNOTE_DEF:
            if (detail != nullptr) {
                auto* d = static_cast<MD_BLOCK_FOOTNOTE_DEF_DETAIL*>(detail);
                FootnoteDetail fd{static_cast<u32>(d->id), static_cast<u32>(d->ref_count),
                                   ExpandAttribute(d->label, ctx->arena)};
                b.detailIdx = ctx->doc->footnoteDetails.Size();
                if (!ctx->doc->footnoteDetails.Push(fd)) {
                    ctx->Truncate();
                    return 1;
                }
            }
            break;
        default:
            break;
    }

    u32 idx = ctx->doc->blocks.Size();
    if (!ctx->doc->blocks.Push(b)) {
        ctx->Truncate(); // Arena 耗尽,同样按截断处理,不崩溃
        return 1;
    }
    ctx->nodeCount++;

    // blockStackSize 只在 depth 检查通过之后才增长,且始终 <= depth <= kMaxNestingDepth,
    // 不会超出 blockStack 数组容量。
    ctx->blockStack[ctx->blockStackSize++] = idx;
    return 0;
}

// leave_block 回调:出栈,把子树内的后代节点总数回填到 childCount。
int OnLeaveBlock(MD_BLOCKTYPE /*type*/, void* /*detail*/, void* userdata) {
    ParseContext* ctx = static_cast<ParseContext*>(userdata);
    if (ctx->aborted) return 1;

    if (ctx->blockStackSize > 0) {
        u32 idx = ctx->blockStack[--ctx->blockStackSize];
        Block& b = ctx->doc->blocks[idx];
        u32 endIdx = ctx->doc->blocks.Size();
        b.childCount = endIdx - b.firstChildIdx;
    }
    if (ctx->depth > 0) ctx->depth--;
    return 0;
}

// 把一个链接/图片目标 push 进 linkTargets,并将其下标压入 targetStack。
// 失败(Arena 耗尽)时触发截断,返回 false。
bool PushLinkTarget(ParseContext* ctx, StrSlice href, StrSlice title) {
    LinkTarget lt{href, title, ClassifyLinkTarget(href)};
    u32 idx = ctx->doc->linkTargets.Size();
    if (!ctx->doc->linkTargets.Push(lt)) {
        ctx->Truncate();
        return false;
    }
    ctx->targetStack[ctx->targetStackSize++] = idx;
    return true;
}

// enter_span 回调:进入粗体/斜体/行内代码/删除线/链接/图片/脚注引用等样式区间。
// 链接/图片会产生一条 linkTargets 记录;脚注引用是自包含 span(无子 text
// 回调),直接在这里合成一个零长度 Inline 节点。
int OnEnterSpan(MD_SPANTYPE type, void* detail, void* userdata) {
    ParseContext* ctx = static_cast<ParseContext*>(userdata);
    if (ctx->aborted) return 1;

    ctx->depth++;
    if (ctx->depth > kMaxNestingDepth) {
        ctx->Truncate();
        return 1;
    }

    switch (type) {
        case MD_SPAN_STRONG: ctx->boldNest++; break;
        case MD_SPAN_EM: ctx->italicNest++; break;
        case MD_SPAN_CODE: ctx->codeNest++; break;
        case MD_SPAN_DEL: ctx->strikeNest++; break;
        case MD_SPAN_A: {
            auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail);
            StrSlice href = ExpandAttribute(d->href, ctx->arena);
            StrSlice title = ExpandAttribute(d->title, ctx->arena);
            if (!PushLinkTarget(ctx, href, title)) return 1;
            ctx->linkNest++;
            if (d->is_autolink) ctx->autolinkNest++;
            break;
        }
        case MD_SPAN_IMG: {
            auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
            StrSlice src = ExpandAttribute(d->src, ctx->arena);
            StrSlice title = ExpandAttribute(d->title, ctx->arena);
            if (!PushLinkTarget(ctx, src, title)) return 1;
            ctx->imageNest++;
            ctx->imgEnterInlineCount = ctx->doc->inlines.Size();
            break;
        }
        case MD_SPAN_FOOTNOTE_REF: {
            // 自包含 span:没有子 MD_TEXT 回调,这里直接合成归属当前块的 Inline 节点。
            if (ctx->AtNodeLimit()) {
                ctx->Truncate();
                return 1;
            }
            if (ctx->blockStackSize > 0) {
                auto* d = static_cast<MD_SPAN_FOOTNOTE_REF_DETAIL*>(detail);
                u32 blockIdx = ctx->blockStack[ctx->blockStackSize - 1];
                Block& b = ctx->doc->blocks[blockIdx];

                Inline in{};
                in.flags = ctx->CurrentFlags() | kInlineFlagFootnoteRef;
                in.textOffset = 0;
                in.textLen = 0;
                in.linkTargetIdx = static_cast<u32>(d->id); // 直接存脚注 id,见 Inline::linkTargetIdx 注释

                if (!ctx->doc->inlines.Push(in)) {
                    ctx->Truncate();
                    return 1;
                }
                ctx->nodeCount++;
                if (b.inlineCount == 0) b.firstInlineIdx = ctx->doc->inlines.Size() - 1;
                b.inlineCount++;
            }
            break;
        }
        default:
            // M1 未启用的其它 span 扩展(wiki 链接/下划线/剧透等)不会被触发到此分支。
            break;
    }
    return 0;
}

// leave_span 回调:退出样式区间,回退计数器/目标栈。
// MD_SPAN_A 的 detail 在 enter/leave 两侧是同一份数据(md4c 内部实现如此,
// 见 md4c.c 的 md_enter_leave_span_a),这里借助它精确回退 autolinkNest,
// 避免"链接内嵌图片"等场景下计数被误减。
int OnLeaveSpan(MD_SPANTYPE type, void* detail, void* userdata) {
    ParseContext* ctx = static_cast<ParseContext*>(userdata);
    if (ctx->aborted) return 1;

    switch (type) {
        case MD_SPAN_STRONG: if (ctx->boldNest > 0) ctx->boldNest--; break;
        case MD_SPAN_EM: if (ctx->italicNest > 0) ctx->italicNest--; break;
        case MD_SPAN_CODE: if (ctx->codeNest > 0) ctx->codeNest--; break;
        case MD_SPAN_DEL: if (ctx->strikeNest > 0) ctx->strikeNest--; break;
        case MD_SPAN_A: {
            if (ctx->linkNest > 0) ctx->linkNest--;
            auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail);
            if (d->is_autolink && ctx->autolinkNest > 0) ctx->autolinkNest--;
            if (ctx->targetStackSize > 0) ctx->targetStackSize--;
            break;
        }
        case MD_SPAN_IMG: {
            // 无 alt 文本的图片(`![](a.png)`)在整个 span 期间不会有任何 MD_TEXT
            // 回调,此处补一个零长度的图片 Inline,让 T33 的布局层能看到这张图。
            // 有 alt 的图片这里什么都不做,既有行为与快照完全不变。
            if (ctx->doc->inlines.Size() == ctx->imgEnterInlineCount &&
                ctx->blockStackSize > 0 && !ctx->AtNodeLimit()) {
                u32 blockIdx = ctx->blockStack[ctx->blockStackSize - 1];
                Block& b = ctx->doc->blocks[blockIdx];
                Inline in{};
                in.flags = ctx->CurrentFlags();  // 此时 imageNest 尚未回退,已含 kInlineFlagImage
                in.textOffset = 0;
                in.textLen = 0;
                in.linkTargetIdx = ctx->CurrentTargetIdx();
                if (!ctx->doc->inlines.Push(in)) {
                    ctx->Truncate();
                    return 1;
                }
                ctx->nodeCount++;
                if (b.inlineCount == 0) b.firstInlineIdx = ctx->doc->inlines.Size() - 1;
                b.inlineCount++;
            }
            if (ctx->imageNest > 0) ctx->imageNest--;
            if (ctx->targetStackSize > 0) ctx->targetStackSize--;
            break;
        }
        default: break;
    }
    if (ctx->depth > 0) ctx->depth--;
    return 0;
}

// text 回调:把一段文本落地成一个 Inline run,归属到当前栈顶的块。
int OnText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    ParseContext* ctx = static_cast<ParseContext*>(userdata);
    if (ctx->aborted) return 1;
    if (ctx->AtNodeLimit()) {
        ctx->Truncate();
        return 1;
    }
    if (ctx->blockStackSize == 0) return 0; // 防御性处理:理论上文本总是出现在某个块内

    u32 blockIdx = ctx->blockStack[ctx->blockStackSize - 1];
    Block& b = ctx->doc->blocks[blockIdx];

    u32 flags = ctx->CurrentFlags();
    if (b.type == BlockType::CodeBlock) {
        // 围栏/缩进代码块的文本不经过 span 回调,这里显式补上 Code 标记。
        flags |= kInlineFlagCode;
    }

    // 边界防御:md4c 并非总是把 text 指向 source 内部——软/硬换行
    // (MD_TEXT_SOFTBR/MD_TEXT_BR)、代码块行拼接、空字符替换(MD_TEXT_NULLCHAR)
    // 等场景会传入库内部的字面量字符串指针(如 "\n"),不在 source 地址范围内。
    // 这类文本若仍按"相对 source 的偏移"记录,后续 source.data + textOffset
    // 会算出野指针,真正读取字节时(如 layout 阶段拼接文本)直接崩溃。
    bool textInSource =
        text >= ctx->doc->source.data &&
        text < ctx->doc->source.data + ctx->doc->source.len &&
        static_cast<u32>(size) <= ctx->doc->source.len &&
        static_cast<u32>(text - ctx->doc->source.data) <= ctx->doc->source.len - static_cast<u32>(size);

    // T11 的野指针防御不能开倒车,但"这段不在 source 里的文本,内容其实就是
    // 单个换行符"这件事本身是可以安全判断的——text 指针虽不在 source 范围内,
    // 但仍是 md4c 传入的、保证可读 size 字节的合法内存(要么指向 source,要么
    // 指向库内部的字面量/静态缓冲,两种情况下读取 [text, text+size) 都是安全
    // 的,只是不能再当成"相对 source 的偏移"去用)。md4c 对软/硬换行
    // (MD_TEXT_SOFTBR/MD_TEXT_BR)以及围栏/缩进代码块"每行结尾补一个换行"
    // (md_process_verbatim_block_contents,type 固定是 MD_TEXT_CODE,不是
    // BR/SOFTBR)统一传入库内部字面量 "\n"(size == 1),这里按内容而非
    // type 判断,才能同时覆盖这两类场景。命中时不读那个不安全的指针,而是
    // 合成一个安全的"换行" Inline:textOffset 归零(不使用)、textLen 固定
    // 为 1、标记 kInlineFlagSyntheticNewline,渲染/统计时统一通过
    // InlineTextBytes() 取字面量 "\n",不接触 source。其余"不在 source 里"
    // 场景(如 MD_TEXT_NULLCHAR 的空字符替换、可折叠空格)维持原有归零行为,
    // 不引入新的假设。
    // 额外用 type 做一层限定(而不是彻底忽略这个参数):只在已知会真正合成
    // "\n" 字面量的四种回调类型上生效,避免未来 md4c 版本升级后,某个偶然也传
    // 单字节 "\n" 但语义完全不同的新场景被误判成换行。
    bool syntheticNewline = !textInSource && size == 1 && text[0] == '\n' &&
        (type == MD_TEXT_SOFTBR || type == MD_TEXT_BR ||
         type == MD_TEXT_CODE || type == MD_TEXT_HTML);

    // md4c 的 md_process_verbatim_block_contents(围栏/缩进代码块、HTML 块
    // 逐行输出)用它自己的静态字面量缓冲区(16 个空格一组,一次最多吐 16
    // 字节)拼每行的前导缩进,同样不指向 source。此前这类 run 落进上面
    // syntheticNewline 判断不到的分支,被当成"不在 source 里"一律归零,
    // 代码块缩进因此整体丢失(真实 bug,2026-09-17 用户实测发现;见
    // model.h::kInlineFlagSyntheticSpaces 的完整注释)。size 上限 16 与
    // md4c 的分块大小一致,不会越界;只在已知会真正合成缩进的两种块类型
    // (代码块/HTML 块)上生效,同样避免未来 md4c 版本里语义不同的场景被误判。
    bool syntheticSpaces = false;
    if (!textInSource && !syntheticNewline && size >= 1 && size <= 16 &&
        (type == MD_TEXT_CODE || type == MD_TEXT_HTML)) {
        syntheticSpaces = true;
        for (MD_SIZE k = 0; k < size; ++k) {
            if (text[k] != ' ') { syntheticSpaces = false; break; }
        }
    }

    Inline in{};
    in.flags = flags | (syntheticNewline ? static_cast<u32>(kInlineFlagSyntheticNewline) : 0u) |
               (syntheticSpaces ? static_cast<u32>(kInlineFlagSyntheticSpaces) : 0u);
    in.textOffset = textInSource ? static_cast<u32>(text - ctx->doc->source.data) : 0;
    in.textLen = textInSource ? static_cast<u32>(size)
                 : syntheticNewline ? 1u
                 : syntheticSpaces ? static_cast<u32>(size)
                 : 0u;
    in.linkTargetIdx = ctx->CurrentTargetIdx();

    if (!ctx->doc->inlines.Push(in)) {
        ctx->Truncate(); // Arena 耗尽,按截断处理
        return 1;
    }
    ctx->nodeCount++;

    if (b.inlineCount == 0) b.firstInlineIdx = ctx->doc->inlines.Size() - 1;
    b.inlineCount++;
    return 0;
}

} // namespace

Document ParseMarkdown(StrSlice source, Arena* arena) {
    Document doc(arena);
    doc.source = source;

    // M1 在 CommonMark 基础上启用 GFM 扩展子集:表格、删除线、任务列表、
    // 宽松自动链接(URL/WWW/邮箱三种)、脚注引用。MD_FLAG_NOHTML 继续保持
    // 启用(裁决 #1):HTML 仍按普通文本处理,不新增 HtmlBlock/kInlineFlagHtml。
    // 明确不启用 MD_FLAG_LATEXMATHSPANS([裁决 #2]);表格已于 M1 纳入,原先
    // 锁定"不启用 MD_FLAG_TABLES"的 static_assert 已删除。
    // 下面的 static_assert 是编译期硬性校验:即便日后有人改动这行,只要不小心
    // 带上被裁定排除的位,编译就会直接失败,不依赖运行期断言。
    constexpr unsigned kParserFlags = MD_DIALECT_COMMONMARK | MD_FLAG_NOHTML |
                                       MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
                                       MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEAUTOLINKS |
                                       MD_FLAG_FOOTNOTES;
    static_assert((kParserFlags & MD_FLAG_LATEXMATHSPANS) == 0,
                  "裁决 #2:禁止启用 MD_FLAG_LATEXMATHSPANS");
    // M1 明确不启用的一组扩展(wiki 链接/下划线/剧透/上下标/警示块/高亮/插入),
    // 防止日后顺手加回。
    constexpr unsigned kM1ExcludedFlags = MD_FLAG_WIKILINKS | MD_FLAG_UNDERLINE |
                                           MD_FLAG_SPOILERS | MD_FLAG_SUPERSCRIPTS |
                                           MD_FLAG_SUBSCRIPTS | MD_FLAG_ADMONITIONS |
                                           MD_FLAG_HIGHLIGHT | MD_FLAG_INSERT;
    static_assert((kParserFlags & kM1ExcludedFlags) == 0,
                  "M1 边界:这组 GFM 扩展明确不启用");

    if (source.data == nullptr || source.len == 0) {
        return doc; // 空输入,返回空文档,不调用 md_parse
    }

    ParseContext ctx(&doc, arena);

    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = kParserFlags;
    parser.enter_block = &OnEnterBlock;
    parser.leave_block = &OnLeaveBlock;
    parser.enter_span = &OnEnterSpan;
    parser.leave_span = &OnLeaveSpan;
    parser.text = &OnText;
    parser.debug_log = nullptr;
    parser.syntax = nullptr;

    // md_parse 的返回值:0 成功,-1 运行期错误,回调返回的非零值(截断中止时)。
    // 三种情况我们都已经把能拿到的部分结果留在 doc 里,统一按"返回可用文档"处理,
    // 不需要再区分返回码(截断与否已经记录在 doc.truncated)。
    md_parse(source.data, static_cast<MD_SIZE>(source.len), &parser, &ctx);

    return doc;
}

} // namespace mdvn
