// S3 spike: md4c 体积与速度探测。
// 目的：验证 vendored md4c 的解析速度是否满足预算，并记录 exe 体积增量。
// 见 05-m0-tasks.md S3。
//
// 编译（开发者命令提示符）：
//   cl /nologo /EHsc /O2 /W4 spikes\s03_md4c.cpp third_party\md4c\md4c.c
//      /I third_party\md4c /link psapi.lib /out:spikes\s03_md4c.exe
//
// 用法：
//   s03_md4c.exe <markdown文件>
// 回调里只计数（不建文档模型），单独测 md4c 本身的解析开销。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "md4c.h"

namespace {

struct Counters {
    long blocks = 0;
    long spans = 0;
    long texts = 0;
};

int EnterBlock(MD_BLOCKTYPE, void*, void* userdata) {
    static_cast<Counters*>(userdata)->blocks++;
    return 0;
}
int LeaveBlock(MD_BLOCKTYPE, void*, void*) { return 0; }
int EnterSpan(MD_SPANTYPE, void*, void* userdata) {
    static_cast<Counters*>(userdata)->spans++;
    return 0;
}
int LeaveSpan(MD_SPANTYPE, void*, void*) { return 0; }
int Text(MD_TEXTTYPE, const MD_CHAR*, MD_SIZE, void* userdata) {
    static_cast<Counters*>(userdata)->texts++;
    return 0;
}
void DebugLog(const char*, void*) {}

SIZE_T PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
    return pmc.PrivateUsage;
}

double NowMs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: s03_md4c.exe <markdown文件> [重复次数]\n");
        return 1;
    }
    int repeat = (argc >= 3) ? atoi(argv[2]) : 1;

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "无法打开文件: %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<char> buf(static_cast<size_t>(size));
    fread(buf.data(), 1, static_cast<size_t>(size), f);
    fclose(f);

    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_COMMONMARK; // M0：不额外开表格/LaTeX 等 flag
    parser.enter_block = EnterBlock;
    parser.leave_block = LeaveBlock;
    parser.enter_span = EnterSpan;
    parser.leave_span = LeaveSpan;
    parser.text = Text;
    parser.debug_log = DebugLog;
    parser.syntax = nullptr;

    SIZE_T memBefore = PrivateBytes();
    double best = 1e18, total = 0;
    Counters lastCounters;
    for (int i = 0; i < repeat; ++i) {
        Counters c;
        double t0 = NowMs();
        int rc = md_parse(buf.data(), static_cast<MD_SIZE>(buf.size()), &parser, &c);
        double dt = NowMs() - t0;
        if (rc != 0) {
            fprintf(stderr, "md_parse 失败 rc=%d\n", rc);
            return 1;
        }
        best = (dt < best) ? dt : best;
        total += dt;
        lastCounters = c;
    }
    SIZE_T memAfter = PrivateBytes();

    fprintf(stderr, "file=%s size_bytes=%ld repeat=%d\n", argv[1], size, repeat);
    fprintf(stderr, "parse_ms_best=%.3f parse_ms_avg=%.3f\n", best, total / repeat);
    fprintf(stderr, "blocks=%ld spans=%ld texts=%ld\n", lastCounters.blocks, lastCounters.spans, lastCounters.texts);
    fprintf(stderr, "private_bytes_before=%zu private_bytes_after=%zu delta_kb=%.1f\n",
            static_cast<size_t>(memBefore), static_cast<size_t>(memAfter),
            static_cast<double>(static_cast<long long>(memAfter) - static_cast<long long>(memBefore)) / 1024.0);
    return 0;
}
