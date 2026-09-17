// mdvn_tests.exe 入口:跑完全部通过 MDVN_TEST 注册的测试用例。
// 全过 exit(0),任意失败 exit(非零),方便 CI 判断。
#include "mdvn_test.h"

int main() {
    int testCount = 0;
    for (mdvn_test::TestCase* tc = mdvn_test::Head(); tc; tc = tc->next) {
        ++testCount;
        tc->fn();
    }

    int failCount = mdvn_test::FailCount();
    fprintf(stderr, "mdvn_tests: %d test(s) run, %d failure(s)\n", testCount, failCount);
    return failCount == 0 ? 0 : 1;
}
