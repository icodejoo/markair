#include "cache.h"

#include <d2d1.h>

namespace markair {

namespace {

// 空桶哨兵值(条目下标不可能取到)。
constexpr u32 kEmptySlot = 0xFFFFFFFFu;

// 初始桶数(2 的幂)。一篇文档的图片通常个位数到几十张,32 个桶起步足够,
// 不够时 GrowIfNeeded 会翻倍重散列。
constexpr u32 kInitialBucketCount = 32u;

// 初始条目容量。
constexpr u32 kInitialEntryCapacity = 16u;

// 两个切片的字节内容是否完全相同。
bool SliceEquals(StrSlice a, StrSlice b) {
    if (a.len != b.len) return false;
    for (u32 i = 0; i < a.len; ++i) {
        if (a.data[i] != b.data[i]) return false;
    }
    return true;
}

}  // namespace

u64 HashImageKey(StrSlice key) {
    u64 h = 0xcbf29ce484222325ull;
    for (u32 i = 0; i < key.len; ++i) {
        h ^= static_cast<u64>(static_cast<u8>(key.data[i]));
        h *= 0x100000001b3ull;
    }
    return h;
}

ImageCache::ImageCache()
    : arena_(nullptr), entries_(nullptr), entryCount_(0), entryCapacity_(0),
      buckets_(nullptr), bucketCount_(0), totalBytes_(0) {}

ImageCache::~ImageCache() { ReleaseAllBitmaps(); }

bool ImageCache::Init(Arena* arena) {
    arena_ = arena;
    entries_ = nullptr;
    entryCount_ = 0;
    entryCapacity_ = 0;
    buckets_ = nullptr;
    bucketCount_ = 0;
    totalBytes_ = 0;
    if (!arena_) return false;

    entries_ = static_cast<ImageCacheEntry*>(
        arena_->Alloc(sizeof(ImageCacheEntry) * kInitialEntryCapacity, alignof(ImageCacheEntry)));
    buckets_ = static_cast<u32*>(arena_->Alloc(sizeof(u32) * kInitialBucketCount, alignof(u32)));
    if (!entries_ || !buckets_) {
        entries_ = nullptr;
        buckets_ = nullptr;
        return false;  // 退化为"永不命中",调用方仍可安全使用
    }
    entryCapacity_ = kInitialEntryCapacity;
    bucketCount_ = kInitialBucketCount;
    for (u32 i = 0; i < bucketCount_; ++i) buckets_[i] = kEmptySlot;
    return true;
}

u32 ImageCache::ProbeSlot(StrSlice key, u64 hash) const {
    if (!buckets_ || bucketCount_ == 0) return kEmptySlot;
    u32 mask = bucketCount_ - 1;  // bucketCount_ 恒为 2 的幂
    u32 slot = static_cast<u32>(hash) & mask;
    // 线性探测:桶里要么是空,要么是某个条目下标;没有删除操作,所以不需要墓碑。
    for (u32 probe = 0; probe < bucketCount_; ++probe) {
        u32 idx = buckets_[slot];
        if (idx == kEmptySlot) return slot;
        if (entries_[idx].keyHash == hash && SliceEquals(entries_[idx].key, key)) return slot;
        slot = (slot + 1) & mask;
    }
    return kEmptySlot;  // 表满(理论上不会发生,GrowIfNeeded 会提前扩容)
}

bool ImageCache::GrowIfNeeded() {
    if (!arena_ || !buckets_) return false;
    // 负载因子 0.7 以内维持线性探测的良好性能。
    if (static_cast<u64>(entryCount_ + 1) * 10ull <= static_cast<u64>(bucketCount_) * 7ull) {
        return true;
    }

    u32 newBucketCount = bucketCount_ * 2;
    u32* newBuckets = static_cast<u32*>(arena_->Alloc(sizeof(u32) * newBucketCount, alignof(u32)));
    if (!newBuckets) return false;
    for (u32 i = 0; i < newBucketCount; ++i) newBuckets[i] = kEmptySlot;

    // 旧桶数组不回收,交给 Arena 整体 Reset 时统一处理(与 Vec 的扩容策略一致)。
    buckets_ = newBuckets;
    bucketCount_ = newBucketCount;
    u32 mask = newBucketCount - 1;
    for (u32 i = 0; i < entryCount_; ++i) {
        u32 slot = static_cast<u32>(entries_[i].keyHash) & mask;
        while (buckets_[slot] != kEmptySlot) slot = (slot + 1) & mask;
        buckets_[slot] = i;
    }
    return true;
}

const ImageCacheEntry* ImageCache::Find(StrSlice key) const {
    if (!buckets_ || !entries_ || entryCount_ == 0) return nullptr;
    u64 hash = HashImageKey(key);
    u32 slot = ProbeSlot(key, hash);
    if (slot == kEmptySlot) return nullptr;
    u32 idx = buckets_[slot];
    if (idx == kEmptySlot) return nullptr;
    return &entries_[idx];
}

const ImageCacheEntry* ImageCache::Put(StrSlice key, ID2D1Bitmap* bitmap, u32 width, u32 height,
                                        ImageStatus status, bool wasDownsampled) {
    if (!arena_ || !buckets_ || !entries_) {
        if (bitmap) bitmap->Release();  // 缓存不可用时也不能泄漏位图
        return nullptr;
    }

    u64 hash = HashImageKey(key);
    u32 slot = ProbeSlot(key, hash);
    if (slot == kEmptySlot) {
        if (bitmap) bitmap->Release();
        return nullptr;
    }

    u32 idx = buckets_[slot];
    if (idx != kEmptySlot) {
        // 同 key 覆盖写:先扣掉旧位图的计量并释放,再写入新内容。
        ImageCacheEntry& e = entries_[idx];
        if (e.bitmap) {
            totalBytes_ -= DecodedByteSize(e.width, e.height);
            e.bitmap->Release();
        }
        e.bitmap = bitmap;
        e.status = status;
        e.wasDownsampled = wasDownsampled;
        // 尺寸只在"这次真的解码出了尺寸"时更新:解码失败/占位不得把已知尺寸清零,
        // 否则布局会从真实尺寸退回占位尺寸,造成滚动跳动。
        if (width > 0 && height > 0) {
            e.width = width;
            e.height = height;
        }
        if (bitmap) totalBytes_ += DecodedByteSize(e.width, e.height);
        return &e;
    }

    if (!GrowIfNeeded()) {
        if (bitmap) bitmap->Release();
        return nullptr;
    }
    // 扩容后桶位置变了,重新定位一次。
    slot = ProbeSlot(key, hash);
    if (slot == kEmptySlot) {
        if (bitmap) bitmap->Release();
        return nullptr;
    }

    if (entryCount_ >= entryCapacity_) {
        u32 newCapacity = entryCapacity_ * 2;
        ImageCacheEntry* newEntries = static_cast<ImageCacheEntry*>(
            arena_->Alloc(sizeof(ImageCacheEntry) * newCapacity, alignof(ImageCacheEntry)));
        if (!newEntries) {
            if (bitmap) bitmap->Release();
            return nullptr;
        }
        for (u32 i = 0; i < entryCount_; ++i) newEntries[i] = entries_[i];
        entries_ = newEntries;
        entryCapacity_ = newCapacity;
    }

    ImageCacheEntry& e = entries_[entryCount_];
    e.key = key;
    e.keyHash = hash;
    e.bitmap = bitmap;
    e.remoteBytes = nullptr;
    e.remoteLen = 0;
    e.width = width;
    e.height = height;
    e.status = status;
    e.wasDownsampled = wasDownsampled;
    buckets_[slot] = entryCount_;
    entryCount_++;
    if (bitmap) totalBytes_ += DecodedByteSize(width, height);
    return &e;
}

bool ImageCache::ReleaseBitmap(StrSlice key) {
    if (!buckets_ || !entries_ || entryCount_ == 0) return false;
    u64 hash = HashImageKey(key);
    u32 slot = ProbeSlot(key, hash);
    if (slot == 0xFFFFFFFFu || buckets_[slot] == kEmptySlot) return false;

    ImageCacheEntry& e = entries_[buckets_[slot]];
    if (!e.bitmap) return false;
    u64 bytes = DecodedByteSize(e.width, e.height);
    totalBytes_ = (totalBytes_ >= bytes) ? (totalBytes_ - bytes) : 0ull;
    e.bitmap->Release();
    e.bitmap = nullptr;
    // width/height/status/remoteBytes 刻意保留:布局尺寸常驻,滚回来重新解码后几何不变。
    return true;
}

bool ImageCache::PutRemoteBytes(StrSlice key, const void* bytes, u32 len) {
    if (!arena_ || !bytes || len == 0) return false;

    // 先确保有条目(占位状态即可),再把字节拷到 Arena 上挂进去。
    const ImageCacheEntry* found = Find(key);
    if (!found) {
        found = Put(key, nullptr, 0, 0, ImageStatus::NotLoaded, false);
        if (!found) return false;
    }

    u8* copy = static_cast<u8*>(arena_->Alloc(len, 1));
    if (!copy) return false;
    const u8* src = static_cast<const u8*>(bytes);
    for (u32 i = 0; i < len; ++i) copy[i] = src[i];

    // Find 返回的是 const 视图,这里按 key 重新定位到可写条目。
    u64 hash = HashImageKey(key);
    u32 slot = ProbeSlot(key, hash);
    if (slot == 0xFFFFFFFFu || buckets_[slot] == kEmptySlot) return false;
    ImageCacheEntry& e = entries_[buckets_[slot]];
    e.remoteBytes = copy;  // 旧字节不回收,交给 Arena 整体释放(同一 URL 通常只下载一次)
    e.remoteLen = len;
    return true;
}

const u8* ImageCache::FindRemoteBytes(StrSlice key, u32* outLen) const {
    if (outLen) *outLen = 0;
    const ImageCacheEntry* e = Find(key);
    if (!e || !e->remoteBytes || e->remoteLen == 0) return nullptr;
    if (outLen) *outLen = e->remoteLen;
    return e->remoteBytes;
}

void ImageCache::ReleaseAllBitmaps() {
    for (u32 i = 0; i < entryCount_; ++i) {
        if (entries_[i].bitmap) {
            entries_[i].bitmap->Release();
            entries_[i].bitmap = nullptr;
        }
    }
    // 尺寸/状态刻意保留:布局依赖它们,重建渲染目标后几何不变,不会造成滚动跳动。
    totalBytes_ = 0;
}

}  // namespace markair
