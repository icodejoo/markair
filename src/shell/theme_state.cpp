#include "theme_state.h"

namespace mdvn {

ThemeSetting NextThemeSetting(ThemeSetting current) {
    switch (current) {
    case ThemeSetting::System: return ThemeSetting::Light;
    case ThemeSetting::Light: return ThemeSetting::Dark;
    case ThemeSetting::Dark: return ThemeSetting::System;
    }
    return ThemeSetting::System;
}

bool ResolveEffectiveTheme(ThemeSetting setting, bool systemIsDark) {
    switch (setting) {
    case ThemeSetting::System: return systemIsDark;
    case ThemeSetting::Light: return false;
    case ThemeSetting::Dark: return true;
    }
    return false;
}

bool AppsUseLightThemeValueToIsDark(DWORD value) {
    return value == 0;
}

bool DetectSystemIsDark() {
    DWORD value = 1;  // 默认浅色(=1),读取失败时保持这个初值即可回落浅色。
    DWORD size = sizeof(value);
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status != ERROR_SUCCESS) return false;  // 键不存在/权限不足:按浅色处理
    return AppsUseLightThemeValueToIsDark(value);
}

}  // namespace mdvn
