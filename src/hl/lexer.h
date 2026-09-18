// mdvn 代码高亮词法器(T51):把一段代码文本切分成 Token 数组,供渲染层
// (T53)按类型上色。纯函数模块,手写状态机,逐字节扫描 UTF-8——不链接任何
// Win32/D2D/DWrite,可脱离图形环境单测。
//
// 与 T52(languages.h)的分工:languages.h 只回答"这个语言有哪些关键字/
// 注释定界符/字符串定界符",本文件负责真正的扫描,消费 LanguageRule 产出
// Token 数组。
#pragma once

#include "../util/arena.h"
#include "../util/span.h"
#include "../util/str.h"
#include "../util/types.h"
#include "languages.h"

namespace mdvn {

/**
 * Token 类型,固定 7 类(不超过 8 类上限),取值刚好放进 u8。
 * 结构化语言(JSON/YAML)的 true/false/null 等字面量归入 kTokenBuiltin。
 */
enum TokenType : u8 {
    kTokenKeyword = 0,  // 关键字(语言保留字)
    kTokenString,        // 字符串(含原始字符串/模板字符串)
    kTokenNumber,        // 数字字面量
    kTokenComment,        // 行注释/块注释
    kTokenPunct,          // 标点/操作符
    kTokenBuiltin,        // 内置类型名/内置标识符/结构化字面量(true/false/null)
    kTokenOther,          // 其他(默认,含普通标识符)
};

/**
 * 单个 Token:偏移 + 长度定位到原文本的字节区间,type 标识着色类别。
 * 要求整体紧凑,数组每元素 12 字节——u32+u32+u8 在默认对齐下已经是
 * 12 字节(9 字节数据按 4 字节对齐补齐到 12),用 static_assert 兜底校验,
 * 不依赖隐式假设。
 */
struct Token {
    u32 offset;  // 相对代码块起始的字节偏移
    u32 len;     // 字节长度(注意是字节数,不是 UTF-8 字符数)
    u8 type;     // TokenType 取值
};

static_assert(sizeof(Token) == 12, "Token 必须保持 12 字节紧凑布局");

/**
 * 单个代码块允许产出的最大 Token 数。选取 4000 的理由:按平均每个 Token
 * 5~6 字节估算,可覆盖约 20~24KB 的代码块(远超常见 Markdown 代码块体量的
 * 99% 分位),同时 4000*12B=48KB 的 Token 数组大小对 scratch arena 而言
 * 可忽略;超过此上限后剩余部分优雅降级为无高亮(不截断原文本,只是不再
 * 产出 Token),避免病态输入(如单行超长 minified 代码)让 Token 数组无界
 * 增长。
 */
constexpr u32 kMaxTokensPerBlock = 4000;

/**
 * 词法扫描结果:Token 数组 + 是否因触达 kMaxTokensPerBlock 而被截断。
 * `truncated` 为 true 时,tokens 只覆盖代码块前缀,原文本仍然完整——
 * 渲染层应当照常显示全部原文,只是尾部不再有语法着色。
 */
struct LexResult {
    Span<Token> tokens;  // 产出的 Token 数组,顺序与原文本偏移递增一致
    bool truncated;      // 是否因达到 kMaxTokensPerBlock 而提前停止产出 Token
};

/**
 * 对一段代码文本做词法扫描,按字节手写状态机逐字节扫描 UTF-8(非 ASCII
 * 字节即最高位为 1 的字节一律当作标识符/普通字符处理,不参与状态转移),
 * 纯函数、不抛异常、不越界读写,遇到未闭合字符串/注释时扫到文本末尾即
 * 结束。
 *
 * 调用约定:Token 数组应分配在一块独立于文档 arena 的、可整体 Reset 的
 * 高亮 scratch arena 上(token 生命周期只到代码块滚出可见范围,而不是
 * 整篇文档的生命周期)——`arena` 参数由调用方传入这样一块 arena,本函数
 * 不关心也不校验其来源。
 *
 * @param code 待扫描的代码文本(不含围栏标记本身)。
 * @param rule 该代码块对应的语言规则(来自 GetLanguageRule)。
 * @param arena Token 数组的分配来源,建议使用独立的高亮 scratch arena。
 * @return 扫描结果;若 arena 分配失败,tokens.len 可能小于实际应有数量,
 *         但绝不会返回悬空/越界指针。
 * @example
 *   mdvn::Arena hlArena;
 *   hlArena.Init(1 * 1024 * 1024);
 *   const mdvn::LanguageRule& rule = mdvn::GetLanguageRule(mdvn::kLanguageCpp);
 *   mdvn::LexResult result = mdvn::LexCodeBlock(mdvn::StrSlice{"int x;", 6}, rule, &hlArena);
 */
LexResult LexCodeBlock(StrSlice code, const LanguageRule& rule, Arena* arena);

} // namespace mdvn
