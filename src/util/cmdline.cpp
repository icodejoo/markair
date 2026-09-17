#include "cmdline.h"

namespace mdvn {

namespace {

// 命令行里的分隔空白:CRT 规则只认空格与制表符,其余空白字符按普通字符处理。
bool IsCmdWhitespace(wchar_t c) { return c == L' ' || c == L'\t'; }

// 把已拼好的一个参数缓冲区补 '\0' 后收尾:取出裸指针存入 argv 数组。
// buf 之后不再被复用(每个参数各自一个 Vec<wchar_t>),补终止符不会覆盖已提交内容。
void CommitArg(Vec<wchar_t>* buf, Vec<wchar_t*>* argv) {
    buf->Push(L'\0');
    argv->Push(buf->Data());
}

}  // namespace

Vec<wchar_t*> ParseCommandLine(const wchar_t* commandLine, Arena* arena) {
    Vec<wchar_t*> argv(arena);
    if (!commandLine) return argv;

    const wchar_t* p = commandLine;

    // ① argv[0]:程序路径。官方文档明确规定它是特例——双引号只用来把内含
    // 空白的路径框成一个参数,不套用后面反斜杠/引号成对转义的规则。
    while (IsCmdWhitespace(*p)) ++p;
    {
        Vec<wchar_t> buf(arena);
        bool inQuotes = false;
        while (*p != L'\0' && (inQuotes || !IsCmdWhitespace(*p))) {
            if (*p == L'"') {
                inQuotes = !inQuotes;
                ++p;
                continue;
            }
            buf.Push(*p);
            ++p;
        }
        CommitArg(&buf, &argv);
    }

    // ② 其余参数:标准反斜杠/双引号规则。
    for (;;) {
        while (IsCmdWhitespace(*p)) ++p;
        if (*p == L'\0') break;

        Vec<wchar_t> buf(arena);
        bool inQuotes = false;
        while (*p != L'\0' && (inQuotes || !IsCmdWhitespace(*p))) {
            if (*p == L'\\') {
                // 先数清这一串反斜杠的个数,再看后面是不是紧跟双引号。
                const wchar_t* q = p;
                u32 backslashCount = 0;
                while (*q == L'\\') {
                    ++backslashCount;
                    ++q;
                }
                if (*q == L'"') {
                    for (u32 i = 0; i < backslashCount / 2; ++i) buf.Push(L'\\');
                    if (backslashCount % 2 == 1) {
                        // 奇数个:半数反斜杠 + 这一个双引号被转义成字面双引号,
                        // 不切换 inQuotes,直接跳过这个双引号继续扫描。
                        buf.Push(L'"');
                        p = q + 1;
                        continue;
                    }
                    // 偶数个:反斜杠已按对写出,这个双引号是"真引号"——
                    // 让 p 停在双引号处,落到下面的双引号计数逻辑统一处理
                    // (处理它以及紧随其后可能出现的连续双引号)。
                    p = q;
                } else {
                    // 后面不是双引号,反斜杠全部按字面输出。
                    for (u32 i = 0; i < backslashCount; ++i) buf.Push(L'\\');
                    p = q;
                    continue;
                }
            }
            if (*p == L'"') {
                // 数一串连续双引号:每两个输出一个字面双引号,奇偶决定
                // 是否切换 inQuotes(这就是"一对双引号在引号内是转义双引号"
                // 规则的通用形式,官方文档表格里 a"b"" c d -> ab" c d 的
                // 那一行正是这套计数逻辑的结果)。
                const wchar_t* q = p;
                u32 quoteCount = 0;
                while (*q == L'"') {
                    ++quoteCount;
                    ++q;
                }
                for (u32 i = 0; i < quoteCount / 2; ++i) buf.Push(L'"');
                if (quoteCount % 2 == 1) inQuotes = !inQuotes;
                p = q;
                continue;
            }
            buf.Push(*p);
            ++p;
        }
        CommitArg(&buf, &argv);
    }

    return argv;
}

}  // namespace mdvn
