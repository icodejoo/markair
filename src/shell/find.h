// mdvn 的 `Ctrl+F` 查找条状态(T37 的 UI 层)。
//
// 与 `shell/scroll.h` / `shell/hit_test.h` 同一口径:**不 include windows.h**,
// 这里只维护"查询串 + 命中集合 + 当前命中下标"这套纯状态机,真正的按键翻译
// (`Ctrl+F` / `Enter` / `Shift+Enter` / `F3` / `Esc`)与绘制留在 window.cpp /
// renderer.cpp,因此本文件可以直接单测。
//
// 交互口径(§9 "内容区保持极简"):顶部浮出一条轻量查找条,**没有**全字匹配/
// 大小写开关这类附加 UI 状态。2026-09-19 起改为"回车才搜"——增量输入只更新
// 查询串,不重搜;按 `Enter`/`F3` 时,若查询串自上次搜索后有变化(`Dirty()`
// 为 true)才重新全文扫描并跳到第一处命中,否则直接跳到下一处已有命中,
// 这样连续按 `Enter` 仍是"逐处跳转"而不是每次都重新扫一遍全文。
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

    /**
     * 整体替换查询串(原生 Win32 Edit 控件 EN_CHANGE 通知同步文本用,取代逐字符
     * AppendChar/Backspace 调用)。超出 `kMaxFindQueryChars` 的部分截断。
     * 总是标记 `Dirty()`(哪怕新文本和旧的一样,调用方只在真的改过时才调这个,
     * 不必在这里做多余的相等性判断)。
     * @param text 新查询串(UTF-16),可为 nullptr(等价于清空)。
     * @param len text 的长度(UTF-16 code unit);超过缓冲上限的部分丢弃。
     * @example find.SetQuery(editText, editTextLen);
     */
    void SetQuery(const wchar_t* text, u32 len);

    /** 当前查询串(以 '\0' 结尾,便于直接交给 DirectWrite 排版)。 */
    const wchar_t* Query() const { return query_; }

    /**
     * 查询串自上次 `Rerun` 以来是否发生过变化(`AppendChar`/`Backspace`
     * 改动过查询串,`Rerun` 会清掉这个标记)。调用方(window.cpp 的回车按键
     * 处理)据此判断本次回车是该发起一次新搜索,还是直接跳到下一处已有命中。
     */
    bool Dirty() const { return dirty_; }

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

    /**
     * 只清空当前命中集合与高亮,不做任何文档扫描(不再"增量输入即时重搜"后,
     * 每次 `AppendChar`/`Backspace` 改完查询串都调这个,避免继续显示上一次
     * 查询串留下的过期高亮)。查询串本身不受影响。
     * @example if (find.AppendChar(ch)) find.ClearMatches();
     */
    void ClearMatches();

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
    bool dirty_;                  // 查询串自上次 Rerun 后是否变过,见 Dirty() 的注释
};

}  // namespace mdvn
