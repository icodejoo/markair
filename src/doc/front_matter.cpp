// SkipFrontMatter 实现:见 front_matter.h。
#include "front_matter.h"

namespace mdvn {

namespace {

// 一行的扫描结果:contentEnd 是行内容(不含行终止符)的结束偏移,
// nextStart 是下一行起始偏移(跳过 \r\n / \n / 孤立 \r 后)。
struct LineInfo {
    u32 contentEnd;
    u32 nextStart;
};

// 从 start 开始扫描一行,兼容 "\n"、"\r\n" 两种行尾(孤立 "\r" 也按行尾处理,
// 兼容极少数老式 Mac 换行,不会因此误判)。
LineInfo ScanLine(const char* data, u32 len, u32 start) {
    u32 i = start;
    while (i < len && data[i] != '\n' && data[i] != '\r') ++i;
    u32 contentEnd = i;
    if (i < len && data[i] == '\r') ++i;
    if (i < len && data[i] == '\n') ++i;
    return LineInfo{contentEnd, i};
}

// 判断 [start, end) 是否恰好是 3 字节的 s(用于匹配 "---" / "..."）。
bool IsExactly3(const char* data, u32 start, u32 end, char c0, char c1, char c2) {
    return end - start == 3 && data[start] == c0 && data[start + 1] == c1 && data[start + 2] == c2;
}

} // namespace

StrSlice SkipFrontMatter(StrSlice source) {
    if (source.data == nullptr || source.len == 0) return source;

    // 首行必须恰好是单独一行的 "---",否则不是 front matter,原样返回。
    LineInfo first = ScanLine(source.data, source.len, 0);
    if (!IsExactly3(source.data, 0, first.contentEnd, '-', '-', '-')) {
        return source;
    }

    // 从第二行起找闭合分隔符("---" 或 "...")。找不到(含到达 EOF 仍未找到)
    // 就原样返回整个输入,不吃掉正文——避免把"只是一条分割线"的普通文档
    // 误判成 front matter。
    u32 pos = first.nextStart;
    while (pos < source.len) {
        LineInfo li = ScanLine(source.data, source.len, pos);
        bool closed = IsExactly3(source.data, pos, li.contentEnd, '-', '-', '-') ||
                      IsExactly3(source.data, pos, li.contentEnd, '.', '.', '.');
        if (closed) {
            return StrSlice{source.data + li.nextStart, source.len - li.nextStart};
        }
        if (li.nextStart <= pos) break; // 防御:理论上不会发生,避免死循环
        pos = li.nextStart;
    }
    return source;
}

} // namespace mdvn
