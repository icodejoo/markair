// markair 的编码嗅探:识别 BOM/无 BOM UTF-8/ANSI 回退,不实现完整通用编码检测算法。
#pragma once

#include "../util/arena.h"
#include "../util/str.h"
#include "../util/types.h"

namespace markair {

/**
 * 嗅探得到的编码类型。
 */
enum class DetectedEncoding {
    Utf8Bom,      // 带 UTF-8 BOM(EF BB BF)
    Utf8NoBom,    // 无 BOM,但字节序列是合法 UTF-8
    Utf16Le,      // 带 UTF-16LE BOM(FF FE)
    AnsiFallback, // 无 BOM 且不是合法 UTF-8,回退按系统 ANSI 代码页解释
};

/**
 * 一次编码嗅探的结果。
 */
struct EncodingDetection {
    DetectedEncoding encoding; // 嗅探出的编码类型
    u32 contentOffset;         // 跳过 BOM 后,实际内容在原始字节中的起始偏移
};

/**
 * 嗅探一段原始字节应该按什么编码解释。
 * 仅识别 UTF-8 BOM / UTF-16LE BOM / 合法 UTF-8(无 BOM) / ANSI 回退这四种情况,
 * 不是通用的多编码检测算法。
 *
 * @param bytes 文件原始字节(零拷贝切片,通常来自 FileMap::Data)。
 * @return 嗅探结果,含编码类型与跳过 BOM 后的内容偏移。
 * @example
 *   markair::EncodingDetection d = markair::DetectEncoding(fm.Data());
 */
EncodingDetection DetectEncoding(StrSlice bytes);

/**
 * 按嗅探结果把原始字节解码为 UTF-16,结果写入 arena(含末尾 '\0')。
 * ANSI 回退使用系统当前 ANSI 代码页(CP_ACP);UTF-16LE 直接重新解释字节
 * (奇数长度的尾部单字节会被替换为一个 U+FFFD,不会越界读取或崩溃)。
 *
 * @param bytes 与 DetectEncoding 相同的原始字节切片。
 * @param detection DetectEncoding 的嗅探结果。
 * @param arena 输出缓冲区所在的 Arena。
 * @return 解码后的 UTF-16 切片,data 以 '\0' 结尾,len 不含该 '\0'。
 * @example
 *   markair::Utf16Slice text = markair::DecodeToUtf16(fm.Data(), d, &arena);
 */
Utf16Slice DecodeToUtf16(StrSlice bytes, EncodingDetection detection, Arena* arena);

} // namespace markair
