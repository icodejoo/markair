// T47 覆盖测试:主题三态状态机(System/Light/Dark 循环、系统值解析规则)以及
// state.ini 里 theme 键的解析。真实注册表读取(DetectSystemIsDark)不做单测,
// 逻辑已收敛到可单测的 AppsUseLightThemeValueToIsDark 里。
#include "mdvn_test.h"
#include "../src/shell/theme_state.h"
#include "../src/util/ini.h"

#include <cstring>

using mdvn::AppSettings;
using mdvn::AppsUseLightThemeValueToIsDark;
using mdvn::DefaultAppSettings;
using mdvn::NextThemeSetting;
using mdvn::ParseIniSettings;
using mdvn::ResolveEffectiveTheme;
using mdvn::StrSlice;
using mdvn::ThemeSetting;
using mdvn::u32;

namespace {

StrSlice Lit(const char* s) { return StrSlice{s, static_cast<u32>(strlen(s))}; }

// 用默认值起一份配置,再解析一段文本(与 test_ini.cpp 的写法保持一致)。
u32 ParseFresh(const char* text, AppSettings* out) {
    DefaultAppSettings(out);
    return ParseIniSettings(Lit(text), out);
}

}  // namespace

// 用例 1:三态循环 System -> Light -> Dark -> System。
MDVN_TEST(ThemeState_NextThemeSettingCycles) {
    MDVN_CHECK(NextThemeSetting(ThemeSetting::System) == ThemeSetting::Light);
    MDVN_CHECK(NextThemeSetting(ThemeSetting::Light) == ThemeSetting::Dark);
    MDVN_CHECK(NextThemeSetting(ThemeSetting::Dark) == ThemeSetting::System);
}

// 用例 2:setting == System 时,ResolveEffectiveTheme 跟随 systemIsDark 变化。
MDVN_TEST(ThemeState_SystemSettingFollowsSystemIsDark) {
    MDVN_CHECK(ResolveEffectiveTheme(ThemeSetting::System, false) == false);
    MDVN_CHECK(ResolveEffectiveTheme(ThemeSetting::System, true) == true);
}

// 用例 3:非 System 时,systemIsDark 无论怎么翻转都不影响结果。
MDVN_TEST(ThemeState_NonSystemSettingIgnoresSystemIsDark) {
    MDVN_CHECK(ResolveEffectiveTheme(ThemeSetting::Light, false) == false);
    MDVN_CHECK(ResolveEffectiveTheme(ThemeSetting::Light, true) == false);
    MDVN_CHECK(ResolveEffectiveTheme(ThemeSetting::Dark, false) == true);
    MDVN_CHECK(ResolveEffectiveTheme(ThemeSetting::Dark, true) == true);
}

// 用例 4:AppsUseLightTheme 的 DWORD 值到 systemIsDark 的映射。
MDVN_TEST(ThemeState_AppsUseLightThemeValueMapping) {
    MDVN_CHECK(AppsUseLightThemeValueToIsDark(1) == false);  // 1 = 浅色
    MDVN_CHECK(AppsUseLightThemeValueToIsDark(0) == true);   // 0 = 深色
}

// 用例 5:ini 里 theme=abc(非法值)时回落 ThemeSetting::System。
MDVN_TEST(ThemeState_IniIllegalThemeFallsBackToSystem) {
    AppSettings s;
    ParseFresh("theme=abc\n", &s);
    MDVN_CHECK(s.theme == ThemeSetting::System);
}

// 用例 6:ini 里 theme=DARK(大小写混合)正确解析成 ThemeSetting::Dark。
MDVN_TEST(ThemeState_IniThemeCaseInsensitiveDark) {
    AppSettings s;
    ParseFresh("theme=DARK\n", &s);
    MDVN_CHECK(s.theme == ThemeSetting::Dark);
}

// 用例 7:ini 里 theme=light / theme=system 都能正确映射到对应枚举值。
MDVN_TEST(ThemeState_IniThemeLightAndSystem) {
    AppSettings light;
    ParseFresh("theme=light\n", &light);
    MDVN_CHECK(light.theme == ThemeSetting::Light);

    AppSettings system;
    ParseFresh("theme=system\n", &system);
    MDVN_CHECK(system.theme == ThemeSetting::System);
}
