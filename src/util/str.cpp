#include "str.h"
#include "span.h"

namespace markair {

namespace {

// 解析 input 开头的一个 UTF-8 码点。
// 出错(非法字节/续字节缺失/overlong/编码出代理区/超出 U+10FFFF)时返回 U+FFFD,
// 且 *outConsumed 恒为 1 —— 对应 str.h 里说明的"每个非法字节消耗 1 个输入
// 字节后继续解析"容错策略,不因为长度判断错误而让后续内容整体错位。
u32 DecodeOneUtf8(const u8* p, u32 remaining, u32* outConsumed) {
    const u8 b0 = p[0];

    if (b0 < 0x80) { // ASCII
        *outConsumed = 1;
        return b0;
    }

    if (b0 >= 0xC2 && b0 <= 0xDF) { // 2 字节序列
        if (remaining >= 2 && (p[1] & 0xC0) == 0x80) {
            *outConsumed = 2;
            return static_cast<u32>((b0 & 0x1F) << 6 | (p[1] & 0x3F));
        }
        *outConsumed = 1;
        return 0xFFFD;
    }

    if (b0 >= 0xE0 && b0 <= 0xEF) { // 3 字节序列
        if (remaining >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            const u8 b1 = p[1];
            const bool overlong = (b0 == 0xE0 && b1 < 0xA0);
            const bool surrogate = (b0 == 0xED && b1 > 0x9F); // 编码出 U+D800~U+DFFF
            if (!overlong && !surrogate) {
                *outConsumed = 3;
                return static_cast<u32>((b0 & 0x0F) << 12 | (b1 & 0x3F) << 6 | (p[2] & 0x3F));
            }
        }
        *outConsumed = 1;
        return 0xFFFD;
    }

    if (b0 >= 0xF0 && b0 <= 0xF4) { // 4 字节序列
        if (remaining >= 4 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            const u8 b1 = p[1];
            const bool overlong = (b0 == 0xF0 && b1 < 0x90);
            const bool tooLarge = (b0 == 0xF4 && b1 > 0x8F); // 超出 U+10FFFF
            if (!overlong && !tooLarge) {
                *outConsumed = 4;
                return static_cast<u32>((b0 & 0x07) << 18 | (b1 & 0x3F) << 12 |
                                         (p[2] & 0x3F) << 6 | (p[3] & 0x3F));
            }
        }
        *outConsumed = 1;
        return 0xFFFD;
    }

    // 孤立续字节(0x80~0xC1)或非法前导字节(0xF5~0xFF)
    *outConsumed = 1;
    return 0xFFFD;
}

// 把一个 Unicode 码点追加编码为 UTF-16(必要时拆成代理对)。
void AppendUtf16(Vec<wchar_t>& out, u32 cp) {
    if (cp <= 0xFFFF) {
        out.Push(static_cast<wchar_t>(cp));
        return;
    }
    cp -= 0x10000;
    out.Push(static_cast<wchar_t>(0xD800 + (cp >> 10)));
    out.Push(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
}

// 把一个 Unicode 码点追加编码为 UTF-8(1~4 字节)。
void AppendUtf8(Vec<char>& out, u32 cp) {
    if (cp <= 0x7F) {
        out.Push(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.Push(static_cast<char>(0xC0 | (cp >> 6)));
        out.Push(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.Push(static_cast<char>(0xE0 | (cp >> 12)));
        out.Push(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.Push(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.Push(static_cast<char>(0xF0 | (cp >> 18)));
        out.Push(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.Push(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.Push(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// 判断一个 Unicode 码点是否落在常见"宽字符"区间(简化版 East Asian Width):
// CJK 统一表意文字(基本区 + 扩展 A)、CJK 符号与标点、日文平假名/片假名、
// 韩文谚文(音节区 + 兼容字母)、全角字符。覆盖中日韩常见场景即可,不追求
// Unicode East Asian Width 标准的完整覆盖(生僻扩展区如 CJK 扩展 B 及以上、
// 罕见韩文古字母暂不收录,真遇到再补)。
bool IsWideCodepoint(u32 cp) {
    if (cp >= 0x1100 && cp <= 0x11FF) return true;  // 谚文字母(Hangul Jamo)
    if (cp >= 0x3000 && cp <= 0x303F) return true;  // CJK 符号与标点
    if (cp >= 0x3040 && cp <= 0x30FF) return true;  // 平假名 + 片假名
    if (cp >= 0x3130 && cp <= 0x318F) return true;  // 谚文兼容字母
    if (cp >= 0x3400 && cp <= 0x4DBF) return true;  // CJK 统一表意文字扩展 A
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;  // CJK 统一表意文字(基本区)
    if (cp >= 0xAC00 && cp <= 0xD7A3) return true;  // 谚文音节
    if (cp >= 0xFF00 && cp <= 0xFFEF) return true;  // 全角字符/半角片假名区
    return false;
}

} // namespace

u32 Utf8VisualWidth(StrSlice input) {
    const u8* bytes = reinterpret_cast<const u8*>(input.data);
    u32 width = 0;
    u32 i = 0;
    while (i < input.len) {
        u32 consumed = 0;
        const u32 cp = DecodeOneUtf8(bytes + i, input.len - i, &consumed);
        i += consumed;
        width += IsWideCodepoint(cp) ? 2u : 1u;
    }
    return width;
}

Utf16Slice Utf8ToUtf16(StrSlice input, Arena* arena) {
    Vec<wchar_t> out(arena);
    const u8* bytes = reinterpret_cast<const u8*>(input.data);

    u32 i = 0;
    while (i < input.len) {
        u32 consumed = 0;
        const u32 cp = DecodeOneUtf8(bytes + i, input.len - i, &consumed);
        i += consumed;
        AppendUtf16(out, cp);
    }
    out.Push(L'\0'); // 方便直接传给期望 null-terminated 宽字符串的 Win32 API

    return Utf16Slice{out.Data(), out.Size() - 1};
}

u32 Utf16LengthOfUtf8(StrSlice input) {
    const u8* bytes = reinterpret_cast<const u8*>(input.data);
    u32 units = 0;
    u32 i = 0;
    while (i < input.len) {
        u32 consumed = 0;
        const u32 cp = DecodeOneUtf8(bytes + i, input.len - i, &consumed);
        i += consumed;
        units += (cp <= 0xFFFF) ? 1u : 2u;  // 与 AppendUtf16 的代理对规则保持一致
    }
    return units;
}

StrSlice Utf16ToUtf8(Utf16Slice input, Arena* arena) {
    Vec<char> out(arena);

    u32 i = 0;
    while (i < input.len) {
        const u32 unit = static_cast<u16>(input.data[i]);
        u32 cp;
        u32 consumed = 1;

        if (unit >= 0xD800 && unit <= 0xDBFF) { // 高代理项
            const bool hasNext = (i + 1 < input.len);
            const u32 low = hasNext ? static_cast<u16>(input.data[i + 1]) : 0;
            if (hasNext && low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                consumed = 2;
            } else {
                cp = 0xFFFD; // 孤立或被截断的高代理项
            }
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            cp = 0xFFFD; // 孤立的低代理项
        } else {
            cp = unit;
        }

        i += consumed;
        AppendUtf8(out, cp);
    }
    out.Push('\0');

    return StrSlice{out.Data(), out.Size() - 1};
}

} // namespace markair
