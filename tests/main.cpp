// markair_tests.exe 入口:跑完全部通过 MARKAIR_TEST 注册的测试用例。
// 全过 exit(0),任意失败 exit(非零),方便 CI 判断。
#include "markair_test.h"

#include <objidl.h>
#include <gdiplus.h>

int main() {
    // 2026-09-22 GDI+ 迁移后,图片解码(image.cpp/svg_decoder.cpp)与渲染
    // (renderer.cpp)都要用到 Gdiplus::Bitmap/Graphics——不先 GdiplusStartup,
    // 所有 GDI+ 调用都会静默失败(GetLastStatus() 非 Ok),不是崩溃而是每个
    // 用例的解码结果状态都不对,容易误判成解码逻辑本身有 bug。整个进程只需
    // 启动一次,main() 结尾统一 Shutdown。
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    int testCount = 0;
    for (markair_test::TestCase* tc = markair_test::Head(); tc; tc = tc->next) {
        ++testCount;
        tc->fn();
    }

    int failCount = markair_test::FailCount();
    fprintf(stderr, "markair_tests: %d test(s) run, %d failure(s)\n", testCount, failCount);

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return failCount == 0 ? 0 : 1;
}
