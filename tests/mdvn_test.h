// mdvn 极简测试框架:自写约 80 行,不引入 gtest/catch2([裁决 #11])。
//
// !!! 重要提示 !!!
// 本文件用"静态对象在全局构造函数里做实际工作"(把测试函数注册进全局链表)
// 的手法来实现 MDVN_TEST 自动注册。这正是 docs/coding-rules.md 第 6 条
// 明令禁止的"有副作用的全局构造函数"模式。
//
// 这里是该规则唯一允许的例外场景,原因是:本文件只被编译进独立的
// mdvn_tests.exe 测试可执行文件,不会链接进 mdvn.exe 主程序,不影响
// 主程序的启动时间/体积/确定性。
//
// 正式产品代码(app/util/doc/text/layout/render/shell 等目录)严禁模仿
// 这种写法,以免被误用到主程序里。
#pragma once

#include <cstdio>
#include <cstring>

namespace mdvn_test {

// 单个测试用例:函数指针 + 名字,构成链表节点。
struct TestCase {
    const char* name;
    void (*fn)();
    TestCase* next;
};

// 全局测试失败计数,MDVN_CHECK* 失败时自增。
inline int& FailCount() {
    static int count = 0;
    return count;
}

// 全局测试链表头(唯一允许的"有副作用全局状态",见文件头注释)。
inline TestCase*& Head() {
    static TestCase* head = nullptr;
    return head;
}

// 把一个测试用例挂到全局链表头部。
inline void Register(TestCase* tc) {
    tc->next = Head();
    Head() = tc;
}

// 用于注册的辅助对象:构造时把测试挂进链表。
struct Registrar {
    Registrar(TestCase* tc) { Register(tc); }
};

} // namespace mdvn_test

// 定义一个测试函数并自动注册。用法:
//   MDVN_TEST(ArenaBasicAlloc) { MDVN_CHECK(1 + 1 == 2); }
#define MDVN_TEST(name)                                                      \
    static void mdvn_test_fn_##name();                                      \
    static ::mdvn_test::TestCase mdvn_test_case_##name{#name, mdvn_test_fn_##name, nullptr}; \
    static ::mdvn_test::Registrar mdvn_test_reg_##name(&mdvn_test_case_##name); \
    static void mdvn_test_fn_##name()

// 断言失败时打印文件名/行号/表达式到 stderr,并计入失败数,不抛异常。
#define MDVN_CHECK(cond)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                      \
            fprintf(stderr, "%s:%d: MDVN_CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ::mdvn_test::FailCount()++;                                     \
        }                                                                    \
    } while (0)

// 数值/可比较对象相等断言,失败时同样打印上下文。
#define MDVN_CHECK_EQ(a, b)                                                   \
    do {                                                                     \
        if (!((a) == (b))) {                                                \
            fprintf(stderr, "%s:%d: MDVN_CHECK_EQ failed: %s == %s\n", __FILE__, __LINE__, #a, #b); \
            ::mdvn_test::FailCount()++;                                     \
        }                                                                    \
    } while (0)

// C 字符串内容相等断言(strcmp == 0),失败时打印上下文。
#define MDVN_CHECK_STREQ(a, b)                                               \
    do {                                                                     \
        if (strcmp((a), (b)) != 0) {                                        \
            fprintf(stderr, "%s:%d: MDVN_CHECK_STREQ failed: %s vs %s\n", __FILE__, __LINE__, (a), (b)); \
            ::mdvn_test::FailCount()++;                                     \
        }                                                                    \
    } while (0)
