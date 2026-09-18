// T37 覆盖测试:全文查找算法层(大小写不敏感子串查找 + 文档扫描 + 命中映射)。
// 全部是纯算法,不涉及 DirectWrite/Win32,也不使用 std::regex(硬性约束)。
#include "mdvn_test.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/doc/search.h"
#include "../src/shell/find.h"
#include "../src/util/arena.h"

#include <cstring>
#include <cwchar>

using mdvn::Arena;
using mdvn::Document;
using mdvn::FindSession;
using mdvn::FindSubstringNoCase;
using mdvn::kInvalidIndex;
using mdvn::Match;
using mdvn::MatchToTextRange;
using mdvn::ParseMarkdown;
using mdvn::SearchDocument;
using mdvn::StrSlice;
using mdvn::u32;
using mdvn::Vec;

namespace {

#define MDVN_MAKE_SEARCH_ARENAS()        \
    Arena arena;                          \
    arena.Init(2 * 1024 * 1024);          \
    Arena results;                        \
    results.Init(1 * 1024 * 1024);        \
    Arena scratch;                        \
    scratch.Init(1 * 1024 * 1024)

StrSlice Lit(const char* s) { return StrSlice{s, static_cast<u32>(strlen(s))}; }

}  // namespace

// 用例:大小写不敏感的基本子串查找,含起始偏移与"找不到"。
MDVN_TEST(Search_SubstringCaseInsensitive) {
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit("Hello World"), Lit("world"), 0), 6u);
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit("Hello World"), Lit("HELLO"), 0), 0u);
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit("aXbXc"), Lit("x"), 2), 3u);
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit("Hello"), Lit("z"), 0), kInvalidIndex);
}

// 用例:空串与超长关键词 —— 都必须安全返回"没有命中",不越界不崩溃。
MDVN_TEST(Search_EmptyAndOverlongNeedle) {
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit("abc"), Lit(""), 0), kInvalidIndex);
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit(""), Lit("abc"), 0), kInvalidIndex);

    // 关键词比整段文本还长。
    char longNeedle[600];
    for (u32 i = 0; i < 599; ++i) longNeedle[i] = 'a';
    longNeedle[599] = 0;
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit("aaaa"), Lit(longNeedle), 0), kInvalidIndex);

    // 起始偏移已经越过可能的匹配位置。
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit("abcabc"), Lit("abc"), 4), kInvalidIndex);
}

// 用例:中英混排 —— UTF-8 多字节字符按字节比较,ASCII 折叠不会误伤续字节。
MDVN_TEST(Search_MixedChineseAndEnglish) {
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit("前言 Overview 概述"), Lit("overview"), 0), 7u);
    // 中文关键词命中,偏移是字节偏移。
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit("前言 Overview 概述"), Lit("概述"), 0), 16u);
    MDVN_CHECK_EQ(FindSubstringNoCase(Lit("中文abc中文"), Lit("ABC"), 0), 6u);
}

// 用例:在真实解析出来的文档上扫描,命中落在正确的块上。
MDVN_TEST(Search_DocumentScanFindsBlocks) {
    MDVN_MAKE_SEARCH_ARENAS();
    const char src[] =
        "# Install Guide\n\n"
        "Run the installer first.\n\n"
        "```\nnpm install mdvn\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    Vec<Match> matches(&results);
    u32 n = SearchDocument(doc, Lit("install"), &scratch, &matches);
    // 标题、段落(installer)、代码块各一处 —— 代码块同样参与查找(裁决 #7)。
    MDVN_CHECK_EQ(n, 3u);
    MDVN_CHECK_EQ(matches.Size(), 3u);
    if (matches.Size() == 3) {
        // 命中按块下标升序排列(渲染层依赖这个顺序做提前 break)。
        MDVN_CHECK(matches[0].blockIdx <= matches[1].blockIdx);
        MDVN_CHECK(matches[1].blockIdx <= matches[2].blockIdx);
        MDVN_CHECK_EQ(matches[0].byteLen, 7u);
    }
}

// 用例:跨 inline run 的匹配 —— "**hello** world" 的 "hello" 与 " world" 是两个
// 独立的 inline run,搜索 "hello wor" 必须能跨过 run 边界命中。
MDVN_TEST(Search_MatchAcrossInlineRuns) {
    MDVN_MAKE_SEARCH_ARENAS();
    const char src[] = "**hello** world\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    Vec<Match> matches(&results);
    u32 n = SearchDocument(doc, Lit("hello wor"), &scratch, &matches);
    MDVN_CHECK_EQ(n, 1u);
    if (matches.Size() == 1) {
        MDVN_CHECK_EQ(matches[0].byteOffset, 0u);
        MDVN_CHECK_EQ(matches[0].byteLen, 9u);

        // 跨 run 的命中仍然能映射成一段连续的 UTF-16 区间(T38 用它画高亮)。
        u32 pos = 0, len = 0;
        MDVN_CHECK(MatchToTextRange(doc, matches[0], &pos, &len));
        MDVN_CHECK_EQ(pos, 0u);
        MDVN_CHECK_EQ(len, 9u);
    }
}

// 用例:空查询串不产生任何命中,也不碰 Arena。
MDVN_TEST(Search_EmptyQueryYieldsNothing) {
    MDVN_MAKE_SEARCH_ARENAS();
    const char src[] = "some text\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    Vec<Match> matches(&results);
    MDVN_CHECK_EQ(SearchDocument(doc, Lit(""), &scratch, &matches), 0u);
    MDVN_CHECK_EQ(matches.Size(), 0u);
    MDVN_CHECK_EQ(SearchDocument(doc, StrSlice{nullptr, 0}, &scratch, &matches), 0u);
}

// 用例:链接 URL 与图片 alt 都参与查找(裁决 #7)。
// URL 命中不出现在正文上,所以标成"不可高亮",但仍是一处可跳转的命中。
MDVN_TEST(Search_LinkUrlAndImageAltParticipate) {
    MDVN_MAKE_SEARCH_ARENAS();
    const char src[] = "See [docs](https://example.com/handbook.html) and ![logo alt](a.png)\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    Vec<Match> urlMatches(&results);
    MDVN_CHECK(SearchDocument(doc, Lit("handbook"), &scratch, &urlMatches) >= 1u);
    if (urlMatches.Size() >= 1) {
        MDVN_CHECK_EQ(urlMatches[0].byteOffset, kInvalidIndex);
        MDVN_CHECK_EQ(urlMatches[0].byteLen, 0u);
        // 不可高亮的命中映射不出文本区间。
        u32 pos = 0, len = 0;
        MDVN_CHECK(!MatchToTextRange(doc, urlMatches[0], &pos, &len));
    }

    Arena altResults;
    altResults.Init(1 * 1024 * 1024);
    Vec<Match> altMatches(&altResults);
    MDVN_CHECK(SearchDocument(doc, Lit("logo alt"), &scratch, &altMatches) >= 1u);
}

// 用例:同一块内的多处命中都要找到(含重叠命中的情形)。
MDVN_TEST(Search_MultipleMatchesInOneBlock) {
    MDVN_MAKE_SEARCH_ARENAS();
    const char src[] = "aaaa\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    Vec<Match> matches(&results);
    // "aa" 在 "aaaa" 里有 3 处(允许重叠)。
    MDVN_CHECK_EQ(SearchDocument(doc, Lit("aa"), &scratch, &matches), 3u);
}

// 用例:命中映射到 UTF-16 区间时,中文按 code unit 计数(1 个汉字 = 1 个 unit)。
MDVN_TEST(Search_MatchToTextRangeCountsUtf16Units) {
    MDVN_MAKE_SEARCH_ARENAS();
    const char src[] = "中文abc\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    Vec<Match> matches(&results);
    MDVN_CHECK_EQ(SearchDocument(doc, Lit("abc"), &scratch, &matches), 1u);
    if (matches.Size() == 1) {
        u32 pos = 0, len = 0;
        MDVN_CHECK(MatchToTextRange(doc, matches[0], &pos, &len));
        MDVN_CHECK_EQ(pos, 2u);  // "中文" 占 6 字节但只有 2 个 UTF-16 code unit
        MDVN_CHECK_EQ(len, 3u);
    }
}

// ---- 以下为 T37 的查找条状态机(UI 层,不依赖 windows.h)----

// 用例:开关状态、增量输入与退格。
MDVN_TEST(Find_QueryEditing) {
    Arena results;
    results.Init(1 * 1024 * 1024);
    Arena scratch;
    scratch.Init(1 * 1024 * 1024);
    FindSession find(&results, &scratch);

    MDVN_CHECK(!find.Visible());
    find.Open();
    MDVN_CHECK(find.Visible());

    MDVN_CHECK(find.AppendChar(L'a'));
    MDVN_CHECK(find.AppendChar(L'b'));
    MDVN_CHECK_EQ(find.QueryLength(), 2u);
    MDVN_CHECK(wcscmp(find.Query(), L"ab") == 0);

    // 控制字符不进查询串。
    MDVN_CHECK(!find.AppendChar(L'\r'));
    MDVN_CHECK(!find.AppendChar(L'\t'));
    MDVN_CHECK_EQ(find.QueryLength(), 2u);

    MDVN_CHECK(find.Backspace());
    MDVN_CHECK(wcscmp(find.Query(), L"a") == 0);
    MDVN_CHECK(find.Backspace());
    MDVN_CHECK(!find.Backspace());  // 已经空了

    find.Close();
    MDVN_CHECK(!find.Visible());
    MDVN_CHECK_EQ(find.QueryLength(), 0u);
}

// 用例:超长关键词被缓冲长度挡住,不越界。
MDVN_TEST(Find_QueryLengthIsBounded) {
    Arena results;
    results.Init(1 * 1024 * 1024);
    Arena scratch;
    scratch.Init(1 * 1024 * 1024);
    FindSession find(&results, &scratch);
    find.Open();

    for (u32 i = 0; i < mdvn::kMaxFindQueryChars + 50; ++i) find.AppendChar(L'x');
    MDVN_CHECK_EQ(find.QueryLength(), mdvn::kMaxFindQueryChars);
    MDVN_CHECK_EQ(find.Query()[mdvn::kMaxFindQueryChars], L'\0');
}

// 用例:重搜 + 上一处/下一处的环绕跳转。
MDVN_TEST(Find_RerunAndStepWrapsAround) {
    MDVN_MAKE_SEARCH_ARENAS();
    const char src[] = "todo one\n\ntodo two\n\ntodo three\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    FindSession find(&results, &scratch);
    find.Open();
    find.AppendChar(L't');
    find.AppendChar(L'o');
    find.AppendChar(L'd');
    find.AppendChar(L'o');

    MDVN_CHECK_EQ(find.Rerun(doc), 3u);
    MDVN_CHECK_EQ(find.MatchCount(), 3u);
    MDVN_CHECK_EQ(find.CurrentIndex(), 0u);
    MDVN_CHECK(find.CurrentMatch() != nullptr);

    MDVN_CHECK(find.GoNext());
    MDVN_CHECK_EQ(find.CurrentIndex(), 1u);
    MDVN_CHECK(find.GoNext());
    MDVN_CHECK_EQ(find.CurrentIndex(), 2u);
    MDVN_CHECK(find.GoNext());
    MDVN_CHECK_EQ(find.CurrentIndex(), 0u);  // 环绕回第一处

    MDVN_CHECK(find.GoPrev());
    MDVN_CHECK_EQ(find.CurrentIndex(), 2u);  // 反向环绕到最后一处

    // 改查询串后重搜,命中集合整体重建。
    find.Backspace();
    find.AppendChar(L'x');
    MDVN_CHECK_EQ(find.Rerun(doc), 0u);
    MDVN_CHECK_EQ(find.CurrentIndex(), mdvn::kInvalidIndex);
    MDVN_CHECK(find.CurrentMatch() == nullptr);
    MDVN_CHECK(!find.GoNext());
    MDVN_CHECK(!find.GoPrev());
}

// 用例(2026-09-19 "回车才搜"):Dirty() 只在查询串改动后为 true,Rerun 后
// 清零;ClearMatches() 只清命中集合,不做扫描、不碰查询串/Dirty 状态。
MDVN_TEST(Find_DirtyFlagTracksQueryEdits) {
    MDVN_MAKE_SEARCH_ARENAS();
    const char src[] = "todo one\n\ntodo two\n\ntodo three\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);

    FindSession find(&results, &scratch);
    find.Open();
    MDVN_CHECK(!find.Dirty());  // 刚打开,空查询串,没改动过

    find.AppendChar(L't');
    MDVN_CHECK(find.Dirty());
    find.AppendChar(L'o');
    find.AppendChar(L'd');
    find.AppendChar(L'o');

    // 输入过程中只清高亮,不扫描——命中集合仍是空的,Dirty 保持 true。
    find.ClearMatches();
    MDVN_CHECK_EQ(find.MatchCount(), 0u);
    MDVN_CHECK(find.Dirty());
    MDVN_CHECK(wcscmp(find.Query(), L"todo") == 0);  // 查询串不受影响

    // 回车触发的 Rerun:真正扫描一次,Dirty 清零。
    MDVN_CHECK_EQ(find.Rerun(doc), 3u);
    MDVN_CHECK(!find.Dirty());

    // 查询串没再变,后续 Enter/F3 应直接跳转(Dirty 仍是 false)。
    MDVN_CHECK(find.GoNext());
    MDVN_CHECK(!find.Dirty());

    // 再次编辑查询串,Dirty 重新变 true。
    find.Backspace();
    MDVN_CHECK(find.Dirty());
}
