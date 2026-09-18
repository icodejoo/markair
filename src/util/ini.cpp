#include "ini.h"

#include "../text/font.h"  // T57:FontSubsystem::ClampToNearestZoomLevel 供 zoom 键复用档位表

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <cwchar>

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

// 把一段十进制文本(允许一个小数点、一个前导 '-'/'+')解析成浮点数;
// 整段必须都是数字/小数点,否则返回 false 让调用方保持原值——与 ParseInt
// 的"非法值不生效"口径一致(T57:zoom 键用)。
bool ParseFloat(StrSlice s, float* out) {
    if (s.len == 0) return false;
    u32 i = 0;
    bool negative = false;
    if (s.data[0] == '-' || s.data[0] == '+') {
        negative = s.data[0] == '-';
        i = 1;
        if (s.len == 1) return false;
    }
    double value = 0.0;
    double frac = 0.1;
    bool seenDot = false;
    bool seenDigit = false;
    for (; i < s.len; ++i) {
        char c = s.data[i];
        if (c == '.') {
            if (seenDot) return false;  // 不允许多个小数点
            seenDot = true;
            continue;
        }
        if (c < '0' || c > '9') return false;
        seenDigit = true;
        if (!seenDot) {
            value = value * 10.0 + (c - '0');
        } else {
            value += (c - '0') * frac;
            frac *= 0.1;
        }
    }
    if (!seenDigit) return false;
    *out = static_cast<float>(negative ? -value : value);
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

// T55/T56:已知键的固定顺序,写盘时按此顺序追加磁盘原文里缺失的键。
constexpr const char* kKnownKeys[] = {
    "load_remote_images", "font_body_primary", "font_body_fallback",
    "font_mono_primary",  "font_mono_fallback", "theme", "zoom",
    "win_x", "win_y", "win_w", "win_h", "win_maximized"};
constexpr u32 kKnownKeyCount = 12;

// T55:本进程最近一次 Load/Save 成功后"磁盘上应该有"的配置快照,用来判断
// SaveAppSettings 调用时哪些键是"本进程真正改动过的"——只有 target 与这份
// 快照不同的字段才会覆盖磁盘上的当前值,其余字段保留磁盘上(可能被别的
// 进程并发改过的)最新值,这正是裁决 #7"读-改-写"要求的合并语义。
// 进程内全局,POD,无副作用构造函数,符合项目"禁止有副作用全局构造"的约束。
bool g_hasBaseline = false;
AppSettings g_baseline{};

void RememberBaseline(const AppSettings& s) {
    g_baseline = s;
    g_hasBaseline = true;
}

// 把 UTF-16 字符串编码进定长 UTF-8 缓冲(与 Utf8ToWideFixed 方向相反,
// 供写盘时把字体族名族转回 UTF-8);cap 含结尾 '\0',放不下就截断,
// 绝不越界写。
u32 WideToUtf8Fixed(const wchar_t* s, char* out, u32 cap) {
    u32 w = 0;
    if (cap == 0) return 0;
    for (const wchar_t* p = s; *p != 0 && w + 1 < cap; ) {
        u32 code = static_cast<u32>(*p);
        u32 consumed = 1;
        if (code >= 0xD800u && code <= 0xDBFFu) {
            wchar_t next = *(p + 1);
            if (next >= 0xDC00u && next <= 0xDFFFu) {
                code = 0x10000u + ((code - 0xD800u) << 10) + (static_cast<u32>(next) - 0xDC00u);
                consumed = 2;
            } else {
                code = 0xFFFDu;
            }
        } else if (code >= 0xDC00u && code <= 0xDFFFu) {
            code = 0xFFFDu;  // 孤立的低代理项
        }
        p += consumed;

        if (code < 0x80u) {
            out[w++] = static_cast<char>(code);
        } else if (code < 0x800u) {
            if (w + 2 >= cap) break;
            out[w++] = static_cast<char>(0xC0u | (code >> 6));
            out[w++] = static_cast<char>(0x80u | (code & 0x3Fu));
        } else if (code < 0x10000u) {
            if (w + 3 >= cap) break;
            out[w++] = static_cast<char>(0xE0u | (code >> 12));
            out[w++] = static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
            out[w++] = static_cast<char>(0x80u | (code & 0x3Fu));
        } else {
            if (w + 4 >= cap) break;
            out[w++] = static_cast<char>(0xF0u | (code >> 18));
            out[w++] = static_cast<char>(0x80u | ((code >> 12) & 0x3Fu));
            out[w++] = static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
            out[w++] = static_cast<char>(0x80u | (code & 0x3Fu));
        }
    }
    out[w] = 0;
    return w;
}

// 往定长缓冲追加一段原始字节,越界就返回 false(调用方据此判定整体失败,
// 不写出半截内容)。
bool AppendBytes(char* out, u32 cap, u32* pos, const char* data, u32 len) {
    if (*pos + len > cap) return false;
    for (u32 i = 0; i < len; ++i) out[*pos + i] = data[i];
    *pos += len;
    return true;
}

bool AppendCStr(char* out, u32 cap, u32* pos, const char* s) {
    u32 len = 0;
    while (s[len] != 0) ++len;
    return AppendBytes(out, cap, pos, s, len);
}

// 把有符号整数编码成十进制 ASCII 追加进定长缓冲(T56 窗口矩形键用);
// 复用 sprintf_s 而不是手写转换,与项目里 swprintf_s 的用法口径一致。
bool AppendInt(char* out, u32 cap, u32* pos, i32 value) {
    char buf[16];
    int n = sprintf_s(buf, sizeof(buf), "%d", value);
    if (n < 0) return false;
    return AppendBytes(out, cap, pos, buf, static_cast<u32>(n));
}

// 把某个已知键按 settings 的当前值序列化成一行"key=value\n",追加到 out。
// `key` 一律来自 kKnownKeys 数组,用 strcmp 精确匹配即可,不需要 KeyEquals
// 的大小写不敏感与切片语义。
bool AppendKnownKeyLine(const char* key, const AppSettings& s, char* out, u32 cap, u32* pos) {
    if (!AppendCStr(out, cap, pos, key)) return false;
    if (!AppendBytes(out, cap, pos, "=", 1)) return false;

    if (strcmp(key, "load_remote_images") == 0) {
        if (!AppendCStr(out, cap, pos, s.loadRemoteImages ? "1" : "0")) return false;
    } else if (strcmp(key, "theme") == 0) {
        const char* v = (s.theme == ThemeSetting::Dark) ? "dark" : "light";
        if (!AppendCStr(out, cap, pos, v)) return false;
    } else if (strcmp(key, "zoom") == 0) {
        char buf[32];
        int n = sprintf_s(buf, sizeof(buf), "%.2f", s.zoom);
        if (n < 0) return false;
        if (!AppendBytes(out, cap, pos, buf, static_cast<u32>(n))) return false;
    } else if (strcmp(key, "win_x") == 0) {
        if (!AppendInt(out, cap, pos, s.winX)) return false;
    } else if (strcmp(key, "win_y") == 0) {
        if (!AppendInt(out, cap, pos, s.winY)) return false;
    } else if (strcmp(key, "win_w") == 0) {
        if (!AppendInt(out, cap, pos, s.winW)) return false;
    } else if (strcmp(key, "win_h") == 0) {
        if (!AppendInt(out, cap, pos, s.winH)) return false;
    } else if (strcmp(key, "win_maximized") == 0) {
        if (!AppendCStr(out, cap, pos, s.winMaximized ? "1" : "0")) return false;
    } else {
        const wchar_t* wide = nullptr;
        if (strcmp(key, "font_body_primary") == 0) wide = s.fontBodyPrimary;
        else if (strcmp(key, "font_body_fallback") == 0) wide = s.fontBodyFallback;
        else if (strcmp(key, "font_mono_primary") == 0) wide = s.fontMonoPrimary;
        else if (strcmp(key, "font_mono_fallback") == 0) wide = s.fontMonoFallback;
        if (!wide) return false;
        char valueBuf[kMaxFontFamilyChars * 3 + 1];
        WideToUtf8Fixed(wide, valueBuf, sizeof(valueBuf));
        if (!AppendCStr(out, cap, pos, valueBuf)) return false;
    }
    return AppendBytes(out, cap, pos, "\n", 1);
}

// 核心合并:把 `original`(磁盘上重读到的原文,可能为空)按行扫描,已知键
// 命中就用 `effective` 的当前值改写该行、未知键/注释/空行原样保留;
// `original` 里没有出现过的已知键,在末尾按固定顺序补齐。
// 这正是验收要求的"读原文 -> 替换已知键 -> 追加新键",不是整体覆盖。
bool MergeIniText(StrSlice original, const AppSettings& effective, char* out, u32 outCap,
                   u32* outLen) {
    bool matched[kKnownKeyCount] = {};
    u32 pos = 0;
    u32 cursor = 0;
    while (cursor < original.len) {
        u32 lineStart = cursor;
        u32 lineEnd = cursor;
        while (lineEnd < original.len && original.data[lineEnd] != '\n') ++lineEnd;
        bool hasNewline = lineEnd < original.len;
        u32 rawEnd = lineEnd;
        bool hasCr = rawEnd > lineStart && original.data[rawEnd - 1] == '\r';
        u32 contentEnd = hasCr ? rawEnd - 1 : rawEnd;
        cursor = hasNewline ? lineEnd + 1 : original.len;

        StrSlice trimmed = Trim(StrSlice{original.data + lineStart, contentEnd - lineStart});

        int knownIdx = -1;
        if (trimmed.len > 0 && trimmed.data[0] != ';' && trimmed.data[0] != '#' &&
            trimmed.data[0] != '[') {
            u32 eq = 0;
            while (eq < trimmed.len && trimmed.data[eq] != '=') ++eq;
            if (eq < trimmed.len) {
                StrSlice key = Trim(StrSlice{trimmed.data, eq});
                for (u32 k = 0; k < kKnownKeyCount; ++k) {
                    if (KeyEquals(key, kKnownKeys[k])) { knownIdx = static_cast<int>(k); break; }
                }
            }
        }

        if (knownIdx >= 0) {
            // 已知键:第一次遇到就用当前值改写;同一键在原文里重复出现的
            // 多余行(理论上不该有,但容错)直接丢弃,避免写出重复键。
            if (matched[knownIdx]) continue;
            matched[knownIdx] = true;
            if (!AppendKnownKeyLine(kKnownKeys[knownIdx], effective, out, outCap, &pos)) return false;
        } else {
            // 未知键 / 注释 / 空行 / [section] 行:原样保留,统一按 '\n' 收尾
            // (即便原文是最后一行且没有换行符,也补上,保持输出格式一致)。
            u32 rawLen = lineEnd - lineStart;
            if (!AppendBytes(out, outCap, &pos, original.data + lineStart, rawLen)) return false;
            if (!AppendBytes(out, outCap, &pos, "\n", 1)) return false;
        }
    }

    for (u32 k = 0; k < kKnownKeyCount; ++k) {
        if (matched[k]) continue;
        if (!AppendKnownKeyLine(kKnownKeys[k], effective, out, outCap, &pos)) return false;
    }

    *outLen = pos;
    return true;
}

// 用磁盘当前值起一份 AppSettings(读不到文件就是纯默认值),再把本进程
// 自上次 Load/Save 以来真正改动过的字段(与 g_baseline 比较)覆盖上去。
// 没有 baseline(本进程从未 Load/Save 过)时,保守地认为 target 全部字段
// 都是"真正想要的",直接整体采信 target,不受磁盘当前值影响。
AppSettings ComputeEffectiveSettings(const AppSettings& diskCurrent, const AppSettings& target) {
    AppSettings effective = diskCurrent;
    if (!g_hasBaseline) return target;

    if (target.loadRemoteImages != g_baseline.loadRemoteImages) {
        effective.loadRemoteImages = target.loadRemoteImages;
    }
    if (target.theme != g_baseline.theme || target.hasTheme != g_baseline.hasTheme) {
        effective.theme = target.theme;
        effective.hasTheme = target.hasTheme;
    }
    if (target.zoom != g_baseline.zoom) {
        effective.zoom = target.zoom;
    }
    if (wcscmp(target.fontBodyPrimary, g_baseline.fontBodyPrimary) != 0) {
        wcscpy_s(effective.fontBodyPrimary, kMaxFontFamilyChars, target.fontBodyPrimary);
    }
    if (wcscmp(target.fontBodyFallback, g_baseline.fontBodyFallback) != 0) {
        wcscpy_s(effective.fontBodyFallback, kMaxFontFamilyChars, target.fontBodyFallback);
    }
    if (wcscmp(target.fontMonoPrimary, g_baseline.fontMonoPrimary) != 0) {
        wcscpy_s(effective.fontMonoPrimary, kMaxFontFamilyChars, target.fontMonoPrimary);
    }
    if (wcscmp(target.fontMonoFallback, g_baseline.fontMonoFallback) != 0) {
        wcscpy_s(effective.fontMonoFallback, kMaxFontFamilyChars, target.fontMonoFallback);
    }
    // T56:窗口矩形 4 个键 + 最大化标记,同样只在"本进程真正改动过"时才覆盖
    // 磁盘上的当前值——多开时 A 窗口移动、B 窗口没动,B 退出时不应把 A
    // 刚写好的矩形又覆盖回 B 自己旧的那份。
    if (target.winX != g_baseline.winX) effective.winX = target.winX;
    if (target.winY != g_baseline.winY) effective.winY = target.winY;
    if (target.winW != g_baseline.winW) effective.winW = target.winW;
    if (target.winH != g_baseline.winH) effective.winH = target.winH;
    if (target.winMaximized != g_baseline.winMaximized) effective.winMaximized = target.winMaximized;
    return effective;
}

// 拼出 state.ini 所在目录、正式文件、临时文件三个路径。`localAppData` 须已
// 是 GetEnvironmentVariableW(LOCALAPPDATA) 的结果;任何一段拼接超出缓冲
// 容量都返回 false(视为非法路径,调用方据此静默放弃)。
bool BuildStatePaths(const wchar_t* localAppData, wchar_t* dirPath, wchar_t* filePath,
                      wchar_t* tmpPath, u32 cap) {
    size_t baseLen = wcslen(localAppData);
    if (baseLen + 5 >= cap) return false;  // "\\mdvn" = 5 字符
    wcscpy_s(dirPath, cap, localAppData);
    wcscat_s(dirPath, cap, L"\\mdvn");

    if (wcslen(dirPath) + 11 >= cap) return false;  // "\\state.ini" = 11 字符
    wcscpy_s(filePath, cap, dirPath);
    wcscat_s(filePath, cap, L"\\state.ini");

    if (wcslen(dirPath) + 15 >= cap) return false;  // "\\state.ini.tmp" = 15 字符
    wcscpy_s(tmpPath, cap, dirPath);
    wcscat_s(tmpPath, cap, L"\\state.ini.tmp");
    return true;
}

// 只读打开 filePath,读出原始字节(去掉 UTF-8 BOM),失败/不存在返回空切片。
// 缓冲区由调用方提供(栈上的 kMaxIniBytes 大小),函数只填充、不分配。
StrSlice ReadRawIniFile(const wchar_t* filePath, char* buffer, u32 bufferCap) {
    HANDLE file = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return StrSlice{buffer, 0};

    DWORD read = 0;
    BOOL ok = ReadFile(file, buffer, bufferCap, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) return StrSlice{buffer, 0};

    u32 offset = 0;
    if (read >= 3 && static_cast<u8>(buffer[0]) == 0xEFu && static_cast<u8>(buffer[1]) == 0xBBu &&
        static_cast<u8>(buffer[2]) == 0xBFu) {
        offset = 3;
    }
    return StrSlice{buffer + offset, static_cast<u32>(read) - offset};
}

// 把 tmpPath 写好后原子替换成 filePath;任何一步失败都清理掉半截 tmp 文件,
// 不留下残留(架构 §9:失败静默降级,不留半截状态)。
bool WriteAndReplaceAtomically(const wchar_t* tmpPath, const wchar_t* filePath, const char* data,
                                u32 len) {
    HANDLE file = CreateFileW(tmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(file, data, len, &written, nullptr);
    if (ok) ok = FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || written != len) {
        DeleteFileW(tmpPath);
        return false;
    }

    if (!MoveFileExW(tmpPath, filePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpPath);
        return false;
    }
    return true;
}

}  // namespace

void DefaultAppSettings(AppSettings* out) {
    if (!out) return;
    out->loadRemoteImages = false;  // 裁决 #5:默认完全不触碰 WinHTTP
    out->fontBodyPrimary[0] = 0;
    out->fontBodyFallback[0] = 0;
    out->fontMonoPrimary[0] = 0;
    out->fontMonoFallback[0] = 0;
    out->theme = ThemeSetting::Light;  // 默认浅色
    out->hasTheme = false;
    out->zoom = 1.0f;  // T57:默认档位,与 FontSubsystem 的 kDefaultZoomIndex 对应
    // T56:winW/winH <= 0 表示"从未存过窗口矩形",window.cpp 据此判断走
    // 系统默认位置/尺寸,不进入越界钳制流程。
    out->winX = 0;
    out->winY = 0;
    out->winW = 0;
    out->winH = 0;
    out->winMaximized = false;
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
            // Theme only has Light and Dark; no System theme.
            // System theme is only read once during software initialization when hasTheme is false.
            //
            // 主题仅有亮色和暗色,无系统色。
            // 当 hasTheme 为 false 时仅在软件初始化期读取一次系统主题并存盘。
            if (KeyEquals(value, "light")) {
                out->theme = ThemeSetting::Light;
                out->hasTheme = true;
                applied++;
            } else if (KeyEquals(value, "dark")) {
                out->theme = ThemeSetting::Dark;
                out->hasTheme = true;
                applied++;
            }
            continue;
        }

        if (KeyEquals(key, "zoom")) {
            // T57:钳制到最近的离散档位,而不是直接采信任意浮点(任意浮点
            // 会让 layout 缓存抖动)。解析失败(非数字)时保持默认值 1.0,
            // 不计入 applied——与其余键"非法值不生效"的口径一致。
            float v = 0.0f;
            if (ParseFloat(value, &v)) {
                out->zoom = FontSubsystem::ClampToNearestZoomLevel(v);
                applied++;
            }
            continue;
        }

        // T56:窗口矩形整数键,直接采信 ParseInt 的结果(不做范围钳制——
        // 越界保护是 window.cpp 里 ClampWindowRectToMonitors 的职责,这里只
        // 负责如实还原上次写盘的数值;解析失败的行按"未识别"处理,保持默认值)。
        if (KeyEquals(key, "win_x")) {
            i32 v = 0;
            if (ParseInt(value, &v)) { out->winX = v; applied++; }
            continue;
        }
        if (KeyEquals(key, "win_y")) {
            i32 v = 0;
            if (ParseInt(value, &v)) { out->winY = v; applied++; }
            continue;
        }
        if (KeyEquals(key, "win_w")) {
            i32 v = 0;
            if (ParseInt(value, &v)) { out->winW = v; applied++; }
            continue;
        }
        if (KeyEquals(key, "win_h")) {
            i32 v = 0;
            if (ParseInt(value, &v)) { out->winH = v; applied++; }
            continue;
        }
        if (KeyEquals(key, "win_maximized")) {
            i32 v = 0;
            if (ParseInt(value, &v)) {
                if (v < 0) v = 0;
                if (v > 1) v = 1;
                out->winMaximized = v != 0;
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
    if (n == 0 || n >= MAX_PATH) {
        RememberBaseline(*out);  // T55:即便读不到,也把默认值记成本进程的基线
        return false;
    }

    u32 cursor = static_cast<u32>(n);
    for (u32 i = 0; kIniRelativePath[i] != 0; ++i) {
        if (cursor + 1 >= MAX_PATH) {
            RememberBaseline(*out);
            return false;
        }
        path[cursor++] = kIniRelativePath[i];
    }
    path[cursor] = 0;

    // 只读打开,允许别人同时写;文件不存在是最常见的正常情况,不报错。
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        RememberBaseline(*out);
        return false;
    }

    char buffer[kMaxIniBytes];
    DWORD read = 0;
    BOOL ok = ReadFile(file, buffer, kMaxIniBytes, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) {
        RememberBaseline(*out);
        return false;
    }

    // UTF-8 BOM 容错:记事本存出来的 ini 常带 BOM,跳过它再解析。
    u32 offset = 0;
    if (read >= 3 && static_cast<u8>(buffer[0]) == 0xEFu &&
        static_cast<u8>(buffer[1]) == 0xBBu && static_cast<u8>(buffer[2]) == 0xBFu) {
        offset = 3;
    }
    ParseIniSettings(StrSlice{buffer + offset, static_cast<u32>(read) - offset}, out);
    RememberBaseline(*out);  // T55:本进程从此认为磁盘就是这份内容,供后续 Save 时做字段级 diff
    return true;
}

bool SaveAppSettings(const AppSettings& settings) {
    wchar_t localAppData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;  // 非法/取不到路径,静默放弃

    wchar_t dirPath[MAX_PATH]{};
    wchar_t filePath[MAX_PATH]{};
    wchar_t tmpPath[MAX_PATH]{};
    if (!BuildStatePaths(localAppData, dirPath, filePath, tmpPath, MAX_PATH)) return false;

    // 目录只有两级:%LOCALAPPDATA%(通常已存在)与其下的 mdvn 子目录。手写
    // CreateDirectoryW 逐级建,不引入 shell32 的 SHCreateDirectoryExW(会在
    // 保存这一刻才真正拉起 shell32.dll,是一次可观的模块加载 —— 见 T55 验收注释)。
    CreateDirectoryW(localAppData, nullptr);  // 通常已存在,失败(含已存在)忽略
    if (!CreateDirectoryW(dirPath, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;  // 只读父目录/权限不足等:静默降级,不弹窗、不写日志
    }

    HANDLE mutex = CreateMutexW(nullptr, FALSE, kStateIniMutexName);
    if (mutex == nullptr) return false;

    DWORD waitResult = WaitForSingleObject(mutex, kStateIniMutexTimeoutMs);
    if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) {
        // 超时(或等待本身出错):绝不无限等,放弃本次写盘,不留半截文件。
        CloseHandle(mutex);
        return false;
    }

    char readBuffer[kMaxIniBytes];
    StrSlice originalText = ReadRawIniFile(filePath, readBuffer, kMaxIniBytes);

    AppSettings diskCurrent;
    DefaultAppSettings(&diskCurrent);
    ParseIniSettings(originalText, &diskCurrent);

    AppSettings effective = ComputeEffectiveSettings(diskCurrent, settings);

    char writeBuffer[kMaxIniBytes];
    u32 writeLen = 0;
    bool merged = MergeIniText(originalText, effective, writeBuffer, kMaxIniBytes, &writeLen);

    bool ok = merged && WriteAndReplaceAtomically(tmpPath, filePath, writeBuffer, writeLen);
    if (ok) RememberBaseline(effective);

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return ok;
}

}  // namespace mdvn
