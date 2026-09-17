// T5 覆盖测试:StrSlice UTF-8/UTF-16 转换的边界情况 + Vec<T> 基本用例。
#include "mdvn_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/util/span.h"

using mdvn::Arena;
using mdvn::StrSlice;
using mdvn::Utf16Slice;
using mdvn::Utf8ToUtf16;
using mdvn::Utf16ToUtf8;
using mdvn::Vec;

// 每个测试各自建一个小 Arena(不可拷贝,不能用返回值的方式共享),避免互相干扰。
#define MDVN_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

// 纯 ASCII 往返转换应保持字节不变。
MDVN_TEST(Utf8ToUtf16_Ascii) {
    MDVN_MAKE_TEST_ARENA();
    StrSlice s{"hello", 5};
    Utf16Slice w = Utf8ToUtf16(s, &arena);
    MDVN_CHECK_EQ(w.len, 5u);
    MDVN_CHECK(w.data[0] == L'h' && w.data[4] == L'o');
}

// 2 字节 UTF-8 序列(如 "café" 里的 é = U+00E9 = 0xC3 0xA9)。
MDVN_TEST(Utf8ToUtf16_TwoByte) {
    MDVN_MAKE_TEST_ARENA();
    const char bytes[] = {'c', 'a', 'f', static_cast<char>(0xC3), static_cast<char>(0xA9)};
    StrSlice s{bytes, sizeof(bytes)};
    Utf16Slice w = Utf8ToUtf16(s, &arena);
    MDVN_CHECK_EQ(w.len, 4u);
    MDVN_CHECK_EQ(static_cast<int>(w.data[3]), 0x00E9);
}

// 3 字节 UTF-8 序列:"你" = U+4F60 = 0xE4 0xBD 0xA0。
MDVN_TEST(Utf8ToUtf16_ThreeByte) {
    MDVN_MAKE_TEST_ARENA();
    const char bytes[] = {static_cast<char>(0xE4), static_cast<char>(0xBD), static_cast<char>(0xA0)};
    StrSlice s{bytes, sizeof(bytes)};
    Utf16Slice w = Utf8ToUtf16(s, &arena);
    MDVN_CHECK_EQ(w.len, 1u);
    MDVN_CHECK_EQ(static_cast<int>(w.data[0]), 0x4F60);
}

// 4 字节 UTF-8 序列 + 代理对:U+1F600 (emoji) = F0 9F 98 80,
// UTF-16 应拆成高代理 0xD83D + 低代理 0xDE00。
MDVN_TEST(Utf8ToUtf16_FourByteSurrogatePair) {
    MDVN_MAKE_TEST_ARENA();
    const char bytes[] = {static_cast<char>(0xF0), static_cast<char>(0x9F),
                           static_cast<char>(0x98), static_cast<char>(0x80)};
    StrSlice s{bytes, sizeof(bytes)};
    Utf16Slice w = Utf8ToUtf16(s, &arena);
    MDVN_CHECK_EQ(w.len, 2u);
    MDVN_CHECK_EQ(static_cast<unsigned>(w.data[0]), 0xD83Du);
    MDVN_CHECK_EQ(static_cast<unsigned>(w.data[1]), 0xDE00u);

    // 反向转换应能拿回原始 UTF-8 字节。
    StrSlice back = Utf16ToUtf8(w, &arena);
    MDVN_CHECK_EQ(back.len, 4u);
    MDVN_CHECK_EQ(static_cast<unsigned char>(back.data[0]), 0xF0u);
    MDVN_CHECK_EQ(static_cast<unsigned char>(back.data[3]), 0x80u);
}

// 被截断的 2 字节序列(缺续字节):'A' + 0xC3(缺后续字节),应各自替换为 U+FFFD。
MDVN_TEST(Utf8ToUtf16_TruncatedSequence) {
    MDVN_MAKE_TEST_ARENA();
    const char bytes[] = {'A', static_cast<char>(0xC3)};
    StrSlice s{bytes, sizeof(bytes)};
    Utf16Slice w = Utf8ToUtf16(s, &arena);
    MDVN_CHECK_EQ(w.len, 2u);
    MDVN_CHECK_EQ(static_cast<int>(w.data[0]), 'A');
    MDVN_CHECK_EQ(static_cast<unsigned>(w.data[1]), 0xFFFDu);
}

// 孤立续字节(0x80~0xBF 单独出现,没有合法前导字节),每字节替换为一个 U+FFFD。
MDVN_TEST(Utf8ToUtf16_LoneContinuationByte) {
    MDVN_MAKE_TEST_ARENA();
    const char bytes[] = {static_cast<char>(0x80), static_cast<char>(0x81)};
    StrSlice s{bytes, sizeof(bytes)};
    Utf16Slice w = Utf8ToUtf16(s, &arena);
    MDVN_CHECK_EQ(w.len, 2u);
    MDVN_CHECK_EQ(static_cast<unsigned>(w.data[0]), 0xFFFDu);
    MDVN_CHECK_EQ(static_cast<unsigned>(w.data[1]), 0xFFFDu);
}

// overlong 编码:用 3 字节序列编码本该 1 字节表示的 '/' (U+002F) 是非法的。
MDVN_TEST(Utf8ToUtf16_OverlongEncoding) {
    MDVN_MAKE_TEST_ARENA();
    const char bytes[] = {static_cast<char>(0xE0), static_cast<char>(0x80), static_cast<char>(0xAF)};
    StrSlice s{bytes, sizeof(bytes)};
    Utf16Slice w = Utf8ToUtf16(s, &arena);
    // 3 个非法字节各自消耗 1 字节替换为 U+FFFD。
    MDVN_CHECK_EQ(w.len, 3u);
    MDVN_CHECK_EQ(static_cast<unsigned>(w.data[0]), 0xFFFDu);
    MDVN_CHECK_EQ(static_cast<unsigned>(w.data[1]), 0xFFFDu);
    MDVN_CHECK_EQ(static_cast<unsigned>(w.data[2]), 0xFFFDu);
}

// Vec<T>::Push / operator[] / Size 基本用例。
MDVN_TEST(Vec_PushAndIndex) {
    MDVN_MAKE_TEST_ARENA();
    Vec<int> v(&arena);
    MDVN_CHECK_EQ(v.Size(), 0u);
    v.Push(10);
    v.Push(20);
    v.Push(30);
    MDVN_CHECK_EQ(v.Size(), 3u);
    MDVN_CHECK_EQ(v[0], 10);
    MDVN_CHECK_EQ(v[1], 20);
    MDVN_CHECK_EQ(v[2], 30);
}

// Vec<T> 触发扩容(超过初始容量 8)后数据应保持完整。
MDVN_TEST(Vec_GrowKeepsData) {
    MDVN_MAKE_TEST_ARENA();
    Vec<int> v(&arena);
    for (int i = 0; i < 100; ++i) {
        v.Push(i);
    }
    MDVN_CHECK_EQ(v.Size(), 100u);
    bool ok = true;
    for (int i = 0; i < 100; ++i) {
        if (v[static_cast<unsigned>(i)] != i) ok = false;
    }
    MDVN_CHECK(ok);
}
