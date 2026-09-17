#include "bench.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace mdvn::bench {

namespace {

// 是否已通过 --bench 启用埋点(POD 全局,零初始化,无副作用,遵守
// docs/coding-rules.md 第 6 条)。不启用时,下面所有 g_t* 恒为 0,
// Mark* 函数第一条判断就返回,不产生任何额外调用。
bool g_enabled = false;

// 各时间点的 QueryPerformanceCounter 计数值,0 表示尚未记录。
int64_t g_tProcessStart = 0;
int64_t g_tParseDone = 0;
int64_t g_tLayoutDone = 0;
int64_t g_tWindowCreated = 0;
int64_t g_tFirstPresent = 0;

// "首次 Present"只应记录一次,后续调用忽略。
bool g_firstPresentRecorded = false;

// 取当前 QueryPerformanceCounter 计数值。
int64_t NowCounter() {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

}  // namespace

ParsedArgs ParseArgs(int argc, wchar_t* const* argv) {
    ParsedArgs result{false, nullptr};
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--bench") == 0) {
            result.benchEnabled = true;
        } else if (result.filePath == nullptr) {
            result.filePath = argv[i];
        }
    }
    return result;
}

void Enable() { g_enabled = true; }

void MarkProcessStart() {
    if (!g_enabled) return;
    g_tProcessStart = NowCounter();
}

void MarkParseDone() {
    if (!g_enabled) return;
    g_tParseDone = NowCounter();
}

void MarkLayoutDone() {
    if (!g_enabled) return;
    g_tLayoutDone = NowCounter();
}

void MarkWindowCreated() {
    if (!g_enabled) return;
    g_tWindowCreated = NowCounter();
}

void MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}

size_t FormatReportFromValues(int64_t freq,
                               int64_t tProcessStart,
                               int64_t tParseDone,
                               int64_t tLayoutDone,
                               int64_t tWindowCreated,
                               int64_t tFirstPresent,
                               uint64_t privateBytes,
                               char* out, size_t outCap) {
    if (out == nullptr || outCap == 0) return 0;
    if (freq <= 0) freq = 1;

    const double toMs = 1000.0 / static_cast<double>(freq);
    const double processToParse = static_cast<double>(tParseDone - tProcessStart) * toMs;
    const double parseToLayout = static_cast<double>(tLayoutDone - tParseDone) * toMs;
    const double layoutToWindow = static_cast<double>(tWindowCreated - tLayoutDone) * toMs;
    const double windowToPresent = static_cast<double>(tFirstPresent - tWindowCreated) * toMs;
    const double processToPresent = static_cast<double>(tFirstPresent - tProcessStart) * toMs;

    int written = _snprintf_s(
        out, outCap, _TRUNCATE,
        "t_process_to_parse_ms=%.3f t_parse_to_layout_ms=%.3f t_layout_to_window_ms=%.3f "
        "t_window_to_present_ms=%.3f t_process_to_present_ms=%.3f private_bytes=%llu\n",
        processToParse, parseToLayout, layoutToWindow, windowToPresent, processToPresent,
        static_cast<unsigned long long>(privateBytes));

    if (written < 0) {
        // _TRUNCATE 触发截断:内容已写入(含结尾 '\0'),按缓冲区已用满长度返回。
        return outCap > 0 ? outCap - 1 : 0;
    }
    return static_cast<size_t>(written);
}

void EmitReport() {
    if (!g_enabled) return;

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    uint64_t privateBytes = 0;
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        privateBytes = static_cast<uint64_t>(pmc.PrivateUsage);
    }

    char line[256];
    FormatReportFromValues(freq.QuadPart, g_tProcessStart, g_tParseDone, g_tLayoutDone,
                            g_tWindowCreated, g_tFirstPresent, privateBytes, line, sizeof(line));
    fputs(line, stderr);
}

}  // namespace mdvn::bench
