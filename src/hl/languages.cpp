// mdvn 语言规则表与别名映射的实现(T52)。全部是只读常量数据 + 两个纯函数,
// 不含任何全局副作用构造、不链接 Win32/D2D/DWrite。
//
// 关于"取第一个空白前的词"裁切逻辑的归属:本文件的 ResolveLanguageId 假设
// 调用方已经传入裁切好的单个词。裁切逻辑放在 T50(`src/doc/parser.cpp`,
// 围栏语言信息落地到文档模型那一步)而不是这里,理由:①裁切依赖 md4c 给出
// 的原始 info string 格式(可能含 CommonMark 规定的反引号/波浪线围栏差异、
// 前导空白等解析期细节),这些是解析阶段已经在处理的东西,本文件不该重新
// 认识一遍 info string 的语法;②T52 的职责是"给一个词,归一化成语言
// ID"——保持这个函数签名的单一职责,才能被 T50 的解析路径与未来任何其他
// 调用方(如设置面板手动指定语言)复用,而不必都先理解围栏语法。
#include "languages.h"

namespace mdvn {

namespace {

// ---- 各语言关键字表:按 strcmp 字节序升序排列,供二分查找。----
// 排序性由 tests/test_languages.cpp 逐对断言,这里只需要保证初始录入有序。

const char* const kCKeywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if", "int",
    "long", "register", "return", "short", "signed", "sizeof", "static",
    "struct", "switch", "typedef", "union", "unsigned", "void", "volatile",
    "while",
};

const char* const kCppKeywords[] = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
    "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class",
    "compl", "const", "const_cast", "constexpr", "continue", "decltype",
    "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
    "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
    "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
    "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private",
    "protected", "public", "register", "reinterpret_cast", "return", "short",
    "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
    "switch", "template", "this", "thread_local", "throw", "true", "try",
    "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
    "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
};

const char* const kCSharpKeywords[] = {
    "abstract", "as", "async", "await", "base", "bool", "break", "byte",
    "case", "catch", "char", "checked", "class", "const", "continue",
    "decimal", "default", "delegate", "do", "double", "else", "enum",
    "event", "explicit", "extern", "false", "finally", "fixed", "float",
    "for", "foreach", "goto", "if", "implicit", "in", "int", "interface",
    "internal", "is", "lock", "long", "namespace", "new", "null", "object",
    "operator", "out", "override", "params", "private", "protected",
    "public", "readonly", "ref", "return", "sbyte", "sealed", "short",
    "sizeof", "stackalloc", "static", "string", "struct", "switch", "this",
    "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe",
    "ushort", "using", "virtual", "void", "volatile", "while",
};

const char* const kJavaKeywords[] = {
    "abstract", "assert", "boolean", "break", "byte", "case", "catch",
    "char", "class", "const", "continue", "default", "do", "double", "else",
    "enum", "extends", "final", "finally", "float", "for", "goto", "if",
    "implements", "import", "instanceof", "int", "interface", "long",
    "native", "new", "package", "private", "protected", "public", "return",
    "short", "static", "strictfp", "super", "switch", "synchronized",
    "this", "throw", "throws", "transient", "try", "void", "volatile",
    "while",
};

// JavaScript/TypeScript 合并为一项,共用规则(裁决 #3):关键字表取两者并集,
// 多出来的 TS 专属关键字(interface/implements/enum 等)在纯 JS 代码里不会
// 出现在标识符位置之外,当普通标识符误标成关键字色的概率可忽略。
const char* const kJsKeywords[] = {
    "async", "await", "break", "case", "catch", "class", "const",
    "continue", "debugger", "default", "delete", "do", "else", "enum",
    "export", "extends", "false", "finally", "for", "function", "if",
    "implements", "import", "in", "instanceof", "interface", "let", "new",
    "null", "package", "private", "protected", "public", "return", "static",
    "super", "switch", "this", "throw", "true", "try", "typeof", "var",
    "void", "while", "with", "yield",
};

const char* const kPythonKeywords[] = {
    "False", "None", "True", "and", "as", "assert", "async", "await",
    "break", "class", "continue", "def", "del", "elif", "else", "except",
    "finally", "for", "from", "global", "if", "import", "in", "is",
    "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
    "while", "with", "yield",
};

const char* const kGoKeywords[] = {
    "break", "case", "chan", "const", "continue", "default", "defer",
    "else", "fallthrough", "for", "func", "go", "goto", "if", "import",
    "interface", "map", "package", "range", "return", "select", "struct",
    "switch", "type", "var",
};

const char* const kRustKeywords[] = {
    "as", "async", "await", "break", "const", "continue", "crate", "dyn",
    "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in",
    "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
    "self", "static", "struct", "super", "trait", "true", "type", "unsafe",
    "use", "where", "while",
};

const char* const kShellKeywords[] = {
    "break", "case", "continue", "do", "done", "elif", "else", "esac",
    "exit", "export", "fi", "for", "function", "if", "in", "local",
    "return", "select", "shift", "then", "time", "until", "while",
};

// 数组元素个数,替代 std::size(项目不引入 <algorithm>/<iterator> 这类工具头)。
template <typename T, u32 N>
constexpr u32 ArrayLen(const T (&)[N]) {
    return N;
}

// 11 种语言 + kLanguageNone 的规则表,下标与 LanguageId 的枚举值一一对应,
// GetLanguageRule 靠这个对应关系做 O(1) 查找。
constexpr LanguageRule kRules[kLanguageCount] = {
    // kLanguageNone:全部字段留空/关闭,渲染期按此表现为"零高亮"。
    {kLanguageNone, LanguageStyle::kGeneric, nullptr, 0, "", "", "", "", false,
     false, false, false},

    // kLanguageC
    {kLanguageC, LanguageStyle::kGeneric, kCKeywords, ArrayLen(kCKeywords),
     "//", "/*", "*/", "\"'", false, /*hex*/ true, /*bin*/ false,
     /*digitSep*/ false},

    // kLanguageCpp:支持 R"(...)" 原始字符串;数字分隔符用单引号(C++14),
    // 不是下划线,digitAllowSeparator 语义是"是否有某种分隔符"标记为 false
    // 以免与 Rust/Go 等下划线分隔符混淆(T51 扫描器需要各自识别具体字符)。
    {kLanguageCpp, LanguageStyle::kGeneric, kCppKeywords,
     ArrayLen(kCppKeywords), "//", "/*", "*/", "\"'", true, true, true,
     false},

    // kLanguageCSharp:@"..."/$"..." 走 supportsRawStrings=true 统一标记。
    {kLanguageCSharp, LanguageStyle::kGeneric, kCSharpKeywords,
     ArrayLen(kCSharpKeywords), "//", "/*", "*/", "\"'", true, true, true,
     true},

    // kLanguageJava:"""文本块"""同样用 supportsRawStrings=true 标记。
    {kLanguageJava, LanguageStyle::kGeneric, kJavaKeywords,
     ArrayLen(kJavaKeywords), "//", "/*", "*/", "\"'", true, true, true,
     true},

    // kLanguageJs:反引号模板字符串计入字符串定界符集合。
    {kLanguageJs, LanguageStyle::kGeneric, kJsKeywords, ArrayLen(kJsKeywords),
     "//", "/*", "*/", "\"'`", false, true, true, true},

    // kLanguagePython:'''...'''/r"..." 走 supportsRawStrings=true;
    // 无块注释(#是唯一注释形式)。
    {kLanguagePython, LanguageStyle::kGeneric, kPythonKeywords,
     ArrayLen(kPythonKeywords), "#", "", "", "\"'", true, true, true, true},

    // kLanguageGo:反引号原始字符串。
    {kLanguageGo, LanguageStyle::kGeneric, kGoKeywords, ArrayLen(kGoKeywords),
     "//", "/*", "*/", "\"'`", true, true, true, true},

    // kLanguageRust:r#"..."#原始字符串;'a 生命周期标注需要 T51 扫描器
    // 单独处理(易与字符字面量混淆的经典陷阱,见 T67 语料要求)。
    {kLanguageRust, LanguageStyle::kGeneric, kRustKeywords,
     ArrayLen(kRustKeywords), "//", "/*", "*/", "\"'", true, true, true,
     true},

    // kLanguageJson:结构化着色,不需要关键字表(true/false/null 属于字面量
    // 而非关键字,留给 T51 扫描器按 style==kStructured 特殊处理);严格 JSON
    // 无注释、无原始字符串、数字不允许 0x/0b/下划线分隔符。
    {kLanguageJson, LanguageStyle::kStructured, nullptr, 0, "", "", "", "\"",
     false, false, false, false},

    // kLanguageYaml:结构化着色(键名/标点/字面量),#行注释,无块注释;
    // 块标量 |/> 与锚点 &x/*x 是 T51 扫描器的处理范围,这里不建模。
    {kLanguageYaml, LanguageStyle::kStructured, nullptr, 0, "#", "", "",
     "\"'", false, false, false, false},

    // kLanguageShell:单引号字符串是字面量(不转义),视作"原始字符串"的
    // 一种;$(...) 与 heredoc 属于 T51 扫描器的处理范围。
    {kLanguageShell, LanguageStyle::kGeneric, kShellKeywords,
     ArrayLen(kShellKeywords), "#", "", "", "\"'", true, true, false, false},
};

// 围栏语言标记 -> 语言 ID 的别名表。别名全部为小写字面量,匹配时对输入做
// ASCII 小写折叠(与 src/util/ini.cpp 的 KeyEquals 同一手法),不做模糊匹配。
struct LanguageAlias {
    const char* alias;
    LanguageId id;
};

const LanguageAlias kAliases[] = {
    {"c", kLanguageC}, {"h", kLanguageC},

    {"cpp", kLanguageCpp}, {"c++", kLanguageCpp}, {"cc", kLanguageCpp},
    {"cxx", kLanguageCpp}, {"hpp", kLanguageCpp},

    {"cs", kLanguageCSharp}, {"csharp", kLanguageCSharp},

    {"java", kLanguageJava},

    {"js", kLanguageJs}, {"javascript", kLanguageJs}, {"jsx", kLanguageJs},
    {"ts", kLanguageJs}, {"typescript", kLanguageJs}, {"tsx", kLanguageJs},
    {"mjs", kLanguageJs},

    {"py", kLanguagePython}, {"python", kLanguagePython},
    {"python3", kLanguagePython},

    {"go", kLanguageGo}, {"golang", kLanguageGo},

    {"rs", kLanguageRust}, {"rust", kLanguageRust},

    {"json", kLanguageJson}, {"jsonc", kLanguageJson},

    {"yaml", kLanguageYaml}, {"yml", kLanguageYaml},

    {"sh", kLanguageShell}, {"bash", kLanguageShell}, {"zsh", kLanguageShell},
    {"shell", kLanguageShell}, {"console", kLanguageShell},
};

// ASCII 小写折叠,语义与写法都对齐 src/util/ini.cpp::LowerAscii。
char LowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// 大小写不敏感地比较一个 StrSlice 与一个已知全小写的 C 字符串字面量,
// 长度须完全一致(与 src/util/ini.cpp::KeyEquals 同一手法)。
bool EqualsIgnoreCase(StrSlice s, const char* literal) {
    u32 i = 0;
    for (; i < s.len; ++i) {
        if (literal[i] == 0) return false;
        if (LowerAscii(s.data[i]) != literal[i]) return false;
    }
    return literal[i] == 0;
}

} // namespace

const LanguageRule& GetLanguageRule(LanguageId id) {
    if (id >= kLanguageCount) return kRules[kLanguageNone];
    return kRules[id];
}

LanguageId ResolveLanguageId(StrSlice fenceInfo) {
    if (fenceInfo.len == 0) return kLanguageNone;
    for (u32 i = 0; i < ArrayLen(kAliases); ++i) {
        if (EqualsIgnoreCase(fenceInfo, kAliases[i].alias)) return kAliases[i].id;
    }
    return kLanguageNone;
}

} // namespace mdvn
