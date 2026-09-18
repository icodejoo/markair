// T79 覆盖测试:BENCH-A/B/C(bench/ 根目录下的启动/图片/高亮基准语料)"打开 ->
// 解析 -> 布局"冒烟测试,不经过任何 GUI/窗口路径。
//
// 为什么需要这个文件:04 测量方法表的 ASAN 手段要求喂 bench/fuzz/ 全部 9 份 +
// BENCH-A/B/C/D。BENCH-D 已有 test_benchd_smoke.cpp 覆盖,fuzz 9 份已有
// test_fuzz_smoke.cpp 覆盖,唯独 BENCH-A/B/C 之前只在 test_outline.cpp 里
// 用到了 BENCH-A(且只测大纲提取,不是完整链路)。T79 排查泄漏/内存安全问题时
// 发现:ASAN 构建的 mdvn.exe(真实 GUI 路径)在本机会在窗口创建前挂死
// (与内存安全无关的构建环境问题,已记录在 bench/M3-LEAK.md 的 T79 章节),
// 因此改用与 test_benchd_smoke.cpp 一致的"直接调用解析/布局函数,不经过窗口"
// 的路子来覆盖 BENCH-A/B/C,与 M1 T43 的既有做法(mdvn_tests.exe 全量跑)保持
// 一致,不新增第二套语料喂法。
#include "mdvn_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/doc/file_map.h"
#include "../src/doc/encoding.h"
#include "../src/doc/front_matter.h"
#include "../src/layout/layout.h"

#include <cstdio>

using mdvn::Arena;
using mdvn::Document;
using mdvn::DetectedEncoding;
using mdvn::EncodingDetection;
using mdvn::BlockLayoutEngine;
using mdvn::FileMap;
using mdvn::FileMapError;
using mdvn::ParseMarkdown;
using mdvn::SkipFrontMatter;
using mdvn::StrSlice;

#ifndef MDVN_BENCH_DIR
#define MDVN_BENCH_DIR "bench"
#endif

// 复用 test_benchd_smoke.cpp 同样的"打开 -> 解析 -> 布局"链路,对 bench/ 根
// 目录下的一份语料跑一次,不崩溃即算通过。fileName 不含目录前缀。
static void RunOpenParseLayoutSmoke(const wchar_t* fileName) {
    wchar_t path[600];
    const char* narrowDir = MDVN_BENCH_DIR;
    wchar_t dir[512];
    size_t i = 0;
    for (; narrowDir[i] != 0 && i + 1 < 512; ++i) dir[i] = static_cast<wchar_t>(narrowDir[i]);
    dir[i] = 0;
    swprintf_s(path, 600, L"%s\\%s", dir, fileName);

    FileMap fm;
    FileMapError err = fm.Open(path);
    MDVN_CHECK(err == FileMapError::None);
    if (err != FileMapError::None) return;

    Arena arena;
    arena.Init(32 * 1024 * 1024);

    StrSlice raw = fm.Data();
    EncodingDetection detection = mdvn::DetectEncoding(raw);
    StrSlice utf8;
    if (detection.encoding == DetectedEncoding::Utf16Le ||
        detection.encoding == DetectedEncoding::AnsiFallback) {
        mdvn::Utf16Slice utf16 = mdvn::DecodeToUtf16(raw, detection, &arena);
        utf8 = mdvn::Utf16ToUtf8(utf16, &arena);
    } else {
        utf8 = StrSlice{raw.data + detection.contentOffset, raw.len - detection.contentOffset};
    }
    StrSlice body = SkipFrontMatter(utf8);

    Document doc = ParseMarkdown(body, &arena);
    fprintf(stderr, "bench_abc_smoke: %ls file_bytes=%u blocks=%u\n", fileName,
            static_cast<unsigned>(raw.len), static_cast<unsigned>(doc.blocks.Size()));
    MDVN_CHECK(doc.blocks.Size() > 0);

    BlockLayoutEngine layout;
    bool relayoutOk = layout.Relayout(doc, 760.0f);
    MDVN_CHECK(relayoutOk);
    MDVN_CHECK_EQ(layout.BlockCount(), doc.blocks.Size());

    fm.Close();
}

// 用例:BENCH-A(启动基准,01 §4 指定的通用语料)。
MDVN_TEST(BenchAbcSmoke_BenchAOpensParsesLayoutsWithoutCrash) {
    RunOpenParseLayoutSmoke(L"BENCH-A.md");
}

// 用例:BENCH-B(图片密集场景基准)。
MDVN_TEST(BenchAbcSmoke_BenchBOpensParsesLayoutsWithoutCrash) {
    RunOpenParseLayoutSmoke(L"BENCH-B.md");
}

// 用例:BENCH-C(代码高亮最坏情况基准)。
MDVN_TEST(BenchAbcSmoke_BenchCOpensParsesLayoutsWithoutCrash) {
    RunOpenParseLayoutSmoke(L"BENCH-C.md");
}
