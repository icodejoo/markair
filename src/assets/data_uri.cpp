#include "data_uri.h"

namespace markair {

namespace {

// 失败结果的快捷构造。
DataUriPayload Invalid() { return DataUriPayload{StrSlice{nullptr, 0}, nullptr, 0, false, false}; }

// base64 字符 -> 6 位值;非 base64 字符返回 -1,'=' 单独由调用方处理。
int Base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// 是否是可以在 base64 载荷里安全跳过的空白(换行/回车/空格/制表符)。
bool IsSkippableWhitespace(char c) {
    return c == '\n' || c == '\r' || c == ' ' || c == '\t';
}

// 十六进制字符 -> 数值;非法返回 -1。
int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ASCII 小写折叠。
char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// 切片内容与一个小写 C 字符串是否相等(大小写不敏感)。
bool EqualsIgnoreCase(StrSlice s, const char* lit) {
    u32 i = 0;
    for (; i < s.len; ++i) {
        if (lit[i] == 0 || LowerAscii(s.data[i]) != lit[i]) return false;
    }
    return lit[i] == 0;
}

}  // namespace

DataUriPayload ParseDataUri(StrSlice uri, Arena* arena) {
    if (!uri.data || !arena || uri.len < 5) return Invalid();

    // 前缀必须是 "data:"(大小写不敏感,与 RFC 2397 一致)。
    const char kPrefix[] = "data:";
    for (u32 i = 0; i < 5; ++i) {
        if (LowerAscii(uri.data[i]) != kPrefix[i]) return Invalid();
    }

    // 找到分隔 header 与 payload 的第一个 ','。
    u32 comma = 0xFFFFFFFFu;
    for (u32 i = 5; i < uri.len; ++i) {
        if (uri.data[i] == ',') { comma = i; break; }
    }
    if (comma == 0xFFFFFFFFu) return Invalid();  // 缺 ',',非法

    StrSlice header{uri.data + 5, comma - 5};
    StrSlice payload{uri.data + comma + 1, uri.len - comma - 1};

    // header 形如 `<mime>;charset=x;base64`,mime 是第一个 ';' 之前的部分,
    // 末尾的 `;base64` 决定载荷编码方式。
    u32 mimeLen = header.len;
    bool isBase64 = false;
    u32 segStart = 0;
    for (u32 i = 0; i <= header.len; ++i) {
        if (i == header.len || header.data[i] == ';') {
            StrSlice seg{header.data + segStart, i - segStart};
            if (segStart == 0) {
                mimeLen = seg.len;
            } else if (EqualsIgnoreCase(seg, "base64")) {
                isBase64 = true;
            }
            segStart = i + 1;
        }
    }
    StrSlice mime{header.data, mimeLen};

    if (isBase64) {
        // 4 个 base64 字符最多产出 3 字节,先按上界预估缓冲区大小。
        u64 upperBound = (static_cast<u64>(payload.len) / 4ull + 1ull) * 3ull;
        if (upperBound > kMaxDataUriBytes) return Invalid();  // 超长输入直接拒绝
        u8* out = static_cast<u8*>(arena->Alloc(static_cast<size_t>(upperBound) + 1u, 1));
        if (!out) return Invalid();

        u32 outLen = 0;
        u32 quad = 0;      // 当前已累计的 6 位组个数(0~3)
        u32 acc = 0;       // 已累计的位
        u32 padding = 0;   // 遇到的 '=' 个数
        for (u32 i = 0; i < payload.len; ++i) {
            char c = payload.data[i];
            if (IsSkippableWhitespace(c)) continue;  // 带换行的 base64 是合法输入
            if (c == '=') {
                padding++;
                if (padding > 2) return Invalid();
                continue;
            }
            if (padding > 0) return Invalid();  // '=' 之后不允许再出现数据字符
            int v = Base64Value(c);
            if (v < 0) return Invalid();        // 非法 base64 字符
            acc = (acc << 6) | static_cast<u32>(v);
            quad++;
            if (quad == 4) {
                out[outLen++] = static_cast<u8>((acc >> 16) & 0xFFu);
                out[outLen++] = static_cast<u8>((acc >> 8) & 0xFFu);
                out[outLen++] = static_cast<u8>(acc & 0xFFu);
                quad = 0;
                acc = 0;
            }
        }
        // 处理最后不足 4 个字符的一组:2 个字符 -> 1 字节,3 个字符 -> 2 字节。
        if (quad == 1) return Invalid();  // 单个残留 6 位组不构成合法 base64
        if (quad == 2) {
            out[outLen++] = static_cast<u8>((acc >> 4) & 0xFFu);
        } else if (quad == 3) {
            out[outLen++] = static_cast<u8>((acc >> 10) & 0xFFu);
            out[outLen++] = static_cast<u8>((acc >> 2) & 0xFFu);
        }
        return DataUriPayload{mime, out, outLen, true, true};
    }

    // 非 base64:按 URL-encoded 解码(%XX -> 字节,其余字符原样)。
    if (payload.len > kMaxDataUriBytes) return Invalid();
    u8* out = static_cast<u8*>(arena->Alloc(static_cast<size_t>(payload.len) + 1u, 1));
    if (!out) return Invalid();

    u32 outLen = 0;
    for (u32 i = 0; i < payload.len; ++i) {
        char c = payload.data[i];
        if (c == '%' && i + 2 < payload.len) {
            int hi = HexValue(payload.data[i + 1]);
            int lo = HexValue(payload.data[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[outLen++] = static_cast<u8>((hi << 4) | lo);
                i += 2;
                continue;
            }
            return Invalid();  // 残缺的百分号转义按非法输入处理
        }
        out[outLen++] = static_cast<u8>(c);
    }
    return DataUriPayload{mime, out, outLen, false, true};
}

const wchar_t* ExtensionForMime(StrSlice mime) {
    if (EqualsIgnoreCase(mime, "image/png")) return L".png";
    if (EqualsIgnoreCase(mime, "image/jpeg") || EqualsIgnoreCase(mime, "image/jpg")) return L".jpg";
    if (EqualsIgnoreCase(mime, "image/gif")) return L".gif";
    if (EqualsIgnoreCase(mime, "image/bmp")) return L".bmp";
    if (EqualsIgnoreCase(mime, "image/webp")) return L".webp";
    if (EqualsIgnoreCase(mime, "image/tiff")) return L".tif";
    if (EqualsIgnoreCase(mime, "image/x-icon")) return L".ico";
    if (EqualsIgnoreCase(mime, "image/svg+xml")) return L".svg";
    return L".bin";
}

const wchar_t* ExtensionForImageBytes(const u8* bytes, u32 len) {
    if (!bytes) return L".jpg";
    if (len >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
        return L".png";
    }
    if (len >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        return L".jpg";
    }
    if (len >= 6 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == '8' &&
        (bytes[4] == '7' || bytes[4] == '9') && bytes[5] == 'a') {
        return L".gif";
    }
    if (len >= 2 && bytes[0] == 'B' && bytes[1] == 'M') {
        return L".bmp";
    }
    if (len >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
        bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P') {
        return L".webp";
    }
    if (len >= 4 &&
        ((bytes[0] == 0x49 && bytes[1] == 0x49 && bytes[2] == 0x2A && bytes[3] == 0x00) ||
         (bytes[0] == 0x4D && bytes[1] == 0x4D && bytes[2] == 0x00 && bytes[3] == 0x2A))) {
        return L".tif";
    }
    // 未识别的字节流兜底为 .jpg 而不是 .img/.bin —— 前者 Windows 通常没有默认
    // 关联程序,ShellExecuteW 会静默失败(SE_ERR_NOASSOC),表现为"点击完全无反应"。
    return L".jpg";
}

}  // namespace markair
