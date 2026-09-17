// mdvn 的 MD_ATTRIBUTE 展开工具:把 md4c 分段的属性文本(href/title/label 等)
// 拼接为 Arena 上连续的 UTF-8 缓冲区。只被 parser.cpp/attr.cpp 使用,
// model.h 不感知 md4c 类型,依赖 md4c.h 的逻辑只留在这一层。
#pragma once

#include "../util/arena.h"
#include "../util/str.h"
#include "model.h"
#include "../../third_party/md4c/md4c.h"

namespace mdvn {

/**
 * 把 MD_ATTRIBUTE 的分段(NORMAL 原样拷贝 / ENTITY 解码 / NULLCHAR 替换为
 * U+FFFD)拼接成一段连续的 UTF-8 文本,写入 arena。
 * 实体解码支持 &amp; &lt; &gt; &quot; &#39; 等命名实体及 &#123;/&#x1A; 形式
 * 的十进制/十六进制数字实体;未识别的命名实体原样保留其字面文本。
 *
 * @param attr md4c 传入的属性(如 MD_SPAN_A_DETAIL::href)。
 * @param arena 输出缓冲区所在的 Arena。
 * @return 展开后的 UTF-8 文本切片;attr 为空时返回长度为 0 的切片。
 * @example
 *   StrSlice href = mdvn::ExpandAttribute(detail->href, &arena);
 */
StrSlice ExpandAttribute(const MD_ATTRIBUTE& attr, Arena* arena);

/**
 * 依据已展开的 href/src 文本判定链接目标种类:"#" 开头 -> Anchor;
 * "data:" 开头 -> DataUri;含 "scheme://" 形态(http/https 及其它协议)
 * -> External;空文本 -> Unknown;其余 -> RelativePath。不依赖 md4c,
 * 纯粹的字符串判断,可独立于 ExpandAttribute 使用/测试。
 *
 * @param href 已展开的链接地址文本(通常来自 ExpandAttribute 的返回值)。
 * @return 判定出的链接目标种类。
 * @example
 *   mdvn::LinkTargetKind kind = mdvn::ClassifyLinkTarget(href);
 */
LinkTargetKind ClassifyLinkTarget(StrSlice href);

} // namespace mdvn
