// T47 覆盖测试:主题双态状态机(Light/Dark 切换、系统值解析规则)以及
// state.ini 里 theme 键的解析。真实注册表读取(DetectSystemIsDark)不做单测,
// 逻辑已收敛到可单测的 AppsUseLightThemeValueToIsDark 里。
#include "markair_test.h"
#include "../src/shell/theme_state.h"
#include "../src/util/ini.h"

#include <cstring>

using markair::AppSettings;
using markair::AppsUseLightThemeValueToIsDark;
using markair::DefaultAppSettings;
using markair::NextThemeSetting;
using markair::ParseIniSettings;
using markair::ResolveEffectiveTheme;
using markair::StrSlice;
using markair::ThemeSetting;
using markair::u32;

namespace {

StrSlice Lit(const char* s) { return StrSlice{s, static_cast<u32>(strlen(s))}; }

// 用默认值起一份配置,再解析一段文本(与 test_ini.cpp 的写法保持一致)。
u32 ParseFresh(const char* text, AppSettings* out) {
    DefaultAppSettings(out);
    return ParseIniSettings(Lit(text), out);
}

}  // namespace

// 用例 1:双态循环 Light <-> Dark。
MARKAIR_TEST(ThemeState_NextThemeSettingCycles) {
    MARKAIR_CHECK(NextThemeSetting(ThemeSetting::Light) == ThemeSetting::Dark);
    MARKAIR_CHECK(NextThemeSetting(ThemeSetting::Dark) == ThemeSetting::Light);
}

// 用例 2:ResolveEffectiveTheme 针对 Light/Dark 返回对应布尔值。
MARKAIR_TEST(ThemeState_ResolveEffectiveThemeReturnsCorrectBool) {
    MARKAIR_CHECK(ResolveEffectiveTheme(ThemeSetting::Light) == false);
    MARKAIR_CHECK(ResolveEffectiveTheme(ThemeSetting::Dark) == true);
    // 带两个参数也能正常工作(向后兼容)
    MARKAIR_CHECK(ResolveEffectiveTheme(ThemeSetting::Light, true) == false);
    MARKAIR_CHECK(ResolveEffectiveTheme(ThemeSetting::Dark, false) == true);
}

// 用例 3:AppsUseLightTheme 的 DWORD 值到 systemIsDark 的映射。
MARKAIR_TEST(ThemeState_AppsUseLightThemeValueMapping) {
    MARKAIR_CHECK(AppsUseLightThemeValueToIsDark(1) == false);  // 1 = 浅色
    MARKAIR_CHECK(AppsUseLightThemeValueToIsDark(0) == true);   // 0 = 深色
}

// 用例 4:ini 里 theme=abc(非法值)不被识别,hasTheme 保持 false。
MARKAIR_TEST(ThemeState_IniIllegalThemeDoesNotSetHasTheme) {
    AppSettings s;
    ParseFresh("theme=abc\n", &s);
    MARKAIR_CHECK(s.theme == ThemeSetting::Light);
    MARKAIR_CHECK(s.hasTheme == false);
}

// 用例 5:ini 里 theme=DARK(大小写混合)正确解析成 ThemeSetting::Dark,hasTheme 为 true。
MARKAIR_TEST(ThemeState_IniThemeCaseInsensitiveDark) {
    AppSettings s;
    ParseFresh("theme=DARK\n", &s);
    MARKAIR_CHECK(s.theme == ThemeSetting::Dark);
    MARKAIR_CHECK(s.hasTheme == true);
}

// 用例 6:ini 里 theme=light 正确映射为 ThemeSetting::Light,hasTheme 为 true。
MARKAIR_TEST(ThemeState_IniThemeLight) {
    AppSettings light;
    ParseFresh("theme=light\n", &light);
    MARKAIR_CHECK(light.theme == ThemeSetting::Light);
    MARKAIR_CHECK(light.hasTheme == true);
}

// 用例 7:ini 里 theme=system(老版本遗留值)不被采纳,hasTheme 保持 false 以触发初始化系统探测。
MARKAIR_TEST(ThemeState_IniOldSystemThemeTriggersReinit) {
    AppSettings s;
    ParseFresh("theme=system\n", &s);
    MARKAIR_CHECK(s.hasTheme == false);
}
