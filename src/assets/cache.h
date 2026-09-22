// markair 图片缓存(T32,2026-09-17 第二次裁决:解码位图跟随块级虚拟化生命周期)。
//
// 本模块**没有任何淘汰算法**,因为它自己不决定"什么时候该丢":解码位图的
// 生死完全挂在已有的"可见 ± 1 屏"块级虚拟化触发点上(BlockLayoutEngine::
// UpdateVisibleRange),与 IDWriteTextLayout 走同一条创建/释放路径。
//
// 三项职责:
//   ① 按 key 查"这张图当前解码好没有、位图还活着没有"(Find);
//   ② 给虚拟化的释放路径提供"释放这张图的解码位图"接口(ReleaseBitmap),
//      **只丢位图、保留 {width, height}**——尺寸常驻是"布局不跳动"的实现保证;
//   ③ 给网络图片(T34)提供"原始压缩字节是否已下载过"的去重查找
//      (PutRemoteBytes / FindRemoteBytes),避免同一张图反复发请求。
//      压缩字节远小于解码位图,常驻可接受。
//
// 本地路径 / data: URI 图片**不缓存任何原始字节**:前者重新进入可见范围时直接
// 重新打开文件解码;后者的 base64 原文零拷贝躺在 Document::source 里(文档本来
// 就要保留 source),重新解码不产生额外常驻内存。
//
// 禁用 STL 容器:哈希桶与条目数组全部落在调用方传入的 Arena 上。
#pragma once

#include "../util/arena.h"
#include "../util/span.h"
#include "../util/str.h"
#include "../util/types.h"
#include "image.h"

struct ID2D1Bitmap;

namespace markair {

/**
 * 缓存中一张图片的条目。
 *
 * 生命周期分两段:`width/height/status/wasDownsampled` 常驻整份文档(几字节),
 * `bitmap` 只在该图所属的块处于"可见 ± 1 屏"范围内时存在,滚出即被
 * ReleaseBitmap 丢掉、滚回来再解码一次。
 */
struct ImageCacheEntry {
    StrSlice key;        // 图片来源 key(零拷贝引用调用方保证存活的 href 缓冲)
    u64 keyHash;         // key 的 FNV-1a 哈希,避免比较时反复扫字符串
    ID2D1Bitmap* bitmap; // 解码位图;跟随块虚拟化生灭,滚出可见范围即为 nullptr
    const u8* remoteBytes; // 网络图片已下载的原始压缩字节(arena 拥有);非网络图恒为 nullptr
    u32 remoteLen;       // remoteBytes 的字节数
    u32 width;           // 解码(按体积预算降采样)后的像素宽;从未解码成功过时为 0
    u32 height;          // 解码(按体积预算降采样)后的像素高;从未解码成功过时为 0
    u32 originalWidth;   // 降采样前的原始像素宽;布局层据此算显示矩形(不是 width)
    u32 originalHeight;  // 降采样前的原始像素高,语义同上
    ImageStatus status;  // 该图片当前的状态,决定渲染时画位图还是画占位块
    bool wasDownsampled; // 解码时真的被降采样过,渲染层据此画"已压缩·点击看原图"标签
};

/**
 * 图片缓存:key -> {常驻尺寸/状态 + 跟随虚拟化生灭的解码位图 + 网络原始字节}。
 *
 * 典型用法是"块进入可见范围时按需解码、滚出时释放位图":
 *
 * @example
 *   markair::ImageCache cache;
 *   cache.Init(&docArena);
 *   // 块进入"可见 ± 1 屏":
 *   const markair::ImageCacheEntry* e = cache.Find(href);
 *   if (!e || !e->bitmap) {
 *       markair::DecodedImage img = decoder.DecodeFromFile(path, renderTarget);
 *       cache.Put(href, img.bitmap, img.width, img.height, img.status, img.wasDownsampled);
 *   }
 *   // 块滚出"可见 ± 1 屏":
 *   cache.ReleaseBitmap(href);  // 只丢位图,{width,height} 留着,布局不跳动
 */
class ImageCache {
public:
    // 构造一个未初始化的缓存,需先调用 Init。
    ImageCache();

    // 释放所有仍持有的 D2D 位图(条目数组本身随 Arena 一起消亡)。
    ~ImageCache();

    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    /**
     * 绑定 Arena 并分配初始哈希桶。
     * @param arena 条目数组与哈希桶所在的 Arena,生命周期须覆盖本对象。
     * @return 分配失败(Arena 空间耗尽)返回 false,此时缓存退化为"永不命中"
     *         但仍可安全调用(Find 恒返回 nullptr、Put 恒返回 nullptr)。
     * @example cache.Init(&docArena);
     */
    bool Init(Arena* arena);

    /**
     * 查找一条缓存。不改变任何内部顺序(本实现没有淘汰顺序)。
     * @param key 图片来源 key(通常是 LinkTarget::href)。
     * @return 命中返回条目指针(指向内部数组,后续 Put 可能因扩容失效,
     *         调用方不应长期持有);未命中返回 nullptr。
     * @example const markair::ImageCacheEntry* e = cache.Find(href);
     */
    const ImageCacheEntry* Find(StrSlice key) const;

    /**
     * 写入/更新一条缓存。同 key 已存在时替换其内容(旧位图会被 Release)。
     *
     * @param key 图片来源 key;缓存只保存切片视图,调用方须保证其底层字节
     *            在缓存存活期内不失效(LinkTarget::href 在文档 Arena 上,天然满足)。
     * @param bitmap 解码得到的位图,所有权转移给缓存;可为 nullptr(占位状态)。
     * @param width 解码后像素宽。
     * @param height 解码后像素高。
     * @param status 该图片的状态。
     * @param wasDownsampled 解码时是否真的降采样过(来自 DecodedImage 同名字段)。
     * @param originalWidth 降采样前的原始像素宽;传 0(默认值,未降采样场景/调用方
     *        不关心原始尺寸的旧用法)时退化为与 width 相同。
     * @param originalHeight 降采样前的原始像素高,语义同上,默认退化为 height。
     * @return 写入后的条目指针;Arena 耗尽导致写入失败时返回 nullptr(此时会
     *         直接 Release 传入的 bitmap,避免泄漏)。
     * @example
     *   cache.Put(href, img.bitmap, img.width, img.height, img.status, img.wasDownsampled,
     *             img.originalWidth, img.originalHeight);
     */
    const ImageCacheEntry* Put(StrSlice key, ID2D1Bitmap* bitmap, u32 width, u32 height,
                                ImageStatus status, bool wasDownsampled = false,
                                u32 originalWidth = 0, u32 originalHeight = 0);

    /**
     * 释放某张图片的解码位图,保留其 {width, height} 与状态。
     * 由块级虚拟化的"滚出可见 ± 1 屏"路径调用,与 IDWriteTextLayout 的淘汰同一时机。
     *
     * @param key 图片来源 key。
     * @return 确实释放了一张位图返回 true;未命中或本来就没有位图返回 false。
     * @example cache.ReleaseBitmap(box.href);
     */
    bool ReleaseBitmap(StrSlice key);

    /**
     * 登记一张网络图片已下载到的原始压缩字节(T34 去重用)。字节会被拷贝到 Arena,
     * 调用方可以立即释放自己的缓冲。
     *
     * @param key 图片 URL。
     * @param bytes 原始压缩字节,非空。
     * @param len 字节数,须大于 0。
     * @return 登记成功返回 true;Arena 耗尽返回 false。
     * @example cache.PutRemoteBytes(result->url, result->bytes, result->len);
     */
    bool PutRemoteBytes(StrSlice key, const void* bytes, u32 len);

    /**
     * 查这张网络图片的原始压缩字节是否已经下载过(避免重复发请求/重复下载)。
     *
     * @param key 图片 URL。
     * @param outLen 输出:字节数,非空。
     * @return 已下载返回字节指针(arena 拥有,活到文档关闭);未下载返回 nullptr。
     * @example
     *   markair::u32 n = 0;
     *   const markair::u8* raw = cache.FindRemoteBytes(url, &n);
     *   if (!raw) loader.RequestOnUserClick(url);
     */
    const u8* FindRemoteBytes(StrSlice key, u32* outLen) const;

    /** 当前缓存的条目数(含只有尺寸、位图已释放的条目)。 */
    u32 Count() const { return entryCount_; }

    /**
     * 当前**仍存活的**解码位图占用的像素缓冲总字节数(Σ width × height × 4),
     * 即裁决 #5 的计量口径;位图已被 ReleaseBitmap 丢掉的条目不计入。
     * @return 总字节数。
     * @example MARKAIR_CHECK(cache.TotalBytes() <= budget);
     */
    u64 TotalBytes() const { return totalBytes_; }

    /** 按插入顺序取第 index 条,供测试/遍历用;调用方保证 index < Count()。 */
    const ImageCacheEntry& At(u32 index) const { return entries_[index]; }

    /**
     * 释放全部 D2D 位图但保留条目(尺寸/状态/网络原始字节仍在)。用于渲染目标重建
     * (D2DERR_RECREATE_TARGET / DPI 变化)——位图绑定在旧渲染目标上,必须丢弃,
     * 但布局依赖的尺寸信息保留下来,重新解码后几何不变,不会造成滚动跳动。
     * @example renderer 在 ReleaseRenderTarget 时调用 cache.ReleaseAllBitmaps();
     */
    void ReleaseAllBitmaps();

private:
    // 开放寻址哈希:线性探测找到 key 所在桶或第一个空桶,返回桶下标。
    // 表未初始化时返回 0xFFFFFFFF。
    u32 ProbeSlot(StrSlice key, u64 hash) const;

    // 负载因子超过 ~0.7 时重新分配更大的桶数组并重散列(旧桶留给 Arena 统一回收)。
    bool GrowIfNeeded();

    Arena* arena_;          // 条目/桶数组所在的 Arena,不拥有
    ImageCacheEntry* entries_; // 条目数组(按插入顺序追加)
    u32 entryCount_;        // 已用条目数
    u32 entryCapacity_;     // 条目数组容量
    u32* buckets_;          // 哈希桶,存条目下标;kEmptySlot 表示空桶
    u32 bucketCount_;       // 桶数量(2 的幂)
    u64 totalBytes_;        // 已解码位图的像素缓冲字节总量
};

/**
 * 计算图片 key 的 FNV-1a 64 位哈希。抽成公开函数便于单测直接验证散列一致性。
 * @param key 任意字节切片。
 * @return 64 位哈希值。
 * @example markair::u64 h = markair::HashImageKey(markair::StrSlice{"a.png", 5});
 */
u64 HashImageKey(StrSlice key);

}  // namespace markair
