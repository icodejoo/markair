// "打开文档"新开进程(新需求)覆盖测试:只测 BuildLaunchCommandLine 这个纯
// 字符串拼接函数(命令行引号转义),不涉及真实 IFileOpenDialog/CreateProcessW
// (那部分需要真实 UI/进程,已在人工回归里验证,见任务记录)。
#include "markair_test.h"
#include "../src/shell/open_dialog.h"

using markair::BuildLaunchCommandLine;

// 不含空格的路径:两段各自加一对引号,中间一个空格分隔。
MARKAIR_TEST(OpenDialog_BuildLaunchCommandLine_NoSpaces) {
    wchar_t buf[128];
    bool ok = BuildLaunchCommandLine(L"C:\\markair.exe", L"C:\\docs\\readme.md", buf, 128);
    MARKAIR_CHECK(ok);
    MARKAIR_CHECK(wcscmp(buf, L"\"C:\\markair.exe\" \"C:\\docs\\readme.md\"") == 0);
}

// 含空格的路径:引号把整段包起来,空格不会被 CreateProcessW 解析成参数分隔符。
MARKAIR_TEST(OpenDialog_BuildLaunchCommandLine_PathWithSpaces) {
    wchar_t buf[128];
    bool ok = BuildLaunchCommandLine(L"C:\\Program Files\\markair\\markair.exe",
                                      L"C:\\my docs\\a b.md", buf, 128);
    MARKAIR_CHECK(ok);
    MARKAIR_CHECK(wcscmp(buf, L"\"C:\\Program Files\\markair\\markair.exe\" \"C:\\my docs\\a b.md\"") == 0);
}

// 空参数(exePath/filePath 任一为空指针或空串)应失败,不产出半截结果。
MARKAIR_TEST(OpenDialog_BuildLaunchCommandLine_EmptyInputsFail) {
    wchar_t buf[128];
    MARKAIR_CHECK(!BuildLaunchCommandLine(nullptr, L"C:\\a.md", buf, 128));
    MARKAIR_CHECK(!BuildLaunchCommandLine(L"C:\\markair.exe", nullptr, buf, 128));
    MARKAIR_CHECK(!BuildLaunchCommandLine(L"", L"C:\\a.md", buf, 128));
    MARKAIR_CHECK(!BuildLaunchCommandLine(L"C:\\markair.exe", L"", buf, 128));
}

// 缓冲不够大时应失败返回,不越界写。
MARKAIR_TEST(OpenDialog_BuildLaunchCommandLine_BufferTooSmallFails) {
    wchar_t buf[8];
    MARKAIR_CHECK(!BuildLaunchCommandLine(L"C:\\markair.exe", L"C:\\docs\\readme.md", buf, 8));
}
