// mdvn 的 `Ctrl+F` 查找条状态(T37 的 UI 层)。
//
// 与 `shell/scroll.h` / `shell/hit_test.h` 同一口径:**不 include windows.h**,
// 这里只维护"查询串 + 命中集合 + 当前命中下标"这套纯状态机,真正的按键翻译
// (`Ctrl+F` / `Enter` / `Shift+Enter` / `F3` / `Esc`)与绘制留在 window.cpp /
// renderer.cpp,因此本文件可以直接单测。
//
// 交互口径(§9 "内容区保持极简" + 裁决 #7):顶部浮出一条轻量查找条,增量输入
// 即时重搜,**没有**全字匹配/大小写开关这类附加 UI 状态。
#pragma once

#include "../doc/search.h"
#include "../util/arena.h"
#include "../util/span.h"
#include "../util/types.h"

namespace mdvn {

// 查询串的最大长度(UTF-16 code unit,不含结尾 '\0')。查找条不是编辑器,
// 固定小缓冲即可,顺带天然挡住"超长关键词"这类退化输入。
constexpr u32 kMaxFindQueryChars = 128;

/**
 * 一次查找会话:查询串 + 全部命中 + 当前命中下标。
 *
 * 内存口径:命中数组挂在调用方给的 Arena 上,每次重搜整体 `Reset` 后重来,
 * 不做增量维护、不使用任何 STL 容器。
 *
 * @example
 *   mdvn::FindSession find(&findArena, &findScratch);
 *   find.Open();
 *   find.AppendChar(L't');
 *   find.Rerun(doc);
 *   const mdvn::Match* m = find.CurrentMatch();
 */
class FindSession {
public:
    /**
     * 绑定两块 Arena。
     * @param results 存放命中数组与 UTF-8 查询串,每次 `Rerun` 前整体 Reset。
     * @param scratch 传给 `SearchDocument` 的逐块拼接缓冲,与 results 必须是
     *        不同的两块(搜索过程中会反复 Reset scratch)。
     * @example mdvn::FindSession find(&findArena, &findScratch);
     */
    FindSession(Arena* results, Arena* scratch);

    FindSession(const FindSession&) = delete;
    FindSession& operator=(const FindSession&) = delete;

    /** 打开查找条(`Ctrl+F`);已打开时保留原查询串,只是重新获得焦点。 */
    void Open();

    /** 关闭查找条(`Esc`):清空查询串与全部命中,释放不再需要的状态。 */
    void Close();

    /** 查找条当前是否可见。 */
    bool Visible() const { return visible_; }

    /**
     * 追加一个输入字符。
     * @param ch 用户键入的字符;控制字符(< 0x20)一律忽略。
     * @return 查询串真的变长了返回 true(调用方据此触发重搜)。
     * @example if (find.AppendChar(ch)) find.Rerun(doc);
     */
    bool AppendChar(wchar_t ch);

    /**
     * 删除查询串末尾一个字符(退格)。
     * @return 查询串真的变短了返回 true;已经为空返回 false。
     * @example if (find.Backspace()) find.Rerun(doc);
     */
    bool Backspace();

    /** 当前查询串(以 '\0' 结尾,便于直接交给 DirectWrite 排版)。 */
    const wchar_t* Query() const { return query_; }

    /** 当前查询串长度(UTF-16 code unit,不含结尾 '\0')。 */
    u32 QueryLength() const { return queryLen_; }

    /**
     * 按当前查询串重新扫描整份文档,命中集合整体重建。
     * 查询串为空时清空命中并直接返回 0。
     *
     * @param doc 当前文档模型。
     * @return 命中总数。
     * @example u32 n = find.Rerun(doc);
     */
    u32 Rerun(const Document& doc);

    /** 当前命中总数。 */
    u32 MatchCount() const { return matches_.Size(); }

    /** 当前命中在命中集合里的下标;没有命中时为 `kInvalidIndex`。 */
    u32 CurrentIndex() const { return current_; }

    /** 全部命中(按块下标升序);没有命中时 len 为 0。 */
    Span<const Match> Matches() const;

    /** 当前命中;没有命中返回 nullptr。 */
    const Match* CurrentMatch() const;

    /**
     * 跳到下一处命中(`Enter` / `F3`),到末尾后环绕回第一处。
     * @return 有命中可跳返回 true。
     * @example if (find.GoNext()) { / * 滚动到 find.CurrentMatch()->blockIdx * / }
     */
    bool GoNext();

    /**
     * 跳到上一处命中(`Shift+Enter` / `Shift+F3`),到开头后环绕到最后一处。
     * @return 有命中可跳返回 true。
     * @example if (find.GoPrev()) { / * 滚动 * / }
     */
    bool GoPrev();

private:
    Arena* results_;              // 命中数组与 UTF-8 查询串所在 Arena,不拥有
    Arena* scratch_;              // 逐块拼接缓冲所在 Arena,不拥有
    Vec<Match> matches_;          // 当前查询的全部命中
    wchar_t query_[kMaxFindQueryChars + 1];  // 以 '\0' 结尾的查询串
    u32 queryLen_;                // 查询串长度
    u32 current_;                 // 当前命中下标,kInvalidIndex 表示无
    bool visible_;                // 查找条是否可见
};

}  // namespace mdvn
