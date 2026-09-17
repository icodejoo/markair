// mdvn 语言规则表与围栏语言别名映射(T52)。纯数据 + 纯函数模块,不链接任何
// Win32/D2D/DWrite,可脱离图形环境单测——这也是后续 T51 词法器的约束。
//
// 职责边界:本文件只回答"这个围栏语言标记归一化成哪个 languageId、这个
// languageId 对应什么词法规则",不做扫描(T51)也不做围栏 info string 的
// 空白裁切(T50,见 ResolveLanguageId 的参数契约说明)。
#pragma once

#include "../util/str.h"
#include "../util/types.h"

namespace mdvn {

/**
 * 语言 ID。`kLanguageNone` 表示"无高亮",是未命中别名表时的归一化结果,
 * 也是缩进代码块(无围栏语言标记)的默认值。数值本身无特殊含义,不对外
 * 承诺稳定的整数值(不落盘、不跨版本持久化),只在进程内使用。
 */
enum LanguageId : u8 {
    kLanguageNone = 0,  // 无高亮(未命中别名表,或围栏未指定语言)
    kLanguageC,
    kLanguageCpp,
    kLanguageCSharp,
    kLanguageJava,
    kLanguageJs,      // JavaScript/TypeScript 合并为一项,共用规则(裁决 #3)
    kLanguagePython,
    kLanguageGo,
    kLanguageRust,
    kLanguageJson,
    kLanguageYaml,
    kLanguageShell,
    kLanguageCount,  // 语言总数(含 kLanguageNone),不是一个合法的语言 ID
};

/**
 * 词法着色风格:区分"通用类 C 语言"(关键字/字符串/注释/数字走同一套语法)
 * 与"结构化数据"(JSON/YAML 的"关键字"实际是键名/标点/字面量的结构着色)。
 * T52 只负责标记这个区分,具体扫描逻辑属于 T51 的范围。
 */
enum class LanguageStyle : u8 {
    kGeneric,     // 通用类 C 语言风格(关键字表 + 字符串 + 行/块注释 + 数字)
    kStructured,  // 结构化数据风格(JSON/YAML)
};

/**
 * 单种语言的词法规则表:关键字表 + 注释定界符 + 字符串定界符 + 数字字面量
 * 风格。全部是只读数据,实例都以 `constexpr`/`static const` 形式存在于
 * languages.cpp 的 `.rdata` 段,不含任何指向堆/arena 的指针。
 *
 * 关键字表要求:按 `strcmp` 升序排序,供二分查找;排序性由
 * `tests/test_languages.cpp` 逐对断言,防手滑。
 */
struct LanguageRule {
    LanguageId id;                    // 本规则对应的语言 ID
    LanguageStyle style;              // 通用类 C / 结构化数据
    const char* const* keywords;      // 关键字表,已排序,指向 .rdata 常量数组
    u32 keywordCount;                 // 关键字个数
    const char* lineCommentPrefix;    // 行注释前缀,空字符串表示不支持
    const char* blockCommentStart;    // 块注释起始定界符,空字符串表示不支持
    const char* blockCommentEnd;      // 块注释结束定界符,与 Start 成对
    const char* stringDelimiters;     // 支持的字符串定界符集合(如 "\"'`")
    bool supportsRawStrings;          // 是否支持原始字符串(R"(...)"/反引号等)
    bool numberAllowHex;              // 数字字面量是否允许 0x 前缀
    bool numberAllowBinary;           // 数字字面量是否允许 0b 前缀
    bool numberAllowDigitSeparator;   // 数字字面量是否允许 _ 分隔符
};

/**
 * 按语言 ID 取词法规则表。`kLanguageNone` 也有对应条目(全部字段为空/关闭),
 * 调用方不需要为 `kLanguageNone` 单独判空。
 *
 * @param id 语言 ID(取值范围 [0, kLanguageCount)),越界视为 kLanguageNone。
 * @return 对应的规则表引用,生命周期与进程等长(静态数据)。
 * @example const LanguageRule& rule = mdvn::GetLanguageRule(mdvn::kLanguageCpp);
 */
const LanguageRule& GetLanguageRule(LanguageId id);

/**
 * 把围栏语言标记归一化为语言 ID,大小写不敏感,未命中别名表一律返回
 * `kLanguageNone`(不做模糊匹配、不做内容嗅探,裁决 #3 原文)。
 *
 * 参数契约:调用方须传入已裁切好的单个词(不含空白、不含围栏后的附加
 * 参数,如 ` ```js title="a.js" ` 中只传 "js")——"取第一个空白前的词"
 * 属于 T50(解析阶段)的职责,不在本函数内做,理由见 languages.cpp 顶部
 * 注释。
 *
 * @param fenceInfo 围栏语言标记(如 "cpp"、"Py"、"JS"),空切片返回 kLanguageNone。
 * @return 归一化后的语言 ID。
 * @example mdvn::LanguageId id = mdvn::ResolveLanguageId(mdvn::StrSlice{"PY", 2}); // kLanguagePython
 */
LanguageId ResolveLanguageId(StrSlice fenceInfo);

} // namespace mdvn
