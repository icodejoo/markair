// T14 覆盖测试:src/app/bench.h 里可以脱离 Win32/QueryPerformanceCounter
// 单独测试的两个纯函数 —— 命令行参数识别、KV 报告格式化。
// Mark*/EmitReport 依赖真实的 QueryPerformanceCounter/GetProcessMemoryInfo,
// 不在此编造假测试。
#include "mdvn_test.h"
#include "../src/app/bench.h"

#include <cstring>

using mdvn::bench::FormatReportFromValues;
using mdvn::bench::ParseArgs;
using mdvn::bench::ParsedArgs;

// 用例:没有任何参数(只有程序名)时,不识别出 --bench,也没有文件路径。
MDVN_TEST(Bench_ParseArgs_NoArgsMeansDisabledAndNoPath) {
    wchar_t* argv[] = {const_cast<wchar_t*>(L"mdvn.exe")};
    ParsedArgs a = ParseArgs(1, argv);
    MDVN_CHECK(!a.benchEnabled);
    MDVN_CHECK(a.filePath == nullptr);
}

// 用例:"--bench <file>" 的常见形式,标志与路径都能正确识别。
MDVN_TEST(Bench_ParseArgs_BenchFlagThenPath) {
    wchar_t* argv[] = {const_cast<wchar_t*>(L"mdvn.exe"), const_cast<wchar_t*>(L"--bench"),
                        const_cast<wchar_t*>(L"C:\\a.md")};
    ParsedArgs a = ParseArgs(3, argv);
    MDVN_CHECK(a.benchEnabled);
    MDVN_CHECK(a.filePath != nullptr);
    MDVN_CHECK(wcscmp(a.filePath, L"C:\\a.md") == 0);
}

// 用例:参数顺序反过来("<file> --bench")同样能正确识别,不依赖固定顺序。
MDVN_TEST(Bench_ParseArgs_PathThenBenchFlag) {
    wchar_t* argv[] = {const_cast<wchar_t*>(L"mdvn.exe"), const_cast<wchar_t*>(L"C:\\a.md"),
                        const_cast<wchar_t*>(L"--bench")};
    ParsedArgs a = ParseArgs(3, argv);
    MDVN_CHECK(a.benchEnabled);
    MDVN_CHECK(wcscmp(a.filePath, L"C:\\a.md") == 0);
}

// 用例:没有 --bench,只给文件路径("mdvn.exe <file>" 的正常打开场景)。
MDVN_TEST(Bench_ParseArgs_PathOnlyNoBenchFlag) {
    wchar_t* argv[] = {const_cast<wchar_t*>(L"mdvn.exe"), const_cast<wchar_t*>(L"C:\\a.md")};
    ParsedArgs a = ParseArgs(2, argv);
    MDVN_CHECK(!a.benchEnabled);
    MDVN_CHECK(wcscmp(a.filePath, L"C:\\a.md") == 0);
}

// 用例:格式化输出是一行以 '\n' 结尾、包含全部约定 key 的 KV 文本,
// 且各耗时差值计算正确(用简单的整数计数值和已知频率,方便手算校验)。
MDVN_TEST(Bench_FormatReportFromValues_ProducesExpectedKvLine) {
    char line[256];
    // freq = 1000(每毫秒 1 个计数),process=0 parse=5 layout=8 window=12 present=20。
    size_t n = FormatReportFromValues(1000, 0, 5, 8, 12, 20, 9000000ull, line, sizeof(line));

    MDVN_CHECK(n > 0);
    MDVN_CHECK(line[n - 1] == '\n');
    MDVN_CHECK(strstr(line, "t_process_to_parse_ms=5.000") != nullptr);
    MDVN_CHECK(strstr(line, "t_parse_to_layout_ms=3.000") != nullptr);
    MDVN_CHECK(strstr(line, "t_layout_to_window_ms=4.000") != nullptr);
    MDVN_CHECK(strstr(line, "t_window_to_present_ms=8.000") != nullptr);
    MDVN_CHECK(strstr(line, "t_process_to_present_ms=20.000") != nullptr);
    MDVN_CHECK(strstr(line, "private_bytes=9000000") != nullptr);
}

// 用例:非正数的频率被当成 1 处理,不产生除零/负数耗时导致的 NaN 或崩溃。
MDVN_TEST(Bench_FormatReportFromValues_NonPositiveFreqFallsBackToOne) {
    char line[256];
    size_t n = FormatReportFromValues(0, 0, 0, 0, 0, 0, 0, line, sizeof(line));
    MDVN_CHECK(n > 0);
    MDVN_CHECK(strstr(line, "t_process_to_parse_ms=0.000") != nullptr);
}

// 用例:输出缓冲区为空/容量为 0 时安全返回 0,不写越界、不崩溃。
MDVN_TEST(Bench_FormatReportFromValues_ZeroCapacityIsSafe) {
    char line[1];
    MDVN_CHECK(FormatReportFromValues(1000, 0, 1, 2, 3, 4, 0, line, 0) == 0);
    MDVN_CHECK(FormatReportFromValues(1000, 0, 1, 2, 3, 4, 0, nullptr, 256) == 0);
}
