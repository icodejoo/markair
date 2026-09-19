// markair 主题三态状态机(T47):系统主题探测 + System/Light/Dark 三态循环。
// 纯状态机模块——除了最底层"真读一次注册表"的薄函数,其余全部是不依赖
// Win32 的纯函数,可脱离窗口/D2D 单测(参见 tests/test_theme_state.cpp)。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace markair {

/**
 * User theme preference: Light or Dark.
 *
 * 用户主题偏好:浅色或深色。
 */
enum class ThemeSetting {
    Light,  // Light theme / 浅色主题
    Dark,   // Dark theme / 深色主题
};

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
ThemeSetting NextThemeSetting(ThemeSetting current);

/**
 * Calculate whether the effective theme is dark. Pure function.
 *
 * 算出实际生效主题是否为深色。纯函数。
 *
 * @param setting User theme preference (Light/Dark).
 *
 *   用户偏好(Light/Dark)。
 *
 * @param systemIsDark Optional unused parameter kept for call-site compatibility.
 *
 *   可选参数,保留以兼容旧调用点。
 *
 * @return True if effective theme is Dark, false otherwise.
 *
 *   实际生效主题是否为深色(Dark 返回 true,Light 返回 false)。
 */
bool ResolveEffectiveTheme(ThemeSetting setting, bool systemIsDark = false);

/**
 * Convert raw DWORD value of registry AppsUseLightTheme to boolean isDark.
 *
 * 把注册表 AppsUseLightTheme 的原始 DWORD 值换算成"系统是否为深色"。
 *
 * @param value Raw DWORD value read from registry (1=Light, 0=Dark).
 *
 *   从注册表读到的原始 DWORD 值(1=浅色, 0=深色)。
 *
 * @return True if system is dark (value == 0), false otherwise.
 *
 *   系统是否为深色(值为 0 返回 true, 否则返回 false)。
 */
bool AppsUseLightThemeValueToIsDark(DWORD value);

/**
 * Read system light/dark theme preference from Windows registry once.
 *
 * 真实读一次 Windows 注册表中的系统深浅色设置。
 *
 * @return True if system is in dark mode, false otherwise (defaults to Light on failure).
 *
 *   系统当前是否为深色模式;读取失败一律返回 false(浅色)。
 */
bool DetectSystemIsDark();

}  // namespace markair
