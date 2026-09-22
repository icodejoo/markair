// T32 覆盖测试:图片缓存(2026-09-17 第二次裁决——解码位图跟随块级虚拟化生灭,
// 尺寸信息常驻;网络图片原始压缩字节去重)。
//
// 覆盖的两条主线:
//   ① 完整生命周期:进入可见范围触发解码 -> 滚出后位图被释放(尺寸仍在)->
//      再次滚入触发重新解码;
//   ② 网络图片原始字节不重复下载(FindRemoteBytes 命中即复用)。
#include "markair_test.h"
#include "image_test_support.h"
#include "../src/assets/cache.h"
#include "../src/assets/image.h"
#include "../src/util/arena.h"

#include <cstring>

using markair::Arena;
using markair::DecodedByteSize;
using markair::DecodedImage;
using markair::ImageCache;
using markair::ImageCacheEntry;
using markair::ImageDecoder;
using markair::ImageStatus;
using markair::StrSlice;
using markair::u32;
using markair::u8;
using markair_test::EncodeTestImage;
using markair_test::ImageTestEnv;

namespace {

#define MARKAIR_MAKE_CACHE_ARENA() Arena arena; arena.Init(4 * 1024 * 1024)

StrSlice Lit(const char* s) { return StrSlice{s, static_cast<u32>(strlen(s))}; }

}  // namespace

// 用例:哈希是纯函数,相同内容的不同缓冲必须得到相同哈希。
MARKAIR_TEST(Cache_HashIsContentBased) {
    char a[] = "assets/logo.png";
    char b[] = "assets/logo.png";
    MARKAIR_CHECK(a != b);
    MARKAIR_CHECK_EQ(markair::HashImageKey(Lit(a)), markair::HashImageKey(Lit(b)));
    MARKAIR_CHECK(markair::HashImageKey(Lit("a.png")) != markair::HashImageKey(Lit("b.png")));
}

// 用例:未命中返回 nullptr;写入后同 key 命中且不重复建条目。
MARKAIR_TEST(Cache_FindAndPutBasics) {
    MARKAIR_MAKE_CACHE_ARENA();
    ImageCache cache;
    MARKAIR_CHECK(cache.Init(&arena));

    MARKAIR_CHECK(cache.Find(Lit("a.png")) == nullptr);
    MARKAIR_CHECK_EQ(cache.Count(), 0u);

    cache.Put(Lit("a.png"), nullptr, 100, 50, ImageStatus::Ok, false);
    const ImageCacheEntry* e = cache.Find(Lit("a.png"));
    MARKAIR_CHECK(e != nullptr);
    if (e) {
        MARKAIR_CHECK_EQ(e->width, 100u);
        MARKAIR_CHECK_EQ(e->height, 50u);
        MARKAIR_CHECK(e->status == ImageStatus::Ok);
    }
    MARKAIR_CHECK_EQ(cache.Count(), 1u);

    // 同 key 覆盖写不新增条目。
    cache.Put(Lit("a.png"), nullptr, 100, 50, ImageStatus::Ok, true);
    MARKAIR_CHECK_EQ(cache.Count(), 1u);
}

// 用例:originalWidth/originalHeight 不传(旧口径调用)时退化为与解码尺寸相同;
// 显式传入时按体积预算降采样场景保留真实原始尺寸,布局层据此显示。
MARKAIR_TEST(Cache_OriginalSizeFallbackAndExplicit) {
    MARKAIR_MAKE_CACHE_ARENA();
    ImageCache cache;
    MARKAIR_CHECK(cache.Init(&arena));

    // 不传 originalWidth/originalHeight:退化为与 width/height 相同(未降采样场景)。
    // 注意:变量名避开 Windows 头文件里的宏 small/big(rpcndr.h 把它们 #define
    // 成 char/long,踩上会报一堆离谱的语法错误)。
    cache.Put(Lit("small.png"), nullptr, 64, 32, ImageStatus::Ok, false);
    const ImageCacheEntry* smallEntry = cache.Find(Lit("small.png"));
    MARKAIR_CHECK(smallEntry != nullptr);
    if (smallEntry) {
        MARKAIR_CHECK_EQ(smallEntry->originalWidth, 64u);
        MARKAIR_CHECK_EQ(smallEntry->originalHeight, 32u);
    }

    // 显式传入:降采样后的位图很小,但原始尺寸保留真实值。
    cache.Put(Lit("big.png"), nullptr, 200, 100, ImageStatus::Ok, true, 2048u, 1024u);
    const ImageCacheEntry* bigEntry = cache.Find(Lit("big.png"));
    MARKAIR_CHECK(bigEntry != nullptr);
    if (bigEntry) {
        MARKAIR_CHECK_EQ(bigEntry->width, 200u);
        MARKAIR_CHECK_EQ(bigEntry->originalWidth, 2048u);
        MARKAIR_CHECK_EQ(bigEntry->originalHeight, 1024u);
    }
}

// 用例:不同图片各自独立计量,大量条目下哈希表扩容后仍然全部可查。
MARKAIR_TEST(Cache_DistinctKeysAndRehash) {
    MARKAIR_MAKE_CACHE_ARENA();
    ImageCache cache;
    MARKAIR_CHECK(cache.Init(&arena));

    // 200 张图,足以触发多次桶扩容(初始 32 桶,负载因子 0.7)。
    char keys[200][16];
    for (u32 i = 0; i < 200; ++i) {
        keys[i][0] = 'i';
        keys[i][1] = 'm';
        keys[i][2] = 'g';
        keys[i][3] = static_cast<char>('0' + (i / 100) % 10);
        keys[i][4] = static_cast<char>('0' + (i / 10) % 10);
        keys[i][5] = static_cast<char>('0' + i % 10);
        keys[i][6] = 0;
        cache.Put(StrSlice{keys[i], 6}, nullptr, 10 + i, 20, ImageStatus::Ok, false);
    }
    MARKAIR_CHECK_EQ(cache.Count(), 200u);
    for (u32 i = 0; i < 200; ++i) {
        const ImageCacheEntry* e = cache.Find(StrSlice{keys[i], 6});
        MARKAIR_CHECK(e != nullptr);
        if (e) MARKAIR_CHECK_EQ(e->width, 10u + i);
    }
}

// 用例(核心):解码位图的完整生命周期 ——
// 进入可见范围解码 -> 计量增加 -> 滚出释放位图(尺寸留存、计量归零)-> 再次滚入重新解码。
MARKAIR_TEST(Cache_BitmapLifecycleFollowsVisibility) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }

    u8* buf = static_cast<u8*>(HeapAlloc(GetProcessHeap(), 0, 1u << 20));
    if (!buf) { env.Shutdown(); return; }
    u32 n = EncodeTestImage(env.wic, GUID_ContainerFormatPng, 80, 40, 1, buf, 1u << 20);
    MARKAIR_CHECK(n > 0);

    MARKAIR_MAKE_CACHE_ARENA();
    ImageCache cache;
    MARKAIR_CHECK(cache.Init(&arena));
    ImageDecoder decoder;
    StrSlice key = Lit("photo.png");

    // ① 块进入"可见 ± 1 屏":未命中 -> 解码 -> 入缓存。
    MARKAIR_CHECK(cache.Find(key) == nullptr);
    DecodedImage first = decoder.DecodeFromMemory(buf, n, env.target);
    MARKAIR_CHECK(first.status == ImageStatus::Ok);
    cache.Put(key, first.bitmap, first.width, first.height, first.status, first.wasDownsampled);

    const ImageCacheEntry* e = cache.Find(key);
    MARKAIR_CHECK(e != nullptr && e->bitmap != nullptr);
    MARKAIR_CHECK_EQ(cache.TotalBytes(), DecodedByteSize(80, 40));

    // ② 块滚出可见范围:只丢位图,{width,height} 必须留着(布局不跳动的保证)。
    MARKAIR_CHECK(cache.ReleaseBitmap(key));
    e = cache.Find(key);
    MARKAIR_CHECK(e != nullptr);
    if (e) {
        MARKAIR_CHECK(e->bitmap == nullptr);
        MARKAIR_CHECK_EQ(e->width, 80u);
        MARKAIR_CHECK_EQ(e->height, 40u);
    }
    MARKAIR_CHECK_EQ(cache.TotalBytes(), 0ull);
    MARKAIR_CHECK_EQ(cache.Count(), 1u);  // 条目本身没被删掉

    // 重复释放是幂等的(返回 false,不重复扣计量)。
    MARKAIR_CHECK(!cache.ReleaseBitmap(key));
    MARKAIR_CHECK_EQ(cache.TotalBytes(), 0ull);

    // ③ 块再次滚入可见范围:重新解码,尺寸与第一次完全一致 -> 几何不变,不跳动。
    DecodedImage second = decoder.DecodeFromMemory(buf, n, env.target);
    MARKAIR_CHECK(second.status == ImageStatus::Ok);
    MARKAIR_CHECK_EQ(second.width, first.width);
    MARKAIR_CHECK_EQ(second.height, first.height);
    cache.Put(key, second.bitmap, second.width, second.height, second.status,
              second.wasDownsampled);
    e = cache.Find(key);
    MARKAIR_CHECK(e != nullptr && e->bitmap != nullptr);
    MARKAIR_CHECK_EQ(cache.TotalBytes(), DecodedByteSize(80, 40));
    MARKAIR_CHECK_EQ(cache.Count(), 1u);

    HeapFree(GetProcessHeap(), 0, buf);
    // 必须先把缓存里的位图放掉,再拆 D2D/COM 环境:否则 cache 的析构会在
    // CoUninitialize 之后才 Release,属于典型的 COM 生命周期倒置。
    cache.ReleaseAllBitmaps();
    env.Shutdown();
}

// 用例:解码失败/占位写入不得把已知尺寸清零(否则布局会从真实尺寸退回占位尺寸)。
MARKAIR_TEST(Cache_PlaceholderPutKeepsKnownSize) {
    MARKAIR_MAKE_CACHE_ARENA();
    ImageCache cache;
    MARKAIR_CHECK(cache.Init(&arena));

    cache.Put(Lit("x.png"), nullptr, 300, 200, ImageStatus::Ok, false);
    cache.Put(Lit("x.png"), nullptr, 0, 0, ImageStatus::Failed, false);

    const ImageCacheEntry* e = cache.Find(Lit("x.png"));
    MARKAIR_CHECK(e != nullptr);
    if (e) {
        MARKAIR_CHECK_EQ(e->width, 300u);
        MARKAIR_CHECK_EQ(e->height, 200u);
        MARKAIR_CHECK(e->status == ImageStatus::Failed);
    }
}

// 用例:ReleaseAllBitmaps(渲染目标重建时调用)只清位图,尺寸与条目数不变。
MARKAIR_TEST(Cache_ReleaseAllBitmapsKeepsSizes) {
    MARKAIR_MAKE_CACHE_ARENA();
    ImageCache cache;
    MARKAIR_CHECK(cache.Init(&arena));
    cache.Put(Lit("a.png"), nullptr, 64, 64, ImageStatus::Ok, false);
    cache.Put(Lit("b.png"), nullptr, 32, 32, ImageStatus::Ok, false);

    cache.ReleaseAllBitmaps();
    MARKAIR_CHECK_EQ(cache.Count(), 2u);
    MARKAIR_CHECK_EQ(cache.TotalBytes(), 0ull);
    const ImageCacheEntry* e = cache.Find(Lit("a.png"));
    MARKAIR_CHECK(e != nullptr);
    if (e) MARKAIR_CHECK_EQ(e->width, 64u);
}

// 用例(核心):网络图片的原始压缩字节只存一次,后续复用不重复下载。
MARKAIR_TEST(Cache_RemoteBytesDeduplication) {
    MARKAIR_MAKE_CACHE_ARENA();
    ImageCache cache;
    MARKAIR_CHECK(cache.Init(&arena));
    StrSlice url = Lit("https://example.com/badge.svg?x=1");

    // 下载前:查不到 -> 上层据此发起(且仅发起)一次请求。
    u32 len = 0;
    MARKAIR_CHECK(cache.FindRemoteBytes(url, &len) == nullptr);
    MARKAIR_CHECK_EQ(len, 0u);

    const char payload[] = "\x89PNG-fake-bytes";
    u32 payloadLen = static_cast<u32>(sizeof(payload) - 1);
    MARKAIR_CHECK(cache.PutRemoteBytes(url, payload, payloadLen));

    // 下载后:查得到,内容一致,且是缓存自己的副本(不引用调用方缓冲)。
    const u8* got = cache.FindRemoteBytes(url, &len);
    MARKAIR_CHECK(got != nullptr);
    MARKAIR_CHECK_EQ(len, payloadLen);
    MARKAIR_CHECK(got != reinterpret_cast<const u8*>(payload));
    if (got) {
        bool same = true;
        for (u32 i = 0; i < payloadLen; ++i) {
            if (got[i] != static_cast<u8>(payload[i])) same = false;
        }
        MARKAIR_CHECK(same);
    }

    // 释放解码位图不会连带丢掉已下载的原始字节(压缩字节常驻,避免重复请求)。
    cache.ReleaseBitmap(url);
    MARKAIR_CHECK(cache.FindRemoteBytes(url, &len) != nullptr);

    // 另一个 URL 仍然查不到,不会被上一条误命中。
    MARKAIR_CHECK(cache.FindRemoteBytes(Lit("https://example.com/other.png"), &len) == nullptr);
}

// 用例:未 Init 的缓存可以安全调用,不崩溃(退化为永不命中)。
MARKAIR_TEST(Cache_UninitializedIsSafe) {
    ImageCache cache;
    MARKAIR_CHECK(!cache.Init(nullptr));
    MARKAIR_CHECK(cache.Find(Lit("a.png")) == nullptr);
    MARKAIR_CHECK(cache.Put(Lit("a.png"), nullptr, 1, 1, ImageStatus::Ok, false) == nullptr);
    MARKAIR_CHECK(!cache.ReleaseBitmap(Lit("a.png")));
    MARKAIR_CHECK(!cache.PutRemoteBytes(Lit("a.png"), "x", 1));
    MARKAIR_CHECK_EQ(cache.Count(), 0u);
    MARKAIR_CHECK_EQ(cache.TotalBytes(), 0ull);
}
