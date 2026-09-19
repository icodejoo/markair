// markair "打开文档"按钮(新需求):弹出 Windows 标准文件选择对话框
// (`IFileOpenDialog`,不用老的 `GetOpenFileNameW`),选中文件后用
// `CreateProcessW` 新开一个独立的 markair.exe 进程打开它——不复用现有的
// "就地替换文档"(`openDocumentInPlace`),因为这是用户主动选择打开另一份
// 文档,应与双击资源管理器里的 .md 文件行为一致(架构决策 01-requirements.md
// §5.3-4:每个文件一个独立窗口/独立进程),不应该导航掉当前正在看的内容。
//
// 用 `CreateProcessW` 新开的这个进程是用户主动发起的一个新的独立 markair 实例,
// 不是当前 markair.exe 派生的"子进程"链——01§4"单个文档实例子进程数恒为 0"
// 约束的是"打开/切换文档过程中 markair 自身不得派生子进程",不禁止用户主动
// 触发"新开一个 markair 窗口"这个动作本身(与双击资源管理器里另一个 .md 文件
// 在 Windows 进程模型下产生的父子关系完全等价)。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../util/types.h"

namespace markair {

/**
 * 弹出标准"打开文件"对话框(`IFileOpenDialog`),文件类型过滤器用
 * `assoc.h` 的 `kAssociatedExtensions`(唯一定义处,不在这里手写一遍扩展名)。
 * 内部按需 `CoInitializeEx`(与 `assets/image.cpp` 的 WIC 惰性初始化同一手法,
 * 调用对称的 `CoUninitialize`),调用方不需要预先初始化 COM。
 *
 * 打开时按 `state.ini` 的 `last_open_dir` 键定位到上次选中文件所在目录
 * (键不存在/目录已失效时 `SetFolder` 静默失败,对话框照常走系统默认目录);
 * 用户成功选中文件后,把新目录写回 `state.ini`,下次弹窗据此定位——两端
 * 都在本函数内部完成,调用方不需要关心这份持久化。
 *
 * @param owner 对话框的父窗口,可为 nullptr(无父窗口)。
 * @param outPath 输出缓冲,选中的文件完整路径(以 '\0' 结尾)。
 * @param outCap outPath 的容量(wchar_t 个数,含结尾 '\0')。
 * @return 用户选中了一个文件并成功写入 outPath 返回 true;取消/失败返回 false。
 * @example
 *   wchar_t path[MAX_PATH];
 *   if (markair::ShowOpenMarkdownDialog(hwnd, path, MAX_PATH)) { ... }
 */
bool ShowOpenMarkdownDialog(HWND owner, wchar_t* outPath, u32 outCap);

/**
 * 拼接 `CreateProcessW` 用的命令行字符串:`"<exePath>" "<filePath>"`。
 * 两段都加引号防止路径中的空格拆散参数;纯字符串拼接,不涉及进程/IO,
 * 便于单测覆盖(不含空格路径/含空格路径等边界情况)。
 *
 * @param exePath markair.exe 自身路径。
 * @param filePath 要打开的文件路径。
 * @param outCmdLine 输出缓冲(CreateProcessW 要求可写,不能指向字面量)。
 * @param outCap outCmdLine 容量(wchar_t 个数,含结尾 '\0')。
 * @return 拼接成功返回 true;任一输入为空或缓冲不够返回 false。
 * @example
 *   wchar_t buf[64];
 *   markair::BuildLaunchCommandLine(L"C:\\markair.exe", L"C:\\a b.md", buf, 64);
 *   // buf == L"\"C:\\markair.exe\" \"C:\\a b.md\""
 */
bool BuildLaunchCommandLine(const wchar_t* exePath, const wchar_t* filePath,
                             wchar_t* outCmdLine, u32 outCap);

/**
 * 用 `CreateProcessW` 拉起一个新的 markair.exe 进程打开 `filePath`(命令行参数
 * 传该文件路径)。新进程完全独立,不等待其退出,不继承本进程的任何窗口/
 * 文档状态——效果等价于用户又双击了一次这个文件。
 *
 * @param filePath 要打开的 Markdown 文件完整路径。
 * @return 新进程成功创建返回 true;`GetModuleFileNameW`/`CreateProcessW`
 *         失败返回 false(调用方应据此显示窗口内提示,不弹 MessageBox)。
 * @example markair::LaunchNewInstance(L"C:\\docs\\readme.md");
 */
bool LaunchNewInstance(const wchar_t* filePath);

}  // namespace markair
