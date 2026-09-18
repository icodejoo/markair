// mdvn 代码高亮词法器的实现(T51)。手写状态机,逐字节扫描,不使用
// std::regex/iostream/异常,不链接图形 API。
#include "lexer.h"

#include <cstring>

namespace mdvn {

namespace {

// 数组元素个数,写法与 languages.cpp::ArrayLen 保持一致(不引入 <algorithm>)。
template <typename T, u32 N>
constexpr u32 ArrayLen(const T (&)[N]) {
    return N;
}

// ---- 字节分类辅助函数 ----

// ASCII 空白(空格/Tab/换行/回车)。
bool IsAsciiSpace(u8 c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// ASCII 十进制数字。
bool IsAsciiDigit(u8 c) { return c >= '0' && c <= '9'; }

// ASCII 十六进制数字。
bool IsAsciiHexDigit(u8 c) {
    return IsAsciiDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// ASCII 字母。
bool IsAsciiAlpha(u8 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// 非 ASCII 字节(最高位为 1,UTF-8 多字节序列的一部分)。要求 #4:一律当作
// 标识符/普通字符处理,不参与状态机的语法转移判断,避免中文注释/字符串
// 内容打乱扫描。
bool IsNonAscii(u8 c) { return c >= 0x80; }

// 标识符起始字符:字母、下划线、或任意非 ASCII 字节。
bool IsIdentStart(u8 c) { return IsAsciiAlpha(c) || c == '_' || IsNonAscii(c); }

// 标识符延续字符:起始字符集合 + 数字。
bool IsIdentCont(u8 c) { return IsIdentStart(c) || IsAsciiDigit(c); }

// 某个字符是否属于字符串定界符集合(如 "\"'`"),空指针/空串视为不含任何字符。
bool ContainsChar(const char* set, u8 c) {
    if (!set) return false;
    for (const char* p = set; *p != '\0'; ++p) {
        if (static_cast<u8>(*p) == c) return true;
    }
    return false;
}

// 在 [pos, len) 范围内判断 code 是否以字面量 lit 开头(不含结尾 '\0' 判断)。
bool MatchesAt(const char* code, u32 len, u32 pos, const char* lit) {
    if (!lit || lit[0] == '\0') return false;
    u32 litLen = static_cast<u32>(strlen(lit));
    if (pos + litLen > len) return false;
    return memcmp(code + pos, lit, litLen) == 0;
}

// 在已排序的关键字表(strcmp 升序)中二分查找 [word, word+wordLen) 是否存在。
bool BinarySearchWord(const char* const* table, u32 count, const char* word, u32 wordLen) {
    u32 lo = 0, hi = count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        const char* entry = table[mid];
        u32 entryLen = static_cast<u32>(strlen(entry));
        u32 cmpLen = wordLen < entryLen ? wordLen : entryLen;
        int cmp = cmpLen == 0 ? 0 : memcmp(word, entry, cmpLen);
        if (cmp == 0) cmp = static_cast<int>(wordLen) - static_cast<int>(entryLen);
        if (cmp == 0) return true;
        if (cmp < 0) hi = mid; else lo = mid + 1;
    }
    return false;
}

// ---- 内置类型名/内置标识符小表:languages.h 的 LanguageRule 不建模这一类,
// 这里只为"类型名/内置标识符"这一 Token 类别补一份最小示例表,不追求
// 完整覆盖标准库,足以让常见内置类型正确上色即可。表内条目按无需排序
// (线性扫描,数量很小)。----

const char* const kCBuiltinTypes[] = {
    "int32_t", "int64_t", "size_t", "uint32_t", "uint64_t", "uint8_t",
};
const char* const kCppBuiltinTypes[] = {
    "int32_t", "int64_t", "size_t", "std", "uint32_t", "uint64_t", "uint8_t",
};
const char* const kCSharpBuiltinTypes[] = {
    "Boolean", "Int32", "Object", "String",
};
const char* const kJavaBuiltinTypes[] = {
    "Boolean", "Integer", "Object", "String",
};
const char* const kJsBuiltinTypes[] = {
    "Array", "Boolean", "Number", "Object", "Promise", "String",
};
const char* const kPythonBuiltinTypes[] = {
    "bool", "dict", "float", "int", "list", "str",
};
const char* const kGoBuiltinTypes[] = {
    "byte", "error", "int32", "rune", "string", "uint64",
};
const char* const kRustBuiltinTypes[] = {
    "String", "Vec", "i64", "u32", "u8", "usize",
};
const char* const kShellBuiltinTypes[] = {
    "printf", "read",
};

// 线性查找一个内置类型/标识符小表。
bool BuiltinTableContains(const char* const* table, u32 count, const char* word, u32 wordLen) {
    for (u32 i = 0; i < count; ++i) {
        u32 entryLen = static_cast<u32>(strlen(table[i]));
        if (entryLen == wordLen && memcmp(word, table[i], wordLen) == 0) return true;
    }
    return false;
}

// 按语言 ID 取内置类型/标识符小表,未建模的语言返回空表(count=0)。
void GetBuiltinTypeTable(LanguageId id, const char* const** outTable, u32* outCount) {
    switch (id) {
        case kLanguageC:
            *outTable = kCBuiltinTypes; *outCount = ArrayLen(kCBuiltinTypes); return;
        case kLanguageCpp:
            *outTable = kCppBuiltinTypes; *outCount = ArrayLen(kCppBuiltinTypes); return;
        case kLanguageCSharp:
            *outTable = kCSharpBuiltinTypes; *outCount = ArrayLen(kCSharpBuiltinTypes); return;
        case kLanguageJava:
            *outTable = kJavaBuiltinTypes; *outCount = ArrayLen(kJavaBuiltinTypes); return;
        case kLanguageJs:
            *outTable = kJsBuiltinTypes; *outCount = ArrayLen(kJsBuiltinTypes); return;
        case kLanguagePython:
            *outTable = kPythonBuiltinTypes; *outCount = ArrayLen(kPythonBuiltinTypes); return;
        case kLanguageGo:
            *outTable = kGoBuiltinTypes; *outCount = ArrayLen(kGoBuiltinTypes); return;
        case kLanguageRust:
            *outTable = kRustBuiltinTypes; *outCount = ArrayLen(kRustBuiltinTypes); return;
        case kLanguageShell:
            *outTable = kShellBuiltinTypes; *outCount = ArrayLen(kShellBuiltinTypes); return;
        case kLanguageNone:
        case kLanguageJson:
        case kLanguageYaml:
        case kLanguageCount:
        default:
            *outTable = nullptr; *outCount = 0; return;
    }
}

// 结构化语言(JSON/YAML)的字面量 true/false/null,归入 kTokenBuiltin。
bool IsStructuredLiteral(const char* word, u32 wordLen) {
    static const char* const kLiterals[] = {"false", "null", "true"};
    return BuiltinTableContains(kLiterals, ArrayLen(kLiterals), word, wordLen);
}

// ---- 扫描器本体:封装当前扫描位置与目标数组,内部方法按状态机分支组织。----
class Lexer {
public:
    Lexer(StrSlice code, const LanguageRule& rule, Vec<Token>* out)
        : code_(code.data), len_(code.len), rule_(rule), out_(out), pos_(0), tokenCount_(0) {}

    // 执行完整扫描,返回是否因触达 kMaxTokensPerBlock 而提前结束。
    bool Run() {
        while (pos_ < len_) {
            if (tokenCount_ >= kMaxTokensPerBlock) return true; // 优雅降级:不再产出 token
            u8 c = static_cast<u8>(code_[pos_]);

            if (IsAsciiSpace(c)) { ++pos_; continue; }
            if (TryLineComment()) continue;
            if (TryBlockComment()) continue;
            if (TryRawString()) continue;
            if (TryQuotedString()) continue;
            if (IsAsciiDigit(c)) { ScanNumber(); continue; }
            if (IsIdentStart(c)) { ScanIdentifier(); continue; }
            ScanPunct();
        }
        return false;
    }

private:
    // 追加一个 token,内部计数同步自增;Push 失败(arena 耗尽)也不崩溃,
    // 只是这个 token 丢失,后续扫描仍然继续(优雅降级的另一种表现)。
    void Emit(u32 start, u32 end, TokenType type) {
        if (end <= start) return;
        out_->Push(Token{start, end - start, static_cast<u8>(type)});
        ++tokenCount_;
    }

    // 行注释:命中 lineCommentPrefix 后扫到 '\n' 或文本末尾为止。
    bool TryLineComment() {
        const char* prefix = rule_.lineCommentPrefix;
        if (!prefix || prefix[0] == '\0') return false;
        if (!MatchesAt(code_, len_, pos_, prefix)) return false;
        u32 start = pos_;
        pos_ += static_cast<u32>(strlen(prefix));
        while (pos_ < len_ && code_[pos_] != '\n') ++pos_;
        Emit(start, pos_, kTokenComment);
        return true;
    }

    // 块注释:命中 blockCommentStart 后寻找 blockCommentEnd;找不到(未闭合)
    // 就扫到文本末尾结束,不会死循环或越界。
    bool TryBlockComment() {
        const char* startLit = rule_.blockCommentStart;
        if (!startLit || startLit[0] == '\0') return false;
        if (!MatchesAt(code_, len_, pos_, startLit)) return false;
        u32 start = pos_;
        pos_ += static_cast<u32>(strlen(startLit));
        const char* endLit = rule_.blockCommentEnd;
        u32 endLitLen = (endLit && endLit[0] != '\0') ? static_cast<u32>(strlen(endLit)) : 0;
        if (endLitLen == 0) {
            // 没有配置结束定界符,视为扫到文本末尾(理论上不应发生,防御性处理)。
            pos_ = len_;
        } else {
            while (pos_ < len_ && !MatchesAt(code_, len_, pos_, endLit)) ++pos_;
            if (pos_ < len_) pos_ += endLitLen; // 找到了闭合定界符,跳过它
            // 找不到就已经停在 len_,未闭合块注释扫到文本末尾。
        }
        Emit(start, pos_, kTokenComment);
        return true;
    }

    // 原始字符串族:C++ R"(...)"/Python 三引号/Go 反引号/Shell 单引号。
    // 内容原样扫描,不处理转义,找不到闭合定界符就扫到文本末尾。
    bool TryRawString() {
        if (!rule_.supportsRawStrings) return false;
        u8 c = static_cast<u8>(code_[pos_]);

        // Rust:生命周期标注 'a 与字符字面量 'x'/'\n'/'\'' 的区分。必须写在
        // TryQuotedString 之前——否则 'a 会被通用扫描当成字符字面量的开
        // 引号,一路吞到文件里下一个单引号(T67 语料点名的经典陷阱)。
        if (rule_.id == kLanguageRust && c == '\'') {
            u32 p = pos_ + 1;
            if (p < len_ && code_[p] == '\\' && p + 1 < len_) {
                // 转义字符字面量,如 '\n' '\t' '\''。转义序列固定占 2 字节
                // (反斜杠+1 字节),紧跟闭合单引号才算合法字符字面量。
                u32 afterEscape = p + 2;
                if (afterEscape < len_ && code_[afterEscape] == '\'') {
                    u32 start = pos_;
                    pos_ = afterEscape + 1;
                    Emit(start, pos_, kTokenString);
                    return true;
                }
                // 不是标准转义字符字面量形态,退化交给 TryQuotedString。
            } else if (p < len_ && IsIdentStart(static_cast<u8>(code_[p]))) {
                u32 identStart = p;
                u32 q = p;
                while (q < len_ && IsIdentCont(static_cast<u8>(code_[q]))) ++q;
                if (q == identStart + 1 && q < len_ && code_[q] == '\'') {
                    // 单字符字符字面量,如 'x'。
                    u32 start = pos_;
                    pos_ = q + 1;
                    Emit(start, pos_, kTokenString);
                    return true;
                }
                // 生命周期标注:' + 标识符,不吞闭合引号(本来就没有)。
                u32 start = pos_;
                pos_ = q;
                Emit(start, pos_, kTokenBuiltin);
                return true;
            }
            // 其余情况(如 ' 后紧跟数字/符号)交给 TryQuotedString 按普通
            // 字符字面量扫描。
        }

        // Rust 原始字符串 r#"..."#(仅支持单个 # 的最简形式,与 C++ 分支的
        // 取舍一致)。
        if (rule_.id == kLanguageRust && c == 'r' && MatchesAt(code_, len_, pos_, "r#\"")) {
            u32 start = pos_;
            pos_ += 3;
            while (pos_ < len_ && !MatchesAt(code_, len_, pos_, "\"#")) ++pos_;
            if (pos_ < len_) pos_ += 2;
            Emit(start, pos_, kTokenString);
            return true;
        }

        // Java 文本块 """..."""(三重双引号,可直接复用 Python 三引号的
        // 消费逻辑)。
        if (rule_.id == kLanguageJava && c == '"' && MatchesAt(code_, len_, pos_, "\"\"\"")) {
            u32 start = pos_;
            pos_ += 3;
            while (pos_ < len_ && !MatchesAt(code_, len_, pos_, "\"\"\"")) ++pos_;
            if (pos_ < len_) pos_ += 3;
            Emit(start, pos_, kTokenString);
            return true;
        }

        // C++ 原始字符串 R"(...)"(仅支持这个最简单的定界符形式)。
        if (rule_.id == kLanguageCpp && c == 'R' && MatchesAt(code_, len_, pos_, "R\"(")) {
            u32 start = pos_;
            pos_ += 3;
            while (pos_ < len_ && !MatchesAt(code_, len_, pos_, ")\"")) ++pos_;
            if (pos_ < len_) pos_ += 2;
            Emit(start, pos_, kTokenString);
            return true;
        }

        // Python 三引号字符串 '''...'''/"""..."""。
        if (rule_.id == kLanguagePython && (c == '\'' || c == '"')) {
            char triple[4] = {static_cast<char>(c), static_cast<char>(c), static_cast<char>(c), '\0'};
            if (MatchesAt(code_, len_, pos_, triple)) {
                u32 start = pos_;
                pos_ += 3;
                while (pos_ < len_ && !MatchesAt(code_, len_, pos_, triple)) ++pos_;
                if (pos_ < len_) pos_ += 3;
                Emit(start, pos_, kTokenString);
                return true;
            }
            return false; // 单/双引号但不是三引号,交给 TryQuotedString 处理
        }

        // Go 反引号原始字符串。
        if (rule_.id == kLanguageGo && c == '`') {
            u32 start = pos_;
            ++pos_;
            while (pos_ < len_ && code_[pos_] != '`') ++pos_;
            if (pos_ < len_) ++pos_;
            Emit(start, pos_, kTokenString);
            return true;
        }

        // Shell 单引号字符串:内容原样,不处理转义。
        if (rule_.id == kLanguageShell && c == '\'') {
            u32 start = pos_;
            ++pos_;
            while (pos_ < len_ && code_[pos_] != '\'') ++pos_;
            if (pos_ < len_) ++pos_;
            Emit(start, pos_, kTokenString);
            return true;
        }

        return false;
    }

    // 普通带引号字符串:支持 \" 与 \\ 转义,扫到同一种闭合引号或文本末尾。
    bool TryQuotedString() {
        u8 c = static_cast<u8>(code_[pos_]);
        if (!ContainsChar(rule_.stringDelimiters, c)) return false;
        u8 quote = c;
        u32 start = pos_;
        ++pos_;
        while (pos_ < len_) {
            u8 cur = static_cast<u8>(code_[pos_]);
            if (cur == '\\' && pos_ + 1 < len_) {
                // 转义字符:跳过反斜杠与其后一个字节(不解析该字节含义),
                // 这样 \" 与 \\ 都不会被误判为提前结束字符串。
                pos_ += 2;
                continue;
            }
            if (cur == quote) { ++pos_; break; }
            ++pos_;
        }
        // 未闭合:循环退出时 pos_ == len_,已经扫到文本末尾,不会越界。
        Emit(start, pos_, kTokenString);
        return true;
    }

    // 数字字面量:整数/浮点数/十六进制/二进制,按 LanguageRule 的标志位
    // 决定允许哪些前缀与分隔符;结尾额外吞掉字母后缀(如 1u32/1L/1f)。
    void ScanNumber() {
        u32 start = pos_;
        bool isHex = rule_.numberAllowHex && pos_ + 1 < len_ && code_[pos_] == '0' &&
                     (code_[pos_ + 1] == 'x' || code_[pos_ + 1] == 'X');
        bool isBin = !isHex && rule_.numberAllowBinary && pos_ + 1 < len_ && code_[pos_] == '0' &&
                     (code_[pos_ + 1] == 'b' || code_[pos_ + 1] == 'B');
        if (isHex || isBin) {
            pos_ += 2;
            while (pos_ < len_) {
                u8 cc = static_cast<u8>(code_[pos_]);
                bool ok = isHex ? IsAsciiHexDigit(cc) : (cc == '0' || cc == '1');
                if (!ok && !(rule_.numberAllowDigitSeparator && cc == '_')) break;
                ++pos_;
            }
        } else {
            while (pos_ < len_) {
                u8 cc = static_cast<u8>(code_[pos_]);
                if (!IsAsciiDigit(cc) && !(rule_.numberAllowDigitSeparator && cc == '_')) break;
                ++pos_;
            }
            // 小数点后继续吞数字(简单浮点数支持,不处理多个小数点的病态输入)。
            if (pos_ < len_ && code_[pos_] == '.' && pos_ + 1 < len_ && IsAsciiDigit(static_cast<u8>(code_[pos_ + 1]))) {
                ++pos_;
                while (pos_ < len_ && IsAsciiDigit(static_cast<u8>(code_[pos_]))) ++pos_;
            }
            // 指数部分 e/E[+-]digits。
            if (pos_ < len_ && (code_[pos_] == 'e' || code_[pos_] == 'E')) {
                u32 save = pos_;
                u32 p = pos_ + 1;
                if (p < len_ && (code_[p] == '+' || code_[p] == '-')) ++p;
                if (p < len_ && IsAsciiDigit(static_cast<u8>(code_[p]))) {
                    pos_ = p;
                    while (pos_ < len_ && IsAsciiDigit(static_cast<u8>(code_[pos_]))) ++pos_;
                } else {
                    pos_ = save; // 'e' 后面不是合法指数,回退,不吞掉这个字母
                }
            }
        }
        // 数字后缀:吞掉紧跟的字母/下划线(1u32、1L、1f 等),避免拆成两个 token。
        while (pos_ < len_ && (IsAsciiAlpha(static_cast<u8>(code_[pos_])) ||
                                (rule_.numberAllowDigitSeparator && code_[pos_] == '_'))) {
            ++pos_;
        }
        Emit(start, pos_, kTokenNumber);
    }

    // 标识符:扫完整段后按关键字表/内置类型表/结构化字面量表分类。
    void ScanIdentifier() {
        u32 start = pos_;
        ++pos_;
        while (pos_ < len_ && IsIdentCont(static_cast<u8>(code_[pos_]))) ++pos_;
        u32 wordLen = pos_ - start;
        const char* word = code_ + start;

        if (rule_.style == LanguageStyle::kStructured) {
            TokenType t = IsStructuredLiteral(word, wordLen) ? kTokenBuiltin : kTokenOther;
            Emit(start, pos_, t);
            return;
        }

        if (rule_.keywords && BinarySearchWord(rule_.keywords, rule_.keywordCount, word, wordLen)) {
            Emit(start, pos_, kTokenKeyword);
            return;
        }

        const char* const* builtinTable = nullptr;
        u32 builtinCount = 0;
        GetBuiltinTypeTable(rule_.id, &builtinTable, &builtinCount);
        if (builtinTable && BuiltinTableContains(builtinTable, builtinCount, word, wordLen)) {
            Emit(start, pos_, kTokenBuiltin);
            return;
        }

        Emit(start, pos_, kTokenOther);
    }

    // 标点/操作符:兜底分支,单字节成一个 token(不做多字符操作符合并,
    // 上色粒度足够,也避免引入组合规则的复杂度)。
    void ScanPunct() {
        u32 start = pos_;
        ++pos_;
        Emit(start, pos_, kTokenPunct);
    }

    const char* code_;
    u32 len_;
    const LanguageRule& rule_;
    Vec<Token>* out_;
    u32 pos_;
    u32 tokenCount_;
};

} // namespace

LexResult LexCodeBlock(StrSlice code, const LanguageRule& rule, Arena* arena) {
    Vec<Token> tokens(arena);
    Lexer lexer(code, rule, &tokens);
    bool truncated = lexer.Run();
    return LexResult{Span<Token>{tokens.Data(), tokens.Size()}, truncated};
}

} // namespace mdvn
