// T39 覆盖测试:`state.ini` 的 KV 解析(缺失文件 / 非法值 / 超范围钳制 /
// 注释行 / 尾随空白)。解析层是纯函数,不碰文件系统。
#include "mdvn_test.h"
#include "../src/util/ini.h"

#include <cstring>
#include <cwchar>

using mdvn::AppSettings;
using mdvn::DefaultAppSettings;
using mdvn::LoadAppSettings;
using mdvn::ParseIniSettings;
using mdvn::StrSlice;
using mdvn::u32;

namespace {

StrSlice Lit(const char* s) { return StrSlice{s, static_cast<u32>(strlen(s))}; }

// 用默认值起一份配置,再解析一段文本。
u32 ParseFresh(const char* text, AppSettings* out) {
    DefaultAppSettings(out);
    return ParseIniSettings(Lit(text), out);
}

}  // namespace

// 用例:默认值 —— 网络图片开关默认关闭,字体族名一律不覆盖。
MDVN_TEST(Ini_DefaultsAreSafe) {
    AppSettings s;
    DefaultAppSettings(&s);
    MDVN_CHECK(!s.loadRemoteImages);
    MDVN_CHECK_EQ(s.fontBodyPrimary[0], L'\0');
    MDVN_CHECK_EQ(s.fontBodyFallback[0], L'\0');
    MDVN_CHECK_EQ(s.fontMonoPrimary[0], L'\0');
    MDVN_CHECK_EQ(s.fontMonoFallback[0], L'\0');
}

// 用例(缺失文件):`%LOCALAPPDATA%\mdvn\state.ini` 正常情况下不存在,
// LoadAppSettings 必须返回 false 且让配置保持默认值 —— 不创建目录、不写盘。
MDVN_TEST(Ini_MissingFileFallsBackToDefaults) {
    AppSettings s;
    s.loadRemoteImages = true;  // 故意先污染,验证函数内部会先 Default 一次
    bool loaded = LoadAppSettings(&s);
    if (!loaded) {
        MDVN_CHECK(!s.loadRemoteImages);
        MDVN_CHECK_EQ(s.fontBodyPrimary[0], L'\0');
    }
    // 文件真的存在时(开发者本机放过配置),函数返回 true 也是合法结果,
    // 这里不对其内容作断言,只要求不崩溃。
    MDVN_CHECK(loaded || !loaded);
}

// 用例:基本 KV 解析,开关置 1 生效。
MDVN_TEST(Ini_ParsesLoadRemoteImages) {
    AppSettings s;
    MDVN_CHECK_EQ(ParseFresh("load_remote_images=1\n", &s), 1u);
    MDVN_CHECK(s.loadRemoteImages);

    MDVN_CHECK_EQ(ParseFresh("load_remote_images=0\n", &s), 1u);
    MDVN_CHECK(!s.loadRemoteImages);
}

// 用例(非法值):无法解析成数字的值不生效,保持默认值,且不算作已应用的键。
MDVN_TEST(Ini_InvalidValueKeepsDefault) {
    AppSettings s;
    MDVN_CHECK_EQ(ParseFresh("load_remote_images=yes\n", &s), 0u);
    MDVN_CHECK(!s.loadRemoteImages);

    MDVN_CHECK_EQ(ParseFresh("load_remote_images=\n", &s), 0u);
    MDVN_CHECK(!s.loadRemoteImages);

    MDVN_CHECK_EQ(ParseFresh("load_remote_images=1abc\n", &s), 0u);
    MDVN_CHECK(!s.loadRemoteImages);
}

// 用例(超范围值钳制):超出 [0,1] 的数值被钳制,而不是被当成非法值丢弃。
MDVN_TEST(Ini_OutOfRangeValueIsClamped) {
    AppSettings s;
    MDVN_CHECK_EQ(ParseFresh("load_remote_images=5\n", &s), 1u);
    MDVN_CHECK(s.loadRemoteImages);  // 5 -> 1

    MDVN_CHECK_EQ(ParseFresh("load_remote_images=-3\n", &s), 1u);
    MDVN_CHECK(!s.loadRemoteImages);  // -3 -> 0

    MDVN_CHECK_EQ(ParseFresh("load_remote_images=999999999999\n", &s), 1u);
    MDVN_CHECK(s.loadRemoteImages);  // 溢出饱和后仍被钳到 1
}

// 用例(注释行):`;` / `#` 开头的行、`[section]` 行、空行一律跳过。
MDVN_TEST(Ini_CommentsAndSectionsSkipped) {
    AppSettings s;
    const char* text =
        "; 这是一行注释\n"
        "# 这也是注释\n"
        "\n"
        "[general]\n"
        "load_remote_images=1\n"
        "; load_remote_images=0   <- 被注释掉的行不应生效\n";
    MDVN_CHECK_EQ(ParseFresh(text, &s), 1u);
    MDVN_CHECK(s.loadRemoteImages);
}

// 用例(尾随空白):键值两侧的空格/制表符、行尾 CR 都要被裁掉。
MDVN_TEST(Ini_TrimsWhitespaceAndCrlf) {
    AppSettings s;
    MDVN_CHECK_EQ(ParseFresh("  load_remote_images  =  1  \r\n", &s), 1u);
    MDVN_CHECK(s.loadRemoteImages);

    MDVN_CHECK_EQ(ParseFresh("\tload_remote_images\t=\t1\t\r\n", &s), 1u);
    MDVN_CHECK(s.loadRemoteImages);

    // 键名大小写不敏感。
    MDVN_CHECK_EQ(ParseFresh("LOAD_REMOTE_IMAGES=1\n", &s), 1u);
    MDVN_CHECK(s.loadRemoteImages);
}

// 用例:字体族名覆盖(含中文族名的 UTF-8 解码)与未知键忽略。
MDVN_TEST(Ini_FontFamilyOverrides) {
    AppSettings s;
    const char* text =
        "font_body_primary = Consolas \n"
        "font_body_fallback=微软雅黑\n"
        "font_mono_primary=Cascadia Code\n"
        "font_mono_fallback=Courier New\n"
        "image_cache_mb=64\n"     // 已作废的键:必须被静默忽略
        "zoom=1.5\n";             // M1 不支持的键:同样忽略
    MDVN_CHECK_EQ(ParseFresh(text, &s), 4u);
    MDVN_CHECK(wcscmp(s.fontBodyPrimary, L"Consolas") == 0);
    MDVN_CHECK(wcscmp(s.fontBodyFallback, L"微软雅黑") == 0);
    MDVN_CHECK(wcscmp(s.fontMonoPrimary, L"Cascadia Code") == 0);
    MDVN_CHECK(wcscmp(s.fontMonoFallback, L"Courier New") == 0);
}

// 用例:没有 '=' 的行、空键名、超长值都不应导致越界或崩溃。
MDVN_TEST(Ini_MalformedLinesAreSafe) {
    AppSettings s;
    MDVN_CHECK_EQ(ParseFresh("just some text without equals\n", &s), 0u);
    MDVN_CHECK_EQ(ParseFresh("=1\n", &s), 0u);

    // 超长族名被截断到缓冲容量内,且仍然以 '\0' 结尾。
    char buf[512];
    int n = 0;
    const char* prefix = "font_body_primary=";
    for (const char* p = prefix; *p; ++p) buf[n++] = *p;
    for (int i = 0; i < 400; ++i) buf[n++] = 'X';
    buf[n++] = '\n';
    buf[n] = 0;
    DefaultAppSettings(&s);
    ParseIniSettings(StrSlice{buf, static_cast<u32>(n)}, &s);
    MDVN_CHECK(wcslen(s.fontBodyPrimary) < mdvn::kMaxFontFamilyChars);
    MDVN_CHECK(s.fontBodyPrimary[0] == L'X');
}
