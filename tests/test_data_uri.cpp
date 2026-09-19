// T31 覆盖测试:data: URI 解析与解码(标准 base64、带换行的 base64、
// URL-encoded、缺 ','、超长输入)。纯字节处理,不依赖 WIC/COM。
#include "markair_test.h"
#include "../src/assets/data_uri.h"
#include "../src/util/arena.h"

#include <cstring>
#include <cwchar>

using markair::Arena;
using markair::DataUriPayload;
using markair::ParseDataUri;
using markair::StrSlice;
using markair::u32;

namespace {

#define MARKAIR_MAKE_URI_ARENA() Arena arena; arena.Init(64 * 1024 * 1024)

// 从 C 字面量构造切片(不含结尾 '\0')。
StrSlice Lit(const char* s) { return StrSlice{s, static_cast<u32>(strlen(s))}; }

// 解码结果的字节内容是否与给定 C 字符串一致。
bool BytesEqual(const DataUriPayload& p, const char* expected) {
    u32 n = static_cast<u32>(strlen(expected));
    if (p.len != n) return false;
    for (u32 i = 0; i < n; ++i) {
        if (p.bytes[i] != static_cast<unsigned char>(expected[i])) return false;
    }
    return true;
}

}  // namespace

// 用例:标准 base64——"Hello, World!" 的 base64 应被正确还原。
MARKAIR_TEST(DataUri_StandardBase64) {
    MARKAIR_MAKE_URI_ARENA();
    DataUriPayload p = ParseDataUri(Lit("data:text/plain;base64,SGVsbG8sIFdvcmxkIQ=="), &arena);
    MARKAIR_CHECK(p.valid);
    MARKAIR_CHECK(p.base64);
    MARKAIR_CHECK(BytesEqual(p, "Hello, World!"));
    MARKAIR_CHECK_EQ(p.mime.len, 10u);  // "text/plain"
}

// 用例:base64 载荷中间夹换行/空格(邮件式折行)同样要能解出来。
MARKAIR_TEST(DataUri_Base64WithNewlines) {
    MARKAIR_MAKE_URI_ARENA();
    DataUriPayload p = ParseDataUri(Lit("data:image/png;base64,SGVsbG8s\nIFdvcm\r\nxkIQ=="), &arena);
    MARKAIR_CHECK(p.valid);
    MARKAIR_CHECK(BytesEqual(p, "Hello, World!"));
}

// 用例:没有 ;base64 时按 URL-encoded 解码,%20 -> 空格。
MARKAIR_TEST(DataUri_UrlEncodedPayload) {
    MARKAIR_MAKE_URI_ARENA();
    DataUriPayload p = ParseDataUri(Lit("data:text/plain,Hello%2C%20World%21"), &arena);
    MARKAIR_CHECK(p.valid);
    MARKAIR_CHECK(!p.base64);
    MARKAIR_CHECK(BytesEqual(p, "Hello, World!"));
}

// 用例:缺少 ',' 分隔符是非法输入,返回失败而不是崩溃。
MARKAIR_TEST(DataUri_MissingCommaIsInvalid) {
    MARKAIR_MAKE_URI_ARENA();
    DataUriPayload p = ParseDataUri(Lit("data:image/png;base64SGVsbG8="), &arena);
    MARKAIR_CHECK(!p.valid);
    MARKAIR_CHECK(p.bytes == nullptr);
    MARKAIR_CHECK_EQ(p.len, 0u);
}

// 用例:不以 data: 开头、空输入都判非法。
MARKAIR_TEST(DataUri_NonDataSchemeIsInvalid) {
    MARKAIR_MAKE_URI_ARENA();
    MARKAIR_CHECK(!ParseDataUri(Lit("https://example.com/a.png"), &arena).valid);
    MARKAIR_CHECK(!ParseDataUri(Lit(""), &arena).valid);
    MARKAIR_CHECK(!ParseDataUri(Lit("data:"), &arena).valid);
}

// 用例:base64 里出现非法字符(如 '@')应判非法,不产出半截数据。
MARKAIR_TEST(DataUri_IllegalBase64CharIsInvalid) {
    MARKAIR_MAKE_URI_ARENA();
    DataUriPayload p = ParseDataUri(Lit("data:image/png;base64,SGVs@G8="), &arena);
    MARKAIR_CHECK(!p.valid);
}

// 用例:超长输入(超过 kMaxDataUriBytes 的 base64 载荷)应被拒绝而不是尝试分配。
MARKAIR_TEST(DataUri_OversizedPayloadRejected) {
    MARKAIR_MAKE_URI_ARENA();
    // 构造一个"声称"超过上限的 base64 载荷:上限 16MB,对应 base64 约 21.3M 字符,
    // 这里直接造 24M 个 'A'(在 arena 之外用一块临时分配,避免污染解码 arena)。
    Arena big;
    big.Init(64 * 1024 * 1024);
    const u32 kPayloadChars = 24u * 1024u * 1024u;
    const char kHeader[] = "data:image/png;base64,";
    u32 headerLen = static_cast<u32>(sizeof(kHeader) - 1);
    char* buf = static_cast<char*>(big.Alloc(headerLen + kPayloadChars, 1));
    MARKAIR_CHECK(buf != nullptr);
    if (!buf) return;
    memcpy(buf, kHeader, headerLen);
    memset(buf + headerLen, 'A', kPayloadChars);

    DataUriPayload p = ParseDataUri(StrSlice{buf, headerLen + kPayloadChars}, &arena);
    MARKAIR_CHECK(!p.valid);
}

// 用例:不带 MIME 的最简形式("data:,xyz")也能解析,mime 长度为 0。
MARKAIR_TEST(DataUri_EmptyMime) {
    MARKAIR_MAKE_URI_ARENA();
    DataUriPayload p = ParseDataUri(Lit("data:,abc"), &arena);
    MARKAIR_CHECK(p.valid);
    MARKAIR_CHECK_EQ(p.mime.len, 0u);
    MARKAIR_CHECK(BytesEqual(p, "abc"));
}

// 用例:MIME -> 扩展名映射(T36b 写临时文件时用)。
MARKAIR_TEST(DataUri_ExtensionForMime) {
    MARKAIR_CHECK(wcscmp(markair::ExtensionForMime(Lit("image/png")), L".png") == 0);
    MARKAIR_CHECK(wcscmp(markair::ExtensionForMime(Lit("image/jpeg")), L".jpg") == 0);
    MARKAIR_CHECK(wcscmp(markair::ExtensionForMime(Lit("IMAGE/GIF")), L".gif") == 0);
    MARKAIR_CHECK(wcscmp(markair::ExtensionForMime(Lit("application/octet-stream")), L".bin") == 0);
    MARKAIR_CHECK(wcscmp(markair::ExtensionForMime(StrSlice{nullptr, 0}), L".bin") == 0);
}
