#include "arena.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace mdvn {

namespace {

// 把 value 向上取整到 multiple 的倍数（multiple 必须是 2 的幂）。
size_t AlignUp(size_t value, size_t multiple) {
    return (value + multiple - 1) & ~(multiple - 1);
}

// 系统页大小，懒加载一次即可，全程不变。
size_t SystemPageSize() {
    static size_t pageSize = [] {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        return static_cast<size_t>(si.dwPageSize);
    }();
    return pageSize;
}

} // namespace

// 构造出空状态，真正的地址空间预留延迟到 Init。
Arena::Arena() : base_(nullptr), reserveSize_(0), committedSize_(0), used_(0) {}

// 释放预留的虚拟地址空间（提交的页会随 MEM_RELEASE 一并释放）。
Arena::~Arena() {
    if (base_) {
        VirtualFree(base_, 0, MEM_RELEASE);
        base_ = nullptr;
    }
}

bool Arena::Init(size_t reserveSize) {
    if (base_) return false; // 已经初始化过，不允许重复 Init

    const size_t pageSize = SystemPageSize();
    reserveSize_ = AlignUp(reserveSize == 0 ? pageSize : reserveSize, pageSize);

    void* mem = VirtualAlloc(nullptr, reserveSize_, MEM_RESERVE, PAGE_NOACCESS);
    if (!mem) {
        reserveSize_ = 0;
        return false;
    }

    base_ = static_cast<unsigned char*>(mem);
    committedSize_ = 0;
    used_ = 0;
    return true;
}

bool Arena::CommitUpTo(size_t neededOffset) {
    if (neededOffset <= committedSize_) return true;

    const size_t pageSize = SystemPageSize();
    const size_t newCommitted = AlignUp(neededOffset, pageSize);
    if (newCommitted > reserveSize_) return false; // 超出预留空间

    const size_t commitBytes = newCommitted - committedSize_;
    void* result = VirtualAlloc(base_ + committedSize_, commitBytes, MEM_COMMIT, PAGE_READWRITE);
    if (!result) return false;

    committedSize_ = newCommitted;
    return true;
}

void* Arena::Alloc(size_t size, size_t align) {
    if (!base_) return nullptr;
    if (align == 0) align = 1;

    const size_t alignedStart = AlignUp(used_, align);
    const size_t newUsed = alignedStart + size;

    if (newUsed > reserveSize_) return nullptr; // 预留空间耗尽
    if (!CommitUpTo(newUsed)) return nullptr;    // 跨页边界按需提交失败

    used_ = newUsed;
    return base_ + alignedStart;
}

void Arena::Reset() {
    used_ = 0; // 不 decommit，后续 Alloc 直接复用已提交的物理页
}

} // namespace mdvn
