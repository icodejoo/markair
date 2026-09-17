// T52 覆盖测试:语言规则表与别名映射。全部是纯数据/纯函数断言,不依赖
// Win32/D2D/DWrite,可脱离图形环境单测。
#include <cstring>

#include "mdvn_test.h"
#include "../src/hl/languages.h"

using mdvn::GetLanguageRule;
using mdvn::kLanguageC;
using mdvn::kLanguageCount;
using mdvn::kLanguageCpp;
using mdvn::kLanguageCSharp;
using mdvn::kLanguageGo;
using mdvn::kLanguageJava;
using mdvn::kLanguageJs;
using mdvn::kLanguageJson;
using mdvn::kLanguageNone;
using mdvn::kLanguagePython;
using mdvn::kLanguageRust;
using mdvn::kLanguageShell;
using mdvn::kLanguageYaml;
using mdvn::LanguageId;
using mdvn::LanguageRule;
using mdvn::ResolveLanguageId;
using mdvn::StrSlice;

namespace {

// 把一个 C 字符串包成 StrSlice,方便测试里直接传字面量。
StrSlice Slice(const char* s) { return StrSlice{s, static_cast<mdvn::u32>(strlen(s))}; }

// 断言某个规则表的关键字表里确实含有某个词(线性扫描即可,测试不追求效率)。
bool KeywordTableContains(const LanguageRule& rule, const char* word) {
    for (mdvn::u32 i = 0; i < rule.keywordCount; ++i) {
        if (strcmp(rule.keywords[i], word) == 0) return true;
    }
    return false;
}

} // namespace

// ---- 别名解析:11 种语言的全部别名都应命中期望的语言 ID ----

MDVN_TEST(Languages_AliasesResolveToC) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("c")), kLanguageC);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("h")), kLanguageC);
}

MDVN_TEST(Languages_AliasesResolveToCpp) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("cpp")), kLanguageCpp);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("c++")), kLanguageCpp);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("cc")), kLanguageCpp);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("cxx")), kLanguageCpp);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("hpp")), kLanguageCpp);
}

MDVN_TEST(Languages_AliasesResolveToCSharp) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("cs")), kLanguageCSharp);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("csharp")), kLanguageCSharp);
}

MDVN_TEST(Languages_AliasesResolveToJava) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("java")), kLanguageJava);
}

MDVN_TEST(Languages_AliasesResolveToJs) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("js")), kLanguageJs);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("javascript")), kLanguageJs);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("jsx")), kLanguageJs);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("ts")), kLanguageJs);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("typescript")), kLanguageJs);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("tsx")), kLanguageJs);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("mjs")), kLanguageJs);
}

MDVN_TEST(Languages_AliasesResolveToPython) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("py")), kLanguagePython);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("python")), kLanguagePython);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("python3")), kLanguagePython);
}

MDVN_TEST(Languages_AliasesResolveToGo) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("go")), kLanguageGo);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("golang")), kLanguageGo);
}

MDVN_TEST(Languages_AliasesResolveToRust) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("rs")), kLanguageRust);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("rust")), kLanguageRust);
}

MDVN_TEST(Languages_AliasesResolveToJson) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("json")), kLanguageJson);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("jsonc")), kLanguageJson);
}

MDVN_TEST(Languages_AliasesResolveToYaml) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("yaml")), kLanguageYaml);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("yml")), kLanguageYaml);
}

MDVN_TEST(Languages_AliasesResolveToShell) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("sh")), kLanguageShell);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("bash")), kLanguageShell);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("zsh")), kLanguageShell);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("shell")), kLanguageShell);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("console")), kLanguageShell);
}

// ---- 大小写不敏感 ----

MDVN_TEST(Languages_CaseInsensitive) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("PY")), kLanguagePython);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("Py")), kLanguagePython);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("py")), kLanguagePython);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("PyThOn")), kLanguagePython);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("CPP")), kLanguageCpp);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("JSON")), kLanguageJson);
}

// ---- 未命中一律 kLanguageNone,不做模糊匹配/内容嗅探 ----

MDVN_TEST(Languages_UnknownTagsMapToNone) {
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("mermaid")), kLanguageNone);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("brainfuck")), kLanguageNone);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("")), kLanguageNone);
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("latex")), kLanguageNone);
    // 不做模糊匹配:"pyth" 不应该被前缀匹配成 python。
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("pyth")), kLanguageNone);
    // 不做模糊匹配:"cpp2" 不应该被误判成 cpp。
    MDVN_CHECK_EQ(ResolveLanguageId(Slice("cpp2")), kLanguageNone);
}

// ---- 关键字表确实有序(逐对 strcmp 断言,防手滑) ----

MDVN_TEST(Languages_KeywordTablesAreSorted) {
    for (mdvn::u32 langIdx = 0; langIdx < kLanguageCount; ++langIdx) {
        const LanguageRule& rule = GetLanguageRule(static_cast<LanguageId>(langIdx));
        for (mdvn::u32 i = 0; i + 1 < rule.keywordCount; ++i) {
            MDVN_CHECK(strcmp(rule.keywords[i], rule.keywords[i + 1]) < 0);
        }
    }
}

// ---- kLanguageNone 的规则表应当是"零高亮"的空表 ----

MDVN_TEST(Languages_NoneRuleIsEmpty) {
    const LanguageRule& rule = GetLanguageRule(kLanguageNone);
    MDVN_CHECK_EQ(rule.keywordCount, 0u);
    MDVN_CHECK(rule.keywords == nullptr);
}

// ---- GetLanguageRule 越界回退到 kLanguageNone,不越界读取 ----

MDVN_TEST(Languages_GetRuleOutOfRangeFallsBackToNone) {
    LanguageId bogus = static_cast<LanguageId>(kLanguageCount + 50);
    const LanguageRule& rule = GetLanguageRule(bogus);
    MDVN_CHECK_EQ(rule.keywordCount, 0u);
}

// ---- 11 种语言各自的关键字/注释定界符符合语言常识(覆盖真实代码片段场景) ----

MDVN_TEST(Languages_CKeywordsAndComments) {
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    MDVN_CHECK(KeywordTableContains(rule, "struct"));
    MDVN_CHECK(KeywordTableContains(rule, "return"));
    MDVN_CHECK(!KeywordTableContains(rule, "class")); // C 没有 class
    MDVN_CHECK_STREQ(rule.lineCommentPrefix, "//");
    MDVN_CHECK_STREQ(rule.blockCommentStart, "/*");
    MDVN_CHECK_STREQ(rule.blockCommentEnd, "*/");
}

MDVN_TEST(Languages_CppKeywordsAndRawStrings) {
    const LanguageRule& rule = GetLanguageRule(kLanguageCpp);
    MDVN_CHECK(KeywordTableContains(rule, "class"));
    MDVN_CHECK(KeywordTableContains(rule, "template"));
    MDVN_CHECK(KeywordTableContains(rule, "namespace"));
    MDVN_CHECK(rule.supportsRawStrings); // R"(...)"
    MDVN_CHECK_STREQ(rule.lineCommentPrefix, "//");
}

MDVN_TEST(Languages_CSharpKeywords) {
    const LanguageRule& rule = GetLanguageRule(kLanguageCSharp);
    MDVN_CHECK(KeywordTableContains(rule, "namespace"));
    MDVN_CHECK(KeywordTableContains(rule, "async"));
    MDVN_CHECK(KeywordTableContains(rule, "var") == false); // C# var 是上下文关键字,不入表
}

MDVN_TEST(Languages_JavaKeywords) {
    const LanguageRule& rule = GetLanguageRule(kLanguageJava);
    MDVN_CHECK(KeywordTableContains(rule, "class"));
    MDVN_CHECK(KeywordTableContains(rule, "interface"));
    MDVN_CHECK(KeywordTableContains(rule, "synchronized"));
}

MDVN_TEST(Languages_JsKeywordsCoverTsAlso) {
    const LanguageRule& rule = GetLanguageRule(kLanguageJs);
    MDVN_CHECK(KeywordTableContains(rule, "function"));
    MDVN_CHECK(KeywordTableContains(rule, "async"));
    MDVN_CHECK(KeywordTableContains(rule, "interface")); // TS 关键字,与 JS 共用规则
}

MDVN_TEST(Languages_PythonKeywordsAndHashComment) {
    const LanguageRule& rule = GetLanguageRule(kLanguagePython);
    MDVN_CHECK(KeywordTableContains(rule, "def"));
    MDVN_CHECK(KeywordTableContains(rule, "lambda"));
    MDVN_CHECK_STREQ(rule.lineCommentPrefix, "#");
    MDVN_CHECK_STREQ(rule.blockCommentStart, ""); // Python 无块注释
}

MDVN_TEST(Languages_GoKeywordsAndBacktickString) {
    const LanguageRule& rule = GetLanguageRule(kLanguageGo);
    MDVN_CHECK(KeywordTableContains(rule, "func"));
    MDVN_CHECK(KeywordTableContains(rule, "defer"));
    MDVN_CHECK(rule.supportsRawStrings); // 反引号原始字符串
}

MDVN_TEST(Languages_RustKeywords) {
    const LanguageRule& rule = GetLanguageRule(kLanguageRust);
    MDVN_CHECK(KeywordTableContains(rule, "fn"));
    MDVN_CHECK(KeywordTableContains(rule, "impl"));
    MDVN_CHECK(KeywordTableContains(rule, "unsafe"));
    MDVN_CHECK(rule.supportsRawStrings); // r#"..."#
}

MDVN_TEST(Languages_JsonIsStructuredWithNoComments) {
    const LanguageRule& rule = GetLanguageRule(kLanguageJson);
    MDVN_CHECK(rule.style == mdvn::LanguageStyle::kStructured);
    MDVN_CHECK_EQ(rule.keywordCount, 0u);
    MDVN_CHECK_STREQ(rule.lineCommentPrefix, ""); // 严格 JSON 无注释
}

MDVN_TEST(Languages_YamlIsStructuredWithHashComment) {
    const LanguageRule& rule = GetLanguageRule(kLanguageYaml);
    MDVN_CHECK(rule.style == mdvn::LanguageStyle::kStructured);
    MDVN_CHECK_STREQ(rule.lineCommentPrefix, "#");
}

MDVN_TEST(Languages_ShellKeywordsAndHashComment) {
    const LanguageRule& rule = GetLanguageRule(kLanguageShell);
    MDVN_CHECK(KeywordTableContains(rule, "for"));
    MDVN_CHECK(KeywordTableContains(rule, "done"));
    MDVN_CHECK_STREQ(rule.lineCommentPrefix, "#");
}
