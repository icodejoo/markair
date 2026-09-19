#include "theme_state.h"

namespace markair {

/**
 * Cycle between Light and Dark theme: Light <-> Dark. Pure function.
 *
 * 浅色与深色主题双态切换:Light <-> Dark。纯函数。
 *
 * @param current Current theme setting.
 *
 *   当前主题设置。
 *
 * @return Next theme setting.
 *
 *   切换后的下一个主题设置。
 */
ThemeSetting NextThemeSetting(ThemeSetting current) {
    switch (current) {
    case ThemeSetting::Light: return ThemeSetting::Dark;
    case ThemeSetting::Dark: return ThemeSetting::Light;
    }
    return ThemeSetting::Light;
}

/**
 * Calculate whether the effective theme is dark. Pure function.
 *
 * 算出实际生效主题是否为深色。纯函数。
 *
 * @param setting User theme preference (Light/Dark).
 *
 *   用户偏好(Light/Dark)。
 *
 * @param systemIsDark Unused parameter kept for call-site compatibility.
 *
 *   未使用的参数,保留以兼容现有调用点。
 *
 * @return True if effective theme is Dark, false otherwise.
 *
 *   实际生效主题是否为深色(Dark 返回 true,Light 返回 false)。
 */
bool ResolveEffectiveTheme(ThemeSetting setting, bool /*systemIsDark*/) {
    return setting == ThemeSetting::Dark;
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

}  // namespace markair
