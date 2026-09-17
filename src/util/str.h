// mdvn 的字符串切片与 UTF-8/UTF-16 转换。不用 std::string/std::wstring,
// 长期持有的字符串数据统一走零拷贝切片 + Arena 分配的缓冲区。
#pragma once

#include "arena.h"
#include "types.h"

namespace mdvn {

/**
 * 零拷贝的 UTF-8 字符串切片,只是"看一眼"某段内存,不持有所有权、不深拷贝。
 * 典型用来指向内存映射的文件内容或 Arena 缓冲区。
 *
 * @example
 *   mdvn::StrSlice s{"hello", 5};
 */
struct StrSlice {
    const char* data;
    u32 len;
};

/**
 * 零拷贝的 UTF-16 字符串切片,语义同 StrSlice,单位是 UTF-16 code unit。
 *
 * @example
 *   mdvn::Utf16Slice s = mdvn::Utf8ToUtf16(utf8, &arena);
 */
struct Utf16Slice {
    const wchar_t* data;
    u32 len; // 不含末尾 '\0',但底层缓冲区保证以 '\0' 结尾
};

/**
 * 把一段 UTF-8 文本转换为 UTF-16,结果写入 arena(含末尾 '\0',可直接传 Win32 API)。
 *
 * 容错策略:采用类似 WHATWG Encoding Standard 的做法 —— 遇到非法/截断的
 * UTF-8 字节序列(孤立续字节、overlong 编码、被截断的多字节序列、编码出
 * 代理区 U+D800~U+DFFF 的非法序列等),用替换字符 U+FFFD 代替,且只消耗
 * 1 个输入字节后继续从下一个字节重新尝试解析。选择这个策略是因为它足够
 * 简单、可预测(每个非法字节最多产生一个 U+FFFD,不会因为错误的长度判断
 * 导致后续内容整体错位),且是浏览器/标准规范验证过的成熟做法。
 *
 * @param input 待转换的 UTF-8 切片。
 * @param arena 输出缓冲区所在的 Arena,失败(空间耗尽)时返回的切片可能被截断。
 * @return 转换结果切片,data 以 '\0' 结尾,len 不含该 '\0'。
 * @example
 *   mdvn::Utf16Slice wide = mdvn::Utf8ToUtf16(mdvn::StrSlice{"你好", 6}, &arena);
 */
Utf16Slice Utf8ToUtf16(StrSlice input, Arena* arena);

/**
 * 把一段 UTF-16 文本转换为 UTF-8,结果写入 arena(含末尾 '\0')。
 *
 * 容错策略与 Utf8ToUtf16 对称:孤立的高/低代理项、被截断的代理对,
 * 都用 U+FFFD 替换,每次只消耗 1 个 UTF-16 code unit 后继续解析。
 *
 * @param input 待转换的 UTF-16 切片。
 * @param arena 输出缓冲区所在的 Arena。
 * @return 转换结果切片,data 以 '\0' 结尾,len 不含该 '\0'。
 * @example
 *   mdvn::StrSlice utf8 = mdvn::Utf16ToUtf8(wide, &arena);
 */
StrSlice Utf16ToUtf8(Utf16Slice input, Arena* arena);

/**
 * 计算一段 UTF-8 文本按 `Utf8ToUtf16` 的容错规则转换后会得到多少个 UTF-16
 * code unit(不含结尾 '\0')。不做任何分配,供"只需要长度、不需要内容"的
 * 场景使用(如 T38 把字节偏移换算成 IDWriteTextLayout 的文本位置)。
 *
 * @param input 待计量的 UTF-8 切片。
 * @return UTF-16 code unit 个数;结果与 `Utf8ToUtf16(input, arena).len` 恒等。
 * @example u32 n = mdvn::Utf16LengthOfUtf8(mdvn::StrSlice{"你好", 6}); // 2
 */
u32 Utf16LengthOfUtf8(StrSlice input);

} // namespace mdvn
