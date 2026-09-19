// markair 的 data: URI 解码(T31):把 `data:[<mime>][;base64],<payload>` 解成
// 原始字节,交给 T30 同一条 WIC 解码路径(§6 "零新增代码路径")。
//
// 解码缓冲区一律来自调用方传入的 Arena,不做任何独立堆分配;非法输入
// (缺 `,`、非法 base64 字符、超长 payload)返回失败而不是崩溃。
#pragma once

#include "../util/arena.h"
#include "../util/str.h"
#include "../util/types.h"

namespace markair {

/** data: URI 解码后的字节数上限,与单图压缩字节上限(kMaxEncodedBytes)一致。 */
constexpr u32 kMaxDataUriBytes = 16u * 1024u * 1024u;

/**
 * 一条 data: URI 的解析结果。
 * `bytes` 指向 arena 上的解码缓冲区(URL-encoded 分支也会拷贝一份,统一所有权)。
 */
struct DataUriPayload {
    StrSlice mime;      // MIME 类型(如 "image/png");未写时 len 为 0
    const u8* bytes;    // 解码后的原始字节;失败时为 nullptr
    u32 len;            // 字节数;失败时为 0
    bool base64;        // 原始 URI 是否声明了 ;base64
    bool valid;         // 解析是否成功
};

/**
 * 解析并解码一条 data: URI。
 *
 * 支持:标准 base64、内部含换行/空白的 base64(逐字符跳过空白)、
 * 未声明 base64 时按 URL-encoded(%XX)解码。
 * 失败情形:不以 `data:` 开头、缺少 `,` 分隔符、base64 含非法字符或长度不合法、
 * 解码结果超过 kMaxDataUriBytes、arena 分配失败 —— 一律返回 valid=false。
 *
 * @param uri 完整的 data: URI(UTF-8 切片),允许不以 '\0' 结尾。
 * @param arena 解码缓冲区所在的 Arena,生命周期须覆盖对返回字节的使用期。
 * @return 解码结果;valid 为 false 时 bytes/len 必为 nullptr/0。
 * @example
 *   markair::DataUriPayload p =
 *       markair::ParseDataUri(markair::StrSlice{"data:image/png;base64,iVBORw==", 30}, &arena);
 *   if (p.valid) { decoder.DecodeFromMemory(p.bytes, p.len, target); }
 */
DataUriPayload ParseDataUri(StrSlice uri, Arena* arena);

/**
 * 按 MIME 类型给出一个常见的文件扩展名(含点号),供 T36b 写临时文件命名用。
 * 无法识别时返回 ".bin"。
 *
 * @param mime MIME 切片(如 "image/jpeg");可为空切片。
 * @return 以 '\0' 结尾的静态字符串,如 L".png";调用方不得释放。
 * @example const wchar_t* ext = markair::ExtensionForMime(markair::StrSlice{"image/gif", 9}); // L".gif"
 */
const wchar_t* ExtensionForMime(StrSlice mime);

}  // namespace markair
