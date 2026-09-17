// mdvn 的 YAML front matter 隐藏(T22):只做起始偏移前移,不做 YAML 解析、
// 不分配内存。BOM 处理由 encoding.cpp 负责,调用方应保证传入的 source
// 已经跳过 BOM(即 encoding.h::EncodingDetection::contentOffset 之后的内容)。
#pragma once

#include "../util/str.h"

namespace mdvn {

/**
 * 若 source 以单独成行的 "---" 开头、并能找到之后单独成行的 "---" 或 "..."
 * 作为闭合分隔符,返回跳过这一整段 front matter 后的切片(从闭合行的下一行
 * 开始);否则原样返回整个输入,不吃掉正文。允许行尾是 "\n" 或 "\r\n"。
 * 纯函数,不做任何内存分配。
 *
 * @param source 已跳过 BOM 的原始文本切片。
 * @return 跳过 front matter 后的切片,或原始切片(未找到/不匹配时)。
 * @example
 *   mdvn::StrSlice body = mdvn::SkipFrontMatter(mdvn::StrSlice{text, len});
 */
StrSlice SkipFrontMatter(StrSlice source);

} // namespace mdvn
