// T51 覆盖测试:代码高亮词法器。纯函数断言,不依赖 Win32/D2D/DWrite,
// 可脱离图形环境单测。
#include <chrono>
#include <cstring>

#include "mdvn_test.h"
#include "../src/hl/lexer.h"

using mdvn::Arena;
using mdvn::GetLanguageRule;
using mdvn::kLanguageC;
using mdvn::kLanguageCpp;
using mdvn::kLanguageGo;
using mdvn::kLanguageJava;
using mdvn::kLanguageJson;
using mdvn::kLanguagePython;
using mdvn::kLanguageRust;
using mdvn::kLanguageShell;
using mdvn::kMaxTokensPerBlock;
using mdvn::kTokenBuiltin;
using mdvn::kTokenComment;
using mdvn::kTokenKeyword;
using mdvn::kTokenNumber;
using mdvn::kTokenOther;
using mdvn::kTokenPunct;
using mdvn::kTokenString;
using mdvn::LanguageRule;
using mdvn::LexCodeBlock;
using mdvn::LexResult;
using mdvn::StrSlice;
using mdvn::Token;
using mdvn::u32;

namespace {

// 把 C 字符串包成 StrSlice,方便测试直接传字面量。
StrSlice Slice(const char* s) { return StrSlice{s, static_cast<u32>(strlen(s))}; }

// 取某个 token 在原文本中的切片内容,方便断言具体文本。
bool TokenTextEquals(const char* code, const Token& tok, const char* expected) {
    u32 expectedLen = static_cast<u32>(strlen(expected));
    if (tok.len != expectedLen) return false;
    return memcmp(code + tok.offset, expected, expectedLen) == 0;
}

} // namespace

// 宏:声明并初始化一个测试用 arena(Arena 不可拷贝,不能用函数返回值封装)。
#define MDVN_TEST_ARENA(name, size) \
    Arena name; \
    MDVN_CHECK((name).Init(size))

// ---- 基础边界:空输入 / 纯空白 ----

MDVN_TEST(Lexer_EmptyCodeProducesNoTokens) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    LexResult r = LexCodeBlock(Slice(""), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 0u);
    MDVN_CHECK(!r.truncated);
}

MDVN_TEST(Lexer_WhitespaceOnlyProducesNoTokens) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    LexResult r = LexCodeBlock(Slice("   \n\t\n  "), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 0u);
}

// ---- 未闭合字符串:扫到文本末尾,不死循环不越界 ----

MDVN_TEST(Lexer_UnclosedDoubleQuoteStringScansToEnd) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    const char* code = "\"hello world";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

MDVN_TEST(Lexer_UnclosedSingleQuoteStringScansToEnd) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageShell);
    const char* code = "'unterminated raw";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

// ---- 未闭合块注释:扫到文本末尾 ----

MDVN_TEST(Lexer_UnclosedBlockCommentScansToEnd) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    const char* code = "/* never closes";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenComment));
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

// ---- 转义字符:\" 与 \\ 不应提前结束字符串 ----

MDVN_TEST(Lexer_EscapedQuoteDoesNotEndString) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    const char* code = "\"a\\\"b\" x";  // "a\"b" x
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK(r.tokens.len >= 1);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK(TokenTextEquals(code, r.tokens[0], "\"a\\\"b\""));
}

MDVN_TEST(Lexer_EscapedBackslashDoesNotEndString) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    const char* code = "\"a\\\\\" x"; // "a\\" x
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK(r.tokens.len >= 1);
    MDVN_CHECK(TokenTextEquals(code, r.tokens[0], "\"a\\\\\""));
}

// ---- C++ 原始字符串 R"(...)" ----

MDVN_TEST(Lexer_CppRawStringLiteral) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageCpp);
    const char* code = "R\"(raw \"content\")\" ;";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK(r.tokens.len >= 1);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK(TokenTextEquals(code, r.tokens[0], "R\"(raw \"content\")\""));
}

MDVN_TEST(Lexer_CppRawStringUnclosedScansToEnd) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageCpp);
    const char* code = "R\"(never closes";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

// ---- Python 三引号字符串 ----

MDVN_TEST(Lexer_PythonTripleDoubleQuoteString) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguagePython);
    const char* code = "\"\"\"doc string\"\"\"";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

MDVN_TEST(Lexer_PythonTripleSingleQuoteString) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguagePython);
    const char* code = "'''doc string'''";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
}

MDVN_TEST(Lexer_PythonSingleQuoteStringStillWorks) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguagePython);
    const char* code = "'normal'";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

// ---- Go 反引号原始字符串 ----

MDVN_TEST(Lexer_GoBacktickRawString) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageGo);
    const char* code = "`raw \\n not escaped`";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

// ---- Rust 生命周期标注 vs 字符字面量(T67 语料发现的必修缺陷,修复回归) ----

MDVN_TEST(Lexer_RustLifetimeNotTreatedAsCharLiteral) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageRust);
    // sample.rs 里的真实陷阱写法:泛型生命周期标注紧跟真实字符字面量。
    const char* code = "struct Greeter<'a> { name: &'a str }";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    // 'a 不应被当成未闭合字符字面量吞掉整段代码;后面的标识符/标点仍能
    // 正常各自成 token。
    for (u32 i = 0; i < r.tokens.len; ++i) {
        MDVN_CHECK(r.tokens[i].type != static_cast<mdvn::u8>(kTokenString));
    }
    MDVN_CHECK(r.tokens.len > 5u);
}

MDVN_TEST(Lexer_RustLifetimeTokenCoversQuoteAndIdent) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageRust);
    const char* code = "'a>";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK(r.tokens.len >= 1);
    MDVN_CHECK(TokenTextEquals(code, r.tokens[0], "'a"));
    MDVN_CHECK(r.tokens[0].type != static_cast<mdvn::u8>(kTokenString));
}

MDVN_TEST(Lexer_RustCharLiteralStillRecognized) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageRust);
    const char* code = "let ch: char = 'x';";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    bool foundCharLiteral = false;
    for (u32 i = 0; i < r.tokens.len; ++i) {
        if (TokenTextEquals(code, r.tokens[i], "'x")) {
            // 不应该走生命周期分支
            MDVN_CHECK(false);
        }
        if (r.tokens[i].type == static_cast<mdvn::u8>(kTokenString) &&
            TokenTextEquals(code, r.tokens[i], "'x'")) {
            foundCharLiteral = true;
        }
    }
    MDVN_CHECK(foundCharLiteral);
}

MDVN_TEST(Lexer_RustEscapedNewlineCharLiteralRecognized) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageRust);
    const char* code = "'\\n'";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

MDVN_TEST(Lexer_RustEscapedQuoteCharLiteralRecognized) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageRust);
    const char* code = "'\\''"; // '\'' 转义单引号字符字面量
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

// ---- Rust r#"..."# 原始字符串 ----

MDVN_TEST(Lexer_RustRawStringWithHashCloses) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageRust);
    // sample.rs 里的真实陷阱:内部含裸双引号,不应提前闭合。
    const char* code = "r#\"path: \"C:\\Users\\test\"\"#";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

MDVN_TEST(Lexer_RustRawStringUnclosedScansToEnd) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageRust);
    const char* code = "r#\"never closes";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

// ---- Java """文本块""" ----

MDVN_TEST(Lexer_JavaTextBlockWithBareQuotesInsideDoesNotSplit) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageJava);
    // sample.java 里的真实陷阱:文本块内部含裸双引号,不应提前闭合。
    const char* code = "\"\"\"\n内部可以直接写引号 \"不需要转义\",\n\"\"\"";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

MDVN_TEST(Lexer_JavaTextBlockUnclosedScansToEnd) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageJava);
    const char* code = "\"\"\"never closes";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

// ---- Shell 单引号:内容原样,不处理转义 ----

MDVN_TEST(Lexer_ShellSingleQuoteNoEscapeProcessing) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageShell);
    const char* code = "'a\\'"; // 单引号内 \ 不是转义,遇到下一个 ' 就闭合
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].len, 4u); // 'a\' 整段 4 字节
}

// ---- 行注释扫到行尾 ----

MDVN_TEST(Lexer_LineCommentStopsAtNewline) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    const char* code = "// comment here\nint x;";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK(r.tokens.len >= 1);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenComment));
    MDVN_CHECK(TokenTextEquals(code, r.tokens[0], "// comment here"));
}

MDVN_TEST(Lexer_LineCommentAtEndOfTextNoNewline) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    const char* code = "// to the end";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].len, static_cast<mdvn::u32>(strlen(code)));
}

// ---- 中文注释/字符串内容不打乱扫描(非 ASCII 字节当标识符/普通字符处理) ----

MDVN_TEST(Lexer_ChineseLineCommentDoesNotBreakScan) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageCpp);
    const char* code = "// 这是中文注释,包含标点。\nint x = 1;";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK(r.tokens.len >= 4);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenComment));
    // 注释后仍能正确识别 int 关键字。
    bool foundKeyword = false;
    for (mdvn::u32 i = 0; i < r.tokens.len; ++i) {
        if (r.tokens[i].type == static_cast<mdvn::u8>(kTokenKeyword)) foundKeyword = true;
    }
    MDVN_CHECK(foundKeyword);
}

MDVN_TEST(Lexer_ChineseStringContentDoesNotBreakScan) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageCpp);
    const char* code = "\"你好,世界\" + 1";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK(r.tokens.len >= 2);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
    MDVN_CHECK(TokenTextEquals(code, r.tokens[0], "\"你好,世界\""));
}

MDVN_TEST(Lexer_ChineseIdentifierTreatedAsOtherToken) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageCpp);
    const char* code = "变量 = 1;";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK(r.tokens.len >= 1);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenOther));
    MDVN_CHECK(TokenTextEquals(code, r.tokens[0], "变量"));
}

// ---- 7 种 token 类型各至少一个正例 ----

MDVN_TEST(Lexer_KeywordTokenExample) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    LexResult r = LexCodeBlock(Slice("return"), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenKeyword));
}

MDVN_TEST(Lexer_StringTokenExample) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    LexResult r = LexCodeBlock(Slice("\"hi\""), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenString));
}

MDVN_TEST(Lexer_NumberTokenExample) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    LexResult r = LexCodeBlock(Slice("12345"), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenNumber));
}

MDVN_TEST(Lexer_CommentTokenExample) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    LexResult r = LexCodeBlock(Slice("/* c */"), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenComment));
}

MDVN_TEST(Lexer_PunctTokenExample) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    LexResult r = LexCodeBlock(Slice("+"), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenPunct));
}

MDVN_TEST(Lexer_BuiltinTokenExample) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    LexResult r = LexCodeBlock(Slice("size_t"), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenBuiltin));
}

MDVN_TEST(Lexer_StructuredLiteralIsBuiltin) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageJson);
    LexResult r = LexCodeBlock(Slice("true"), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenBuiltin));
}

MDVN_TEST(Lexer_OtherTokenExample) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    LexResult r = LexCodeBlock(Slice("myVariableName"), rule, &arena);
    MDVN_CHECK_EQ(r.tokens.len, 1u);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenOther));
}

// ---- 综合场景:一段真实 C 代码片段,逐 token 校验类型序列 ----

MDVN_TEST(Lexer_RealisticCSnippet) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    const char* code = "int add(int a, int b) { return a + b; } // sum";
    LexResult r = LexCodeBlock(Slice(code), rule, &arena);
    MDVN_CHECK(r.tokens.len > 5);
    MDVN_CHECK_EQ(r.tokens[0].type, static_cast<mdvn::u8>(kTokenKeyword)); // int
    MDVN_CHECK(TokenTextEquals(code, r.tokens[0], "int"));
    MDVN_CHECK_EQ(r.tokens[r.tokens.len - 1].type, static_cast<mdvn::u8>(kTokenComment));
}

// ---- kMaxTokensPerBlock 优雅降级:超过上限后不再产出 token,不崩溃 ----

MDVN_TEST(Lexer_ExceedsMaxTokensDegradesGracefully) {
    Arena realArena;
    MDVN_CHECK(realArena.Init(16 * 1024 * 1024));
    const LanguageRule& rule = GetLanguageRule(kLanguageC);

    // 构造远超 kMaxTokensPerBlock 个 token 的代码:每个 "a;" 产生 2 个 token。
    mdvn::u32 repeat = kMaxTokensPerBlock * 2;
    mdvn::u32 codeLen = repeat * 2;
    char* buf = static_cast<char*>(realArena.Alloc(codeLen, 1));
    MDVN_CHECK(buf != nullptr);
    for (mdvn::u32 i = 0; i < repeat; ++i) {
        buf[i * 2] = 'a';
        buf[i * 2 + 1] = ';';
    }

    LexResult r = LexCodeBlock(StrSlice{buf, codeLen}, rule, &realArena);
    MDVN_CHECK(r.truncated);
    MDVN_CHECK(r.tokens.len <= kMaxTokensPerBlock);
    MDVN_CHECK(r.tokens.len > 0);
}

MDVN_TEST(Lexer_WithinMaxTokensNotTruncated) {
    MDVN_TEST_ARENA(arena, 4 * 1024 * 1024);
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    LexResult r = LexCodeBlock(Slice("int x = 1;"), rule, &arena);
    MDVN_CHECK(!r.truncated);
}

// ---- 10000 行超长代码块性能验收:扫描耗时 <= 50ms ----

MDVN_TEST(Lexer_TenThousandLinesPerformance) {
    Arena docArena;
    MDVN_CHECK(docArena.Init(64 * 1024 * 1024));

    const char* line = "int value_1234 = 0x1F + foo_bar_baz(1, 2, 3); // trailing comment\n";
    mdvn::u32 lineLen = static_cast<mdvn::u32>(strlen(line));
    mdvn::u32 lines = 10000;
    mdvn::u32 totalLen = lineLen * lines;
    char* buf = static_cast<char*>(docArena.Alloc(totalLen, 1));
    MDVN_CHECK(buf != nullptr);
    for (mdvn::u32 i = 0; i < lines; ++i) {
        memcpy(buf + static_cast<size_t>(i) * lineLen, line, lineLen);
    }

    Arena hlArena;
    MDVN_CHECK(hlArena.Init(64 * 1024 * 1024));
    const LanguageRule& rule = GetLanguageRule(kLanguageC);

    auto start = std::chrono::steady_clock::now();
    LexResult r = LexCodeBlock(StrSlice{buf, totalLen}, rule, &hlArena);
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    MDVN_CHECK(r.tokens.len > 0);
    MDVN_CHECK(ms <= 50.0);
    if (ms > 50.0) {
        fprintf(stderr, "Lexer_TenThousandLinesPerformance: took %.3fms (limit 50ms)\n", ms);
    }
}
