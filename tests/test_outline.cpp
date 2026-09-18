// T62 覆盖测试:大纲数据提取(src/doc/outline.h::ExtractOutline)。
//
// 覆盖:无标题文档、只有 H1、跳级(H1 直接到 H4)、300 条标题、标题在
// 表格/引用块内部(md4c 仍会把它建成独立的 Heading 块,大纲应照常提取)。
// 另外用 bench/BENCH-A.md 验收提取耗时 <= 0.5ms(见任务验收标准)。
#include "mdvn_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/doc/outline.h"
#include "../src/doc/file_map.h"
#include "../src/doc/encoding.h"
#include "../src/doc/front_matter.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

using mdvn::Arena;
using mdvn::Document;
using mdvn::DetectedEncoding;
using mdvn::EncodingDetection;
using mdvn::ExtractOutline;
using mdvn::FileMap;
using mdvn::FileMapError;
using mdvn::OutlineItem;
using mdvn::ParseMarkdown;
using mdvn::SkipFrontMatter;
using mdvn::StrSlice;
using mdvn::Vec;
using mdvn::u32;

#ifndef MDVN_BENCH_DIR
#define MDVN_BENCH_DIR "bench"
#endif

namespace {

StrSlice Src(const char* s) { return StrSlice{s, static_cast<mdvn::u32>(strlen(s))}; }

} // namespace

// 用例:没有任何标题的文档,提取结果应为 0 条。
MDVN_TEST(Outline_NoHeadings_ExtractsNothing) {
    Arena arena;
    MDVN_CHECK(arena.Init(4 * 1024 * 1024));
    Document doc = ParseMarkdown(Src("just a paragraph\n\nanother one\n"), &arena);

    Vec<OutlineItem> items(&arena);
    u32 n = ExtractOutline(doc, &items);
    MDVN_CHECK_EQ(n, 0u);
    MDVN_CHECK_EQ(items.Size(), 0u);
}

// 用例:只有若干个 H1,层级全为 1,块下标升序。
MDVN_TEST(Outline_OnlyH1_ExtractsAllWithLevelOne) {
    Arena arena;
    MDVN_CHECK(arena.Init(4 * 1024 * 1024));
    Document doc = ParseMarkdown(Src("# A\n\n# B\n\n# C\n"), &arena);

    Vec<OutlineItem> items(&arena);
    u32 n = ExtractOutline(doc, &items);
    MDVN_CHECK_EQ(n, 3u);
    MDVN_CHECK_EQ(items.Size(), 3u);
    for (mdvn::u32 i = 0; i < items.Size(); ++i) {
        MDVN_CHECK_EQ(static_cast<int>(items[i].level), 1);
        if (i > 0) MDVN_CHECK(items[i].blockIdx > items[i - 1].blockIdx);
    }
}

// 用例:跳级(H1 直接到 H4,中间没有 H2/H3),大纲应照样按原始层级记录,
// 不做任何"补齐"或纠正——这是数据提取层,不负责展示逻辑。
MDVN_TEST(Outline_SkippedLevels_RecordsRawLevels) {
    Arena arena;
    MDVN_CHECK(arena.Init(4 * 1024 * 1024));
    Document doc = ParseMarkdown(Src("# Top\n\n#### Deep\n\ntext\n"), &arena);

    Vec<OutlineItem> items(&arena);
    u32 n = ExtractOutline(doc, &items);
    MDVN_CHECK_EQ(n, 2u);
    MDVN_CHECK_EQ(static_cast<int>(items[0].level), 1);
    MDVN_CHECK_EQ(static_cast<int>(items[1].level), 4);
}

// 用例:300 条标题(循环 1-6 级),提取条数与层级都要精确匹配。
MDVN_TEST(Outline_ThreeHundredHeadings_ExtractsAllInOrder) {
    Arena arena;
    MDVN_CHECK(arena.Init(16 * 1024 * 1024));

    // 拼一份 300 个标题的源文本,level 按 1..6 循环。
    char buf[300 * 16 + 16];
    size_t pos = 0;
    for (int i = 0; i < 300; ++i) {
        int level = (i % 6) + 1;
        for (int h = 0; h < level; ++h) buf[pos++] = '#';
        buf[pos++] = ' ';
        buf[pos++] = 'H';
        buf[pos++] = '\n';
        buf[pos++] = '\n';
    }
    buf[pos] = 0;

    Document doc = ParseMarkdown(StrSlice{buf, static_cast<mdvn::u32>(pos)}, &arena);

    Vec<OutlineItem> items(&arena);
    u32 n = ExtractOutline(doc, &items);
    MDVN_CHECK_EQ(n, 300u);
    MDVN_CHECK_EQ(items.Size(), 300u);
    for (int i = 0; i < 300; ++i) {
        int expectLevel = (i % 6) + 1;
        MDVN_CHECK_EQ(static_cast<int>(items[static_cast<mdvn::u32>(i)].level), expectLevel);
        if (i > 0) {
            MDVN_CHECK(items[static_cast<mdvn::u32>(i)].blockIdx >
                       items[static_cast<mdvn::u32>(i - 1)].blockIdx);
        }
    }
}

// 用例:标题出现在表格/引用块"内部"(严格来说 CommonMark 的标题不能真的
// 嵌套进表格单元格/引用块里当子块——但引用块内的 "> # Heading" 会被 md4c
// 解析成 BlockQuote 的子块 Heading,这正是任务要求覆盖的场景:大纲提取
// 不关心父块类型,只看 BlockType::Heading,应照常提取。
MDVN_TEST(Outline_HeadingInsideBlockQuote_StillExtracted) {
    Arena arena;
    MDVN_CHECK(arena.Init(4 * 1024 * 1024));
    Document doc = ParseMarkdown(Src("> # Quoted Heading\n> body text\n"), &arena);

    Vec<OutlineItem> items(&arena);
    u32 n = ExtractOutline(doc, &items);
    MDVN_CHECK_EQ(n, 1u);
    MDVN_CHECK_EQ(static_cast<int>(items[0].level), 1);
    MDVN_CHECK_EQ(doc.blocks[items[0].blockIdx].type, mdvn::BlockType::Heading);
}

namespace {

// 复刻 src/app/main.cpp::LoadMarkdownFile 的编码嗅探 + front matter 跳过 +
// 解析逻辑,与 test_corpus_smoke.cpp::LoadCorpusFile 完全一致。
Document LoadBenchFile(StrSlice raw, Arena* arena) {
    EncodingDetection detection = mdvn::DetectEncoding(raw);
    StrSlice utf8;
    if (detection.encoding == DetectedEncoding::Utf16Le ||
        detection.encoding == DetectedEncoding::AnsiFallback) {
        mdvn::Utf16Slice utf16 = mdvn::DecodeToUtf16(raw, detection, arena);
        utf8 = mdvn::Utf16ToUtf8(utf16, arena);
    } else {
        utf8 = StrSlice{raw.data + detection.contentOffset, raw.len - detection.contentOffset};
    }
    StrSlice body = SkipFrontMatter(utf8);
    return ParseMarkdown(body, arena);
}

double ElapsedMillis(LARGE_INTEGER start, LARGE_INTEGER end, LARGE_INTEGER freq) {
    return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
           static_cast<double>(freq.QuadPart);
}

} // namespace

// 用例(性能验收):在 BENCH-A.md 上提取大纲耗时 <= 0.5ms。
MDVN_TEST(Outline_BenchA_ExtractionUnderHalfMillisecond) {
    wchar_t path[600];
    swprintf_s(path, 600, L"%hs\\BENCH-A.md", MDVN_BENCH_DIR);

    FileMap fm;
    FileMapError err = fm.Open(path);
    MDVN_CHECK(err == FileMapError::None);
    if (err != FileMapError::None) return;

    Arena arena;
    MDVN_CHECK(arena.Init(64 * 1024 * 1024));
    Document doc = LoadBenchFile(fm.Data(), &arena);
    fm.Close();

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);

    Vec<OutlineItem> items(&arena);
    QueryPerformanceCounter(&t0);
    u32 n = ExtractOutline(doc, &items);
    QueryPerformanceCounter(&t1);

    double ms = ElapsedMillis(t0, t1, freq);
    fprintf(stderr, "outline: BENCH-A.md headings=%u extract_ms=%.4f\n", n, ms);
    MDVN_CHECK(ms <= 0.5);
}
