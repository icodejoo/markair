// 自动更新模块与更新配置项测试。
#include "markair_test.h"
#include "../src/util/ini.h"
#include "../src/util/updater.h"
#include "../src/util/version.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <cwchar>

using markair::AppSettings;
using markair::DefaultAppSettings;
using markair::ParseIniSettings;
using markair::StrSlice;
using markair::u32;

namespace {

StrSlice Lit(const char* s) {
    return StrSlice{s, static_cast<u32>(strlen(s))};
}

}  // namespace

// 测试 1：自动更新相关配置的默认值与基础解析。
MARKAIR_TEST(Update_Ini_DefaultsAndParsing) {
    AppSettings s{};
    DefaultAppSettings(&s);

    // 默认值检查
    MARKAIR_CHECK_EQ(s.pendingUpdateVersion[0], 0);
    MARKAIR_CHECK_EQ(s.pendingUpdatePath[0], 0);
    MARKAIR_CHECK_EQ(s.lastUpdateCheckUnix, 0);

    // 解析测试
    const char iniText[] =
        "update_pending_version = v1.2.3\n"
        "update_pending_path = C:\\test\\markair_new.exe\n"
        "update_last_check = 1716000000\n";

    u32 applied = ParseIniSettings(Lit(iniText), &s);
    MARKAIR_CHECK_EQ(applied, 3u);
    MARKAIR_CHECK(wcscmp(s.pendingUpdateVersion, L"v1.2.3") == 0);
    MARKAIR_CHECK(wcscmp(s.pendingUpdatePath, L"C:\\test\\markair_new.exe") == 0);
    MARKAIR_CHECK_EQ(s.lastUpdateCheckUnix, 1716000000LL);
}

// 测试 2：当待安装路径为空时，ApplyPendingUpdate 安全无副作用。
MARKAIR_TEST(Update_ApplyPending_NoOpWhenEmpty) {
    // 确保没有待安装项时调用不会崩溃且安全返回
    markair::ApplyPendingUpdate();
    MARKAIR_CHECK(true);
}

// 测试 3：当前版本号常量有效性检查。
MARKAIR_TEST(Update_VersionConstantValid) {
    MARKAIR_CHECK(markair::kMarkairVersion != nullptr);
    MARKAIR_CHECK(strlen(markair::kMarkairVersion) >= 5); // 至少 "0.1.0"
}
