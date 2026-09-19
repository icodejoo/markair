// T73 覆盖测试:BENCH-D(10MB 超大文档语料)的"打开不崩溃 + 如实记录截断状态"冒烟测试。
//
// 与 test_corpus_smoke.cpp 的区别:BENCH-D.md 由 bench/make_bench_d.ps1 确定性生成,
// 按 M3 裁决 #8② **不进 git**,复跑前需要先手动执行一次生成脚本。因此本用例在
// 文件不存在时不判失败(避免"没跑生成脚本"变成一次假的 CI 红灯),只在文件存在
// 时才跑完整的"打开 -> 解析 -> 布局"链路,并如实打印(而不是断言)是否触发了
// kMaxDocumentNodeCount 节点数上限截断——08-m3-tasks.md 明确要求:若触发截断,
// 这是产品事实,必须记录,不能当成测量瑕疵回避,也不能因为触发了就判用例失败。
#include "markair_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/doc/file_map.h"
#include "../src/doc/encoding.h"
#include "../src/doc/front_matter.h"
#include "../src/layout/layout.h"

#include <cstdio>

using markair::Arena;
using markair::Document;
using markair::DetectedEncoding;
using markair::EncodingDetection;
using markair::BlockLayoutEngine;
using markair::FileMap;
using markair::FileMapError;
using markair::ParseMarkdown;
using markair::SkipFrontMatter;
using markair::StrSlice;

#ifndef MARKAIR_BENCH_DIR
#define MARKAIR_BENCH_DIR "bench"
#endif

// 用例:对 bench/BENCH-D.md 跑一次"打开 -> 解析 -> 布局",不崩溃即算通过；
// 是否触发节点数上限截断只如实打印到 stderr,不作为失败条件(截断与否是
// 产品事实,由 bench/M3-BENCHD.md 记录、由人判断是否需要处理,不是本用例
// 该拍板的事)。
MARKAIR_TEST(BenchDSmoke_TenMegabyteDocOpensParsesLayoutsWithoutCrash) {
    wchar_t path[600];
    const char* narrowDir = MARKAIR_BENCH_DIR;
    wchar_t dir[512];
    size_t i = 0;
    for (; narrowDir[i] != 0 && i + 1 < 512; ++i) dir[i] = static_cast<wchar_t>(narrowDir[i]);
    dir[i] = 0;
    swprintf_s(path, 600, L"%s\\BENCH-D.md", dir);

    FileMap fm;
    FileMapError err = fm.Open(path);
    if (err != FileMapError::None) {
        fprintf(stderr,
                "benchd_smoke: bench/BENCH-D.md 不存在或打不开(err=%d)，"
                "跳过本用例——先执行一次 bench\\make_bench_d.ps1 生成语料。\n",
                static_cast<int>(err));
        return;
    }

    Arena arena;
    // BENCH-D 目标 10MB±0.5MB 源文本，解析后的模型数组按 M1 经验远小于源文本，
    // 256MB 预留足够冗余。
    arena.Init(256 * 1024 * 1024);

    StrSlice raw = fm.Data();
    EncodingDetection detection = markair::DetectEncoding(raw);
    StrSlice utf8;
    if (detection.encoding == DetectedEncoding::Utf16Le ||
        detection.encoding == DetectedEncoding::AnsiFallback) {
        markair::Utf16Slice utf16 = markair::DecodeToUtf16(raw, detection, &arena);
        utf8 = markair::Utf16ToUtf8(utf16, &arena);
    } else {
        utf8 = StrSlice{raw.data + detection.contentOffset, raw.len - detection.contentOffset};
    }
    StrSlice body = SkipFrontMatter(utf8);

    Document doc = ParseMarkdown(body, &arena);

    fprintf(stderr,
            "benchd_smoke: file_bytes=%u blocks=%u truncated=%s\n",
            static_cast<unsigned>(raw.len), static_cast<unsigned>(doc.blocks.Size()),
            doc.truncated ? "true(触发kMaxDocumentNodeCount上限截断)" : "false");

    MARKAIR_CHECK(doc.blocks.Size() > 0);

    BlockLayoutEngine layout;
    bool relayoutOk = layout.Relayout(doc, 760.0f);
    MARKAIR_CHECK(relayoutOk);
    MARKAIR_CHECK_EQ(layout.BlockCount(), doc.blocks.Size());

    fm.Close();
}
