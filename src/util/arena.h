// markair 的内存分配策略：不用 new/malloc 做长生命周期分配，统一走 Arena。
#pragma once

#include <cstddef>

namespace markair {

/**
 * 基于 VirtualAlloc 的线性内存池。
 *
 * 构造时预留一段虚拟地址空间，按需提交物理页；`Alloc` 只前移游标，
 * 不做单块释放，靠 `Reset` 整体复位。适合"解析一份文档、渲染完整体丢弃"
 * 这种生命周期，避免大量小对象走系统堆造成的碎片与开销。
 *
 * @example
 *   markair::Arena arena;
 *   if (!arena.Init(64 * 1024 * 1024)) { // 预留 64MB 地址空间
 *       // 处理初始化失败
 *   }
 *   void* p = arena.Alloc(128, alignof(double));
 *   arena.Reset(); // 复用同一块地址空间处理下一份文档
 */
class Arena {
public:
    // 构造一个未初始化的 Arena，需调用 Init 后才能分配。
    Arena();

    // 析构时释放预留的虚拟地址空间（若已 Init）。
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    /**
     * 预留一段虚拟地址空间，尚未提交物理内存。
     * @param reserveSize 预留的字节数，内部会按系统页大小向上取整。
     * @return 成功返回 true；已经 Init 过或系统拒绝预留时返回 false。
     * @example arena.Init(64 * 1024 * 1024);
     */
    bool Init(size_t reserveSize);

    /**
     * 从池中分配一块内存，按需提交新的物理页。
     * @param size 需要的字节数（可以为 0，返回当前游标位置的有效指针）。
     * @param align 对齐要求，必须是 2 的幂；传 0 视为 1。
     * @return 成功返回对齐后的指针；预留空间耗尽或提交页失败返回 nullptr，
     *         调用方必须检查空指针，不会抛异常也不会崩溃。
     * @example void* buf = arena.Alloc(256, alignof(void*));
     */
    void* Alloc(size_t size, size_t align);

    /**
     * 把分配游标复位到起点，之前分配出去的指针全部失效。
     * 不主动 decommit 物理页，复位后的 Alloc 会直接复用已提交的页。
     * @example arena.Reset();
     */
    void Reset();

private:
    // 已提交页数不足时按页粒度追加提交，返回是否成功。
    bool CommitUpTo(size_t neededOffset);

    unsigned char* base_;   // 预留虚拟地址空间的起始地址
    size_t reserveSize_;    // 预留总大小（页对齐后）
    size_t committedSize_;  // 已提交（MEM_COMMIT）的字节数
    size_t used_;           // 已分配游标，相对 base_ 的偏移
};

} // namespace markair
