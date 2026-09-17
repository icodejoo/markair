// T41 覆盖测试:真实文档回归语料的"打开不崩溃"冒烟测试。
//
// 目的:对 bench/corpus/ 下每一份从真实开源项目抓取的 README/CHANGELOG 跑一次
// "读取文件 -> 编码嗅探 -> 跳过 front matter -> 解析为文档模型 -> 块级布局"
// 这条与 src/app/main.cpp::LoadMarkdownFile 完全一致的链路,断言:
//   1) 文件能被 FileMap 成功映射(语料本身应当都能打开);
//   2) ParseMarkdown 不崩溃、不断言失败(禁异常,失败只会体现为 MDVN_CHECK 计数);
//   3) 非空输入解析出的块数 > 0(排除"整份文档被吞成空文档"这种明显异常);
//   4) BlockLayoutEngine::Relayout 成功且几何数组长度与块数一致。
// mdvn 本来就不使用异常,真正的越界/空指针解引用会让测试进程直接崩溃退出,
// ctest 会将其上报为失败,这就是本文件对"不崩溃"的验证手段。
//
// 语料清单硬编码在这里(而不是运行时遍历目录):CMake 测试目标不方便做目录
// 遍历,硬编码列表也顺带起到"新增语料必须显式登记"的效果,防止遗漏。
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
#include <cwchar>

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

#ifndef MDVN_CORPUS_DIR
#define MDVN_CORPUS_DIR "bench/corpus"
#endif

namespace {

// 语料文件名清单,与 bench/corpus/SOURCES.md 登记的 39 份一一对应。
const wchar_t* kCorpusFiles[] = {
    L"facebook-react-readme.md",
    L"facebook-react-changelog.md",
    L"microsoft-vscode-readme.md",
    L"nodejs-node-readme.md",
    L"nodejs-release-readme.md",
    L"rust-lang-rust-readme.md",
    L"golang-go-readme.md",
    L"vuejs-core-readme.md",
    L"angular-angular-readme.md",
    L"sveltejs-svelte-readme.md",
    L"tensorflow-tensorflow-readme.md",
    L"pytorch-pytorch-readme.md",
    L"kubernetes-kubernetes-readme.md",
    L"docker-compose-readme.md",
    L"expressjs-express-readme.md",
    L"axios-axios-readme.md",
    L"pages-themes-minimal-readme.md",
    L"actions-checkout-readme.md",
    L"sindresorhus-awesome-readme.md",
    L"vitejs-vite-readme.md",
    L"webpack-webpack-readme.md",
    L"electron-electron-readme.md",
    L"flutter-flutter-readme.md",
    L"ohmyzsh-ohmyzsh-readme.md",
    L"redis-redis-readme.md",
    L"pandas-dev-pandas-readme.md",
    L"github-docs-get-started-index.md",
    L"github-docs-onboarding-getting-started-account.md",
    L"reactjs-react.dev-blog-react19.md",
    L"remarkjs-remark-gfm-readme.md",
    L"micromark-gfm-footnote-readme.md",
    L"microsoft-playwright-readme.md",
    L"n8n-io-n8n-readme.md",
    L"mermaid-js-mermaid-readme.md",
    L"mermaid-js-mermaid-cli-readme.md",
    L"prettier-prettier-changelog.md",
    L"pandoc-manual.md",
    L"electron-electron-pull-request-template.md",
    L"angular-angular-pull-request-template.md",
};

// 把 MDVN_CORPUS_DIR(UTF-8 narrow 字面量)与语料文件名拼成宽字符完整路径。
// 固定缓冲区足够容纳仓库内路径深度,不做动态分配。
void BuildCorpusPath(const wchar_t* fileName, wchar_t* out, size_t outCap) {
    wchar_t dir[512];
    const char* narrowDir = MDVN_CORPUS_DIR;
    size_t i = 0;
    for (; narrowDir[i] != 0 && i + 1 < 512; ++i) dir[i] = static_cast<wchar_t>(narrowDir[i]);
    dir[i] = 0;
    swprintf_s(out, outCap, L"%s\\%s", dir, fileName);
}

// 复刻 src/app/main.cpp::LoadMarkdownFile 的编码嗅探 + front matter 跳过 +
// 解析逻辑,跑完整条"打开文档"链路的前半段(不含渲染/窗口)。
// 顺带把跳过 front matter 之后的正文切片通过 outBody 带出来,供调用方判断
// "正文是否非空"(而不是拿跳过前的原始字节长度去判断——纯 front matter、
// 正文全部由外部模板渲染的页面是真实存在的合法文档,不应被当成解析异常)。
Document LoadCorpusFile(StrSlice raw, Arena* arena, StrSlice* outBody) {
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
    *outBody = body;
    return ParseMarkdown(body, arena);
}

// 判断一段文本是否"全是空白字符"(空格/制表/换行/回车)。用于区分"front
// matter 之后确实没有正文"(合法空文档)与"正文非空但解析异常吞掉了"。
bool IsAllWhitespace(StrSlice s) {
    for (mdvn::u32 i = 0; i < s.len; ++i) {
        char c = s.data[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
    }
    return true;
}

}  // namespace

// 用例:对语料清单里的每一份文件跑一次"打开 -> 解析 -> 布局",全部不崩溃、
// 不产生空文档、几何数组与块数一致。任何一步异常都会体现为 MDVN_CHECK 失败
// 或进程崩溃(ctest 判定为失败退出码),不需要额外的异常捕获。
MDVN_TEST(CorpusSmoke_AllRealWorldFilesOpenParseLayoutWithoutCrash) {
    int opened = 0;
    int totalBlocks = 0;
    for (const wchar_t* fileName : kCorpusFiles) {
        wchar_t path[600];
        BuildCorpusPath(fileName, path, 600);

        FileMap fm;
        FileMapError err = fm.Open(path);
        if (err != FileMapError::None) {
            fprintf(stderr, "corpus_smoke: FAILED TO OPEN %ls (err=%d)\n", fileName,
                    static_cast<int>(err));
            MDVN_CHECK(err == FileMapError::None);
            continue;
        }
        ++opened;

        Arena arena;
        // 单份语料最大约 300KB 源文本,解析后的模型数组远小于该量级;
        // 128MB 预留给这条链路里最大的那份(facebook-react-changelog.md)绰绰有余。
        arena.Init(128 * 1024 * 1024);

        StrSlice raw = fm.Data();
        StrSlice body{nullptr, 0};
        Document doc = LoadCorpusFile(raw, &arena, &body);

        // 正文(跳过 front matter 之后)非空白时才要求解析出至少一个块——
        // 纯 front matter、正文完全由外部模板渲染的页面(如
        // github-docs-get-started-index.md)是合法的空文档,不是解析异常。
        if (!IsAllWhitespace(body) && doc.blocks.Size() == 0) {
            fprintf(stderr, "corpus_smoke: EMPTY DOC for %ls (body.len=%u)\n", fileName,
                    static_cast<unsigned>(body.len));
        }
        if (!IsAllWhitespace(body)) {
            MDVN_CHECK(doc.blocks.Size() > 0);
        }
        totalBlocks += static_cast<int>(doc.blocks.Size());

        // kMaxNestingDepth/kMaxDocumentNodeCount 截断机制:真实 README 不应触发。
        MDVN_CHECK(!doc.truncated);

        BlockLayoutEngine layout;
        bool relayoutOk = layout.Relayout(doc, 760.0f);
        MDVN_CHECK(relayoutOk);
        MDVN_CHECK_EQ(layout.BlockCount(), doc.blocks.Size());

        fm.Close();
    }

    fprintf(stderr, "corpus_smoke: opened=%d/%d total_blocks=%d\n", opened,
            static_cast<int>(sizeof(kCorpusFiles) / sizeof(kCorpusFiles[0])), totalBlocks);
    MDVN_CHECK_EQ(opened, static_cast<int>(sizeof(kCorpusFiles) / sizeof(kCorpusFiles[0])));
}
