// markair 静默后台自动更新模块。
// 流程：启动时检查待安装更新 → 后台线程轮询 GitHub releases API →
// 下载并校验新版 exe → 写入 state.ini → 下次启动时自替换。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace markair {

/** 更新下载完成后投递给主窗口的消息。wParam 恒为 0，lParam 是堆上的 UpdateResult*。 */
constexpr UINT kWmUpdateReady = WM_APP + 20;

/**
 * 在任何 D2D/窗口初始化之前调用，检查 state.ini 是否有待安装的新版本。
 * 有则执行 MoveFileExW 自替换；无则直接返回。
 *
 * @example markair::ApplyPendingUpdate(); // 在 wWinMain 最开头调用
 */
void ApplyPendingUpdate();

/**
 * 在主窗口句柄就绪后调用，启动后台线程检查 GitHub releases API。
 * 完成后向 hwnd 投递 kWmUpdateReady 消息；bench 模式下调用方自行跳过。
 *
 * @param hwnd 接收 kWmUpdateReady 通知的主窗口句柄，非空。
 * @example markair::BeginUpdateCheck(hwnd);
 */
void BeginUpdateCheck(HWND hwnd);

/**
 * 在窗口过程响应 kWmUpdateReady 时调用，把临时文件路径写入 state.ini。
 * 负责释放 resultPtr 指向的 UpdateResult 堆内存。
 *
 * @param hwnd 主窗口句柄（预留，目前未使用）。
 * @param resultPtr kWmUpdateReady 消息的 lParam 转换来的 UpdateResult* 指针。
 * @example markair::CommitPendingUpdate(hwnd, reinterpret_cast<void*>(lparam));
 */
void CommitPendingUpdate(HWND hwnd, void* resultPtr);

}  // namespace markair
