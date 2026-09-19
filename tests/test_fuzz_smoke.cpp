// T43 覆盖测试:畸形/恶意文档语料的"打开不崩溃、不卡死、按预期截断"冒烟测试。
//
// 复刻 test_corpus_smoke.cpp(T41)的手法,跑一条与 src/app/main.cpp::LoadMarkdownFile
// 完全一致的链路:FileMap -> 编码嗅探 -> 跳过 front matter -> ParseMarkdown ->
// BlockLayoutEngine::Relayout。区别于 T41 的是:
//   1) 这里断言的不是"不截断",而是每份语料对"是否应触发 truncated"有明确预期
//      (超深嵌套/百万链接两份预期截断,其余不预期截断);
//   2) 额外用 QueryPerformanceCounter 给每份语料计时,断言单文件 <= 3 秒
//      (验收线原文:"单文件处理 ≤3s");
//   3) 不要求"块数 > 0"(未闭合表格/围栏/front matter 等畸形输入允许解析出
//      任意数量的块,只要不崩溃、不越界)。
// 真正的越界/空指针解引用会让测试进程直接崩溃退出,ctest 将其上报为失败,
// 这就是本文件对"不崩溃"的验证手段;"不卡死"由计时断言兜底。
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
#include <cwchar>
#include <windows.h>

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

#ifndef MARKAIR_FUZZ_DIR
#define MARKAIR_FUZZ_DIR "bench/fuzz"
#endif

namespace {

// 单份语料的预期行为登记。expectTruncated 反映验收线里明确要求触发截断的
// 两类场景(超深嵌套 -> kMaxNestingDepth,百万链接 -> kMaxDocumentNodeCount);
// 其余畸形场景(未闭合表格/围栏/front matter、非法 UTF-8、超大 data URI、
// 路径穿越、超长单行)不要求触发截断,只要求不崩溃/不卡死/不越界。
struct FuzzCase {
    const wchar_t* fileName;
    bool expectTruncated;
};

const FuzzCase kFuzzFiles[] = {
    {L"deep-nesting.md", true},          // 200 层引用嵌套 > kMaxNestingDepth(64)
    {L"million-links.md", true},         // 105 万条链接段落 > kMaxDocumentNodeCount(20 万)
    {L"long-line-10mb.md", false},       // 10MB 无换行单行
    {L"unclosed-table.md", false},       // 未闭合/畸形表格
    {L"unclosed-fence.md", false},       // 未闭合围栏代码块
    {L"unclosed-frontmatter.md", false}, // 未闭合 front matter
    {L"invalid-utf8.md", false},         // 非法 UTF-8 字节混入
    {L"huge-data-uri.md", false},        // 伪造的超大 data: URI(约 8MB base64)
    {L"path-traversal-image.md", false}, // 图片路径穿越
};

// 单文件处理耗时上限(验收线原文:"单文件处理 ≤3s")。
constexpr double kMaxSecondsPerFile = 3.0;

// 把 MARKAIR_FUZZ_DIR(UTF-8 narrow 字面量)与语料文件名拼成宽字符完整路径,
// 手法与 test_corpus_smoke.cpp::BuildCorpusPath 一致。
void BuildFuzzPath(const wchar_t* fileName, wchar_t* out, size_t outCap) {
    wchar_t dir[512];
    const char* narrowDir = MARKAIR_FUZZ_DIR;
    size_t i = 0;
    for (; narrowDir[i] != 0 && i + 1 < 512; ++i) dir[i] = static_cast<wchar_t>(narrowDir[i]);
    dir[i] = 0;
    swprintf_s(out, outCap, L"%s\\%s", dir, fileName);
}

// 复刻 src/app/main.cpp::LoadMarkdownFile 的编码嗅探 + front matter 跳过 +
// 解析逻辑,与 test_corpus_smoke.cpp::LoadCorpusFile 完全一致。
Document LoadFuzzFile(StrSlice raw, Arena* arena) {
    EncodingDetection detection = markair::DetectEncoding(raw);
    StrSlice utf8;
    if (detection.encoding == DetectedEncoding::Utf16Le ||
        detection.encoding == DetectedEncoding::AnsiFallback) {
        markair::Utf16Slice utf16 = markair::DecodeToUtf16(raw, detection, arena);
        utf8 = markair::Utf16ToUtf8(utf16, arena);
    } else {
        utf8 = StrSlice{raw.data + detection.contentOffset, raw.len - detection.contentOffset};
    }
    StrSlice body = SkipFrontMatter(utf8);
    return ParseMarkdown(body, arena);
}

// 返回两次 QueryPerformanceCounter 之间的秒数。markair 全程不用 <chrono>,
// 沿用 src/app/bench.cpp 里同款的 QPC 计时手法,保持风格一致。
double ElapsedSeconds(LARGE_INTEGER start, LARGE_INTEGER end, LARGE_INTEGER freq) {
    return static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(freq.QuadPart);
}

}  // namespace

// 用例:对 bench/fuzz/ 下每一份畸形语料跑一次"打开 -> 解析 -> 布局",
// 全部不崩溃、单文件 <=3s,且 kMaxNestingDepth/kMaxDocumentNodeCount 在
// 对应语料上按预期触发(或不触发)。
MARKAIR_TEST(FuzzSmoke_AllMalformedFilesHandledSafely) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    int opened = 0;
    for (const FuzzCase& fc : kFuzzFiles) {
        wchar_t path[600];
        BuildFuzzPath(fc.fileName, path, 600);

        FileMap fm;
        FileMapError err = fm.Open(path);
        if (err != FileMapError::None) {
            fprintf(stderr, "fuzz_smoke: FAILED TO OPEN %ls (err=%d)\n", fc.fileName,
                    static_cast<int>(err));
            MARKAIR_CHECK(err == FileMapError::None);
            continue;
        }
        ++opened;

        // 单个语料(最大约 27MB 的 million-links.md)解析后的模型数组按
        // kMaxDocumentNodeCount(20 万节点、每节点 sizeof(Block)=32 上限)估算,
        // 加上 huge-data-uri.md 的 8MB 字符串展开副本,256MB 留有充分余量。
        Arena arena;
        MARKAIR_CHECK(arena.Init(256 * 1024 * 1024));

        StrSlice raw = fm.Data();

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        Document doc = LoadFuzzFile(raw, &arena);
        QueryPerformanceCounter(&t1);
        double seconds = ElapsedSeconds(t0, t1, freq);

        fprintf(stderr, "fuzz_smoke: %ls parse_seconds=%.3f truncated=%d blocks=%u\n", fc.fileName,
                seconds, doc.truncated ? 1 : 0, static_cast<unsigned>(doc.blocks.Size()));

        MARKAIR_CHECK(seconds <= kMaxSecondsPerFile);
        MARKAIR_CHECK_EQ(doc.truncated ? 1 : 0, fc.expectTruncated ? 1 : 0);

        // 布局阶段同样纳入计时预算与"不崩溃"验证。注意:BlockLayoutEngine
        // 自己的几何数组用固定 32MB Arena(见 layout.cpp kGeometryArenaReserveBytes),
        // 这是独立于 kMaxDocumentNodeCount 的另一层安全网——对 million-links.md
        // 这种截断后仍有 10 万级块的极端文档,32MB 可能不够装下全部块的几何
        // 结果,Relayout 会按其既有设计"安全放弃"并返回 false(layout.cpp 第
        // 166 行注释:"Arena 空间耗尽,安全放弃,不崩溃"),不是崩溃/越界。
        // 所以这里不强求 relayoutOk 恒为 true,只在其为 true 时才要求几何
        // 数组长度与块数一致;无论真假都必须不崩溃、不超时。
        QueryPerformanceCounter(&t0);
        BlockLayoutEngine layout;
        bool relayoutOk = layout.Relayout(doc, 760.0f);
        QueryPerformanceCounter(&t1);
        seconds += ElapsedSeconds(t0, t1, freq);

        if (relayoutOk) {
            MARKAIR_CHECK_EQ(layout.BlockCount(), doc.blocks.Size());
        } else {
            fprintf(stderr, "fuzz_smoke: %ls Relayout safely gave up (geometry arena exhausted)\n",
                    fc.fileName);
        }
        MARKAIR_CHECK(seconds <= kMaxSecondsPerFile);

        fm.Close();
    }

    MARKAIR_CHECK_EQ(opened, static_cast<int>(sizeof(kFuzzFiles) / sizeof(kFuzzFiles[0])));
}
