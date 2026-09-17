#include "ini.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace mdvn {

namespace {

// 配置文件相对 %LOCALAPPDATA% 的固定位置。
constexpr wchar_t kIniRelativePath[] = L"\\mdvn\\state.ini";

// 空白字符判定(只认 ASCII 空格/制表符,ini 不需要更复杂的规则)。
bool IsBlank(char c) { return c == ' ' || c == '\t'; }

// ASCII 小写折叠,用于键名的大小写不敏感比较。
char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// 键名比较:大小写不敏感,长度须完全一致。
bool KeyEquals(StrSlice key, const char* literal) {
    u32 i = 0;
    for (; i < key.len; ++i) {
        if (literal[i] == 0) return false;
        if (LowerAscii(key.data[i]) != literal[i]) return false;
    }
    return literal[i] == 0;
}

// 裁掉切片两端的空格/制表符。
StrSlice Trim(StrSlice s) {
    u32 begin = 0;
    u32 end = s.len;
    while (begin < end && IsBlank(s.data[begin])) ++begin;
    while (end > begin && IsBlank(s.data[end - 1])) --end;
    return StrSlice{s.data + begin, end - begin};
}

// 把一段十进制文本解析成整数;整段必须都是数字(允许一个前导 '-'),
// 否则返回 false 让调用方保持原值(这是"非法值不生效"的实现)。
bool ParseInt(StrSlice s, i32* out) {
    if (s.len == 0) return false;
    u32 i = 0;
    bool negative = false;
    if (s.data[0] == '-' || s.data[0] == '+') {
        negative = s.data[0] == '-';
        i = 1;
        if (s.len == 1) return false;
    }
    i64 value = 0;
    for (; i < s.len; ++i) {
        char c = s.data[i];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
        if (value > 0x7FFFFFFFLL) value = 0x7FFFFFFFLL;  // 溢出饱和,交给下游钳制
    }
    *out = static_cast<i32>(negative ? -value : value);
    return true;
}

// 把 UTF-8 文本解码进固定大小的宽字符缓冲(含结尾 '\0')。
// 自己实现而不是调 MultiByteToWideChar,是为了让解析层保持"纯函数、不依赖
// Win32"的可单测性;非法字节按 U+FFFD 处理,与 util/str.cpp 的口径一致。
void Utf8ToWideFixed(StrSlice s, wchar_t* out, u32 cap) {
    u32 w = 0;
    u32 i = 0;
    while (i < s.len && w + 1 < cap) {
        u8 b0 = static_cast<u8>(s.data[i]);
        u32 code = 0xFFFDu;
        u32 consumed = 1;
        if (b0 < 0x80u) {
            code = b0;
        } else if ((b0 & 0xE0u) == 0xC0u && i + 1 < s.len) {
            u8 b1 = static_cast<u8>(s.data[i + 1]);
            if ((b1 & 0xC0u) == 0x80u) {
                code = ((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu);
                consumed = 2;
                if (code < 0x80u) code = 0xFFFDu;
            }
        } else if ((b0 & 0xF0u) == 0xE0u && i + 2 < s.len) {
            u8 b1 = static_cast<u8>(s.data[i + 1]);
            u8 b2 = static_cast<u8>(s.data[i + 2]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
                code = ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
                consumed = 3;
                if (code < 0x800u || (code >= 0xD800u && code <= 0xDFFFu)) code = 0xFFFDu;
            }
        } else if ((b0 & 0xF8u) == 0xF0u && i + 3 < s.len) {
            u8 b1 = static_cast<u8>(s.data[i + 1]);
            u8 b2 = static_cast<u8>(s.data[i + 2]);
            u8 b3 = static_cast<u8>(s.data[i + 3]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u) {
                code = ((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) | ((b2 & 0x3Fu) << 6) |
                        (b3 & 0x3Fu);
                consumed = 4;
                if (code < 0x10000u || code > 0x10FFFFu) code = 0xFFFDu;
            }
        }
        i += consumed;

        if (code >= 0x10000u) {
            if (w + 2 >= cap) break;  // 放不下完整代理对就停下,不写半个字符
            code -= 0x10000u;
            out[w++] = static_cast<wchar_t>(0xD800u + (code >> 10));
            out[w++] = static_cast<wchar_t>(0xDC00u + (code & 0x3FFu));
        } else {
            out[w++] = static_cast<wchar_t>(code);
        }
    }
    out[w] = 0;
}

}  // namespace

void DefaultAppSettings(AppSettings* out) {
    if (!out) return;
    out->loadRemoteImages = false;  // 裁决 #5:默认完全不触碰 WinHTTP
    out->fontBodyPrimary[0] = 0;
    out->fontBodyFallback[0] = 0;
    out->fontMonoPrimary[0] = 0;
    out->fontMonoFallback[0] = 0;
    out->theme = ThemeSetting::System;  // 默认跟随系统
}

u32 ParseIniSettings(StrSlice text, AppSettings* out) {
    if (!out || !text.data) return 0;

    u32 applied = 0;
    u32 pos = 0;
    while (pos < text.len) {
        // 切出一行(兼容 LF / CRLF,行尾 CR 在 Trim 之前先剥掉)。
        u32 lineEnd = pos;
        while (lineEnd < text.len && text.data[lineEnd] != '\n') ++lineEnd;
        u32 rawEnd = lineEnd;
        if (rawEnd > pos && text.data[rawEnd - 1] == '\r') --rawEnd;

        StrSlice line = Trim(StrSlice{text.data + pos, rawEnd - pos});
        pos = lineEnd + 1;

        if (line.len == 0) continue;
        if (line.data[0] == ';' || line.data[0] == '#') continue;  // 注释行
        if (line.data[0] == '[') continue;                          // [section] 行,M1 不分节

        u32 eq = 0;
        while (eq < line.len && line.data[eq] != '=') ++eq;
        if (eq >= line.len) continue;  // 没有 '=' 的行不是 KV,忽略

        StrSlice key = Trim(StrSlice{line.data, eq});
        StrSlice value = Trim(StrSlice{line.data + eq + 1, line.len - eq - 1});
        if (key.len == 0) continue;

        if (KeyEquals(key, "load_remote_images")) {
            i32 v = 0;
            if (ParseInt(value, &v)) {
                if (v < 0) v = 0;   // 超范围值钳制到 [0, 1]
                if (v > 1) v = 1;
                out->loadRemoteImages = v != 0;
                applied++;
            }
            continue;
        }

        if (KeyEquals(key, "theme")) {
            // 非法值口径与 load_remote_images 一致:不识别就整段跳过、不计入
            // applied、也不改动 out->theme——由于调用方总是先 DefaultAppSettings
            // 再解析,"不改动"的效果就是保留默认值 System,天然满足"非法值回落
            // system"的验收要求,不需要另外写一次"回落"逻辑。
            if (KeyEquals(value, "system")) {
                out->theme = ThemeSetting::System;
                applied++;
            } else if (KeyEquals(value, "light")) {
                out->theme = ThemeSetting::Light;
                applied++;
            } else if (KeyEquals(value, "dark")) {
                out->theme = ThemeSetting::Dark;
                applied++;
            }
            continue;
        }

        wchar_t* target = nullptr;
        if (KeyEquals(key, "font_body_primary")) target = out->fontBodyPrimary;
        else if (KeyEquals(key, "font_body_fallback")) target = out->fontBodyFallback;
        else if (KeyEquals(key, "font_mono_primary")) target = out->fontMonoPrimary;
        else if (KeyEquals(key, "font_mono_fallback")) target = out->fontMonoFallback;
        if (target) {
            Utf8ToWideFixed(value, target, kMaxFontFamilyChars);
            applied++;
        }
    }
    return applied;
}

bool LoadAppSettings(AppSettings* out) {
    if (!out) return false;
    DefaultAppSettings(out);

    wchar_t path[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    u32 cursor = static_cast<u32>(n);
    for (u32 i = 0; kIniRelativePath[i] != 0; ++i) {
        if (cursor + 1 >= MAX_PATH) return false;
        path[cursor++] = kIniRelativePath[i];
    }
    path[cursor] = 0;

    // 只读打开,允许别人同时写;文件不存在是最常见的正常情况,不报错。
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    char buffer[kMaxIniBytes];
    DWORD read = 0;
    BOOL ok = ReadFile(file, buffer, kMaxIniBytes, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) return false;

    // UTF-8 BOM 容错:记事本存出来的 ini 常带 BOM,跳过它再解析。
    u32 offset = 0;
    if (read >= 3 && static_cast<u8>(buffer[0]) == 0xEFu &&
        static_cast<u8>(buffer[1]) == 0xBBu && static_cast<u8>(buffer[2]) == 0xBFu) {
        offset = 3;
    }
    ParseIniSettings(StrSlice{buffer + offset, static_cast<u32>(read) - offset}, out);
    return true;
}

}  // namespace mdvn
