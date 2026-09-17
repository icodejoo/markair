// mdvn 的命令行参数解析。不依赖 CommandLineToArgvW(shell32.dll 的导出符号)——
// wWinMain 在最开始、无条件调用它会立刻触发 shell32.dll 真正加载,连带拉起
// windows.storage.dll/comctl32.dll 等一整条 Shell 基础设施(约 18MB),
// 而 mdvn 自己从不使用任何 Shell 命名空间/公共控件功能,纯属"连坐"。
// 这里用一份不依赖 Win32 的纯函数替代,规则与 CRT/CommandLineToArgvW 完全
// 一致(官方文档 "Parsing C++ Command-Line Arguments"),只是不产生 shell32
// 的导入依赖。
#pragma once

#include "arena.h"
#include "span.h"

namespace mdvn {

/**
 * 把原始命令行字符串解析成参数数组,规则等价于 CommandLineToArgvW:
 * - 参数间以空格/制表符分隔;
 * - argv[0](程序路径)只用双引号做"允许内含空白"的分组,不套用反斜杠转义;
 * - 其余参数里,双引号内的空白不算分隔符,一对连续双引号在引号内被解释为
 *   一个字面双引号;反斜杠在不紧邻双引号时按字面输出,紧邻双引号时按
 *   "N 个反斜杠 + 双引号"规则处理(N 为偶数:输出 N/2 个反斜杠、双引号
 *   作为边界;N 为奇数:输出 (N-1)/2 个反斜杠 + 一个字面双引号,不切换边界)。
 *
 * @param commandLine 原始命令行,通常是 GetCommandLineW() 的返回值;可为空指针
 *        (等价于空字符串,返回空数组)。
 * @param arena 输出的参数字符串与数组所在的 Arena。
 * @return 解析出的参数数组,argv[0] 是程序路径;每个元素以 '\0' 结尾。
 *         Arena 分配失败时对应参数可能被截断,但不会返回悬空指针。
 * @example
 *   mdvn::Vec<wchar_t*> argv = mdvn::ParseCommandLine(GetCommandLineW(), &arena);
 *   int argc = static_cast<int>(argv.Size());
 *   mdvn::bench::ParsedArgs benchArgs = mdvn::bench::ParseArgs(argc, argv.Data());
 */
Vec<wchar_t*> ParseCommandLine(const wchar_t* commandLine, Arena* arena);

}  // namespace mdvn
