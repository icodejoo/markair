#include "recent_files.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#include <cstring>

namespace markair {

namespace {

// Relative path to history file from %LOCALAPPDATA%.
//
// 历史记录文件相对于 %LOCALAPPDATA% 的固定子路径。
constexpr wchar_t kHistoryRelativePath[] = L"\\markair\\history.txt";

// Buffer size for reading/writing history file. Sized for the documented
// worst case, not the common case: kMaxRecentFiles (100) entries at
// kMaxRecentPathChars (260) UTF-16 code units each, UTF-8-encoded at up to
// 3 bytes per code unit (BMP CJK, the worst realistic case for a Windows
// file path) plus one newline byte per entry — 100 * (260*3 + 1) ≈ 78.1KB.
// 16KB (the original estimate, ASCII-path-only) silently truncated the
// history list once real CJK paths pushed the formatted text past it (code
// review, 2026-09-19); rounded up to 128KB for headroom.
//
// 读写历史记录文件的缓冲区大小。按文档记录的最坏情形而非常见情形定容量:
// kMaxRecentFiles(100)条 × kMaxRecentPathChars(260)个 UTF-16 code unit,
// 每个 code unit 按 UTF-8 最坏 3 字节(BMP 内中文等场景,Windows 路径下
// 现实存在的最坏情况)编码,再加每条一个换行符——100 * (260*3 + 1) ≈
// 78.1KB。原先 16KB(只按 ASCII 路径估的)在真实中文路径把格式化后文本
// 推过这个上限时会静默截断历史列表(代码评审,2026-09-19发现),这里放宽到
// 128KB 留足余量。
constexpr u32 kMaxHistoryFileBytes = 128 * 1024;

/**
 * Decode UTF-8 string into fixed-length UTF-16 wide char buffer. Pure function.
 *
 * 将 UTF-8 字符串解码到定长宽字符缓冲区中。纯函数。
 *
 * @param s UTF-8 input string slice.
 *
 *   输入的 UTF-8 字符串切片。
 *
 * @param out Destination wide character buffer.
 *
 *   目标宽字符缓冲区。
 *
 * @param cap Capacity of wide buffer in characters.
 *
 *   目标宽字符缓冲区的字符容量。
 */
void Utf8ToWide(StrSlice s, wchar_t* out, u32 cap) {
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
            if (w + 2 >= cap) break;
            code -= 0x10000u;
            out[w++] = static_cast<wchar_t>(0xD800u + (code >> 10));
            out[w++] = static_cast<wchar_t>(0xDC00u + (code & 0x3FFu));
        } else {
            out[w++] = static_cast<wchar_t>(code);
        }
    }
    out[w] = 0;
}

/**
 * Encode UTF-16 wide string into UTF-8 char buffer. Pure function.
 *
 * 将 UTF-16 宽字符串编码为 UTF-8 字符缓冲区。纯函数。
 *
 * @param s Input null-terminated UTF-16 string.
 *
 *   以 null 结尾的 UTF-16 宽字符串。
 *
 * @param out Destination char buffer.
 *
 *   目标字符缓冲区。
 *
 * @param cap Capacity of char buffer in bytes.
 *
 *   目标字符缓冲区的字节容量。
 *
 * @return Number of UTF-8 bytes written (excluding null terminator).
 *
 *   实际写入的 UTF-8 字节数（不包含结尾 null 终止符）。
 */
u32 WideToUtf8(const wchar_t* s, char* out, u32 cap) {
    u32 w = 0;
    if (cap == 0) return 0;
    for (const wchar_t* p = s; *p != 0 && w + 1 < cap;) {
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
            code = 0xFFFDu;
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

}  // namespace

void InitRecentFiles(RecentFiles* out) {
    if (!out) return;
    out->count = 0;
    for (u32 i = 0; i < kMaxRecentFiles; ++i) {
        out->entries[i].path[0] = 0;
    }
}

u32 ParseRecentFiles(StrSlice text, RecentFiles* out) {
    if (!out || !text.data) return 0;
    InitRecentFiles(out);

    u32 pos = 0;
    while (pos < text.len && out->count < kMaxRecentFiles) {
        u32 lineEnd = pos;
        while (lineEnd < text.len && text.data[lineEnd] != '\n') ++lineEnd;
        u32 rawEnd = lineEnd;
        if (rawEnd > pos && text.data[rawEnd - 1] == '\r') --rawEnd;

        StrSlice line{text.data + pos, rawEnd - pos};
        pos = lineEnd + 1;

        // Skip leading/trailing whitespace
        u32 start = 0;
        while (start < line.len && (line.data[start] == ' ' || line.data[start] == '\t')) ++start;
        u32 end = line.len;
        while (end > start && (line.data[end - 1] == ' ' || line.data[end - 1] == '\t')) --end;

        if (end > start) {
            wchar_t pathBuf[kMaxRecentPathChars];
            Utf8ToWide(StrSlice{line.data + start, end - start}, pathBuf, kMaxRecentPathChars);
            if (pathBuf[0] != 0) {
                // Deduplicate check
                bool duplicate = false;
                for (u32 i = 0; i < out->count; ++i) {
                    if (_wcsicmp(out->entries[i].path, pathBuf) == 0) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    wcsncpy_s(out->entries[out->count].path, kMaxRecentPathChars, pathBuf, _TRUNCATE);
                    out->count++;
                }
            }
        }
    }
    return out->count;
}

u32 FormatRecentFiles(const RecentFiles& list, char* out, u32 cap) {
    if (!out || cap == 0) return 0;
    u32 pos = 0;
    for (u32 i = 0; i < list.count && i < kMaxRecentFiles; ++i) {
        char lineBuf[kMaxRecentPathChars * 4];
        u32 len = WideToUtf8(list.entries[i].path, lineBuf, sizeof(lineBuf));
        if (len > 0) {
            if (pos + len + 1 >= cap) break;
            memcpy(out + pos, lineBuf, len);
            pos += len;
            out[pos++] = '\n';
        }
    }
    if (pos < cap) out[pos] = 0;
    return pos;
}

void AddRecentFile(RecentFiles* list, const wchar_t* path) {
    if (!list || !path || path[0] == 0) return;

    // Find if already exists
    i32 existingIdx = -1;
    for (u32 i = 0; i < list->count; ++i) {
        if (_wcsicmp(list->entries[i].path, path) == 0) {
            existingIdx = static_cast<i32>(i);
            break;
        }
    }

    if (existingIdx >= 0) {
        // Shift items before existingIdx down by 1
        for (i32 i = existingIdx; i > 0; --i) {
            list->entries[i] = list->entries[i - 1];
        }
    } else {
        // Shift all items down by 1
        u32 maxToShift = (list->count < kMaxRecentFiles) ? list->count : kMaxRecentFiles - 1;
        for (i32 i = static_cast<i32>(maxToShift); i > 0; --i) {
            list->entries[i] = list->entries[i - 1];
        }
        if (list->count < kMaxRecentFiles) {
            list->count++;
        }
    }

    // Place path at front (index 0)
    wcsncpy_s(list->entries[0].path, kMaxRecentPathChars, path, _TRUNCATE);
}

bool RemoveRecentFileAt(RecentFiles* list, u32 index) {
    if (!list || index >= list->count) return false;

    for (u32 i = index; i + 1 < list->count; ++i) {
        list->entries[i] = list->entries[i + 1];
    }
    list->count--;
    list->entries[list->count].path[0] = 0;
    return true;
}

bool LoadRecentFiles(RecentFiles* out) {
    if (!out) return false;
    InitRecentFiles(out);

    wchar_t localAppData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    wchar_t filePath[MAX_PATH]{};
    wcscpy_s(filePath, MAX_PATH, localAppData);
    wcscat_s(filePath, MAX_PATH, kHistoryRelativePath);

    HANDLE file = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    char buffer[kMaxHistoryFileBytes];
    DWORD read = 0;
    BOOL ok = ReadFile(file, buffer, kMaxHistoryFileBytes, &read, nullptr);
    CloseHandle(file);
    if (!ok || read == 0) return false;

    u32 offset = 0;
    if (read >= 3 && static_cast<u8>(buffer[0]) == 0xEFu &&
        static_cast<u8>(buffer[1]) == 0xBBu && static_cast<u8>(buffer[2]) == 0xBFu) {
        offset = 3;
    }

    ParseRecentFiles(StrSlice{buffer + offset, static_cast<u32>(read) - offset}, out);
    return true;
}

bool SaveRecentFiles(const RecentFiles& list) {
    wchar_t localAppData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    wchar_t dirPath[MAX_PATH]{};
    wcscpy_s(dirPath, MAX_PATH, localAppData);
    wcscat_s(dirPath, MAX_PATH, L"\\markair");

    CreateDirectoryW(localAppData, nullptr);
    if (!CreateDirectoryW(dirPath, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }

    wchar_t filePath[MAX_PATH]{};
    wcscpy_s(filePath, MAX_PATH, dirPath);
    wcscat_s(filePath, MAX_PATH, L"\\history.txt");

    wchar_t tmpPath[MAX_PATH]{};
    wcscpy_s(tmpPath, MAX_PATH, dirPath);
    wcscat_s(tmpPath, MAX_PATH, L"\\history.txt.tmp");

    char writeBuffer[kMaxHistoryFileBytes];
    u32 writeLen = FormatRecentFiles(list, writeBuffer, kMaxHistoryFileBytes);

    HANDLE file = CreateFileW(tmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(file, writeBuffer, writeLen, &written, nullptr);
    if (ok) ok = FlushFileBuffers(file);
    CloseHandle(file);

    if (!ok || written != writeLen) {
        DeleteFileW(tmpPath);
        return false;
    }

    if (!MoveFileExW(tmpPath, filePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpPath);
        return false;
    }
    return true;
}

}  // namespace markair
