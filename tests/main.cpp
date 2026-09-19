// markair_tests.exe 入口:跑完全部通过 MARKAIR_TEST 注册的测试用例。
// 全过 exit(0),任意失败 exit(非零),方便 CI 判断。
#include "markair_test.h"

int main() {
    int testCount = 0;
    for (markair_test::TestCase* tc = markair_test::Head(); tc; tc = tc->next) {
        ++testCount;
        tc->fn();
    }

    int failCount = markair_test::FailCount();
    fprintf(stderr, "markair_tests: %d test(s) run, %d failure(s)\n", testCount, failCount);
    return failCount == 0 ? 0 : 1;
}
