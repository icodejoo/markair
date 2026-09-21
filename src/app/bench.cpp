#include "bench.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace markair::bench {

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

// T76 逐帧埋点:样本环容量。10 秒滚动最多产生几百帧,4096 足够容纳整轮测量;
// 满了之后停止采样(不覆盖),保证分布统计对应的是一段连续时间窗口。
constexpr uint32_t kFrameSampleCap = 4096;

// 每帧两个耗时(毫秒):total = 整个 PaintOnce;draw = 其中的 D2D 绘制段。
// POD 全局数组,零初始化,无构造函数副作用(docs/coding-rules.md 第 6 条)。
float g_frameTotalMs[kFrameSampleCap] = {};
float g_frameDrawMs[kFrameSampleCap] = {};
uint32_t g_frameCount = 0;

int64_t g_tFrameBegin = 0;
int64_t g_tFrameLayoutDone = 0;

// QPC 频率,首次取样时惰性读取一次。
int64_t g_qpcFreq = 0;

// 运行期"换文档"分段埋点的五个时间点,0 表示尚未记录。同一进程内多次换
// 文档会互相覆盖——够用,因为每次换文档后调用方(main.cpp)都会立即
// EmitSwitchReport 输出,不需要跨多次换文档累积样本。
int64_t g_tSwitchBegin = 0;
int64_t g_tSwitchReleaseDone = 0;
int64_t g_tSwitchFileOpened = 0;
int64_t g_tSwitchParseDone = 0;
int64_t g_tSwitchRelayoutDone = 0;
int64_t g_tSwitchScrollResetDone = 0;
int64_t g_tSwitchFolderScanDone = 0;

// 对 [0, n) 区间的 float 数组做插入排序(样本量在千级,且只在退出前排一次,
// 不值得为它引入更复杂的排序;禁 std::sort 之外的考量见 coding-rules)。
void SortFloats(float* a, uint32_t n) {
    for (uint32_t i = 1; i < n; ++i) {
        float key = a[i];
        uint32_t j = i;
        while (j > 0 && a[j - 1] > key) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = key;
    }
}

// 取已排序数组的百分位值(最近秩法);n 为 0 时返回 0。
float Percentile(const float* sorted, uint32_t n, double p) {
    if (n == 0) return 0.0f;
    uint32_t idx = static_cast<uint32_t>(p * static_cast<double>(n));
    if (idx >= n) idx = n - 1;
    return sorted[idx];
}

}  // namespace

ParsedArgs ParseArgs(int argc, wchar_t* const* argv) {
    ParsedArgs result{false, nullptr, false, false};
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--bench") == 0) {
            result.benchEnabled = true;
        } else if (wcscmp(argv[i], L"--register") == 0) {
            result.registerRequested = true;
        } else if (wcscmp(argv[i], L"--unregister") == 0) {
            result.unregisterRequested = true;
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

void MarkFrameBegin() {
    if (!g_enabled || g_frameCount >= kFrameSampleCap) return;
    if (g_qpcFreq == 0) {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        g_qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1;
    }
    g_tFrameBegin = NowCounter();
    g_tFrameLayoutDone = g_tFrameBegin;
}

void MarkFrameLayoutDone() {
    if (!g_enabled || g_frameCount >= kFrameSampleCap) return;
    g_tFrameLayoutDone = NowCounter();
}

void MarkFrameEnd() {
    if (!g_enabled || g_frameCount >= kFrameSampleCap) return;
    const double toMs = 1000.0 / static_cast<double>(g_qpcFreq);
    int64_t end = NowCounter();
    g_frameTotalMs[g_frameCount] = static_cast<float>(static_cast<double>(end - g_tFrameBegin) * toMs);
    g_frameDrawMs[g_frameCount] =
        static_cast<float>(static_cast<double>(end - g_tFrameLayoutDone) * toMs);
    ++g_frameCount;
}

void EmitFrameReport() {
    if (!g_enabled) return;

    float first0 = g_frameCount > 0 ? g_frameTotalMs[0] : 0.0f;
    float firstDraw0 = g_frameCount > 0 ? g_frameDrawMs[0] : 0.0f;

    // 排序会打乱原顺序,首帧值先取走再排。
    SortFloats(g_frameTotalMs, g_frameCount);
    SortFloats(g_frameDrawMs, g_frameCount);

    char line[512];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "frames_n=%u frame_total_p50_ms=%.3f frame_total_p95_ms=%.3f "
                "frame_total_p99_ms=%.3f frame_total_max_ms=%.3f "
                "frame_draw_p50_ms=%.3f frame_draw_p95_ms=%.3f frame_draw_p99_ms=%.3f "
                "frame_draw_max_ms=%.3f first_frame_total_ms=%.3f first_frame_draw_ms=%.3f\n",
                g_frameCount,
                Percentile(g_frameTotalMs, g_frameCount, 0.50),
                Percentile(g_frameTotalMs, g_frameCount, 0.95),
                Percentile(g_frameTotalMs, g_frameCount, 0.99),
                g_frameCount > 0 ? g_frameTotalMs[g_frameCount - 1] : 0.0f,
                Percentile(g_frameDrawMs, g_frameCount, 0.50),
                Percentile(g_frameDrawMs, g_frameCount, 0.95),
                Percentile(g_frameDrawMs, g_frameCount, 0.99),
                g_frameCount > 0 ? g_frameDrawMs[g_frameCount - 1] : 0.0f,
                first0, firstDraw0);
    fputs(line, stderr);
}

void MarkSwitchBegin() {
    if (!g_enabled) return;
    g_tSwitchBegin = NowCounter();
}

void MarkSwitchReleaseDone() {
    if (!g_enabled) return;
    g_tSwitchReleaseDone = NowCounter();
}

void MarkSwitchFileOpened() {
    if (!g_enabled) return;
    g_tSwitchFileOpened = NowCounter();
}

void MarkSwitchParseDone() {
    if (!g_enabled) return;
    g_tSwitchParseDone = NowCounter();
}

void MarkSwitchRelayoutDone() {
    if (!g_enabled) return;
    g_tSwitchRelayoutDone = NowCounter();
}

void MarkSwitchScrollResetDone() {
    if (!g_enabled) return;
    g_tSwitchScrollResetDone = NowCounter();
}

void MarkSwitchFolderScanDone() {
    if (!g_enabled) return;
    g_tSwitchFolderScanDone = NowCounter();
}

void EmitSwitchReport(bool folderScanTriggered) {
    if (!g_enabled) return;

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    int64_t f = freq.QuadPart > 0 ? freq.QuadPart : 1;
    const double toMs = 1000.0 / static_cast<double>(f);

    const double releaseMs =
        static_cast<double>(g_tSwitchReleaseDone - g_tSwitchBegin) * toMs;
    const double openMs =
        static_cast<double>(g_tSwitchFileOpened - g_tSwitchReleaseDone) * toMs;
    const double parseMs =
        static_cast<double>(g_tSwitchParseDone - g_tSwitchFileOpened) * toMs;
    const double relayoutMs =
        static_cast<double>(g_tSwitchRelayoutDone - g_tSwitchParseDone) * toMs;
    const double scrollResetMs =
        static_cast<double>(g_tSwitchScrollResetDone - g_tSwitchRelayoutDone) * toMs;

    char line[384];
    int written;
    if (folderScanTriggered) {
        const double scanMs =
            static_cast<double>(g_tSwitchFolderScanDone - g_tSwitchScrollResetDone) * toMs;
        const double totalMs =
            static_cast<double>(g_tSwitchFolderScanDone - g_tSwitchBegin) * toMs;
        written = _snprintf_s(
            line, sizeof(line), _TRUNCATE,
            "switch_release_ms=%.3f switch_open_ms=%.3f switch_parse_ms=%.3f "
            "switch_relayout_ms=%.3f switch_scroll_reset_ms=%.3f switch_folder_scan_ms=%.3f "
            "switch_total_ms=%.3f\n",
            releaseMs, openMs, parseMs, relayoutMs, scrollResetMs, scanMs, totalMs);
    } else {
        const double totalMs =
            static_cast<double>(g_tSwitchScrollResetDone - g_tSwitchBegin) * toMs;
        written = _snprintf_s(
            line, sizeof(line), _TRUNCATE,
            "switch_release_ms=%.3f switch_open_ms=%.3f switch_parse_ms=%.3f "
            "switch_relayout_ms=%.3f switch_scroll_reset_ms=%.3f switch_total_ms=%.3f\n",
            releaseMs, openMs, parseMs, relayoutMs, scrollResetMs, totalMs);
    }
    if (written >= 0) fputs(line, stderr);
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

    // T74:在"首次 Present 已完成"这一时刻(而不是外部工具事后快照的任意
    // 时刻)精确复核 /DELAYLOAD 的四个 DLL 是否已经出现在模块列表里——
    // GetModuleHandleW 只查已加载模块表,不触发加载,零副作用。这四个
    // 布尔值直接回答"延迟加载是否真的推迟到了首帧之后"这个问题,比外部
    // 用 Get-Process.Modules 做时间点不确定的事后快照更精确。
    fprintf(stderr,
            "module_winhttp_loaded_at_first_present=%d module_shell32_loaded_at_first_present=%d "
            "module_dwmapi_loaded_at_first_present=%d module_uxtheme_loaded_at_first_present=%d\n",
            GetModuleHandleW(L"winhttp.dll") != nullptr ? 1 : 0,
            GetModuleHandleW(L"shell32.dll") != nullptr ? 1 : 0,
            GetModuleHandleW(L"dwmapi.dll") != nullptr ? 1 : 0,
            GetModuleHandleW(L"uxtheme.dll") != nullptr ? 1 : 0);
}

}  // namespace markair::bench
