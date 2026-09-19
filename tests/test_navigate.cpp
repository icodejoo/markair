// T36b 覆盖测试:图片点击查看原图(本地路径 / data: URI 两种打开分支的判定,
// 临时文件的写入、清单登记与统一清理),外加 T34 的 URL 解析纯函数。
//
// 不在单测里真的调用 ShellExecuteW —— 拉起系统看图工具属于人工验证项;
// 这里覆盖的是"选择哪条分支"与"临时文件生命周期"这两块可自动化的逻辑。
#include "markair_test.h"
#include "../src/assets/remote.h"
#include "../src/doc/parser.h"
#include "../src/shell/navigate.h"
#include "../src/util/arena.h"

#include <cstring>
#include <cwchar>

using markair::Arena;
using markair::DecideImageOpenAction;
using markair::DecideLinkAction;
using markair::Document;
using markair::FindAnchorBlock;
using markair::IsAllowedExternalScheme;
using markair::kInvalidIndex;
using markair::LinkAction;
using markair::MakeHeadingSlug;
using markair::OpenExternalTarget;
using markair::ParseMarkdown;
using markair::ResolveMarkdownPath;
using markair::ImageOpenAction;
using markair::ImageOpenResult;
using markair::LinkTargetKind;
using markair::OpenImageOriginal;
using markair::ParseHttpUrl;
using markair::StrSlice;
using markair::TempFileRegistry;
using markair::u16;
using markair::u32;

namespace {

#define MARKAIR_MAKE_NAV_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

StrSlice Lit(const char* s) { return StrSlice{s, static_cast<u32>(strlen(s))}; }

// 判断某个路径当前在磁盘上是否存在。
bool FileExists(const wchar_t* path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

}  // namespace

// 用例:本地路径图片走"直接打开原文件"分支,不需要任何原始字节。
MARKAIR_TEST(Navigate_LocalPathOpensOriginalFile) {
    MARKAIR_CHECK(DecideImageOpenAction(LinkTargetKind::RelativePath, false) ==
               ImageOpenAction::OpenLocalPath);
    MARKAIR_CHECK(DecideImageOpenAction(LinkTargetKind::RelativePath, true) ==
               ImageOpenAction::OpenLocalPath);
}

// 用例:data: URI 有原始字节时走"写临时文件再打开",没有字节则拒绝。
MARKAIR_TEST(Navigate_DataUriWritesTempFile) {
    MARKAIR_CHECK(DecideImageOpenAction(LinkTargetKind::DataUri, true) ==
               ImageOpenAction::WriteTempThenOpen);
    MARKAIR_CHECK(DecideImageOpenAction(LinkTargetKind::DataUri, false) ==
               ImageOpenAction::Reject);
}

// 用例:网络图片未下载(无字节)时拒绝打开原图 —— 那一次点击的含义是 T34 的"点击加载"。
MARKAIR_TEST(Navigate_RemoteWithoutBytesIsRejected) {
    MARKAIR_CHECK(DecideImageOpenAction(LinkTargetKind::External, false) ==
               ImageOpenAction::Reject);
    MARKAIR_CHECK(DecideImageOpenAction(LinkTargetKind::External, true) ==
               ImageOpenAction::WriteTempThenOpen);
}

// 用例:锚点/未知目标一律拒绝(它们根本不是图片来源)。
MARKAIR_TEST(Navigate_AnchorAndUnknownRejected) {
    MARKAIR_CHECK(DecideImageOpenAction(LinkTargetKind::Anchor, true) == ImageOpenAction::Reject);
    MARKAIR_CHECK(DecideImageOpenAction(LinkTargetKind::Unknown, true) == ImageOpenAction::Reject);
}

// 用例:临时文件真的被写到 %TEMP%\markair\、登记进清单,CleanupAll 之后全部消失。
MARKAIR_TEST(Navigate_TempFileRegistryWritesAndCleansUp) {
    MARKAIR_MAKE_NAV_ARENA();
    TempFileRegistry temps(&arena);
    MARKAIR_CHECK_EQ(temps.Count(), 0u);

    const char payload[] = "markair-temp-file-payload";
    u32 payloadLen = static_cast<u32>(sizeof(payload) - 1);

    const wchar_t* p1 = temps.WriteTempFile(payload, payloadLen, L".png");
    const wchar_t* p2 = temps.WriteTempFile(payload, payloadLen, L".jpg");
    MARKAIR_CHECK(p1 != nullptr);
    MARKAIR_CHECK(p2 != nullptr);
    MARKAIR_CHECK_EQ(temps.Count(), 2u);

    if (p1 && p2) {
        // 会话内自增编号:两次写出的路径必须不同。
        MARKAIR_CHECK(wcscmp(p1, p2) != 0);
        MARKAIR_CHECK(FileExists(p1));
        MARKAIR_CHECK(FileExists(p2));
        // 路径落在 %TEMP%\markair\ 下,且扩展名被保留。
        MARKAIR_CHECK(wcsstr(p1, L"\\markair\\markair_") != nullptr);
        MARKAIR_CHECK(wcsstr(p1, L".png") != nullptr);
        MARKAIR_CHECK(wcsstr(p2, L".jpg") != nullptr);
    }

    // 清单登记的就是刚写出的这两个路径。
    MARKAIR_CHECK(p1 != nullptr && temps.At(0) == p1);
    MARKAIR_CHECK(p2 != nullptr && temps.At(1) == p2);

    u32 deleted = temps.CleanupAll();
    MARKAIR_CHECK_EQ(deleted, 2u);
    MARKAIR_CHECK_EQ(temps.Count(), 0u);
    if (p1) MARKAIR_CHECK(!FileExists(p1));
    if (p2) MARKAIR_CHECK(!FileExists(p2));
}

// 用例:非法入参(空字节/零长度/空扩展名)不崩溃;空扩展名退化为 .bin。
MARKAIR_TEST(Navigate_TempFileRegistryRejectsBadInput) {
    MARKAIR_MAKE_NAV_ARENA();
    TempFileRegistry temps(&arena);

    MARKAIR_CHECK(temps.WriteTempFile(nullptr, 10, L".png") == nullptr);
    MARKAIR_CHECK(temps.WriteTempFile("x", 0, L".png") == nullptr);
    MARKAIR_CHECK_EQ(temps.Count(), 0u);

    const wchar_t* p = temps.WriteTempFile("abc", 3, nullptr);
    MARKAIR_CHECK(p != nullptr);
    if (p) MARKAIR_CHECK(wcsstr(p, L".bin") != nullptr);
    temps.CleanupAll();
}

// 用例:OpenImageOriginal 在"没有可打开数据"时返回 NoData,不触碰文件系统、不崩溃。
MARKAIR_TEST(Navigate_OpenImageOriginalNoData) {
    MARKAIR_MAKE_NAV_ARENA();
    TempFileRegistry temps(&arena);

    ImageOpenResult r = OpenImageOriginal(Lit("https://example.com/a.png"),
                                           LinkTargetKind::External, nullptr, nullptr, 0,
                                           nullptr, &temps);
    MARKAIR_CHECK(r == ImageOpenResult::NoData);
    MARKAIR_CHECK_EQ(temps.Count(), 0u);

    r = OpenImageOriginal(Lit("#section"), LinkTargetKind::Anchor, nullptr, nullptr, 0,
                          nullptr, &temps);
    MARKAIR_CHECK(r == ImageOpenResult::NoData);

    // 空 href 的本地路径同样安全返回,不会拼出一个空路径去 ShellExecute。
    r = OpenImageOriginal(StrSlice{nullptr, 0}, LinkTargetKind::RelativePath, L"C:\\docs",
                          nullptr, 0, nullptr, &temps);
    MARKAIR_CHECK(r == ImageOpenResult::NoData);
}

// ---- 以下为 T36 链接点击行为的纯判定部分(不真的 ShellExecuteW / 不读磁盘)----

// 用例(scheme 判定 + 安全边界):只有 http/https/mailto/file 允许交给系统打开,
// javascript: 之类一律拒绝 —— 拒绝发生在任何 ShellExecuteW 之前。
MARKAIR_TEST(Navigate_SchemeAllowList) {
    MARKAIR_CHECK(IsAllowedExternalScheme(Lit("http://a.com")));
    MARKAIR_CHECK(IsAllowedExternalScheme(Lit("HTTPS://a.com")));
    MARKAIR_CHECK(IsAllowedExternalScheme(Lit("mailto:me@example.com")));
    MARKAIR_CHECK(IsAllowedExternalScheme(Lit("file:///C:/a.md")));

    MARKAIR_CHECK(!IsAllowedExternalScheme(Lit("javascript:alert(1)")));
    MARKAIR_CHECK(!IsAllowedExternalScheme(Lit("JavaScript:alert(1)")));
    MARKAIR_CHECK(!IsAllowedExternalScheme(Lit("vbscript:msgbox")));
    MARKAIR_CHECK(!IsAllowedExternalScheme(Lit("data:text/html,<b>x</b>")));
    MARKAIR_CHECK(!IsAllowedExternalScheme(Lit("ftp://a.com/x")));
    MARKAIR_CHECK(!IsAllowedExternalScheme(Lit("readme.md")));   // 没有 scheme
    MARKAIR_CHECK(!IsAllowedExternalScheme(Lit("C:\\docs\\a.md"))); // 盘符不是 scheme
    MARKAIR_CHECK(!IsAllowedExternalScheme(Lit("")));

    // 被拒绝的 scheme 连执行入口都进不去。
    MARKAIR_CHECK(!OpenExternalTarget(Lit("javascript:alert(1)")));
    MARKAIR_CHECK(!OpenExternalTarget(Lit("")));
}

// 用例:三类目标的行为判定,以及"非 .md 本地路径"一律拒绝。
MARKAIR_TEST(Navigate_LinkActionClassification) {
    MARKAIR_CHECK(DecideLinkAction(Lit("https://example.com")) == LinkAction::OpenExternal);
    MARKAIR_CHECK(DecideLinkAction(Lit("mailto:a@b.com")) == LinkAction::OpenExternal);
    MARKAIR_CHECK(DecideLinkAction(Lit("#install")) == LinkAction::ScrollToAnchor);
    MARKAIR_CHECK(DecideLinkAction(Lit("readme.md")) == LinkAction::OpenMarkdown);
    MARKAIR_CHECK(DecideLinkAction(Lit("../docs/GUIDE.MARKDOWN")) == LinkAction::OpenMarkdown);
    MARKAIR_CHECK(DecideLinkAction(Lit("other.md#section")) == LinkAction::OpenMarkdown);
    MARKAIR_CHECK(DecideLinkAction(Lit("C:\\docs\\a.md")) == LinkAction::OpenMarkdown);

    MARKAIR_CHECK(DecideLinkAction(Lit("javascript:alert(1)")) == LinkAction::Reject);
    MARKAIR_CHECK(DecideLinkAction(Lit("image.png")) == LinkAction::Reject);
    MARKAIR_CHECK(DecideLinkAction(Lit("")) == LinkAction::Reject);
}

// 用例(slug 生成):GitHub 式规则 —— 小写、空格转 '-'、丢标点、保留 '-'/'_' 与中文。
MARKAIR_TEST(Navigate_HeadingSlugRules) {
    char slug[128];

    MARKAIR_CHECK_EQ(MakeHeadingSlug(Lit("Hello World"), slug, 128), 11u);
    MARKAIR_CHECK_STREQ(slug, "hello-world");

    MARKAIR_CHECK(MakeHeadingSlug(Lit("What's New? (v2.0)"), slug, 128) > 0);
    MARKAIR_CHECK_STREQ(slug, "whats-new-v20");

    MARKAIR_CHECK(MakeHeadingSlug(Lit("API_Reference - Part 1"), slug, 128) > 0);
    MARKAIR_CHECK_STREQ(slug, "api_reference---part-1");

    // 中文标题:非 ASCII 字节原样保留。
    MARKAIR_CHECK(MakeHeadingSlug(Lit("安装 说明"), slug, 128) > 0);
    MARKAIR_CHECK_STREQ(slug, "安装-说明");

    // 缓冲很小也不越界,且恒以 '\0' 结尾。
    char tiny[4];
    u32 n = MakeHeadingSlug(Lit("abcdefg"), tiny, 4);
    MARKAIR_CHECK_EQ(n, 3u);
    MARKAIR_CHECK_STREQ(tiny, "abc");
}

// 用例(锚点未命中 + 命中):按 slug 在标题块里查找,含中文的百分号编码锚点。
MARKAIR_TEST(Navigate_AnchorLookup) {
    MARKAIR_MAKE_NAV_ARENA();
    const char src[] =
        "# Install Guide\n\n"
        "text\n\n"
        "## 安装说明\n\n"
        "more text\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    u32 hit = FindAnchorBlock(doc, Lit("#install-guide"));
    MARKAIR_CHECK(hit != kInvalidIndex);
    if (hit != kInvalidIndex) MARKAIR_CHECK(doc.blocks[hit].type == markair::BlockType::Heading);

    // 不带 '#' 前缀同样可用。
    MARKAIR_CHECK(FindAnchorBlock(doc, Lit("install-guide")) == hit);

    // 中文锚点:GitHub 生成的链接是百分号编码的,要能解码后匹配。
    MARKAIR_CHECK(FindAnchorBlock(doc, Lit("#安装说明")) != kInvalidIndex);
    MARKAIR_CHECK(FindAnchorBlock(doc, Lit("#%E5%AE%89%E8%A3%85%E8%AF%B4%E6%98%8E")) !=
               kInvalidIndex);

    // 未命中:不存在的锚点、空锚点。
    MARKAIR_CHECK(FindAnchorBlock(doc, Lit("#nope")) == kInvalidIndex);
    MARKAIR_CHECK(FindAnchorBlock(doc, Lit("#")) == kInvalidIndex);
    MARKAIR_CHECK(FindAnchorBlock(doc, StrSlice{nullptr, 0}) == kInvalidIndex);
}

// 用例(路径穿越):`../../` 会被 PathCch 真正折叠掉,且不会越过盘符根目录。
MARKAIR_TEST(Navigate_PathResolutionAndTraversal) {
    wchar_t full[MAX_PATH * 2];

    MARKAIR_CHECK(ResolveMarkdownPath(Lit("readme.md"), L"C:\\docs\\sub", full, MAX_PATH * 2));
    MARKAIR_CHECK(wcscmp(full, L"C:\\docs\\sub\\readme.md") == 0);

    // URL 风格的正斜杠与 %20 都要正确处理。
    MARKAIR_CHECK(ResolveMarkdownPath(Lit("a/b%20c.md"), L"C:\\docs", full, MAX_PATH * 2));
    MARKAIR_CHECK(wcscmp(full, L"C:\\docs\\a\\b c.md") == 0);

    // 路径穿越:上跳两级被真正折叠。
    MARKAIR_CHECK(ResolveMarkdownPath(Lit("../../x.md"), L"C:\\docs\\sub", full, MAX_PATH * 2));
    MARKAIR_CHECK(wcscmp(full, L"C:\\x.md") == 0);

    // 穿越到盘符根之上:不会得到 "C:\..\..\x.md" 这种越界路径。
    MARKAIR_CHECK(!ResolveMarkdownPath(Lit("../../../../../../x.md"), L"C:\\docs", full,
                                     MAX_PATH * 2) ||
               wcsstr(full, L"..") == nullptr);

    // 片段/查询串被丢弃。
    MARKAIR_CHECK(ResolveMarkdownPath(Lit("other.md#sec"), L"C:\\docs", full, MAX_PATH * 2));
    MARKAIR_CHECK(wcscmp(full, L"C:\\docs\\other.md") == 0);

    // 绝对路径不做拼接,只做规范化。
    MARKAIR_CHECK(ResolveMarkdownPath(Lit("C:\\docs\\.\\a.md"), L"D:\\other", full, MAX_PATH * 2));
    MARKAIR_CHECK(wcscmp(full, L"C:\\docs\\a.md") == 0);

    // 空 href 安全失败。
    MARKAIR_CHECK(!ResolveMarkdownPath(StrSlice{nullptr, 0}, L"C:\\docs", full, MAX_PATH * 2));
    MARKAIR_CHECK(!ResolveMarkdownPath(Lit(""), L"C:\\docs", full, MAX_PATH * 2));
}

// ---- 以下为 T34 的 URL 解析纯函数(不发起任何网络请求)----

// 用例:https URL 拆解出 host / path / 默认端口 443。
MARKAIR_TEST(Remote_ParseHttpsUrl) {
    wchar_t host[256]{}, path[1024]{};
    bool https = false;
    u16 port = 0;
    MARKAIR_CHECK(ParseHttpUrl(Lit("https://img.shields.io/badge/a-b.svg?style=flat"),
                            &https, host, 256, path, 1024, &port));
    MARKAIR_CHECK(https);
    MARKAIR_CHECK_EQ(port, static_cast<u16>(443));
    MARKAIR_CHECK(wcscmp(host, L"img.shields.io") == 0);
    MARKAIR_CHECK(wcscmp(path, L"/badge/a-b.svg?style=flat") == 0);
}

// 用例:http + 显式端口;无路径时补 "/"。
MARKAIR_TEST(Remote_ParseHttpUrlWithPort) {
    wchar_t host[256]{}, path[1024]{};
    bool https = true;
    u16 port = 0;
    MARKAIR_CHECK(ParseHttpUrl(Lit("http://localhost:8080"), &https, host, 256, path, 1024, &port));
    MARKAIR_CHECK(!https);
    MARKAIR_CHECK_EQ(port, static_cast<u16>(8080));
    MARKAIR_CHECK(wcscmp(host, L"localhost") == 0);
    MARKAIR_CHECK(wcscmp(path, L"/") == 0);
}

// 用例:fragment 不发给服务器,path 里不应出现 '#' 之后的内容。
MARKAIR_TEST(Remote_ParseUrlDropsFragment) {
    wchar_t host[256]{}, path[1024]{};
    bool https = false;
    u16 port = 0;
    MARKAIR_CHECK(ParseHttpUrl(Lit("https://a.com/x.png#frag"), &https, host, 256, path, 1024, &port));
    MARKAIR_CHECK(wcscmp(path, L"/x.png") == 0);
}

// 用例:非 http/https scheme、缺 host、非法端口一律拒绝 —— 拒绝发生在任何
// WinHTTP 调用之前,这是"零网络行为"承诺的第一道闸门。
MARKAIR_TEST(Remote_ParseUrlRejectsInvalid) {
    wchar_t host[256]{}, path[1024]{};
    bool https = false;
    u16 port = 0;
    MARKAIR_CHECK(!ParseHttpUrl(Lit("ftp://a.com/x.png"), &https, host, 256, path, 1024, &port));
    MARKAIR_CHECK(!ParseHttpUrl(Lit("javascript:alert(1)"), &https, host, 256, path, 1024, &port));
    MARKAIR_CHECK(!ParseHttpUrl(Lit("https:///x.png"), &https, host, 256, path, 1024, &port));
    MARKAIR_CHECK(!ParseHttpUrl(Lit("http://a.com:99999/x"), &https, host, 256, path, 1024, &port));
    MARKAIR_CHECK(!ParseHttpUrl(Lit("http://a.com:abc/x"), &https, host, 256, path, 1024, &port));
    MARKAIR_CHECK(!ParseHttpUrl(Lit(""), &https, host, 256, path, 1024, &port));
}

// 用例:开关关闭(默认)时 Request 直接返回 false —— 此时不可能有任何
// WinHTTP 调用发生,这是 T34 "零网络行为"在代码层的可测断言。
MARKAIR_TEST(Remote_DisabledLoaderNeverRequests) {
    markair::RemoteImageLoader loader;
    loader.Init(nullptr, false);
    MARKAIR_CHECK(!loader.Enabled());
    MARKAIR_CHECK(!loader.Request(Lit("https://example.com/a.png")));

    // 即便开关打开,没有通知窗口 / URL 非法时也不会起线程。
    loader.Init(nullptr, true);
    MARKAIR_CHECK(loader.Enabled());
    MARKAIR_CHECK(!loader.Request(Lit("https://example.com/a.png")));  // notifyWindow 为空
    MARKAIR_CHECK(!loader.RequestOnUserClick(Lit("ftp://example.com/a.png")));
}
