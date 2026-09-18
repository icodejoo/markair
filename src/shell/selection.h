// mdvn 的正文文本拖选状态(T80):鼠标拖选 + Ctrl+C 复制纯文本。
//
// 与 `shell/find.h` 同一口径:**不 include windows.h**,只维护"起点/终点两个
// 文档位置标记"这套轻量状态机,真正的鼠标/键盘消息翻译留在 window.cpp,
// 剪贴板写入复用已有的 clipboard.h。
//
// 选区粒度是 {块下标, 该块 IDWriteTextLayout 文本里的 UTF-16 偏移},不是
// 逐字符标记数组——这样选区状态永远只占几个整数,不会因为"支持跨块选择"
// 就强制把选区涉及的全部块都实例化 IDWriteTextLayout(见架构 §5 的虚拟化
// 约束)。跨块提取纯文本时直接读 `Document::blocks`/`inlines`(解析阶段就
// 已经全量在内存里了,T62 大纲提取用的是同一个前提),不依赖布局层。
#pragma once

#include "../doc/model.h"
#include "../util/arena.h"
#include "../util/str.h"
#include "../util/types.h"

namespace mdvn {

/** 文档里的一个文本位置:块下标 + 该块自身文本内的 UTF-16 偏移。 */
struct DocTextPos {
    u32 blockIndex;
    u32 charOffset;
};

/**
 * 判断两个文档位置的先后顺序。块下标是文档前序遍历的下标,天然与视觉/
 * 阅读顺序一致(见 doc/model.h 的子树语义说明),因此先比块下标,同块内再
 * 比字符偏移即可,不需要反查几何坐标。
 *
 * @param a 位置 A。
 * @param b 位置 B。
 * @return A 严格早于 B 返回 true。
 * @example bool earlier = mdvn::PositionLess({2, 5}, {3, 0}); // true
 */
inline bool PositionLess(const DocTextPos& a, const DocTextPos& b) {
    if (a.blockIndex != b.blockIndex) return a.blockIndex < b.blockIndex;
    return a.charOffset < b.charOffset;
}

/** 一段已排好序的选区(start 恒不晚于 end)。 */
struct SelectionRange {
    DocTextPos start;
    DocTextPos end;
};

/**
 * 把拖选的起点/终点(用户按下和松开时的位置,先后顺序不定——可能从后往前拖)
 * 排成 start <= end 的选区。
 * @param a 一个端点(通常是拖选起点/锚点)。
 * @param b 另一个端点(通常是当前鼠标位置/焦点)。
 * @return 排好序的选区。
 * @example mdvn::SelectionRange r = mdvn::NormalizeSelection(anchor, focus);
 */
inline SelectionRange NormalizeSelection(const DocTextPos& a, const DocTextPos& b) {
    return PositionLess(b, a) ? SelectionRange{b, a} : SelectionRange{a, b};
}

/** 选区是否为空(起止点完全相同,即单击未拖动)。 */
inline bool SelectionEmpty(const SelectionRange& r) {
    return r.start.blockIndex == r.end.blockIndex && r.start.charOffset == r.end.charOffset;
}

/**
 * 拼出一个块**自身直属文本**的纯文本(UTF-8),口径与 `clipboard.h` 的
 * `CodeBlockPlainTextUtf8` 完全一致(必须走 `InlineTextBytes`,不能直接读
 * source),只是不再限定块类型——标题/段落/代码块/表格单元格等任意
 * 携带直属文本的块都适用。容器块(inlineCount == 0,如列表/引用/表格容器)
 * 返回 0,拖选跨越它们时天然被跳过,不需要调用方额外过滤。
 *
 * @param doc 已解析的文档模型。
 * @param blockIndex 目标块下标;越界时返回 0。
 * @param out 输出缓冲区,可为 nullptr(此时只统计长度)。
 * @param cap `out` 的容量(字节),`out` 为 nullptr 时应传 0。
 * @return 该块直属文本的完整字节数(不含结尾 '\0')。
 * @example u32 need = mdvn::BlockOwnPlainTextUtf8(doc, idx, nullptr, 0);
 */
u32 BlockOwnPlainTextUtf8(const Document& doc, u32 blockIndex, char* out, u32 cap);

/**
 * 提取一段跨块选区覆盖的全部纯文本(UTF-8),按文档顺序拼接;不同块之间
 * 用一个换行分隔,块内部保留原有的换行(代码块的合成换行同样保留)。
 * 起止块内部按 UTF-16 偏移精确裁到选区边界,中间经过的块取全部文本。
 *
 * 只读 `Document::blocks`/`inlines`,不需要任何 `IDWriteTextLayout`——
 * 选区跨越当前不在"可见 ± 1 屏"内、没有实例化 layout 的块时,复制内容依旧
 * 完整(架构 §5 虚拟化约束只限制"画高亮"这一视觉效果,不限制取文本内容)。
 *
 * 结果一次性分配在 `arena` 上并整体返回(不是"先问长度再拼"的两段式接口)——
 * 裁边界要走一轮 UTF-8/UTF-16 往返转换,中途结果也落在同一块 Arena 上,
 * 两段式接口会导致第二次调用的内部 `Reset` 冲掉第一次调用期间分配好的
 * 输出缓冲区,因此不采用那套约定。
 *
 * @param doc 已解析的文档模型。
 * @param range 已排好序的选区(`start` 不晚于 `end`)。
 * @param arena 输出与内部转换共用的 Arena,非空;函数入口会整体 `Reset`,
 *        调用方不应在其上保存需要跨调用存活的数据(与
 *        `clipboard.h::CopyCodeBlockToClipboard` 的 scratch 同一口径)。
 * @return 提取结果切片(UTF-8,零结尾,`data` 指向 `arena` 上的内存);
 *         选区为空或 `arena` 为空时 `data` 为 `nullptr`、`len` 为 0。
 * @example
 *   mdvn::StrSlice text = mdvn::SelectionPlainTextUtf8(doc, range, &scratch);
 *   if (text.data) { / * 转 UTF-16 写剪贴板 * / }
 */
StrSlice SelectionPlainTextUtf8(const Document& doc, const SelectionRange& range, Arena* arena);

/**
 * 鼠标拖选的运行期状态机:锚点(按下位置)+ 焦点(当前/松开时位置)。
 * 与 `FindSession` 同一口径:纯状态,不含任何 Win32/D2D 调用,可直接单测。
 *
 * @example
 *   mdvn::SelectionState sel;
 *   sel.Begin({0, 3});
 *   sel.Update({2, 1});
 *   if (sel.HasSelection()) { / * 画高亮 / Ctrl+C 时取 sel.Range() * / }
 */
class SelectionState {
public:
    // 构造一个空选区(未拖选)。
    SelectionState() : anchor_{0, 0}, focus_{0, 0}, active_(false) {}

    /**
     * 开始一次拖选(`WM_LBUTTONDOWN`):锚点与焦点都设成按下位置,此时选区
     * 仍为空(单击未拖动时应视作"没有选区",由 `HasSelection` 体现)。
     * @param pos 按下位置。
     */
    void Begin(const DocTextPos& pos) {
        anchor_ = pos;
        focus_ = pos;
        active_ = true;
    }

    /**
     * 拖选过程中更新焦点(`WM_MOUSEMOVE`,左键按住时)。未先调用 `Begin`
     * 时忽略,不产生任何状态变化。
     * @param pos 当前鼠标位置。
     */
    void Update(const DocTextPos& pos) {
        if (!active_) return;
        focus_ = pos;
    }

    /** 结束一次拖选(`WM_LBUTTONUP`):选区本身保留,直到用户点别处清除。 */
    void End() { active_ = false; }

    /** 清除选区(点击文档任意位置/按 Esc 等)。 */
    void Clear() {
        anchor_ = DocTextPos{0, 0};
        focus_ = DocTextPos{0, 0};
        active_ = false;
    }

    /** 当前是否存在非空选区(拖动了至少一个字符),供高亮绘制/Ctrl+C 判断。 */
    bool HasSelection() const { return !SelectionEmpty(NormalizeSelection(anchor_, focus_)); }

    /** 是否正处于拖选过程中(左键仍按住)。 */
    bool IsDragging() const { return active_; }

    /** 取排好序的选区;没有选区时返回一个空区间(start == end)。 */
    SelectionRange Range() const { return NormalizeSelection(anchor_, focus_); }

private:
    DocTextPos anchor_;  // 拖选起点(按下时的位置)
    DocTextPos focus_;   // 拖选终点(当前/松开时的位置)
    bool active_;        // 左键是否仍按住(拖选进行中)
};

}  // namespace mdvn
