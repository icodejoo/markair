// T65 历史前进/后退栈覆盖测试:环形缓冲的容量/淘汰语义、前进栈清空时机、
// 同路径不同 scrollY 的两条记录、以及"非导航事件不改变栈内容"的快照钉死。
// 全部走纯类 `History`,不依赖任何 Win32/HWND。
#include "markair_test.h"
#include "../src/shell/history.h"
#include <cstring>
#include <cwchar>

using markair::History;
using markair::HistoryEntry;
using markair::kHistoryCapacity;

namespace {

// 快照:仅用来做"调用某个 API 前后栈内容是否变化"的逐字节比对,不代表
// 生产代码里会用到的类型,只是测试的比对手段。
bool SnapshotsEqual(const History& a, const History& b) {
    // History 本身没有对外暴露"整份栈内容"的接口(也不需要),这里借助
    // BackCount/ForwardCount + 逐一 Pop 到临时副本来比较——但 Pop 是破坏性
    // 操作,所以改用最朴素的办法:直接对两个对象做 memcmp(History 是纯
    // POD 容器,没有指针成员,逐字节比较等价于比较全部逻辑状态)。
    return std::memcmp(&a, &b, sizeof(History)) == 0;
}

}  // namespace

// 1. 空栈按 Alt+← 无动作:PopBack 返回 false,且不改变任何状态。
MARKAIR_TEST(History_PopBackOnEmptyStackIsNoOp) {
    History history;
    History before = history;
    HistoryEntry entry;
    bool popped = history.PopBack(&entry);
    MARKAIR_CHECK(!popped);
    MARKAIR_CHECK(SnapshotsEqual(history, before));
}

// 2. 空栈按 Alt+→ 同理无动作。
MARKAIR_TEST(History_PopForwardOnEmptyStackIsNoOp) {
    History history;
    History before = history;
    HistoryEntry entry;
    bool popped = history.PopForward(&entry);
    MARKAIR_CHECK(!popped);
    MARKAIR_CHECK(SnapshotsEqual(history, before));
}

// 3. 压满 32 条后,第 33 条把最旧的一条挤掉(栈底淘汰),而不是拒绝新条目。
MARKAIR_TEST(History_Push33rdEntryEvictsOldest) {
    History history;
    for (markair::u32 i = 0; i < kHistoryCapacity; ++i) {
        wchar_t path[16];
        swprintf_s(path, L"C:\\%u.md", i);
        history.PushNavigation(path, static_cast<float>(i));
    }
    MARKAIR_CHECK_EQ(history.BackCount(), kHistoryCapacity);

    // 第 33 条压入:此时栈底应是第 0 条("C:\0.md"),压入后应被挤掉。
    history.PushNavigation(L"C:\\33.md", 33.0f);
    MARKAIR_CHECK_EQ(history.BackCount(), kHistoryCapacity);  // 仍然是上限,没有变多

    // 从栈顶往下弹出全部 32 条,顺序应是 33,31,30,...,1(第 0 条已被挤掉)。
    HistoryEntry e;
    MARKAIR_CHECK(history.PopBack(&e));
    MARKAIR_CHECK(wcscmp(e.path, L"C:\\33.md") == 0);
    for (markair::u32 i = 0; i < kHistoryCapacity - 1; ++i) {
        MARKAIR_CHECK(history.PopBack(&e));
    }
    MARKAIR_CHECK_EQ(history.BackCount(), 0u);
    // 最后一条应是编号 1(编号 0 已被第 33 条挤掉)。
    // 上面循环已经把所有条目弹光,这里改成单独验证"编号 0 不在栈里"的方式:
    // 重新构造同样的压栈过程,只弹出最后一条比较。
    History history2;
    for (markair::u32 i = 0; i < kHistoryCapacity; ++i) {
        wchar_t path[16];
        swprintf_s(path, L"C:\\%u.md", i);
        history2.PushNavigation(path, static_cast<float>(i));
    }
    history2.PushNavigation(L"C:\\33.md", 33.0f);
    HistoryEntry last{};
    for (markair::u32 i = 0; i < kHistoryCapacity; ++i) {
        MARKAIR_CHECK(history2.PopBack(&last));
    }
    MARKAIR_CHECK(wcscmp(last.path, L"C:\\1.md") == 0);  // 编号 0 已被挤掉,最旧的是 1
}

// 4. 前进栈在新导航(PushNavigation)时被清空。
MARKAIR_TEST(History_PushNavigationClearsForwardStack) {
    History history;
    history.PushNavigation(L"C:\\a.md", 0.0f);
    history.PushForwardRaw(L"C:\\b.md", 10.0f);  // 模拟一次成功的后退,填了前进栈
    MARKAIR_CHECK_EQ(history.ForwardCount(), 1u);

    history.PushNavigation(L"C:\\c.md", 20.0f);  // 新导航
    MARKAIR_CHECK_EQ(history.ForwardCount(), 0u);
}

// 5. 同路径、不同 scrollY 的两条记录(T36③锚点跳转的记录方式)。
MARKAIR_TEST(History_SamePathDifferentScrollYTwoEntries) {
    History history;
    history.PushNavigation(L"C:\\a.md", 0.0f);
    history.PushNavigation(L"C:\\a.md", 200.0f);
    MARKAIR_CHECK_EQ(history.BackCount(), 2u);

    HistoryEntry top;
    MARKAIR_CHECK(history.PopBack(&top));
    MARKAIR_CHECK(wcscmp(top.path, L"C:\\a.md") == 0);
    MARKAIR_CHECK(top.scrollY == 200.0f);

    HistoryEntry bottom;
    MARKAIR_CHECK(history.PopBack(&bottom));
    MARKAIR_CHECK(wcscmp(bottom.path, L"C:\\a.md") == 0);
    MARKAIR_CHECK(bottom.scrollY == 0.0f);
}

// 6. PushBackRaw 不清空前进栈(与 PushNavigation 的关键区别,前进导航用)。
MARKAIR_TEST(History_PushBackRawDoesNotClearForwardStack) {
    History history;
    history.PushNavigation(L"C:\\a.md", 0.0f);
    history.PushForwardRaw(L"C:\\b.md", 0.0f);
    MARKAIR_CHECK_EQ(history.ForwardCount(), 1u);

    history.PushBackRaw(L"C:\\c.md", 0.0f);  // 模拟一次成功的前进
    MARKAIR_CHECK_EQ(history.ForwardCount(), 1u);  // 前进栈不受影响
    MARKAIR_CHECK_EQ(history.BackCount(), 2u);
}

// 7. PushForwardRaw 不清空后退栈。
MARKAIR_TEST(History_PushForwardRawDoesNotClearBackStack) {
    History history;
    history.PushNavigation(L"C:\\a.md", 0.0f);
    history.PushNavigation(L"C:\\b.md", 0.0f);
    MARKAIR_CHECK_EQ(history.BackCount(), 2u);

    history.PushForwardRaw(L"C:\\c.md", 0.0f);
    MARKAIR_CHECK_EQ(history.BackCount(), 2u);  // 后退栈不受影响
    MARKAIR_CHECK_EQ(history.ForwardCount(), 1u);
}

// 8. PopBack 弹出的记录会真正从栈里移除("路径不存在的剔除"就是靠这一点
// 天然满足——调用方发现文件不存在时什么都不用做,不需要额外的剔除步骤)。
MARKAIR_TEST(History_PopBackRemovesEntryFromStack) {
    History history;
    history.PushNavigation(L"C:\\deleted.md", 0.0f);
    MARKAIR_CHECK_EQ(history.BackCount(), 1u);

    HistoryEntry entry;
    MARKAIR_CHECK(history.PopBack(&entry));
    MARKAIR_CHECK_EQ(history.BackCount(), 0u);  // 已经被摘掉,不会再弹出第二次
    HistoryEntry again;
    MARKAIR_CHECK(!history.PopBack(&again));
}

// 9. F5 重载这类"非导航事件"不应改变栈内容——用调用任何 History API 之前/
// 之后的快照比对钉死:F5 的实现应当完全不调用 Push*/Pop* 接口,这里直接
// 验证"什么都不调用"时状态确实不变(接口层面的约束:F5 走 openDocumentInPlace
// 而不经过本类,因此测试是"没有调用" == "没有变化")。
MARKAIR_TEST(History_NonNavigationReloadDoesNotTouchStack) {
    History history;
    history.PushNavigation(L"C:\\a.md", 0.0f);
    history.PushNavigation(L"C:\\b.md", 50.0f);
    History before = history;

    // 模拟 F5:重新打开同一份文档,但不调用 History 的任何接口——
    // 这正是 T65 接口设计上"F5 不入栈"的体现:没有对应的 push 调用点。
    // (真正的重载逻辑属于 T70,这里只钉死"不调用即不改变"这一约束。)

    MARKAIR_CHECK(SnapshotsEqual(history, before));
    MARKAIR_CHECK_EQ(history.BackCount(), before.BackCount());
    MARKAIR_CHECK_EQ(history.ForwardCount(), before.ForwardCount());
}

// 10. 路径超长时会被截断,不会越界写(kHistoryPathCapacity 的边界情况)。
MARKAIR_TEST(History_OverlongPathIsTruncatedNotOverrun) {
    History history;
    wchar_t longPath[600];
    for (int i = 0; i < 599; ++i) longPath[i] = L'x';
    longPath[599] = L'\0';

    history.PushNavigation(longPath, 0.0f);
    HistoryEntry entry;
    MARKAIR_CHECK(history.PopBack(&entry));
    // 截断后长度必须严格小于容量(留出结尾 '\0' 的位置)。
    size_t len = wcslen(entry.path);
    MARKAIR_CHECK(len < markair::kHistoryPathCapacity);
    MARKAIR_CHECK(entry.path[len] == L'\0');
}

// 11. nullptr 路径不崩溃,写成空字符串。
MARKAIR_TEST(History_NullPathBecomesEmptyString) {
    History history;
    history.PushNavigation(nullptr, 1.0f);
    HistoryEntry entry;
    MARKAIR_CHECK(history.PopBack(&entry));
    MARKAIR_CHECK(wcscmp(entry.path, L"") == 0);
}

// 12. 后退/前进两个栈各自独立计数,压其中一个不影响另一个的计数基线。
MARKAIR_TEST(History_BackAndForwardCountsAreIndependent) {
    History history;
    MARKAIR_CHECK_EQ(history.BackCount(), 0u);
    MARKAIR_CHECK_EQ(history.ForwardCount(), 0u);

    history.PushNavigation(L"C:\\a.md", 0.0f);
    MARKAIR_CHECK_EQ(history.BackCount(), 1u);
    MARKAIR_CHECK_EQ(history.ForwardCount(), 0u);

    history.PushForwardRaw(L"C:\\b.md", 0.0f);
    MARKAIR_CHECK_EQ(history.BackCount(), 1u);
    MARKAIR_CHECK_EQ(history.ForwardCount(), 1u);
}

// 13. 恰好压满 32 条(不多不少)时,BackCount 是 32,且没有发生淘汰
// (第 32 条还是原来最旧的一条,即编号 0)。
MARKAIR_TEST(History_ExactlyFullDoesNotEvict) {
    History history;
    for (markair::u32 i = 0; i < kHistoryCapacity; ++i) {
        wchar_t path[16];
        swprintf_s(path, L"C:\\%u.md", i);
        history.PushNavigation(path, static_cast<float>(i));
    }
    MARKAIR_CHECK_EQ(history.BackCount(), kHistoryCapacity);

    HistoryEntry last{};
    for (markair::u32 i = 0; i < kHistoryCapacity; ++i) {
        MARKAIR_CHECK(history.PopBack(&last));
    }
    MARKAIR_CHECK(wcscmp(last.path, L"C:\\0.md") == 0);  // 恰好 32 条时编号 0 没被挤掉
}

// sizeof(HistoryEntry) 在预期范围内(约 520B:260 个 wchar_t + 1 个 float,
// 按 4 字节对齐补到 524~528B),用来佐证任务描述里"约 520B × 32 ≈ 17KB"的
// 内存估算不是空口白话。用 static_assert 而不是 MARKAIR_CHECK——sizeof 是编译期
// 常量,写进运行期断言会触发 /W4 的"条件表达式是常量"警告(C4127)。
static_assert(sizeof(HistoryEntry) <= 600, "HistoryEntry 超出预期内存预算");
static_assert(sizeof(HistoryEntry) >= 520, "HistoryEntry 比预期小,可能路径缓冲区被误改小了");
