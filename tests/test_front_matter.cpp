// T22 覆盖测试:SkipFrontMatter 的 6 种场景——无 front matter、正常、未闭合、
// 貌似 front matter 实为分割线(无第二个分隔符)、CRLF、带 BOM。
#include "markair_test.h"
#include "../src/util/str.h"
#include "../src/doc/front_matter.h"

#include <cstring>

using markair::SkipFrontMatter;
using markair::StrSlice;
using markair::u32;

namespace {
// 用 C 字符串构造 StrSlice(不含末尾 '\0')。
StrSlice Slice(const char* s) { return StrSlice{s, static_cast<u32>(strlen(s))}; }
}

// 样本 1:无 front matter,原样返回整个输入。
MARKAIR_TEST(FrontMatter_None) {
    const char src[] = "# Title\n\nBody text.\n";
    StrSlice out = SkipFrontMatter(Slice(src));
    MARKAIR_CHECK_EQ(out.len, static_cast<u32>(sizeof(src) - 1));
    MARKAIR_CHECK(out.data == src); // 未匹配到 front matter,指针应就是原始起点
}

// 样本 2:正常的 front matter,用 "---" 闭合。
MARKAIR_TEST(FrontMatter_Normal) {
    const char src[] = "---\ntitle: Hello\n---\n# Body\n";
    StrSlice out = SkipFrontMatter(Slice(src));
    const char expected[] = "# Body\n";
    MARKAIR_CHECK_EQ(out.len, static_cast<u32>(sizeof(expected) - 1));
    if (out.len == sizeof(expected) - 1) MARKAIR_CHECK(memcmp(out.data, expected, out.len) == 0);
}

// 样本 2b:用 "..." 闭合同样有效。
MARKAIR_TEST(FrontMatter_DotsCloser) {
    const char src[] = "---\ntitle: Hello\n...\nBody\n";
    StrSlice out = SkipFrontMatter(Slice(src));
    const char expected[] = "Body\n";
    MARKAIR_CHECK_EQ(out.len, static_cast<u32>(sizeof(expected) - 1));
    if (out.len == sizeof(expected) - 1) MARKAIR_CHECK(memcmp(out.data, expected, out.len) == 0);
}

// 样本 3:未闭合(只有开头 "---",没有第二个分隔符,直到 EOF),原样返回。
MARKAIR_TEST(FrontMatter_Unclosed) {
    const char src[] = "---\ntitle: Hello\nmore: 1\n";
    StrSlice out = SkipFrontMatter(Slice(src));
    MARKAIR_CHECK_EQ(out.len, static_cast<u32>(sizeof(src) - 1));
    MARKAIR_CHECK(out.data == src);
}

// 样本 4:首行是 "---" 但其实是分割线——只有一行内容然后 EOF,没有第二个
// "---"/"..." 出现,应原样返回(不误判为 front matter)。
MARKAIR_TEST(FrontMatter_JustAThematicBreak) {
    const char src[] = "---\nJust one paragraph, then EOF.";
    StrSlice out = SkipFrontMatter(Slice(src));
    MARKAIR_CHECK_EQ(out.len, static_cast<u32>(sizeof(src) - 1));
    MARKAIR_CHECK(out.data == src);
}

// 样本 5:CRLF 行尾。
MARKAIR_TEST(FrontMatter_Crlf) {
    const char src[] = "---\r\ntitle: Hello\r\n---\r\n# Body\r\n";
    StrSlice out = SkipFrontMatter(Slice(src));
    const char expected[] = "# Body\r\n";
    MARKAIR_CHECK_EQ(out.len, static_cast<u32>(sizeof(expected) - 1));
    if (out.len == sizeof(expected) - 1) MARKAIR_CHECK(memcmp(out.data, expected, out.len) == 0);
}

// 样本 6:带 BOM 的输入——SkipFrontMatter 约定调用方已跳过 BOM
// (main.cpp 里 encoding.cpp 的 contentOffset 已处理),这里验证跳过 BOM
// 之后传入依然按预期工作,BOM 本身不在 SkipFrontMatter 的职责范围内。
MARKAIR_TEST(FrontMatter_AfterBomAlreadyStripped) {
    const char withBom[] = "\xEF\xBB\xBF---\ntitle: Hello\n---\nBody\n";
    // 模拟调用方(main.cpp)已经跳过 3 字节 BOM 后再传入。
    StrSlice afterBom{withBom + 3, static_cast<u32>(sizeof(withBom) - 1 - 3)};
    StrSlice out = SkipFrontMatter(afterBom);
    const char expected[] = "Body\n";
    MARKAIR_CHECK_EQ(out.len, static_cast<u32>(sizeof(expected) - 1));
    if (out.len == sizeof(expected) - 1) MARKAIR_CHECK(memcmp(out.data, expected, out.len) == 0);
}
