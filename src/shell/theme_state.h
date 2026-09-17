// mdvn 主题三态状态机(T47):系统主题探测 + System/Light/Dark 三态循环。
// 纯状态机模块——除了最底层"真读一次注册表"的薄函数,其余全部是不依赖
// Win32 的纯函数,可脱离窗口/D2D 单测(参见 tests/test_theme_state.cpp)。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace mdvn {

/**
 * 用户的主题偏好三态:跟随系统 / 强制浅色 / 强制深色。
 * `state.ini` 的 `theme` 键、`Ctrl+Shift+T` 循环都围绕这个类型转。
 */
enum class ThemeSetting {
    System,  // 跟随系统深浅色设置(默认)
    Light,   // 强制浅色,忽略系统设置
    Dark,    // 强制深色,忽略系统设置
};

/**
 * 三态循环的下一态:`System -> Light -> Dark -> System`。纯函数。
 * @param current 当前设置。
 * @return 循环后的下一个设置。
 * @example mdvn::ThemeSetting next = mdvn::NextThemeSetting(mdvn::ThemeSetting::System);  // Light
 */
ThemeSetting NextThemeSetting(ThemeSetting current);

/**
 * 根据用户三态偏好与当前系统深浅色探测结果,算出实际生效主题是否为深色。
 * 纯函数,不碰注册表。
 * @param setting 用户偏好(System/Light/Dark)。
 * @param systemIsDark 当前系统是否为深色模式(仅 `setting == System` 时才生效)。
 * @return 实际生效主题是否为深色。
 * @example bool dark = mdvn::ResolveEffectiveTheme(mdvn::ThemeSetting::System, true);  // true
 */
bool ResolveEffectiveTheme(ThemeSetting setting, bool systemIsDark);

/**
 * 把注册表 `AppsUseLightTheme` 的原始 DWORD 值换算成"系统是否为深色"。
 * 该键语义是"1=浅色/0=深色",纯函数,便于脱离真实注册表单测。
 * @param value 从注册表读到的原始 DWORD 值。
 * @return 值为 0 时系统是深色(true),否则(含非 0/1 的任意值)按浅色处理(false)。
 * @example bool dark = mdvn::AppsUseLightThemeValueToIsDark(0);  // true
 */
bool AppsUseLightThemeValueToIsDark(DWORD value);

/**
 * 真实读一次系统深浅色设置(`HKCU\...\Personalize` 的 `AppsUseLightTheme`)。
 * 只读,绝不写注册表;键不存在/读取失败/类型不对时不崩溃,按浅色处理。
 * 这是唯一依赖真实 Win32 注册表调用的函数,不参与单测,逻辑已收敛到
 * `AppsUseLightThemeValueToIsDark`(可单测)里。
 * @return 系统当前是否为深色模式;读取失败一律返回 false(浅色)。
 * @example bool systemIsDark = mdvn::DetectSystemIsDark();
 */
bool DetectSystemIsDark();

}  // namespace mdvn
