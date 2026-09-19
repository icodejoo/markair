// markair 的 `state.ini` 配置读取(T39):自写 KV 解析,不引 JSON/TOML 库。
//
// 路径固定为 `%LOCALAPPDATA%\markair\state.ini`,启动期一次性读完(单次 < 1KB),
// 文件不存在按默认值走 —— **不创建目录、不写盘**(M1 没有任何需要持久化的值,
// 缩放级别按裁决 #4 只存内存态)。
//
// M1 支持的键(其余键一律忽略,便于日后扩展时老版本不报错):
//   load_remote_images = 0|1      网络图片开关(T34),默认 1(2026-09-19 裁决反转)
//   font_body_primary  = <族名>   正文主字体族名覆盖(裁决 #10 白名单之外的用户偏好)
//   font_body_fallback = <族名>   正文回退族名覆盖(中文回退链首选)
//   font_mono_primary  = <族名>   等宽主字体族名覆盖
//   font_mono_fallback = <族名>   等宽回退族名覆盖
//   theme              = system|light|dark  主题偏好(T47),大小写不敏感,
//                        默认 system,非法值回落 system
// 明确**不含** `image_cache_mb`(随 T32 二次裁决作废)。
//
// T57 追加(接过 M1 T29 的挂账,字号缩放持久化):
//   zoom = <浮点值>   字号缩放系数,读入后钳制到 FontSubsystem 固定的 8 个
//                      离散档位 {0.8, 0.9, 1.0, 1.15, 1.3, 1.5, 1.75, 2.0}
//                      中最近的一个(`FontSubsystem::ClampToNearestZoomLevel`),
//                      不直接采信任意浮点(任意浮点会让 layout 缓存抖动);
//                      非法值(解析失败)回落默认档位 1.0。
//
// T56 追加(裁决 #8:窗口状态记忆,全局一份 + 层叠偏移):
//   win_x/win_y/win_w/win_h = 整数    上次窗口的还原态矩形(物理像素,
//                        `WINDOWPLACEMENT::rcNormalPosition`,不是最大化后的
//                        矩形);`win_w`/`win_h` 缺失或 <= 0 表示"没有存过"
//                        (首次启动),按系统默认位置/尺寸走。
//   win_maximized      = 0|1          上次退出时窗口是否处于最大化态,默认 0。
//   last_open_dir      = <路径>       "打开文件"对话框上次选中文件所在目录,
//                        下次弹窗据此调用 SetFolder 定位到同一目录;默认空
//                        (未存过,对话框走系统默认目录)。
//
// T55 起本文件同时具备**写盘**能力(`SaveAppSettings`):写出格式与本文件
// 的解析口径完全对称,UTF-8 无 BOM;保留未识别的原有键;先写 `.tmp` 再
// `MoveFileExW` 原子替换;写盘失败一律静默降级(不弹窗、不写日志)。
// 并发策略是裁决 #7 定死的"读-改-写 + 命名互斥体":复用与 `src/app/main.cpp`
// "同文件重复打开前置已有窗口"那套完全相同的技术手段(命名 Mutex +
// `WaitForSingleObject` 超时 + `ReleaseMutex`),但用固定的独立名字
// `kStateIniMutexName`(state.ini 是全局唯一一份,不能按文件路径区分),
// 因此没有直接复用 main.cpp 里那个按路径哈希取名的具体函数——两者是同一
// 机制的两个独立实例,而不是又发明了一套新的并发原语。
#pragma once

#include "../shell/theme_state.h"
#include "str.h"
#include "types.h"

namespace markair {

// 字体族名覆盖项的缓冲长度(含结尾 '\0');Win32 的 LF_FACESIZE 是 32,这里留一倍余量。
constexpr u32 kMaxFontFamilyChars = 64;

// 单次读取 state.ini 的字节上限(架构 §6 "启动期一次性读完、单次 < 1KB")。
//
// T55 核算:M1 现有键(4 个字体族名,每个最多 kMaxFontFamilyChars-1=63 个
// UTF-16 code unit,UTF-8 最坏情形每单元 3 字节 -> 单键值最多 ~189 字节)
// 加 load_remote_images/theme,合计已接近 900 字节;M2 后续会再加入
// theme(已含)/zoom/win_x/win_y/win_w/win_h/win_maximized 共 6 个短数值键
// (每个不超过 20 字节),额外约 120 字节。原 1024 字节上限对"读+保留未识别
// 旧键+写回"这条链路(读原文时可能还叠加老版本遗留的若干行)裕量太薄,
// 故上调到 2048——键总数仍 < 15、单次读仍是几百字节到 1~2KB 量级,不影响
// "启动期一次性读完"的架构约束,只是把裕量留够。
constexpr u32 kMaxIniBytes = 2048;

// state.ini 写盘用的命名互斥体名字(裁决 #7:读-改-写 + 命名互斥体)。
// 与 src/app/main.cpp 里"同文件重复打开前置已有窗口"用的是同一套技术手段
// (CreateMutexW + WaitForSingleObject 超时 + ReleaseMutex),只是这里的名字
// 固定不按路径哈希区分——state.ini 全局只有一份,不需要按文件区分互斥体。
constexpr wchar_t kStateIniMutexName[] = L"markair_state_ini_write_mutex";

// 命名互斥体等待超时(毫秒)。绝不无限等:超时即放弃本次写盘并静默返回。
constexpr DWORD kStateIniMutexTimeoutMs = 2000;

/**
 * 运行期配置:`state.ini` 里 M1 全部键的解析结果。
 * 纯 POD,不含任何有副作用的构造函数,直接放栈上即可。
 */
struct AppSettings {
    bool loadRemoteImages;                        // 网络图片开关,默认 true(= 1,2026-09-19 裁决反转)
    wchar_t fontBodyPrimary[kMaxFontFamilyChars]; // 空串表示不覆盖,走裁决 #10 默认族
    wchar_t fontBodyFallback[kMaxFontFamilyChars];
    wchar_t fontMonoPrimary[kMaxFontFamilyChars];
    wchar_t fontMonoFallback[kMaxFontFamilyChars];
    /**
     * Theme preference (ThemeSetting::Light or ThemeSetting::Dark).
     *
     * 主题偏好(ThemeSetting::Light 或 ThemeSetting::Dark)。
     */
    ThemeSetting theme;

    /**
     * Whether a valid theme key was explicitly present in state.ini.
     *
     * 是否在 state.ini 中显式解析到了有效的主题配置键。
     */
    bool hasTheme;

    // T57:字号缩放系数,恒为 FontSubsystem 8 个离散档位之一,默认 1.0。
    float zoom;

    // T56:上次窗口的还原态矩形(物理像素)。winW/winH <= 0 表示"没有存过",
    // 调用方(window.cpp)据此判断要不要走系统默认位置/尺寸,而不是走越界钳制。
    i32 winX;
    i32 winY;
    i32 winW;
    i32 winH;
    bool winMaximized;  // 上次退出时是否处于最大化态,默认 false

    // "打开文件"对话框上次选中文件所在目录,空串表示未存过。
    wchar_t lastOpenDir[MAX_PATH];
};

/**
 * 用默认值填满一份配置(等价于"配置文件不存在")。
 *
 * @param out 待填充的配置,非空。
 * @example
 *   markair::AppSettings s;
 *   markair::DefaultAppSettings(&s);   // s.loadRemoteImages == true
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
 *   markair::AppSettings s;
 *   markair::DefaultAppSettings(&s);
 *   markair::ParseIniSettings(markair::StrSlice{"load_remote_images=1\n", 21}, &s);
 */
u32 ParseIniSettings(StrSlice text, AppSettings* out);

/**
 * 从 `%LOCALAPPDATA%\markair\state.ini` 读取配置(启动期调用一次)。
 * 文件不存在/读取失败时 `out` 保持默认值,**不创建目录、不写盘**。
 *
 * @param out 配置输出,非空;函数内部会先调用 `DefaultAppSettings`。
 * @return 真的读到并解析了配置文件返回 true;文件不存在或不可读返回 false。
 * @example
 *   markair::AppSettings settings;
 *   markair::LoadAppSettings(&settings);
 *   remoteLoader.Init(hwnd, settings.loadRemoteImages);
 */
bool LoadAppSettings(AppSettings* out);

/**
 * 把配置写到 `%LOCALAPPDATA%\markair\state.ini`(裁决 #7:读-改-写 + 命名互斥体)。
 *
 * 内部流程:取固定名字的命名互斥体(超时 `kStateIniMutexTimeoutMs`,超时即
 * 放弃、静默返回)→ 重读磁盘上的当前文件 → 只把"本进程自上次
 * `LoadAppSettings`/`SaveAppSettings` 以来真正改动过的键"合入,其余键(含
 * 未识别的旧键、别的进程并发写入的键)原样保留 → 写 `state.ini.tmp` →
 * `MoveFileExW` 原子替换为 `state.ini`。目录只有两级,用 `CreateDirectoryW`
 * 逐级手写创建,不引入 `SHCreateDirectoryExW`(避免在保存这一刻额外拉起
 * shell32.dll)。任何一步失败(只读目录、磁盘满、互斥体超时、路径非法)都
 * 一律静默降级,不弹窗、不写日志。
 *
 * @param settings 待写入的配置(通常是本进程当前内存态)。
 * @return 成功写盘返回 true;任何失败(含静默降级)返回 false。
 * @example
 *   markair::AppSettings settings;
 *   markair::LoadAppSettings(&settings);
 *   settings.theme = markair::ThemeSetting::Dark;
 *   markair::SaveAppSettings(settings);
 */
bool SaveAppSettings(const AppSettings& settings);

}  // namespace markair
