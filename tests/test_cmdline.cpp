// 消除 shell32.dll 强制依赖:用 mdvn::ParseCommandLine 替代 CommandLineToArgvW,
// 这里覆盖它的正确性——规则来自官方文档 "Parsing C++ Command-Line Arguments"
// (https://learn.microsoft.com/cpp/cpp/main-function-command-line-args)。
#include "mdvn_test.h"
#include "../src/util/cmdline.h"
#include "../src/app/bench.h"

#include <cstring>

using mdvn::Arena;
using mdvn::ParseCommandLine;
using mdvn::Vec;
using mdvn::bench::ParseArgs;
using mdvn::bench::ParsedArgs;

namespace {

// 起一块够用的 Arena,解析一段命令行,返回参数数组。
Vec<wchar_t*> Parse(const wchar_t* cmdLine, Arena* arena) {
    arena->Init(64 * 1024);
    return ParseCommandLine(cmdLine, arena);
}

}  // namespace

// 用例:单个不含空格的文件路径,argv[0] 是程序名,argv[1] 是路径原样透传。
MDVN_TEST(CmdLine_SingleUnquotedPath) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"mdvn.exe C:\\a.md", &arena);
    MDVN_CHECK(argv.Size() == 2);
    MDVN_CHECK(wcscmp(argv[0], L"mdvn.exe") == 0);
    MDVN_CHECK(wcscmp(argv[1], L"C:\\a.md") == 0);
}

// 用例:含空格的路径必须整体加引号,解析后引号被剥离、空格保留在参数内部。
MDVN_TEST(CmdLine_QuotedPathWithSpaces) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"mdvn.exe \"C:\\my docs\\a.md\"", &arena);
    MDVN_CHECK(argv.Size() == 2);
    MDVN_CHECK(wcscmp(argv[1], L"C:\\my docs\\a.md") == 0);
}

// 用例:"--bench <file>" 是 bench.h::ParseArgs 的核心使用场景,argv 要能
// 原样接上(顺序:程序名、--bench、路径)。
MDVN_TEST(CmdLine_BenchFlagThenPath) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"mdvn.exe --bench C:\\a.md", &arena);
    MDVN_CHECK(argv.Size() == 3);
    MDVN_CHECK(wcscmp(argv[1], L"--bench") == 0);
    MDVN_CHECK(wcscmp(argv[2], L"C:\\a.md") == 0);
}

// 用例:路径以反斜杠结尾、紧跟右引号收尾("C:\a\") —— 官方规则里最容易出错
// 的边界:偶数个反斜杠(这里是 1 个,奇数!)后跟引号,按规则应输出
// 0 个反斜杠 + 转义出的字面反斜杠?不——规则是看反斜杠个数本身的奇偶:
// 1 个反斜杠(奇数)+ 引号 => 输出 0 个反斜杠 + 1 个字面双引号,且不切换
// 引号边界。也就是说 "C:\a\" 里最后的 \" 会被解释成"字面双引号"而不是
// 收尾引号,后续字符仍处于引号内,直到字符串结束才把已读内容整体收尾
// (这正是官方文档"如果命令行在找到收尾引号前结束,把已读到的字符整体
// 作为最后一个参数输出"这条规则)。
MDVN_TEST(CmdLine_PathEndingWithBackslashBeforeClosingQuote) {
    Arena arena;
    // 输入:mdvn.exe "C:\a\" d  —— 引号内的内容是 C:\a\ ,末尾反斜杠数为 1(奇数),
    // 紧跟的引号被转义成字面 "，字符串未闭合，一直吃到行尾。
    Vec<wchar_t*> argv = Parse(L"mdvn.exe \"C:\\a\\\" d", &arena);
    MDVN_CHECK(argv.Size() == 2);
    // 期望:C:\a" d 全部粘成一个参数(反斜杠奇数+引号 => 字面引号，不闭合，
    // 空格因此仍在引号内，不再是分隔符）。
    MDVN_CHECK(wcscmp(argv[1], L"C:\\a\" d") == 0);
}

// 用例:偶数个反斜杠后跟引号——反斜杠减半输出，引号是真正的边界。
// 官方文档表格里的 a\\\\"b c" d e -> argv[1] == a\\b c 这一行。
MDVN_TEST(CmdLine_EvenBackslashesBeforeQuoteIsRealBoundary) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"mdvn.exe a\\\\\\\\\"b c\" d e", &arena);
    MDVN_CHECK(argv.Size() == 4);
    MDVN_CHECK(wcscmp(argv[1], L"a\\\\b c") == 0);
    MDVN_CHECK(wcscmp(argv[2], L"d") == 0);
    MDVN_CHECK(wcscmp(argv[3], L"e") == 0);
}

// 用例:官方文档表格 a"b"" c d -> argv[1] == ab" c d(引号内连续两个双引号
// 被解释为一个字面双引号，且不闭合当前引号段)。
MDVN_TEST(CmdLine_DoubledQuoteInsideQuotedSegment) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"mdvn.exe a\"b\"\" c d", &arena);
    MDVN_CHECK(argv.Size() == 2);
    MDVN_CHECK(wcscmp(argv[1], L"ab\" c d") == 0);
}

// 用例:空命令行——没有程序名也没有参数,解析结果是一个空字符串的 argv[0]。
MDVN_TEST(CmdLine_EmptyCommandLine) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"", &arena);
    MDVN_CHECK(argv.Size() == 1);
    MDVN_CHECK(wcscmp(argv[0], L"") == 0);
}

// 用例:只有程序名,没有任何参数——argc == 1,与 bench::ParseArgs 的
// "无参数" 场景对应。
MDVN_TEST(CmdLine_OnlyProgramName) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"mdvn.exe", &arena);
    MDVN_CHECK(argv.Size() == 1);
    MDVN_CHECK(wcscmp(argv[0], L"mdvn.exe") == 0);
}

// 用例:多个参数,含引号分组与普通空格分隔混合,顺序保持不变。
MDVN_TEST(CmdLine_MultipleArgs) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"mdvn.exe --bench \"C:\\my docs\\a.md\" --extra", &arena);
    MDVN_CHECK(argv.Size() == 4);
    MDVN_CHECK(wcscmp(argv[0], L"mdvn.exe") == 0);
    MDVN_CHECK(wcscmp(argv[1], L"--bench") == 0);
    MDVN_CHECK(wcscmp(argv[2], L"C:\\my docs\\a.md") == 0);
    MDVN_CHECK(wcscmp(argv[3], L"--extra") == 0);
}

// 用例:反斜杠不紧邻双引号时按字面输出,不做任何转义(官方文档表格
// a\\b d"e f"g h -> argv[1] == a\\b)。
MDVN_TEST(CmdLine_LiteralBackslashesNotBeforeQuote) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"mdvn.exe a\\\\b d\"e f\"g h", &arena);
    MDVN_CHECK(argv.Size() == 4);
    MDVN_CHECK(wcscmp(argv[1], L"a\\\\b") == 0);
    MDVN_CHECK(wcscmp(argv[2], L"de fg") == 0);
    MDVN_CHECK(wcscmp(argv[3], L"h") == 0);
}

// 用例:奇数个反斜杠+引号 -> 半数反斜杠 + 一个字面双引号,不闭合引号段
// (官方文档表格 a\\\"b c d -> argv[1] == a\"b)。
MDVN_TEST(CmdLine_OddBackslashesBeforeQuoteEscapesQuote) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"mdvn.exe a\\\\\\\"b c d", &arena);
    MDVN_CHECK(argv.Size() == 4);
    MDVN_CHECK(wcscmp(argv[1], L"a\\\"b") == 0);
    MDVN_CHECK(wcscmp(argv[2], L"c") == 0);
    MDVN_CHECK(wcscmp(argv[3], L"d") == 0);
}

// 用例:argv[0] 本身带引号(可执行文件路径含空格时,系统调用约定会这样传),
// 引号被剥离,不受后续反斜杠规则影响。
MDVN_TEST(CmdLine_ProgramNameItselfQuoted) {
    Arena arena;
    Vec<wchar_t*> argv = Parse(L"\"C:\\Program Files\\mdvn\\mdvn.exe\" C:\\a.md", &arena);
    MDVN_CHECK(argv.Size() == 2);
    MDVN_CHECK(wcscmp(argv[0], L"C:\\Program Files\\mdvn\\mdvn.exe") == 0);
    MDVN_CHECK(wcscmp(argv[1], L"C:\\a.md") == 0);
}

// T59:--register 单独出现,识别出 registerRequested,不影响 unregisterRequested。
MDVN_TEST(CmdLine_RegisterFlagRecognized) {
    wchar_t* argv[] = {const_cast<wchar_t*>(L"mdvn.exe"), const_cast<wchar_t*>(L"--register")};
    ParsedArgs a = ParseArgs(2, argv);
    MDVN_CHECK(a.registerRequested);
    MDVN_CHECK(!a.unregisterRequested);
    MDVN_CHECK(a.filePath == nullptr);
}

// T59:--unregister 单独出现,识别出 unregisterRequested。
MDVN_TEST(CmdLine_UnregisterFlagRecognized) {
    wchar_t* argv[] = {const_cast<wchar_t*>(L"mdvn.exe"), const_cast<wchar_t*>(L"--unregister")};
    ParsedArgs a = ParseArgs(2, argv);
    MDVN_CHECK(!a.registerRequested);
    MDVN_CHECK(a.unregisterRequested);
    MDVN_CHECK(a.filePath == nullptr);
}

// T59:--register 与 --unregister 同时出现——ParseArgs 本身只做纯粹的标志
// 识别,两者都被置位,真正的"报错并返回退出码 1"由调用方(main.cpp)判断,
// 这里只验证 ParseArgs 没有偷偷丢弃任何一个标志。
MDVN_TEST(CmdLine_RegisterAndUnregisterBothPresent) {
    wchar_t* argv[] = {const_cast<wchar_t*>(L"mdvn.exe"), const_cast<wchar_t*>(L"--register"),
                        const_cast<wchar_t*>(L"--unregister")};
    ParsedArgs a = ParseArgs(3, argv);
    MDVN_CHECK(a.registerRequested);
    MDVN_CHECK(a.unregisterRequested);
}

// T59:--register 与文件路径同时出现——ParseArgs 仍会识别出 filePath(纯函数
// 层面不丢信息),但 main.cpp 的约定是"忽略路径,只执行注册",这条约定在
// main.cpp 里通过"识别到 register/unregister 就直接 return,不再读 filePath"
// 实现,不需要 ParseArgs 本身抹掉 filePath。
MDVN_TEST(CmdLine_RegisterFlagWithFilePathStillParsesBoth) {
    wchar_t* argv[] = {const_cast<wchar_t*>(L"mdvn.exe"), const_cast<wchar_t*>(L"--register"),
                        const_cast<wchar_t*>(L"C:\\a.md")};
    ParsedArgs a = ParseArgs(3, argv);
    MDVN_CHECK(a.registerRequested);
    MDVN_CHECK(wcscmp(a.filePath, L"C:\\a.md") == 0);
}

// T59:--unregister 与文件路径同时出现,同上,只验证识别不丢标志/路径。
MDVN_TEST(CmdLine_UnregisterFlagWithFilePathStillParsesBoth) {
    wchar_t* argv[] = {const_cast<wchar_t*>(L"mdvn.exe"), const_cast<wchar_t*>(L"C:\\a.md"),
                        const_cast<wchar_t*>(L"--unregister")};
    ParsedArgs a = ParseArgs(3, argv);
    MDVN_CHECK(a.unregisterRequested);
    MDVN_CHECK(wcscmp(a.filePath, L"C:\\a.md") == 0);
}
