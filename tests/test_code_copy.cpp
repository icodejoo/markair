// T45 代码块复制按钮:纯逻辑层测试。
//
// 只覆盖两部分不依赖真实 Win32 剪贴板/DirectWrite 的纯函数:
// 1) CodeBlockPlainTextUtf8 —— 代码块纯文本拼接是否与源码逐字节一致
//    (含缩进/换行,复用 tests/test_synthetic_newline.cpp 已验证过的合成
//    换行/合成缩进机制,这里只再验证一层"拼给剪贴板用"的组装逻辑本身)。
// 2) BlockGeometry::codeCopyButton 的位置计算是否落在代码块背景右上角。
// 真正的 OpenClipboard/SetClipboardData 调用(SetClipboardUnicodeText)
// 依赖系统剪贴板,不在单元测试里覆盖(见 clipboard.h 顶部注释)。
#include "mdvn_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/layout/layout.h"
#include "../src/shell/clipboard.h"

using mdvn::Arena;
using mdvn::Block;
using mdvn::BlockLayoutEngine;
using mdvn::BlockType;
using mdvn::CodeBlockPlainTextUtf8;
using mdvn::Document;
using mdvn::ParseMarkdown;
using mdvn::StrSlice;
using mdvn::u32;

#define MDVN_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

namespace {

u32 FindBlockOfType(const Document& doc, BlockType type, u32 occurrence) {
    u32 seen = 0;
    for (u32 i = 0; i < doc.blocks.Size(); ++i) {
        if (doc.blocks[i].type == type) {
            if (seen == occurrence) return i;
            ++seen;
        }
    }
    return mdvn::kInvalidIndex;
}

bool TextEquals(const char* buf, u32 len, const char* expected) {
    u32 expectedLen = 0;
    while (expected[expectedLen] != 0) ++expectedLen;
    if (len != expectedLen) return false;
    for (u32 i = 0; i < len; ++i) {
        if (buf[i] != expected[i]) return false;
    }
    return true;
}

}  // namespace

// 用例 1:多行缩进代码块——先问长度(out=nullptr),再按需拼接,结果应与
// 源码(不含围栏/语言标注)逐字节一致,缩进/换行都要保留。
MDVN_TEST(CodeCopy_PlainTextPreservesIndentAndNewlines) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] =
        "```python\n"
        "def f():\n"
        "    return 1\n"
        "```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MDVN_CHECK(codeIdx != mdvn::kInvalidIndex);

    u32 need = CodeBlockPlainTextUtf8(doc, codeIdx, nullptr, 0);
    MDVN_CHECK(need > 0);

    char* buf = static_cast<char*>(arena.Alloc(need, 1));
    MDVN_CHECK(buf != nullptr);
    u32 written = CodeBlockPlainTextUtf8(doc, codeIdx, buf, need);
    MDVN_CHECK_EQ(written, need);
    MDVN_CHECK(TextEquals(buf, written, "def f():\n    return 1\n"));
}

// 用例 2:缓冲区比需要的短时,只写前 cap 个字节,但返回值仍是完整长度
// (调用方按此判断"要不要重新分配更大的缓冲区再拼一次")。
MDVN_TEST(CodeCopy_TruncatesToCapButReturnsFullLength) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "```\nhello world\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MDVN_CHECK(codeIdx != mdvn::kInvalidIndex);

    u32 fullLen = CodeBlockPlainTextUtf8(doc, codeIdx, nullptr, 0);
    MDVN_CHECK(fullLen > 4u);

    // 注意:不要用 "small" 做变量名——Windows SDK 的 rpcndr.h 把它宏定义成
    // char,`char small[4]` 会被预处理器展开成非法的 `char char[4]`。
    char shortBuf[4] = {};
    u32 reportedLen = CodeBlockPlainTextUtf8(doc, codeIdx, shortBuf, 4);
    MDVN_CHECK_EQ(reportedLen, fullLen);  // 返回值是完整长度,不是 4
    MDVN_CHECK(TextEquals(shortBuf, 4, "hell"));  // 只写了前 4 字节
}

// 用例 3:非代码块/越界下标应返回 0,不崩溃、不读到别的块的内容。
MDVN_TEST(CodeCopy_NonCodeBlockOrOutOfRangeReturnsZero) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "just a paragraph, no code block here\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    u32 paraIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    MDVN_CHECK(paraIdx != mdvn::kInvalidIndex);
    MDVN_CHECK_EQ(CodeBlockPlainTextUtf8(doc, paraIdx, nullptr, 0), 0u);

    MDVN_CHECK_EQ(CodeBlockPlainTextUtf8(doc, doc.blocks.Size() + 100, nullptr, 0), 0u);
}

// 用例 4:复制按钮的几何应该落在代码块背景矩形的右上角内部(不超出背景右/
// 上边界),且是正方形(width == height)。
MDVN_TEST(CodeCopy_ButtonGeometrySitsInsideBackgroundTopRight) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "```\nsome code here\n```\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 400.0f));

    u32 codeIdx = FindBlockOfType(doc, BlockType::CodeBlock, 0);
    MDVN_CHECK(codeIdx != mdvn::kInvalidIndex);
    const auto& g = layout.Geometry(codeIdx);

    MDVN_CHECK(g.codeCopyButton.width > 0.0f);
    MDVN_CHECK_EQ(g.codeCopyButton.width, g.codeCopyButton.height);  // 正方形按钮
    // 右边界不超过代码块背景的右边界。
    MDVN_CHECK(g.codeCopyButton.x + g.codeCopyButton.width <=
               g.codeBackground.x + g.codeBackground.width + 0.01f);
    // 顶部落在背景矩形内(不早于背景顶边)。
    MDVN_CHECK(g.codeCopyButton.y >= g.codeBackground.y);
}

// 用例 5:非代码块(如普通段落)不应该有复制按钮几何(width 恒为 0)。
MDVN_TEST(CodeCopy_NonCodeBlockHasNoButtonGeometry) {
    MDVN_MAKE_TEST_ARENA();
    const char src[] = "just a normal paragraph\n";
    Document doc = ParseMarkdown(StrSlice{src, sizeof(src) - 1}, &arena);
    MDVN_CHECK(!doc.truncated);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 400.0f));

    u32 paraIdx = FindBlockOfType(doc, BlockType::Paragraph, 0);
    MDVN_CHECK(paraIdx != mdvn::kInvalidIndex);
    MDVN_CHECK_EQ(layout.Geometry(paraIdx).codeCopyButton.width, 0.0f);
}
