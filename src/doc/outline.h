// markair 大纲数据提取(T62):从已解析好的 Document 里抽出标题块下标 + 层级,
// 供 T63 的侧栏 UI 使用。
//
// 与虚拟化不冲突(架构 §5 明确的点,写死在这里避免后续维护者误判):
// 架构 §5 "可见 ± 1 屏"约束的是 IDWriteTextLayout(排版对象)的生成,
// 而大纲只需要**文档模型层**的标题块下标与层级 —— 解析阶段 md4c 回调
// 转换完成后 Document::blocks 本来就是全量、紧凑存放在内存里的数组,
// 提取只是在这个已有数组上做一次 O(块数) 的线性扫描,不触发任何布局、
// 不创建任何 COM 对象(IDWriteTextLayout/IDWriteFactory 等一律不涉及)。
//
// 文本不复制:标题的可显示文本已经在对应块的 Inline 零拷贝切片里
// (Document::inlines + Document::source),大纲这里只存块下标,渲染
// 侧栏文字时现取现用,不额外占用/复制任何字符串内存。
#pragma once

#include "../util/arena.h"
#include "../util/span.h"
#include "../util/types.h"
#include "model.h"

namespace markair {

/**
 * 一条大纲条目:指向一个标题块,8 字节/条(u32 + u8,末尾 3 字节对齐填充)。
 */
struct OutlineItem {
    u32 blockIdx; // 该标题在 Document::blocks 里的下标
    u8 level;     // 标题级别 1-6,与 Block::level 一致
};

/**
 * 在整份文档上提取大纲(全部 Heading 块),结果按块下标升序追加进 out。
 *
 * 只做一次 O(Document::blocks 长度) 的线性扫描,不做任何排版/COM 调用,
 * 侧栏关闭时调用方不应调用本函数(保持"关闭时开销为 0")。
 *
 * @param doc 已解析好的文档模型。
 * @param out 输出的大纲数组,非空;函数只追加,不清空(调用方负责重新绑定 Arena)。
 * @return 本次提取到的标题条数。
 * @example
 *   markair::Vec<markair::OutlineItem> items(&arena);
 *   u32 n = markair::ExtractOutline(doc, &items);
 */
u32 ExtractOutline(const Document& doc, Vec<OutlineItem>* out);

} // namespace markair
