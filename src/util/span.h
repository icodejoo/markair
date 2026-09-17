// mdvn 的通用小容器:非拥有型 Span<T> 视图 + 基于 Arena 的追加数组 Vec<T>。
// 都不使用 STL 容器,配合 Arena 的"一次性用完丢弃"生命周期。
#pragma once

#include "arena.h"
#include "types.h"

namespace mdvn {

/**
 * 非拥有型连续内存视图,不做深拷贝,不负责释放。
 *
 * @example
 *   int arr[3] = {1, 2, 3};
 *   mdvn::Span<int> s{arr, 3};
 *   int first = s[0];
 */
template <typename T>
struct Span {
    T* data;
    u32 len;

    // 下标访问,调用方保证 index < len,越界不做检查(内部工具类型,信任调用契约)。
    T& operator[](u32 index) const { return data[index]; }
};

/**
 * 基于 mdvn::Arena 的追加数组,替代 std::vector。
 * 容量不足时从 Arena 重新分配更大块并拷贝旧数据;旧块不回收,
 * 符合"解析一次性用完整体丢弃"的生命周期,不追求单块内存复用。
 *
 * @example
 *   mdvn::Arena arena;
 *   arena.Init(1 * 1024 * 1024);
 *   mdvn::Vec<int> v(&arena);
 *   v.Push(1);
 *   v.Push(2);
 *   int total = v[0] + v[1]; // 3
 */
template <typename T>
class Vec {
public:
    // 绑定到一个 Arena,后续所有扩容分配都走这个 Arena。
    explicit Vec(Arena* arena) : arena_(arena), data_(nullptr), size_(0), capacity_(0) {}

    /**
     * 追加一个元素到末尾,容量不足时自动扩容(翻倍,首次分配 8 个)。
     * @param value 要追加的元素(拷贝存入)。
     * @return 扩容失败(Arena 分配失败)返回 false 并放弃本次追加;成功返回 true。
     */
    bool Push(const T& value) {
        if (size_ >= capacity_) {
            u32 newCapacity = capacity_ == 0 ? 8 : capacity_ * 2;
            void* mem = arena_->Alloc(sizeof(T) * newCapacity, alignof(T));
            if (!mem) return false; // Arena 空间耗尽,追加失败但不崩溃

            T* newData = static_cast<T*>(mem);
            for (u32 i = 0; i < size_; ++i) newData[i] = data_[i];
            data_ = newData; // 旧块留给 Arena 复位时统一处理,这里不主动回收
            capacity_ = newCapacity;
        }
        data_[size_++] = value;
        return true;
    }

    // 下标访问,调用方保证 index < Size()。
    T& operator[](u32 index) const { return data_[index]; }

    // 当前元素个数。
    u32 Size() const { return size_; }

    // 底层连续内存的起始指针,供需要直接拿到最终缓冲区的场景使用(如字符串转换)。
    T* Data() const { return data_; }

private:
    Arena* arena_;
    T* data_;
    u32 size_;
    u32 capacity_;
};

} // namespace mdvn
