// markair 的全文查找算法层(T37):在已解析好的 `Document` 上做**大小写不敏感**的
// 子串查找。纯算法,不依赖布局/渲染/Win32,可直接单测(见 tests/test_search.cpp)。
//
// 查找范围(裁决 #7):正文 / 代码块 / 链接 URL / 图片 alt 全部参与,直接复用已有的
// `Inline` / `LinkTarget` 数据做**一次性扫描,不额外建索引缓冲**,零常驻内存增量;
// YAML front matter 因为从未进入文档模型,天然不参与。**不做全字匹配、不做
// 大小写开关**,省一份 UI 状态与一次分支判断。
//
// 实现约束:**禁止 `std::regex`**(硬性约束),这里用手写的 Boyer-Moore-Horspool
// 坏字符跳转表(退化情况自动等价于朴素扫描)。
#pragma once

#include "../util/arena.h"
#include "../util/span.h"
#include "../util/str.h"
#include "../util/types.h"
#include "model.h"

namespace markair {

/**
 * 一处查找命中。
 *
 * `byteOffset` 的基准是**该块内全部 inline run 的源文本按顺序首尾相接**得到的
 * 逻辑缓冲(所以一次命中可以自然地跨越多个 inline run),不是相对
 * `Document::source` 的绝对偏移。命中发生在链接 URL(`LinkTarget::href`)里时,
 * 这段文本根本不出现在正文上、无法高亮,此时 `byteOffset` 取 `kInvalidIndex`、
 * `byteLen` 取 0 —— 它仍然是一处可跳转的命中,只是没有可画的矩形。
 */
struct Match {
    u32 blockIdx;    // 命中所在块下标
    u32 byteOffset;  // 块内拼接文本的字节偏移;kInvalidIndex 表示命中在链接 URL 上
    u32 byteLen;     // 命中的字节长度;URL 命中时为 0
};

/**
 * 大小写不敏感的子串查找(ASCII 折叠;CJK 无大小写之分,按字节原样比较)。
 *
 * @param haystack 被搜索的文本(UTF-8)。
 * @param needle 关键词(UTF-8),长度为 0 时恒不命中。
 * @param fromOffset 起始搜索偏移(字节),用于找下一处命中。
 * @return 命中处的字节偏移;没有命中返回 `kInvalidIndex`。
 * @example
 *   u32 at = markair::FindSubstringNoCase(markair::StrSlice{"Hello", 5},
 *                                       markair::StrSlice{"ell", 3}, 0); // 1
 */
u32 FindSubstringNoCase(StrSlice haystack, StrSlice needle, u32 fromOffset);

/**
 * 在整份文档上做一次全文查找,结果按块下标升序追加进 `out`。
 *
 * @param doc 已解析好的文档模型。
 * @param needle 关键词(UTF-8);空串直接返回 0,不产生任何命中。
 * @param scratch 逐块拼接文本用的临时 Arena,非空;函数内部每块用完即 `Reset`,
 *                返回后其中不留任何需要继续存活的数据。
 * @param out 命中结果输出,非空;函数只追加,不清空(调用方负责重新绑定 Arena)。
 * @return 本次找到的命中总数。
 * @example
 *   markair::Vec<markair::Match> matches(&findArena);
 *   u32 n = markair::SearchDocument(doc, markair::StrSlice{"todo", 4}, &scratch, &matches);
 */
u32 SearchDocument(const Document& doc, StrSlice needle, Arena* scratch, Vec<Match>* out);

/**
 * 把一处命中换算成它在该块 `IDWriteTextLayout` 里的 UTF-16 区间(T38 高亮用)。
 *
 * 命中若落在"不进入正文排版"的 run 上(图片 alt、脚注引用合成的 `[n]`),或者
 * 命中来自链接 URL,则无法对应到正文里的任何文字,返回 false。
 *
 * @param doc 文档模型。
 * @param m 一处命中。
 * @param outPosition UTF-16 起始位置输出,非空。
 * @param outLength UTF-16 长度输出,非空。
 * @return 可以映射返回 true;不可高亮返回 false(此时两个输出参数不被修改)。
 * @example
 *   u32 pos = 0, len = 0;
 *   if (markair::MatchToTextRange(doc, m, &pos, &len)) { / * HitTestTextRange * / }
 */
bool MatchToTextRange(const Document& doc, const Match& m, u32* outPosition, u32* outLength);

}  // namespace markair
