// T52 覆盖测试:语言规则表与别名映射。全部是纯数据/纯函数断言,不依赖
// Win32/D2D/DWrite,可脱离图形环境单测。
#include <cstring>

#include "markair_test.h"
#include "../src/hl/languages.h"

using markair::GetLanguageRule;
using markair::kLanguageC;
using markair::kLanguageCount;
using markair::kLanguageCpp;
using markair::kLanguageCSharp;
using markair::kLanguageGo;
using markair::kLanguageJava;
using markair::kLanguageJs;
using markair::kLanguageJson;
using markair::kLanguageNone;
using markair::kLanguagePython;
using markair::kLanguageRust;
using markair::kLanguageShell;
using markair::kLanguageYaml;
using markair::LanguageId;
using markair::LanguageRule;
using markair::ResolveLanguageId;
using markair::StrSlice;

namespace {

// 把一个 C 字符串包成 StrSlice,方便测试里直接传字面量。
StrSlice Slice(const char* s) { return StrSlice{s, static_cast<markair::u32>(strlen(s))}; }

// 断言某个规则表的关键字表里确实含有某个词(线性扫描即可,测试不追求效率)。
bool KeywordTableContains(const LanguageRule& rule, const char* word) {
    for (markair::u32 i = 0; i < rule.keywordCount; ++i) {
        if (strcmp(rule.keywords[i], word) == 0) return true;
    }
    return false;
}

} // namespace

// ---- 别名解析:11 种语言的全部别名都应命中期望的语言 ID ----

MARKAIR_TEST(Languages_AliasesResolveToC) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("c")), kLanguageC);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("h")), kLanguageC);
}

MARKAIR_TEST(Languages_AliasesResolveToCpp) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("cpp")), kLanguageCpp);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("c++")), kLanguageCpp);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("cc")), kLanguageCpp);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("cxx")), kLanguageCpp);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("hpp")), kLanguageCpp);
}

MARKAIR_TEST(Languages_AliasesResolveToCSharp) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("cs")), kLanguageCSharp);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("csharp")), kLanguageCSharp);
}

MARKAIR_TEST(Languages_AliasesResolveToJava) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("java")), kLanguageJava);
}

MARKAIR_TEST(Languages_AliasesResolveToJs) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("js")), kLanguageJs);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("javascript")), kLanguageJs);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("jsx")), kLanguageJs);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("ts")), kLanguageJs);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("typescript")), kLanguageJs);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("tsx")), kLanguageJs);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("mjs")), kLanguageJs);
}

MARKAIR_TEST(Languages_AliasesResolveToPython) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("py")), kLanguagePython);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("python")), kLanguagePython);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("python3")), kLanguagePython);
}

MARKAIR_TEST(Languages_AliasesResolveToGo) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("go")), kLanguageGo);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("golang")), kLanguageGo);
}

MARKAIR_TEST(Languages_AliasesResolveToRust) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("rs")), kLanguageRust);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("rust")), kLanguageRust);
}

MARKAIR_TEST(Languages_AliasesResolveToJson) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("json")), kLanguageJson);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("jsonc")), kLanguageJson);
}

MARKAIR_TEST(Languages_AliasesResolveToYaml) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("yaml")), kLanguageYaml);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("yml")), kLanguageYaml);
}

MARKAIR_TEST(Languages_AliasesResolveToShell) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("sh")), kLanguageShell);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("bash")), kLanguageShell);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("zsh")), kLanguageShell);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("shell")), kLanguageShell);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("console")), kLanguageShell);
}

// ---- 大小写不敏感 ----

MARKAIR_TEST(Languages_CaseInsensitive) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("PY")), kLanguagePython);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("Py")), kLanguagePython);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("py")), kLanguagePython);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("PyThOn")), kLanguagePython);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("CPP")), kLanguageCpp);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("JSON")), kLanguageJson);
}

// ---- 未命中一律 kLanguageNone,不做模糊匹配/内容嗅探 ----

MARKAIR_TEST(Languages_UnknownTagsMapToNone) {
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("mermaid")), kLanguageNone);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("brainfuck")), kLanguageNone);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("")), kLanguageNone);
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("latex")), kLanguageNone);
    // 不做模糊匹配:"pyth" 不应该被前缀匹配成 python。
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("pyth")), kLanguageNone);
    // 不做模糊匹配:"cpp2" 不应该被误判成 cpp。
    MARKAIR_CHECK_EQ(ResolveLanguageId(Slice("cpp2")), kLanguageNone);
}

// ---- 关键字表确实有序(逐对 strcmp 断言,防手滑) ----

MARKAIR_TEST(Languages_KeywordTablesAreSorted) {
    for (markair::u32 langIdx = 0; langIdx < kLanguageCount; ++langIdx) {
        const LanguageRule& rule = GetLanguageRule(static_cast<LanguageId>(langIdx));
        for (markair::u32 i = 0; i + 1 < rule.keywordCount; ++i) {
            MARKAIR_CHECK(strcmp(rule.keywords[i], rule.keywords[i + 1]) < 0);
        }
    }
}

// ---- kLanguageNone 的规则表应当是"零高亮"的空表 ----

MARKAIR_TEST(Languages_NoneRuleIsEmpty) {
    const LanguageRule& rule = GetLanguageRule(kLanguageNone);
    MARKAIR_CHECK_EQ(rule.keywordCount, 0u);
    MARKAIR_CHECK(rule.keywords == nullptr);
}

// ---- GetLanguageRule 越界回退到 kLanguageNone,不越界读取 ----

MARKAIR_TEST(Languages_GetRuleOutOfRangeFallsBackToNone) {
    LanguageId bogus = static_cast<LanguageId>(kLanguageCount + 50);
    const LanguageRule& rule = GetLanguageRule(bogus);
    MARKAIR_CHECK_EQ(rule.keywordCount, 0u);
}

// ---- 11 种语言各自的关键字/注释定界符符合语言常识(覆盖真实代码片段场景) ----

MARKAIR_TEST(Languages_CKeywordsAndComments) {
    const LanguageRule& rule = GetLanguageRule(kLanguageC);
    MARKAIR_CHECK(KeywordTableContains(rule, "struct"));
    MARKAIR_CHECK(KeywordTableContains(rule, "return"));
    MARKAIR_CHECK(!KeywordTableContains(rule, "class")); // C 没有 class
    MARKAIR_CHECK_STREQ(rule.lineCommentPrefix, "//");
    MARKAIR_CHECK_STREQ(rule.blockCommentStart, "/*");
    MARKAIR_CHECK_STREQ(rule.blockCommentEnd, "*/");
}

MARKAIR_TEST(Languages_CppKeywordsAndRawStrings) {
    const LanguageRule& rule = GetLanguageRule(kLanguageCpp);
    MARKAIR_CHECK(KeywordTableContains(rule, "class"));
    MARKAIR_CHECK(KeywordTableContains(rule, "template"));
    MARKAIR_CHECK(KeywordTableContains(rule, "namespace"));
    MARKAIR_CHECK(rule.supportsRawStrings); // R"(...)"
    MARKAIR_CHECK_STREQ(rule.lineCommentPrefix, "//");
}

MARKAIR_TEST(Languages_CSharpKeywords) {
    const LanguageRule& rule = GetLanguageRule(kLanguageCSharp);
    MARKAIR_CHECK(KeywordTableContains(rule, "namespace"));
    MARKAIR_CHECK(KeywordTableContains(rule, "async"));
    MARKAIR_CHECK(KeywordTableContains(rule, "var") == false); // C# var 是上下文关键字,不入表
}

MARKAIR_TEST(Languages_JavaKeywords) {
    const LanguageRule& rule = GetLanguageRule(kLanguageJava);
    MARKAIR_CHECK(KeywordTableContains(rule, "class"));
    MARKAIR_CHECK(KeywordTableContains(rule, "interface"));
    MARKAIR_CHECK(KeywordTableContains(rule, "synchronized"));
}

MARKAIR_TEST(Languages_JsKeywordsCoverTsAlso) {
    const LanguageRule& rule = GetLanguageRule(kLanguageJs);
    MARKAIR_CHECK(KeywordTableContains(rule, "function"));
    MARKAIR_CHECK(KeywordTableContains(rule, "async"));
    MARKAIR_CHECK(KeywordTableContains(rule, "interface")); // TS 关键字,与 JS 共用规则
}

MARKAIR_TEST(Languages_PythonKeywordsAndHashComment) {
    const LanguageRule& rule = GetLanguageRule(kLanguagePython);
    MARKAIR_CHECK(KeywordTableContains(rule, "def"));
    MARKAIR_CHECK(KeywordTableContains(rule, "lambda"));
    MARKAIR_CHECK_STREQ(rule.lineCommentPrefix, "#");
    MARKAIR_CHECK_STREQ(rule.blockCommentStart, ""); // Python 无块注释
}

MARKAIR_TEST(Languages_GoKeywordsAndBacktickString) {
    const LanguageRule& rule = GetLanguageRule(kLanguageGo);
    MARKAIR_CHECK(KeywordTableContains(rule, "func"));
    MARKAIR_CHECK(KeywordTableContains(rule, "defer"));
    MARKAIR_CHECK(rule.supportsRawStrings); // 反引号原始字符串
}

MARKAIR_TEST(Languages_RustKeywords) {
    const LanguageRule& rule = GetLanguageRule(kLanguageRust);
    MARKAIR_CHECK(KeywordTableContains(rule, "fn"));
    MARKAIR_CHECK(KeywordTableContains(rule, "impl"));
    MARKAIR_CHECK(KeywordTableContains(rule, "unsafe"));
    MARKAIR_CHECK(rule.supportsRawStrings); // r#"..."#
}

MARKAIR_TEST(Languages_JsonIsStructuredWithNoComments) {
    const LanguageRule& rule = GetLanguageRule(kLanguageJson);
    MARKAIR_CHECK(rule.style == markair::LanguageStyle::kStructured);
    MARKAIR_CHECK_EQ(rule.keywordCount, 0u);
    MARKAIR_CHECK_STREQ(rule.lineCommentPrefix, ""); // 严格 JSON 无注释
}

MARKAIR_TEST(Languages_YamlIsStructuredWithHashComment) {
    const LanguageRule& rule = GetLanguageRule(kLanguageYaml);
    MARKAIR_CHECK(rule.style == markair::LanguageStyle::kStructured);
    MARKAIR_CHECK_STREQ(rule.lineCommentPrefix, "#");
}

MARKAIR_TEST(Languages_ShellKeywordsAndHashComment) {
    const LanguageRule& rule = GetLanguageRule(kLanguageShell);
    MARKAIR_CHECK(KeywordTableContains(rule, "for"));
    MARKAIR_CHECK(KeywordTableContains(rule, "done"));
    MARKAIR_CHECK_STREQ(rule.lineCommentPrefix, "#");
}
