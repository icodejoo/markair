#include "navigate.h"

#include <pathcch.h>
#include <shellapi.h>

namespace mdvn {

namespace {

// T36 允许交给系统默认程序打开的 scheme 白名单(安全边界:其余一律拒绝)。
const char* const kAllowedSchemes[] = {"http", "https", "mailto", "file"};
constexpr u32 kAllowedSchemeCount = 4;

// 认定为"可在窗口内替换打开"的 Markdown 扩展名。
const char* const kMarkdownExtensions[] = {".md", ".markdown"};
constexpr u32 kMarkdownExtensionCount = 2;

// ASCII 小写折叠。
char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// 切片与字面量的大小写不敏感比较(长度须完全一致)。
bool EqualsIgnoreCase(StrSlice s, const char* literal) {
    u32 i = 0;
    for (; i < s.len; ++i) {
        if (literal[i] == 0) return false;
        if (LowerAscii(s.data[i]) != literal[i]) return false;
    }
    return literal[i] == 0;
}

// 取出 href 的 scheme(不含冒号)。没有 scheme、或只有单个字母(Windows 盘符
// `C:\...`)时返回 len 为 0 的切片。
StrSlice SchemeOf(StrSlice href) {
    if (!href.data || href.len == 0) return StrSlice{nullptr, 0};
    char first = LowerAscii(href.data[0]);
    if (first < 'a' || first > 'z') return StrSlice{nullptr, 0};
    u32 i = 1;
    while (i < href.len) {
        char c = href.data[i];
        if (c == ':') break;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '+' || c == '-' || c == '.';
        if (!ok) return StrSlice{nullptr, 0};
        ++i;
    }
    if (i >= href.len) return StrSlice{nullptr, 0};  // 没找到冒号
    if (i == 1) return StrSlice{nullptr, 0};          // 单字母 = 盘符,不是 scheme
    return StrSlice{href.data, i};
}

// 去掉 href 末尾的 `#fragment` 与 `?query`,只留路径部分。
StrSlice PathPartOf(StrSlice href) {
    u32 end = href.len;
    for (u32 i = 0; i < href.len; ++i) {
        if (href.data[i] == '#' || href.data[i] == '?') { end = i; break; }
    }
    return StrSlice{href.data, end};
}

// 路径部分是否以某个 Markdown 扩展名结尾(大小写不敏感)。
bool HasMarkdownExtension(StrSlice path) {
    for (u32 e = 0; e < kMarkdownExtensionCount; ++e) {
        const char* ext = kMarkdownExtensions[e];
        u32 extLen = 0;
        while (ext[extLen] != 0) ++extLen;
        if (path.len < extLen) continue;
        StrSlice tail{path.data + path.len - extLen, extLen};
        if (EqualsIgnoreCase(tail, ext)) return true;
    }
    return false;
}

// 十六进制字符 -> 数值,非法返回 -1。
int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 百分号解码到固定缓冲(不写结尾符,返回写入字节数)。非法的 `%xx` 原样保留。
u32 PercentDecode(StrSlice s, char* out, u32 cap) {
    u32 w = 0;
    for (u32 i = 0; i < s.len && w < cap; ++i) {
        if (s.data[i] == '%' && i + 2 < s.len) {
            int hi = HexValue(s.data[i + 1]);
            int lo = HexValue(s.data[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[w++] = static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out[w++] = s.data[i];
    }
    return w;
}

// %TEMP% 下专属子目录名,所有会话临时文件都放在这里,便于用户/系统识别与清理。
constexpr wchar_t kTempSubdirectory[] = L"mdvn";

// 临时文件名前缀,配合会话内自增编号拼成 mdvn_0001.png 这样的名字。
constexpr wchar_t kTempFilePrefix[] = L"mdvn_";

// 扩展名缺省值。
constexpr wchar_t kDefaultExtension[] = L".bin";

// 宽字符串长度(不含结尾符)。
u32 WideLength(const wchar_t* s) {
    u32 n = 0;
    while (s && s[n] != 0) ++n;
    return n;
}

// 往 dst 追加一段宽字符,越界时截断并返回 false。cursor 会被就地推进。
bool AppendWide(wchar_t* dst, u32 cap, u32* cursor, const wchar_t* src, u32 srcLen) {
    if (*cursor + srcLen + 1u > cap) return false;
    for (u32 i = 0; i < srcLen; ++i) dst[*cursor + i] = src[i];
    *cursor += srcLen;
    dst[*cursor] = 0;
    return true;
}

// 把一个 u32 按固定 4 位补零写成十进制(超过 9999 时退化为自然长度),
// 手写实现避免为几个数字引入 swprintf 的格式串解析。
u32 FormatSequence(u32 value, wchar_t* buf, u32 cap) {
    wchar_t tmp[12];
    u32 n = 0;
    if (value == 0) {
        tmp[n++] = L'0';
    } else {
        while (value > 0 && n < 12) {
            tmp[n++] = static_cast<wchar_t>(L'0' + (value % 10));
            value /= 10;
        }
    }
    u32 pad = (n < 4) ? (4u - n) : 0u;
    if (n + pad + 1u > cap) return 0;
    u32 out = 0;
    for (u32 i = 0; i < pad; ++i) buf[out++] = L'0';
    for (u32 i = 0; i < n; ++i) buf[out++] = tmp[n - 1 - i];
    buf[out] = 0;
    return out;
}

// 把 UTF-8 的 href 拓宽成宽字符路径,顺便把 URL 风格的 '/' 换成 '\\'。
// 本地路径在 Markdown 里通常是 ASCII 或 UTF-8;这里用 MultiByteToWideChar
// 正确处理非 ASCII 文件名。
bool HrefToWidePath(StrSlice href, wchar_t* dst, u32 cap) {
    if (!href.data || href.len == 0) return false;
    int written = MultiByteToWideChar(CP_UTF8, 0, href.data, static_cast<int>(href.len),
                                       dst, static_cast<int>(cap) - 1);
    if (written <= 0) return false;
    dst[written] = 0;
    for (int i = 0; i < written; ++i) {
        if (dst[i] == L'/') dst[i] = L'\\';
    }
    return true;
}

// 把 href 原样(不做 '/' -> '\\' 转换)拓宽成宽字符串,供 URL 使用。
bool HrefToWideUrl(StrSlice href, wchar_t* dst, u32 cap) {
    if (!href.data || href.len == 0) return false;
    int written = MultiByteToWideChar(CP_UTF8, 0, href.data, static_cast<int>(href.len),
                                       dst, static_cast<int>(cap) - 1);
    if (written <= 0) return false;
    dst[written] = 0;
    return true;
}

// slug 缓冲上限:标题超长时截断即可,锚点本来就不该是一整段文章。
constexpr u32 kMaxSlugBytes = 512;

// 把一个标题块的全部可见 inline 文本拼起来后生成 slug。
u32 BuildBlockSlug(const Document& doc, u32 blockIndex, char* out, u32 cap) {
    const Block& b = doc.blocks[blockIndex];
    char text[kMaxSlugBytes];
    u32 len = 0;
    for (u32 i = 0; i < b.inlineCount && len < kMaxSlugBytes; ++i) {
        const Inline& in = doc.inlines[b.firstInlineIdx + i];
        // 图片 alt 与脚注引用不是标题的可见文字,不参与 slug(与 GitHub 口径一致)。
        if (in.flags & (kInlineFlagImage | kInlineFlagFootnoteRef)) continue;
        for (u32 k = 0; k < in.textLen && len < kMaxSlugBytes; ++k) {
            text[len++] = doc.source.data[in.textOffset + k];
        }
    }
    return MakeHeadingSlug(StrSlice{text, len}, out, cap);
}

}  // namespace

bool IsAllowedExternalScheme(StrSlice href) {
    StrSlice scheme = SchemeOf(href);
    if (scheme.len == 0) return false;
    for (u32 i = 0; i < kAllowedSchemeCount; ++i) {
        if (EqualsIgnoreCase(scheme, kAllowedSchemes[i])) return true;
    }
    return false;
}

LinkAction DecideLinkAction(StrSlice href) {
    if (!href.data || href.len == 0) return LinkAction::Reject;
    if (href.data[0] == '#') return LinkAction::ScrollToAnchor;

    StrSlice scheme = SchemeOf(href);
    if (scheme.len > 0) {
        // 带 scheme 的目标只有白名单里的才允许执行;`javascript:` / `data:` /
        // `vbscript:` 等一律在这里被挡掉,后面不会有任何 ShellExecuteW。
        return IsAllowedExternalScheme(href) ? LinkAction::OpenExternal : LinkAction::Reject;
    }

    // 没有 scheme:当成本地路径。只有 .md / .markdown 才在窗口内替换打开,
    // 其余本地文件(图片、压缩包……)在只读查看器里不提供打开入口。
    return HasMarkdownExtension(PathPartOf(href)) ? LinkAction::OpenMarkdown : LinkAction::Reject;
}

u32 MakeHeadingSlug(StrSlice text, char* out, u32 cap) {
    if (!out || cap == 0) return 0;
    u32 w = 0;
    for (u32 i = 0; i < text.len && w + 1 < cap; ++i) {
        unsigned char c = static_cast<unsigned char>(text.data[i]);
        if (c >= 0x80u) {
            out[w++] = static_cast<char>(c);  // 非 ASCII 字节原样保留(中文标题可用)
        } else if (c >= 'A' && c <= 'Z') {
            out[w++] = static_cast<char>(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out[w++] = static_cast<char>(c);
        } else if (c == ' ' || c == '\t') {
            out[w++] = '-';
        }
        // 其余 ASCII 标点(. , ! ? : ; 括号……)按 GitHub 规则直接丢弃。
    }
    out[w] = 0;
    return w;
}

u32 FindAnchorBlock(const Document& doc, StrSlice anchor) {
    if (!anchor.data || anchor.len == 0) return kInvalidIndex;
    StrSlice raw = anchor;
    if (raw.data[0] == '#') raw = StrSlice{raw.data + 1, raw.len - 1};
    if (raw.len == 0) return kInvalidIndex;

    // GitHub 对中文标题生成的链接是百分号编码的,先解码再按同一套 slug 规则比对。
    char decoded[kMaxSlugBytes];
    u32 decodedLen = PercentDecode(raw, decoded, kMaxSlugBytes);

    char wanted[kMaxSlugBytes];
    u32 wantedLen = MakeHeadingSlug(StrSlice{decoded, decodedLen}, wanted, kMaxSlugBytes);
    if (wantedLen == 0) return kInvalidIndex;

    char slug[kMaxSlugBytes];
    for (u32 i = 0; i < doc.blocks.Size(); ++i) {
        if (doc.blocks[i].type != BlockType::Heading) continue;
        u32 slugLen = BuildBlockSlug(doc, i, slug, kMaxSlugBytes);
        if (slugLen != wantedLen) continue;
        bool same = true;
        for (u32 k = 0; k < slugLen && same; ++k) same = slug[k] == wanted[k];
        if (same) return i;
    }
    return kInvalidIndex;
}

bool ResolveMarkdownPath(StrSlice href, const wchar_t* docDirectory, wchar_t* out, u32 cap) {
    if (!out || cap == 0) return false;
    out[0] = 0;

    StrSlice path = PathPartOf(href);
    if (path.len == 0) return false;

    // 链接里的 %20 之类先解码,再转成宽字符,最后把 URL 风格的 '/' 换成 '\\'。
    char decoded[MAX_PATH * 2];
    u32 decodedLen = PercentDecode(path, decoded, MAX_PATH * 2);
    if (decodedLen == 0) return false;

    wchar_t rel[MAX_PATH]{};
    int written = MultiByteToWideChar(CP_UTF8, 0, decoded, static_cast<int>(decodedLen),
                                       rel, MAX_PATH - 1);
    if (written <= 0) return false;
    rel[written] = 0;
    for (int i = 0; i < written; ++i) {
        if (rel[i] == L'/') rel[i] = L'\\';
    }

    bool absolute = (rel[0] == L'\\') || (rel[0] != 0 && rel[1] == L':');
    HRESULT hr;
    if (absolute || !docDirectory || docDirectory[0] == 0) {
        // 绝对路径(或没有文档目录可参照):只做规范化,不做拼接。
        hr = PathCchCanonicalize(out, cap, rel);
    } else {
        // PathCchCombineEx 内部已完成 "拼接 + 规范化",`..\..\` 会被真正折叠掉,
        // 且不会越过盘符根目录 —— 这就是路径穿越在本实现里的处理方式。
        hr = PathCchCombineEx(out, cap, docDirectory, rel, PATHCCH_ALLOW_LONG_PATHS);
    }
    if (FAILED(hr)) {
        out[0] = 0;
        return false;
    }
    return out[0] != 0;
}

bool OpenExternalTarget(StrSlice href) {
    // 安全边界:白名单判定在任何 ShellExecuteW 之前,拒绝即彻底不执行。
    if (!IsAllowedExternalScheme(href)) return false;

    wchar_t url[MAX_PATH * 4]{};
    if (!HrefToWideUrl(href, url, MAX_PATH * 4)) return false;

    HINSTANCE rc = ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;  // Win32 的历史约定:> 32 表示成功
}

bool MarkdownFileExists(const wchar_t* path) {
    if (!path || path[0] == 0) return false;
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

ImageOpenAction DecideImageOpenAction(LinkTargetKind kind, bool hasRawBytes) {
    switch (kind) {
    case LinkTargetKind::RelativePath:
        // 本地文件:直接打开原文件,不需要任何中转,也拿得到未降采样的原图。
        return ImageOpenAction::OpenLocalPath;
    case LinkTargetKind::DataUri:
    case LinkTargetKind::External:
        // data: URI 与网络图片都没有磁盘上的原文件,必须先把原始字节落成临时文件;
        // 网络图片还没下载完时没有字节可写,只能拒绝。
        return hasRawBytes ? ImageOpenAction::WriteTempThenOpen : ImageOpenAction::Reject;
    case LinkTargetKind::Anchor:
    case LinkTargetKind::Unknown:
    default:
        return ImageOpenAction::Reject;
    }
}

TempFileRegistry::TempFileRegistry(Arena* arena)
    : arena_(arena), paths_(arena), nextSequence_(1) {}

const wchar_t* TempFileRegistry::WriteTempFile(const void* bytes, u32 len,
                                                const wchar_t* extension) {
    if (!arena_ || !bytes || len == 0) return nullptr;
    if (!extension || extension[0] == 0) extension = kDefaultExtension;

    wchar_t tempRoot[MAX_PATH]{};
    DWORD rootLen = GetTempPathW(MAX_PATH, tempRoot);
    if (rootLen == 0 || rootLen >= MAX_PATH) return nullptr;

    // 拼 %TEMP%\mdvn\ 并确保目录存在(已存在时 CreateDirectoryW 返回 ERROR_ALREADY_EXISTS)。
    wchar_t dir[MAX_PATH]{};
    u32 cursor = 0;
    if (!AppendWide(dir, MAX_PATH, &cursor, tempRoot, static_cast<u32>(rootLen))) return nullptr;
    if (cursor > 0 && dir[cursor - 1] != L'\\') {
        if (!AppendWide(dir, MAX_PATH, &cursor, L"\\", 1)) return nullptr;
    }
    if (!AppendWide(dir, MAX_PATH, &cursor, kTempSubdirectory,
                    WideLength(kTempSubdirectory))) {
        return nullptr;
    }
    if (!CreateDirectoryW(dir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return nullptr;
    }

    // 会话内自增编号:同一次运行中不会重名;跨会话可能同名,写入用 CREATE_ALWAYS 覆盖。
    wchar_t seq[16]{};
    u32 seqLen = FormatSequence(nextSequence_, seq, 16);
    if (seqLen == 0) return nullptr;

    if (!AppendWide(dir, MAX_PATH, &cursor, L"\\", 1)) return nullptr;
    if (!AppendWide(dir, MAX_PATH, &cursor, kTempFilePrefix, WideLength(kTempFilePrefix))) {
        return nullptr;
    }
    if (!AppendWide(dir, MAX_PATH, &cursor, seq, seqLen)) return nullptr;
    if (!AppendWide(dir, MAX_PATH, &cursor, extension, WideLength(extension))) return nullptr;

    HANDLE file = CreateFileW(dir, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return nullptr;

    DWORD written = 0;
    BOOL ok = WriteFile(file, bytes, len, &written, nullptr);
    CloseHandle(file);
    if (!ok || written != len) {
        DeleteFileW(dir);
        return nullptr;
    }

    // 路径复制到 Arena 上并登记,供退出时统一 DeleteFileW。
    u32 pathLen = cursor;
    wchar_t* stored = static_cast<wchar_t*>(
        arena_->Alloc(sizeof(wchar_t) * (pathLen + 1u), alignof(wchar_t)));
    if (!stored) {
        DeleteFileW(dir);
        return nullptr;
    }
    for (u32 i = 0; i <= pathLen; ++i) stored[i] = dir[i];
    if (!paths_.Push(stored)) {
        // 清单登记失败(Arena 耗尽):不留下无人认领的临时文件。
        DeleteFileW(stored);
        return nullptr;
    }

    nextSequence_++;
    return stored;
}

u32 TempFileRegistry::CleanupAll() {
    u32 deleted = 0;
    for (u32 i = 0; i < paths_.Size(); ++i) {
        if (DeleteFileW(paths_[i])) deleted++;
        // 删除失败(文件被查看器独占等)静默忽略:%TEMP% 的系统级清理会兜底。
    }
    // Vec 没有 Clear 接口;重新绑定同一个 Arena 即可让清单归零(与 layout.cpp 同样的手法)。
    paths_ = Vec<const wchar_t*>(arena_);
    return deleted;
}

ImageOpenResult OpenImageOriginal(StrSlice href, LinkTargetKind kind,
                                   const wchar_t* docDirectory,
                                   const void* rawBytes, u32 rawLen,
                                   const wchar_t* extension,
                                   TempFileRegistry* temps) {
    ImageOpenAction action = DecideImageOpenAction(kind, rawBytes != nullptr && rawLen > 0);
    if (action == ImageOpenAction::Reject) return ImageOpenResult::NoData;

    const wchar_t* targetPath = nullptr;
    wchar_t localPath[MAX_PATH * 2]{};

    if (action == ImageOpenAction::OpenLocalPath) {
        wchar_t rel[MAX_PATH]{};
        if (!HrefToWidePath(href, rel, MAX_PATH)) return ImageOpenResult::NoData;

        // 相对路径按文档所在目录解析;绝对路径(含盘符或以 '\' 开头)原样使用。
        bool absolute = (rel[0] == L'\\') ||
                         (rel[0] != 0 && rel[1] == L':');
        if (absolute || !docDirectory || docDirectory[0] == 0) {
            u32 cursor = 0;
            if (!AppendWide(localPath, MAX_PATH * 2, &cursor, rel, WideLength(rel))) {
                return ImageOpenResult::NoData;
            }
        } else {
            u32 cursor = 0;
            u32 dirLen = WideLength(docDirectory);
            if (!AppendWide(localPath, MAX_PATH * 2, &cursor, docDirectory, dirLen)) {
                return ImageOpenResult::NoData;
            }
            if (cursor > 0 && localPath[cursor - 1] != L'\\') {
                if (!AppendWide(localPath, MAX_PATH * 2, &cursor, L"\\", 1)) {
                    return ImageOpenResult::NoData;
                }
            }
            if (!AppendWide(localPath, MAX_PATH * 2, &cursor, rel, WideLength(rel))) {
                return ImageOpenResult::NoData;
            }
        }
        targetPath = localPath;
    } else {
        if (!temps) return ImageOpenResult::WriteFailed;
        targetPath = temps->WriteTempFile(rawBytes, rawLen, extension);
        if (!targetPath) return ImageOpenResult::WriteFailed;
    }

    // ShellExecuteW 的返回值 > 32 表示成功(Win32 的历史约定)。
    HINSTANCE rc = ShellExecuteW(nullptr, L"open", targetPath, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(rc) <= 32) return ImageOpenResult::ShellFailed;
    return ImageOpenResult::Opened;
}

}  // namespace mdvn
