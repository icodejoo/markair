// 单实例 IPC(WM_COPYDATA)协议:魔数定义 + 纯函数校验/解析。独立成头文件
// (而不是塞进 window.h/.cpp),是为了让 markair_tests 能直接单测这段解析
// 逻辑,不必为此把整个 window.cpp(及其 D2D/DWM/UxTheme 依赖)拖进测试目标。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>

namespace markair {

/**
 * 单实例 IPC(WM_COPYDATA)协议魔数:次实例启动时把待打开路径通过
 * WM_COPYDATA 投递给已运行的主实例,主实例 WndProc 用它校验消息来源
 * 确实是本程序自己的次实例,而不是其它程序误发的 WM_COPYDATA。
 * 取值即字符串 "MKAR" 的 ASCII 编码。
 */
constexpr ULONG_PTR kCopyDataMagic = 0x4D4B4152;  // "MKAR"

/**
 * 校验并解析次实例通过 WM_COPYDATA 投递过来的路径字符串,纯函数、
 * 不碰任何 Win32 窗口 API,便于单测覆盖各种畸形输入。
 * 校验规则:魔数匹配、`cbData` 非零且是 `wchar_t` 的整数倍、字符串必须以
 * L'\0' 结尾、路径非空且长度不超过 MAX_PATH。
 *
 * @param cds WM_COPYDATA 的 lparam 转换来的 COPYDATASTRUCT 指针,可为 nullptr。
 * @param outPath 校验通过时,输出指向 `cds->lpData` 内以 null 结尾字符串起始处的指针。
 * @return 校验通过返回 true,否则返回 false(`outPath` 不写)。
 * @example
 *   const wchar_t* path = nullptr;
 *   if (markair::TryParseCopyDataPath(cds, &path)) { OpenPath(path); }
 */
inline bool TryParseCopyDataPath(const COPYDATASTRUCT* cds, const wchar_t** outPath) {
    if (!cds || cds->dwData != kCopyDataMagic || cds->cbData == 0 || !cds->lpData) return false;
    if (cds->cbData % sizeof(wchar_t) != 0) return false;
    size_t charCount = cds->cbData / sizeof(wchar_t);
    const wchar_t* path = reinterpret_cast<const wchar_t*>(cds->lpData);
    if (path[charCount - 1] != L'\0') return false;
    size_t pathLen = wcsnlen(path, charCount);
    if (pathLen == 0 || pathLen >= charCount || pathLen > MAX_PATH) return false;
    *outPath = path;
    return true;
}

}  // namespace markair
