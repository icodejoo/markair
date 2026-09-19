// ExpandAttribute 实现:见 attr.h。
#include "attr.h"

#include "../util/span.h"

#include <cstring>

namespace markair {

namespace {

// 把一个 Unicode 码点编码为 UTF-8 并追加到 buf;非法码点(代理区/超出范围)
// 替换为 U+FFFD,与 str.h 里 Utf8ToUtf16/Utf16ToUtf8 的容错策略保持一致。
void AppendCodepointUtf8(Vec<char>& buf, u32 cp) {
    if ((cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) cp = 0xFFFDu;
    if (cp <= 0x7Fu) {
        buf.Push(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        buf.Push(static_cast<char>(0xC0u | (cp >> 6)));
        buf.Push(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        buf.Push(static_cast<char>(0xE0u | (cp >> 12)));
        buf.Push(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        buf.Push(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        buf.Push(static_cast<char>(0xF0u | (cp >> 18)));
        buf.Push(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        buf.Push(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        buf.Push(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

// 已知命名实体表:实体名(不含 & ;)-> 对应字符。
struct NamedEntity {
    const char* name;
    u32 nameLen;
    char value;
};
constexpr NamedEntity kNamedEntities[] = {
    {"amp", 3, '&'}, {"lt", 2, '<'}, {"gt", 2, '>'}, {"quot", 4, '"'}, {"apos", 4, '\''},
};

// 原样追加一段字节(未识别实体/异常格式的兜底路径)。
void AppendRaw(Vec<char>& buf, const char* s, u32 len) {
    for (u32 i = 0; i < len; ++i) buf.Push(s[i]);
}

// 解码一个 MD_TEXT_ENTITY 子串(如 "&amp;"、"&#39;"、"&#x1F600;")并追加到 buf。
void AppendEntity(Vec<char>& buf, const char* s, u32 len) {
    if (len < 2 || s[0] != '&' || s[len - 1] != ';') {
        AppendRaw(buf, s, len); // 不符合实体形状,原样保留
        return;
    }
    const char* inner = s + 1;
    u32 innerLen = len - 2;

    if (innerLen > 0 && inner[0] == '#') {
        const char* numStart = inner + 1;
        u32 numLen = innerLen - 1;
        bool isHex = numLen > 0 && (numStart[0] == 'x' || numStart[0] == 'X');
        if (isHex) {
            numStart += 1;
            numLen -= 1;
        }
        u32 cp = 0;
        bool valid = numLen > 0;
        for (u32 i = 0; valid && i < numLen; ++i) {
            char c = numStart[i];
            u32 digit;
            if (c >= '0' && c <= '9') digit = static_cast<u32>(c - '0');
            else if (isHex && c >= 'a' && c <= 'f') digit = static_cast<u32>(c - 'a' + 10);
            else if (isHex && c >= 'A' && c <= 'F') digit = static_cast<u32>(c - 'A' + 10);
            else { valid = false; break; }
            cp = cp * (isHex ? 16u : 10u) + digit;
        }
        if (valid) {
            AppendCodepointUtf8(buf, cp);
            return;
        }
    } else {
        for (const NamedEntity& e : kNamedEntities) {
            if (innerLen == e.nameLen && memcmp(inner, e.name, e.nameLen) == 0) {
                buf.Push(e.value);
                return;
            }
        }
    }
    // 未识别的命名实体或格式异常:退化为原始字面文本,不丢数据。
    AppendRaw(buf, s, len);
}

} // namespace

StrSlice ExpandAttribute(const MD_ATTRIBUTE& attr, Arena* arena) {
    if (attr.size == 0 || attr.text == nullptr) {
        return StrSlice{"", 0};
    }

    Vec<char> buf(arena);
    u32 total = static_cast<u32>(attr.size);
    u32 segStart = 0;
    u32 seg = 0;
    while (segStart < total) {
        u32 segEnd = static_cast<u32>(attr.substr_offsets[seg + 1]);
        const char* segData = attr.text + segStart;
        u32 segLen = segEnd - segStart;
        switch (attr.substr_types[seg]) {
            case MD_TEXT_ENTITY:
                AppendEntity(buf, segData, segLen);
                break;
            case MD_TEXT_NULLCHAR:
                // U+FFFD 的 UTF-8 编码。
                buf.Push(static_cast<char>(0xEF));
                buf.Push(static_cast<char>(0xBF));
                buf.Push(static_cast<char>(0xBD));
                break;
            case MD_TEXT_NORMAL:
            default:
                AppendRaw(buf, segData, segLen);
                break;
        }
        segStart = segEnd;
        ++seg;
    }
    return StrSlice{buf.Data(), buf.Size()};
}

LinkTargetKind ClassifyLinkTarget(StrSlice href) {
    if (href.data == nullptr || href.len == 0) return LinkTargetKind::Unknown;
    if (href.data[0] == '#') return LinkTargetKind::Anchor;
    if (href.len >= 5 && memcmp(href.data, "data:", 5) == 0) return LinkTargetKind::DataUri;
    // 含 "scheme://" 形态(http/https 及其它协议)一律视为外部链接。
    for (u32 i = 0; i + 2 < href.len; ++i) {
        if (href.data[i] == ':' && href.data[i + 1] == '/' && href.data[i + 2] == '/') {
            return LinkTargetKind::External;
        }
    }
    return LinkTargetKind::RelativePath;
}

} // namespace markair
