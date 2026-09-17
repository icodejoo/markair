// mdvn 的块级 detail 侧表:表格/单元格对齐/任务列表/脚注定义的按类型附加信息。
// 不依赖 md4c.h,md4c 的枚举/结构体到这里的类型转换统一在 parser.cpp 里完成。
#pragma once

#include "../util/str.h"
#include "../util/types.h"

namespace mdvn {

/** 单元格对齐方式,对应 md4c 的 MD_ALIGN(转换发生在 parser.cpp)。 */
enum class CellAlign : u8 {
    Default, // 未指定对齐
    Left,
    Center,
    Right,
};

/** 表格块(BlockType::Table)的附加信息。 */
struct TableDetail {
    u32 colCount;      // 列数
    u32 headRowCount;  // 表头行数(目前恒为 1)
    u32 bodyRowCount;  // 表体行数
};

/** 表头/表格单元格(TableHeadCell/TableCell)的附加信息。 */
struct CellDetail {
    CellAlign align; // 该单元格的对齐方式
};

/** 列表项(ListItem)的任务列表附加信息,仅 is_task 为真时才会分配。 */
struct ListItemDetail {
    bool isTask;      // 是否为任务列表项([ ] / [x])
    bool taskChecked; // 任务是否已勾选
};

/**
 * 有序列表容器(BlockType::OrderedList)的附加信息,取自 md4c 的
 * MD_BLOCK_OL_DETAIL——起始序号(<ol start="N"> 或 "3. foo" 这种写法里的 3)
 * 与序号分隔符,布局阶段据此从 start 开始给每个直属 ListItem 递增编号
 * (T44,见 layout.cpp::LayoutSubtree),不需要在 ListItem 自身另存序号。
 */
struct OrderedListDetail {
    u32 start;          // 起始序号,默认 1
    char markDelimiter; // 序号分隔符,如 '.' 或 ')'
};

/** 脚注定义块(FootnoteDef)的附加信息。 */
struct FootnoteDetail {
    u32 id;         // 1-based 脚注编号,与引用处 Inline::linkTargetIdx 配对
    u32 refCount;   // 该脚注被引用的次数
    StrSlice label; // 原始标签文本,如 "1" 或 "note"
};

} // namespace mdvn
