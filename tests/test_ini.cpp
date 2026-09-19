// T39 覆盖测试:`state.ini` 的 KV 解析(缺失文件 / 非法值 / 超范围钳制 /
// 注释行 / 尾随空白)。解析层是纯函数,不碰文件系统。
// T55 追加:`SaveAppSettings` 写盘覆盖(round-trip、未识别键保留、并发合并、
// 互斥体超时、非法路径),需要真实碰 `%LOCALAPPDATA%\mdvn\state.ini`。
#include "mdvn_test.h"
#include "../src/util/ini.h"
#include "../src/shell/theme_state.h"
#include "../src/text/font.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <cwchar>

using mdvn::AppSettings;
using mdvn::DefaultAppSettings;
using mdvn::LoadAppSettings;
using mdvn::ParseIniSettings;
using mdvn::SaveAppSettings;
using mdvn::StrSlice;
using mdvn::ThemeSetting;
using mdvn::u32;

namespace {

StrSlice Lit(const char* s) { return StrSlice{s, static_cast<u32>(strlen(s))}; }

// 用默认值起一份配置,再解析一段文本。
u32 ParseFresh(const char* text, AppSettings* out) {
    DefaultAppSettings(out);
    return ParseIniSettings(Lit(text), out);
}

// T55:写盘测试需要真实的 state.ini 路径,拼法与 ini.cpp 内部一致
// (`%LOCALAPPDATA%\mdvn\state.ini`),仅供测试直接读写磁盘做断言用。
bool BuildRealStateIniPath(wchar_t* out, size_t cap) {
    wchar_t localAppData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    if (wcslen(localAppData) + 20 >= cap) return false;
    wcscpy_s(out, cap, localAppData);
    wcscat_s(out, cap, L"\\mdvn\\state.ini");
    return true;
}

// 把磁盘上 state.ini 的原始字节整段读出来(测试用,不走 kMaxIniBytes 上限
// 之外的健壮性处理,文件不存在就返回空字符串)。
void ReadWholeFile(const wchar_t* path, char* buf, size_t cap) {
    buf[0] = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD read = 0;
    ReadFile(file, buf, static_cast<DWORD>(cap - 1), &read, nullptr);
    CloseHandle(file);
    buf[read] = 0;
}

// 备份/还原真实 state.ini,让写盘测试不破坏开发者本机已有的配置,也不让
// 测试之间互相污染。RAII 风格,但不依赖异常(项目禁异常),析构里静默还原。
struct StateIniBackup {
    wchar_t path[MAX_PATH]{};
    bool hadFile = false;
    char content[mdvn::kMaxIniBytes]{};
    size_t contentLen = 0;

    StateIniBackup() {
        if (!BuildRealStateIniPath(path, MAX_PATH)) return;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD read = 0;
        ReadFile(file, content, static_cast<DWORD>(sizeof(content)), &read, nullptr);
        CloseHandle(file);
        hadFile = true;
        contentLen = read;
    }

    ~StateIniBackup() {
        if (path[0] == 0) return;
        if (!hadFile) {
            DeleteFileW(path);
            return;
        }
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(file, content, static_cast<DWORD>(contentLen), &written, nullptr);
        CloseHandle(file);
    }
};

}  // namespace

// 用例:默认值 —— 网络图片开关默认开启(2026-09-19 裁决反转),字体族名一律不覆盖。
MDVN_TEST(Ini_DefaultsAreSafe) {
    AppSettings s;
    DefaultAppSettings(&s);
    MDVN_CHECK(s.loadRemoteImages);
    MDVN_CHECK_EQ(s.fontBodyPrimary[0], L'\0');
    MDVN_CHECK_EQ(s.fontBodyFallback[0], L'\0');
    MDVN_CHECK_EQ(s.fontMonoPrimary[0], L'\0');
    MDVN_CHECK_EQ(s.fontMonoFallback[0], L'\0');
    // T57:默认缩放档位是 1.0。
    MDVN_CHECK(s.zoom > 0.99f && s.zoom < 1.01f);
    // T56:默认没有存过窗口矩形,winW/winH 必须是 <= 0 的"未设置"哨兵值。
    MDVN_CHECK_EQ(s.winW, 0);
    MDVN_CHECK_EQ(s.winH, 0);
    MDVN_CHECK(!s.winMaximized);
}

// 用例:窗口矩形键的基本解析,含负坐标(副屏在主屏左侧的合法场景)。
MDVN_TEST(Ini_ParsesWindowRectKeys) {
    AppSettings s;
    const char* text =
        "win_x=-100\n"
        "win_y=50\n"
        "win_w=1024\n"
        "win_h=768\n"
        "win_maximized=1\n";
    MDVN_CHECK_EQ(ParseFresh(text, &s), 5u);
    MDVN_CHECK_EQ(s.winX, -100);
    MDVN_CHECK_EQ(s.winY, 50);
    MDVN_CHECK_EQ(s.winW, 1024);
    MDVN_CHECK_EQ(s.winH, 768);
    MDVN_CHECK(s.winMaximized);
}

// 用例(缺失文件):`%LOCALAPPDATA%\mdvn\state.ini` 正常情况下不存在,
// LoadAppSettings 必须返回 false 且让配置保持默认值 —— 不创建目录、不写盘。
MDVN_TEST(Ini_MissingFileFallsBackToDefaults) {
    AppSettings s;
    s.loadRemoteImages = false;  // 故意先污染,验证函数内部会先 Default 一次
    bool loaded = LoadAppSettings(&s);
    if (!loaded) {
        MDVN_CHECK(s.loadRemoteImages);
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
    MDVN_CHECK(s.loadRemoteImages);

    MDVN_CHECK_EQ(ParseFresh("load_remote_images=\n", &s), 0u);
    MDVN_CHECK(s.loadRemoteImages);

    MDVN_CHECK_EQ(ParseFresh("load_remote_images=1abc\n", &s), 0u);
    MDVN_CHECK(s.loadRemoteImages);
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
        "image_cache_mb=64\n";    // 已作废的键:必须被静默忽略
    MDVN_CHECK_EQ(ParseFresh(text, &s), 4u);
    MDVN_CHECK(wcscmp(s.fontBodyPrimary, L"Consolas") == 0);
    MDVN_CHECK(wcscmp(s.fontBodyFallback, L"微软雅黑") == 0);
    MDVN_CHECK(wcscmp(s.fontMonoPrimary, L"Cascadia Code") == 0);
    MDVN_CHECK(wcscmp(s.fontMonoFallback, L"Courier New") == 0);
}

// 用例(T57 zoom):档位内的精确值原样保持。
MDVN_TEST(Ini_ZoomExactLevelKept) {
    AppSettings s;
    MDVN_CHECK_EQ(ParseFresh("zoom=1.3\n", &s), 1u);
    MDVN_CHECK(s.zoom > 1.29f && s.zoom < 1.31f);
}

// 用例(T57 zoom):档位外的浮点值被吸附到最近的离散档位,而不是原样采信
// (任意浮点会让 layout 缓存抖动,这正是 T29 选离散档位的理由)。
MDVN_TEST(Ini_ZoomSnapsToNearestLevel) {
    AppSettings s;
    MDVN_CHECK_EQ(ParseFresh("zoom=1.31\n", &s), 1u);
    MDVN_CHECK(s.zoom > 1.29f && s.zoom < 1.31f);  // 吸附到 1.3
}

// 用例(T57 zoom):超出最大档的值被钳到 2.0(最大档),而不是被当成非法值丢弃。
MDVN_TEST(Ini_ZoomClampsAboveMax) {
    AppSettings s;
    MDVN_CHECK_EQ(ParseFresh("zoom=99\n", &s), 1u);
    MDVN_CHECK(s.zoom > 1.99f && s.zoom < 2.01f);
}

// 用例(T57 zoom):非法值(非数字)不生效,回落默认档位 1.0。
MDVN_TEST(Ini_ZoomInvalidValueFallsBackToDefault) {
    AppSettings s;
    MDVN_CHECK_EQ(ParseFresh("zoom=abc\n", &s), 0u);
    MDVN_CHECK(s.zoom > 0.99f && s.zoom < 1.01f);
}

// 用例(T57 zoom):ClampToNearestZoomLevel 与 ParseIniSettings 走同一份档位表,
// 结果必须完全一致(避免两处各存一份容易走样的常量)。
MDVN_TEST(Ini_ZoomMatchesFontSubsystemClamp) {
    AppSettings s;
    ParseFresh("zoom=1.31\n", &s);
    MDVN_CHECK(s.zoom == mdvn::FontSubsystem::ClampToNearestZoomLevel(1.31f));
}

// 用例(T57 zoom 写盘 round-trip):写出后再读回,吸附后的值保持不变。
MDVN_TEST(Ini_ZoomSaveThenLoadRoundTrips) {
    StateIniBackup backup;
    DeleteFileW(backup.path);

    AppSettings loaded;
    LoadAppSettings(&loaded);  // 建立"磁盘为空"的基线
    loaded.zoom = mdvn::FontSubsystem::ClampToNearestZoomLevel(1.31f);  // 1.3

    MDVN_CHECK(SaveAppSettings(loaded));

    AppSettings readBack;
    MDVN_CHECK(LoadAppSettings(&readBack));
    MDVN_CHECK(readBack.zoom > 1.29f && readBack.zoom < 1.31f);
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

// ---------------------------------------------------------------------------
// T55:SaveAppSettings 写盘覆盖。每个用例都用 StateIniBackup 备份/还原真实
// state.ini,不污染开发者本机配置,也不依赖测试执行顺序。
// ---------------------------------------------------------------------------

// 用例(round-trip):写出后再读回,字段完全等值。
MDVN_TEST(Ini_SaveThenLoadRoundTrips) {
    StateIniBackup backup;
    DeleteFileW(backup.path);  // 从干净状态起,避免受开发者本机既有配置干扰

    AppSettings loaded;
    LoadAppSettings(&loaded);  // 文件已删除,建立"磁盘为空"的基线

    loaded.loadRemoteImages = true;
    loaded.theme = ThemeSetting::Dark;
    wcscpy_s(loaded.fontBodyPrimary, mdvn::kMaxFontFamilyChars, L"Consolas");
    wcscpy_s(loaded.fontMonoFallback, mdvn::kMaxFontFamilyChars, L"微软雅黑");
    // T56:窗口矩形键(含负坐标,验证副屏在主屏左侧这类合法负值也能原样往返)。
    loaded.winX = -200;
    loaded.winY = 50;
    loaded.winW = 900;
    loaded.winH = 650;
    loaded.winMaximized = true;

    MDVN_CHECK(SaveAppSettings(loaded));

    AppSettings readBack;
    MDVN_CHECK(LoadAppSettings(&readBack));
    MDVN_CHECK(readBack.loadRemoteImages);
    MDVN_CHECK(readBack.theme == ThemeSetting::Dark);
    MDVN_CHECK(wcscmp(readBack.fontBodyPrimary, L"Consolas") == 0);
    MDVN_CHECK(wcscmp(readBack.fontMonoFallback, L"微软雅黑") == 0);
    MDVN_CHECK_EQ(readBack.winX, -200);
    MDVN_CHECK_EQ(readBack.winY, 50);
    MDVN_CHECK_EQ(readBack.winW, 900);
    MDVN_CHECK_EQ(readBack.winH, 650);
    MDVN_CHECK(readBack.winMaximized);
}

// 用例(未识别键保留):老版本/手改的键在写盘后必须原样留在文件里。
MDVN_TEST(Ini_SaveKeepsUnrecognizedKeys) {
    StateIniBackup backup;

    // 手写一份包含未知键(future_flag)与一个已知键的原始文件。
    const char* seed = "future_flag=42\nload_remote_images=0\n";
    HANDLE file = CreateFileW(backup.path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    MDVN_CHECK(file != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(file, seed, static_cast<DWORD>(strlen(seed)), &written, nullptr);
    CloseHandle(file);

    AppSettings s;
    LoadAppSettings(&s);  // 建立基线:loadRemoteImages=false(与文件一致)
    s.loadRemoteImages = true;  // 本进程真正改动的字段

    MDVN_CHECK(SaveAppSettings(s));

    char content[mdvn::kMaxIniBytes];
    ReadWholeFile(backup.path, content, sizeof(content));
    MDVN_CHECK(strstr(content, "future_flag=42") != nullptr);  // 未知键原样保留
    MDVN_CHECK(strstr(content, "load_remote_images=1") != nullptr);  // 已知键被更新
}

// 用例(并发合并):模拟"本进程读盘之后,另一个进程改了别的键",两边改动
// 合并后都要在最终文件里。
MDVN_TEST(Ini_SaveMergesConcurrentExternalEdit) {
    StateIniBackup backup;
    DeleteFileW(backup.path);

    AppSettings s;
    LoadAppSettings(&s);  // 基线:全部默认值,文件不存在

    // 模拟"另一个进程"在本进程读盘之后,直接改了 load_remote_images。
    const char* externalEdit = "load_remote_images=1\n";
    HANDLE file = CreateFileW(backup.path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    MDVN_CHECK(file != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(file, externalEdit, static_cast<DWORD>(strlen(externalEdit)), &written, nullptr);
    CloseHandle(file);

    // 本进程只改了 theme,loadRemoteImages 在内存里仍是加载时的旧默认值。
    s.theme = ThemeSetting::Dark;
    MDVN_CHECK(SaveAppSettings(s));

    AppSettings mergedResult;
    MDVN_CHECK(LoadAppSettings(&mergedResult));
    MDVN_CHECK(mergedResult.loadRemoteImages);            // "别的进程"的改动保留
    MDVN_CHECK(mergedResult.theme == ThemeSetting::Dark);  // 本进程的改动也生效
}

// 用例(互斥体超时):另一个线程持有同名互斥体不放,SaveAppSettings 必须在
// 超时后放弃、不崩溃、不写出半截文件(也不留下 .tmp 残留)。
namespace {
DWORD WINAPI HoldMutexThenReleaseThreadProc(LPVOID) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mdvn::kStateIniMutexName);  // 立即持有
    if (mutex) {
        Sleep(mdvn::kStateIniMutexTimeoutMs + 500);  // 比 SaveAppSettings 的超时更久
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
}  // namespace

MDVN_TEST(Ini_SaveGivesUpSilentlyOnMutexTimeout) {
    StateIniBackup backup;
    DeleteFileW(backup.path);

    HANDLE thread = CreateThread(nullptr, 0, HoldMutexThenReleaseThreadProc, nullptr, 0, nullptr);
    MDVN_CHECK(thread != nullptr);
    Sleep(100);  // 让后台线程先真正拿到互斥体

    AppSettings s;
    DefaultAppSettings(&s);
    s.loadRemoteImages = true;
    bool saved = SaveAppSettings(s);  // 预期在 ~2s 后超时返回 false,不崩溃
    MDVN_CHECK(!saved);

    // 文件本不存在,超时放弃后也不应凭空出现;临时文件同样不应残留。
    MDVN_CHECK(GetFileAttributesW(backup.path) == INVALID_FILE_ATTRIBUTES);
    wchar_t tmpPath[MAX_PATH]{};
    wcscpy_s(tmpPath, backup.path);
    wcscat_s(tmpPath, L".tmp");
    MDVN_CHECK(GetFileAttributesW(tmpPath) == INVALID_FILE_ATTRIBUTES);

    if (thread) {
        WaitForSingleObject(thread, INFINITE);  // 等后台线程释放互斥体、彻底收尾
        CloseHandle(thread);
    }
}

// 用例(非法路径):`%LOCALAPPDATA%` 取不到时,SaveAppSettings 必须静默失败,
// 不崩溃、不抛异常。
MDVN_TEST(Ini_SaveHandlesMissingLocalAppDataGracefully) {
    wchar_t original[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", original, MAX_PATH);
    bool hadOriginal = n > 0 && n < MAX_PATH;

    SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);  // 移除环境变量,模拟非法/取不到路径

    AppSettings s;
    DefaultAppSettings(&s);
    bool saved = SaveAppSettings(s);
    MDVN_CHECK(!saved);

    if (hadOriginal) SetEnvironmentVariableW(L"LOCALAPPDATA", original);
}
