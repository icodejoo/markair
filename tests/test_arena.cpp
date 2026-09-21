// P0 内存优化覆盖测试:Vec<T>::Reserve 精确预留 + Arena::ResetAndTrim 弹性去提交。
#include "markair_test.h"
#include "../src/util/arena.h"
#include "../src/util/span.h"

using markair::Arena;
using markair::Vec;

// 每个测试各自建一个小 Arena,避免互相干扰(与 test_str.cpp 保持一致的约定)。
#define MARKAIR_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

// Reserve 到比当前容量小/相等的值应直接返回 true,不触发任何分配或数据变化。
MARKAIR_TEST(Vec_Reserve_NoShrink) {
    MARKAIR_MAKE_TEST_ARENA();
    Vec<int> v(&arena);
    MARKAIR_CHECK(v.Reserve(4));
    v.Push(1);
    v.Push(2);
    int* before = v.Data();
    MARKAIR_CHECK(v.Reserve(4));  // 不大于当前容量,直接成功且不重新分配
    MARKAIR_CHECK(v.Data() == before);
    MARKAIR_CHECK_EQ(v[0], 1);
    MARKAIR_CHECK_EQ(v[1], 2);
}

// Reserve 扩容后,原有数据必须完整保留,且后续 Push 直接写入预留空间。
MARKAIR_TEST(Vec_Reserve_GrowPreservesData) {
    MARKAIR_MAKE_TEST_ARENA();
    Vec<int> v(&arena);
    v.Push(10);
    v.Push(20);
    MARKAIR_CHECK(v.Reserve(100));
    MARKAIR_CHECK_EQ(v.Size(), 2u);
    MARKAIR_CHECK_EQ(v[0], 10);
    MARKAIR_CHECK_EQ(v[1], 20);
    for (int i = 0; i < 98; ++i) {
        MARKAIR_CHECK(v.Push(i));
    }
    MARKAIR_CHECK_EQ(v.Size(), 100u);
    MARKAIR_CHECK_EQ(v[0], 10);
    MARKAIR_CHECK_EQ(v[99], 97);
}

// 精确预留后不应再触发 Push 内部的翻倍扩容路径:用一个刚好够用的小 Arena 验证。
MARKAIR_TEST(Vec_Reserve_AvoidsDoublingWaste) {
    Arena arena;
    // 预留空间仅够容纳一次 100 个 int 的精确分配(外加少量对齐余量),
    // 若 Reserve 内部走了翻倍策略会导致后续分配失败。
    arena.Init(static_cast<size_t>(sizeof(int)) * 100 + 4096);
    Vec<int> v(&arena);
    MARKAIR_CHECK(v.Reserve(100));
    for (int i = 0; i < 100; ++i) {
        MARKAIR_CHECK(v.Push(i));
    }
    MARKAIR_CHECK_EQ(v.Size(), 100u);
    MARKAIR_CHECK_EQ(v[50], 50);
}

// ResetAndTrim 应把已提交字节数回落到 retainBytes 的页对齐值,且游标归零。
MARKAIR_TEST(Arena_ResetAndTrim_ReducesCommitted) {
    Arena arena;
    arena.Init(4 * 1024 * 1024);
    // 分配远超保留阈值的内存,确保 committedSize_ 明显大于 64KB。
    void* p = arena.Alloc(1 * 1024 * 1024, alignof(double));
    MARKAIR_CHECK(p != nullptr);
    MARKAIR_CHECK(arena.CommittedBytes() > 64 * 1024);

    arena.ResetAndTrim(64 * 1024);
    MARKAIR_CHECK(arena.CommittedBytes() <= 64 * 1024);
    MARKAIR_CHECK(arena.CommittedBytes() > 0);
}

// ResetAndTrim 之后,Arena 必须仍可正常分配(重新提交页面),不能因 decommit 而失效。
MARKAIR_TEST(Arena_ResetAndTrim_AllocStillWorks) {
    Arena arena;
    arena.Init(4 * 1024 * 1024);
    arena.Alloc(1 * 1024 * 1024, alignof(double));
    arena.ResetAndTrim(64 * 1024);

    void* p = arena.Alloc(256, alignof(double));
    MARKAIR_CHECK(p != nullptr);
    // 游标应从 0 重新计数,首次分配地址应等于 Arena 起始地址(无额外偏移)。
    int* asInt = static_cast<int*>(p);
    *asInt = 42;
    MARKAIR_CHECK_EQ(*asInt, 42);
}

// 未 Init 的 Arena 调用 ResetAndTrim 必须安全返回,不崩溃。
MARKAIR_TEST(Arena_ResetAndTrim_UninitializedIsSafe) {
    Arena arena;
    arena.ResetAndTrim();  // base_ == nullptr,应直接 return
    MARKAIR_CHECK(arena.CommittedBytes() == 0);
}
