// mdvn 的 md4c SAX 回调 -> 文档模型转换器(T8)。
#pragma once

#include "model.h"

namespace mdvn {

// 节点总数上限(Block + Inline 合计)。选取几十万级别:M0 目标文档量级在
// 百 KB~10MB(见 02-tech-stack.md §6 的 BENCH-A/10MB 场景),正常 Markdown
// 文档的节点数远低于此值;设置该上限只是为了兜底"畸形/恶意超大输入"场景,
// 避免解析过程无界占用 Arena 内存。
constexpr u32 kMaxDocumentNodeCount = 200000;

// 嵌套深度上限(块级 + 行内 span 合计)。正常 Markdown 文档的引用/列表嵌套
// 极少超过个位数到十位数层级;取几十层(64)既给正常文档充分余量,又能在
// md4c 回调栈(每层几十到上百字节栈帧)可能出现的恶意超深嵌套输入下,
// 把栈使用量限制在几 KB 级,远低于线程默认栈大小,避免栈溢出崩溃。
constexpr u32 kMaxNestingDepth = 64;

/**
 * 把一段 Markdown 源文本解析为文档模型(Block/Inline 紧凑数组)。
 *
 * 安全边界:节点总数上限 kMaxDocumentNodeCount、嵌套深度上限
 * kMaxNestingDepth,超限时安全截断(通过令 md4c 回调返回非零值中止解析),
 * 不会崩溃、不会无限递归、不会无界占用内存;截断时返回文档的 truncated
 * 字段为 true。M1 启用的 md4c flags:CommonMark + MD_FLAG_NOHTML(HTML 按
 * 普通文本处理,裁决 #1)+ 表格/删除线/任务列表/宽松自动链接/脚注(GFM 子集)。
 * 明确不启用 MD_FLAG_LATEXMATHSPANS([裁决 #2])及 wiki 链接/下划线/剧透/
 * 上下标/警示块/高亮/插入这组扩展。
 *
 * @param source 原始 Markdown 字节(零拷贝引用,调用方需保证其生命周期
 *               覆盖返回 Document 的使用期 —— Document::inlines 里的
 *               文本切片直接指向这段内存,不做复制)。
 * @param arena 存放 Block/Inline 数组的 Arena,调用方负责其生命周期。
 * @return 解析得到的文档模型;输入为空、Arena 分配失败或触发截断时,
 *         仍返回可用的(可能为空或被截断的)文档,不返回错误码、不抛异常。
 * @example
 *   mdvn::Arena arena;
 *   arena.Init(4 * 1024 * 1024);
 *   const char* text = "# Hello\n";
 *   mdvn::Document doc = mdvn::ParseMarkdown(mdvn::StrSlice{text, 8}, &arena);
 */
Document ParseMarkdown(StrSlice source, Arena* arena);

} // namespace mdvn
