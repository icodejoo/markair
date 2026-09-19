#include "encoding.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace markair {

namespace {

// 判断一段字节是否是合法 UTF-8:借助 MultiByteToWideChar + MB_ERR_INVALID_CHARS
// 做启发式校验,不自己实现完整的 UTF-8 状态机。
bool IsValidUtf8(const char* data, u32 len) {
    if (len == 0) return true;
    int rc = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data,
                                  static_cast<int>(len), nullptr, 0);
    return rc > 0;
}

} // namespace

EncodingDetection DetectEncoding(StrSlice bytes) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.data);

    if (bytes.len >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        return EncodingDetection{DetectedEncoding::Utf8Bom, 3};
    }
    if (bytes.len >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        return EncodingDetection{DetectedEncoding::Utf16Le, 2};
    }
    if (IsValidUtf8(bytes.data, bytes.len)) {
        return EncodingDetection{DetectedEncoding::Utf8NoBom, 0};
    }
    return EncodingDetection{DetectedEncoding::AnsiFallback, 0};
}

Utf16Slice DecodeToUtf16(StrSlice bytes, EncodingDetection detection, Arena* arena) {
    const char* content = bytes.data + detection.contentOffset;
    u32 contentLen = bytes.len - detection.contentOffset;

    switch (detection.encoding) {
    case DetectedEncoding::Utf8Bom:
    case DetectedEncoding::Utf8NoBom: {
        return Utf8ToUtf16(StrSlice{content, contentLen}, arena);
    }
    case DetectedEncoding::Utf16Le: {
        // 字节本身已是 UTF-16LE,直接重新解释;奇数长度的尾部单字节
        // 无法组成完整 code unit,用一个 U+FFFD 代替,不越界读取。
        u32 wcharCount = contentLen / 2;
        bool hasDanglingByte = (contentLen % 2) != 0;
        u32 outLen = wcharCount + (hasDanglingByte ? 1u : 0u);

        wchar_t* out = static_cast<wchar_t*>(
            arena->Alloc((static_cast<size_t>(outLen) + 1) * sizeof(wchar_t), alignof(wchar_t)));
        if (!out) {
            return Utf16Slice{nullptr, 0};
        }

        if (wcharCount > 0) {
            std::memcpy(out, content, static_cast<size_t>(wcharCount) * sizeof(wchar_t));
        }
        if (hasDanglingByte) {
            out[wcharCount] = static_cast<wchar_t>(0xFFFD);
        }
        out[outLen] = L'\0';
        return Utf16Slice{out, outLen};
    }
    case DetectedEncoding::AnsiFallback: {
        // 用系统当前 ANSI 代码页把字节直接转成 UTF-16。
        if (contentLen == 0) {
            wchar_t* out = static_cast<wchar_t*>(arena->Alloc(sizeof(wchar_t), alignof(wchar_t)));
            if (!out) return Utf16Slice{nullptr, 0};
            out[0] = L'\0';
            return Utf16Slice{out, 0};
        }
        int needed = MultiByteToWideChar(CP_ACP, 0, content, static_cast<int>(contentLen), nullptr, 0);
        if (needed <= 0) {
            return Utf16Slice{nullptr, 0};
        }
        wchar_t* out = static_cast<wchar_t*>(
            arena->Alloc((static_cast<size_t>(needed) + 1) * sizeof(wchar_t), alignof(wchar_t)));
        if (!out) {
            return Utf16Slice{nullptr, 0};
        }
        int written = MultiByteToWideChar(CP_ACP, 0, content, static_cast<int>(contentLen), out, needed);
        u32 outLen = static_cast<u32>(written > 0 ? written : 0);
        out[outLen] = L'\0';
        return Utf16Slice{out, outLen};
    }
    }

    return Utf16Slice{nullptr, 0};
}

} // namespace markair
