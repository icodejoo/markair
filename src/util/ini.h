// mdvn 的 `state.ini` 配置读取(T39):自写 KV 解析,不引 JSON/TOML 库。
//
// 路径固定为 `%LOCALAPPDATA%\mdvn\state.ini`,启动期一次性读完(单次 < 1KB),
// 文件不存在按默认值走 —— **不创建目录、不写盘**(M1 没有任何需要持久化的值,
// 缩放级别按裁决 #4 只存内存态)。
//
// M1 支持的键(其余键一律忽略,便于日后扩展时老版本不报错):
//   load_remote_images = 0|1      网络图片开关(T34),默认 0
//   font_body_primary  = <族名>   正文主字体族名覆盖(裁决 #10 白名单之外的用户偏好)
//   font_body_fallback = <族名>   正文回退族名覆盖(中文回退链首选)
//   font_mono_primary  = <族名>   等宽主字体族名覆盖
//   font_mono_fallback = <族名>   等宽回退族名覆盖
// 明确**不含** `image_cache_mb`(随 T32 二次裁决作废)与 `zoom`(裁决 #4,留给 M2)。
#pragma once

#include "str.h"
#include "types.h"

namespace mdvn {

// 字体族名覆盖项的缓冲长度(含结尾 '\0');Win32 的 LF_FACESIZE 是 32,这里留一倍余量。
constexpr u32 kMaxFontFamilyChars = 64;

// 单次读取 state.ini 的字节上限(架构 §6 "启动期一次性读完、单次 < 1KB")。
constexpr u32 kMaxIniBytes = 1024;

/**
 * 运行期配置:`state.ini` 里 M1 全部键的解析结果。
 * 纯 POD,不含任何有副作用的构造函数,直接放栈上即可。
 */
struct AppSettings {
    bool loadRemoteImages;                        // 网络图片开关,默认 false(= 0)
    wchar_t fontBodyPrimary[kMaxFontFamilyChars]; // 空串表示不覆盖,走裁决 #10 默认族
    wchar_t fontBodyFallback[kMaxFontFamilyChars];
    wchar_t fontMonoPrimary[kMaxFontFamilyChars];
    wchar_t fontMonoFallback[kMaxFontFamilyChars];
};

/**
 * 用默认值填满一份配置(等价于"配置文件不存在")。
 *
 * @param out 待填充的配置,非空。
 * @example
 *   mdvn::AppSettings s;
 *   mdvn::DefaultAppSettings(&s);   // s.loadRemoteImages == false
 */
void DefaultAppSettings(AppSettings* out);

/**
 * 解析一段 ini 文本并就地覆盖对应字段。纯函数,不碰文件系统,可脱离 Win32 单测。
 *
 * 容错规则:空行、`;` / `#` 注释行、`[section]` 行一律跳过;键值两侧的空格与
 * 制表符、行尾 CR 全部裁掉;无法解析成数字的值保持原值不变;超范围的数值被
 * 钳制到合法区间;未知键静默忽略。
 *
 * @param text ini 文本(UTF-8),允许不以 '\0' 结尾。
 * @param out 解析结果,调用前须先用 `DefaultAppSettings` 初始化,非空。
 * @return 实际被识别并应用的键个数。
 * @example
 *   mdvn::AppSettings s;
 *   mdvn::DefaultAppSettings(&s);
 *   mdvn::ParseIniSettings(mdvn::StrSlice{"load_remote_images=1\n", 21}, &s);
 */
u32 ParseIniSettings(StrSlice text, AppSettings* out);

/**
 * 从 `%LOCALAPPDATA%\mdvn\state.ini` 读取配置(启动期调用一次)。
 * 文件不存在/读取失败时 `out` 保持默认值,**不创建目录、不写盘**。
 *
 * @param out 配置输出,非空;函数内部会先调用 `DefaultAppSettings`。
 * @return 真的读到并解析了配置文件返回 true;文件不存在或不可读返回 false。
 * @example
 *   mdvn::AppSettings settings;
 *   mdvn::LoadAppSettings(&settings);
 *   remoteLoader.Init(hwnd, settings.loadRemoteImages);
 */
bool LoadAppSettings(AppSettings* out);

}  // namespace mdvn
