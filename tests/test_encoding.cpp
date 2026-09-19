// T7 覆盖测试:编码嗅探(DetectEncoding)与解码(DecodeToUtf16)的 6 类样本。
#include "markair_test.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/encoding.h"

using markair::Arena;
using markair::DetectedEncoding;
using markair::DetectEncoding;
using markair::DecodeToUtf16;
using markair::EncodingDetection;
using markair::StrSlice;
using markair::Utf16Slice;

#define MARKAIR_MAKE_TEST_ARENA() Arena arena; arena.Init(1 * 1024 * 1024)

// 样本 1:UTF-8 无 BOM,含中文"你好"(E4 BD A0 E5 A5 BD)。
MARKAIR_TEST(Encoding_Utf8NoBom_Chinese) {
    MARKAIR_MAKE_TEST_ARENA();
    const char bytes[] = {static_cast<char>(0xE4), static_cast<char>(0xBD), static_cast<char>(0xA0),
                           static_cast<char>(0xE5), static_cast<char>(0xA5), static_cast<char>(0xBD)};
    StrSlice s{bytes, sizeof(bytes)};

    EncodingDetection d = DetectEncoding(s);
    MARKAIR_CHECK(d.encoding == DetectedEncoding::Utf8NoBom);
    MARKAIR_CHECK_EQ(d.contentOffset, 0u);

    Utf16Slice w = DecodeToUtf16(s, d, &arena);
    MARKAIR_CHECK_EQ(w.len, 2u);
    MARKAIR_CHECK_EQ(static_cast<unsigned>(w.data[0]), 0x4F60u); // 你
    MARKAIR_CHECK_EQ(static_cast<unsigned>(w.data[1]), 0x597Du); // 好
}

// 样本 2:UTF-8 BOM(EF BB BF)+ "AB"。
MARKAIR_TEST(Encoding_Utf8Bom) {
    MARKAIR_MAKE_TEST_ARENA();
    const char bytes[] = {static_cast<char>(0xEF), static_cast<char>(0xBB), static_cast<char>(0xBF), 'A', 'B'};
    StrSlice s{bytes, sizeof(bytes)};

    EncodingDetection d = DetectEncoding(s);
    MARKAIR_CHECK(d.encoding == DetectedEncoding::Utf8Bom);
    MARKAIR_CHECK_EQ(d.contentOffset, 3u);

    Utf16Slice w = DecodeToUtf16(s, d, &arena);
    MARKAIR_CHECK_EQ(w.len, 2u);
    MARKAIR_CHECK_EQ(static_cast<int>(w.data[0]), 'A');
    MARKAIR_CHECK_EQ(static_cast<int>(w.data[1]), 'B');
}

// 样本 3:UTF-16LE BOM(FF FE)+ 'A'(0x41 0x00),另附一个奇数长度尾字节的边界情况。
MARKAIR_TEST(Encoding_Utf16LeBom) {
    MARKAIR_MAKE_TEST_ARENA();
    const char bytes[] = {static_cast<char>(0xFF), static_cast<char>(0xFE), 0x41, 0x00};
    StrSlice s{bytes, sizeof(bytes)};

    EncodingDetection d = DetectEncoding(s);
    MARKAIR_CHECK(d.encoding == DetectedEncoding::Utf16Le);
    MARKAIR_CHECK_EQ(d.contentOffset, 2u);

    Utf16Slice w = DecodeToUtf16(s, d, &arena);
    MARKAIR_CHECK_EQ(w.len, 1u);
    MARKAIR_CHECK_EQ(static_cast<int>(w.data[0]), 'A');

    // 奇数长度尾部单字节:FF FE 41 00 42,最后的 0x42 无法组成完整 code unit,
    // 应替换为 U+FFFD 而不是崩溃或越界读取。
    const char oddBytes[] = {static_cast<char>(0xFF), static_cast<char>(0xFE), 0x41, 0x00, 0x42};
    StrSlice oddS{oddBytes, sizeof(oddBytes)};
    EncodingDetection oddD = DetectEncoding(oddS);
    MARKAIR_CHECK(oddD.encoding == DetectedEncoding::Utf16Le);
    Utf16Slice oddW = DecodeToUtf16(oddS, oddD, &arena);
    MARKAIR_CHECK_EQ(oddW.len, 2u);
    MARKAIR_CHECK_EQ(static_cast<int>(oddW.data[0]), 'A');
    MARKAIR_CHECK_EQ(static_cast<unsigned>(oddW.data[1]), 0xFFFDu);
}

// 样本 4:GBK 无 BOM 的中文编码字节序列("中文" GBK = D6 D0 CE C4),
// 该序列不是合法 UTF-8,应回退判定为 AnsiFallback。解码结果依赖系统 ANSI
// 代码页,这里只校验嗅探结果与"不崩溃、产出非空结果",不断言具体解码字符。
MARKAIR_TEST(Encoding_GbkNoBom_FallsBackToAnsi) {
    MARKAIR_MAKE_TEST_ARENA();
    const char bytes[] = {static_cast<char>(0xD6), static_cast<char>(0xD0),
                           static_cast<char>(0xCE), static_cast<char>(0xC4)};
    StrSlice s{bytes, sizeof(bytes)};

    EncodingDetection d = DetectEncoding(s);
    MARKAIR_CHECK(d.encoding == DetectedEncoding::AnsiFallback);
    MARKAIR_CHECK_EQ(d.contentOffset, 0u);

    Utf16Slice w = DecodeToUtf16(s, d, &arena);
    MARKAIR_CHECK(w.data != nullptr);
    MARKAIR_CHECK(w.len >= 1u);
}

// 样本 5:纯 ASCII,无 BOM,应判定为合法 UTF-8(Utf8NoBom)。
MARKAIR_TEST(Encoding_PureAscii) {
    MARKAIR_MAKE_TEST_ARENA();
    const char bytes[] = {'H', 'e', 'l', 'l', 'o'};
    StrSlice s{bytes, sizeof(bytes)};

    EncodingDetection d = DetectEncoding(s);
    MARKAIR_CHECK(d.encoding == DetectedEncoding::Utf8NoBom);
    MARKAIR_CHECK_EQ(d.contentOffset, 0u);

    Utf16Slice w = DecodeToUtf16(s, d, &arena);
    MARKAIR_CHECK_EQ(w.len, 5u);
    MARKAIR_CHECK(w.data[0] == L'H' && w.data[4] == L'o');
}

// 样本 6:混入非法字节的序列(孤立的 0xFF 不是合法 UTF-8 前导字节),
// 应回退为 AnsiFallback,且解码不崩溃、能产出结果。
MARKAIR_TEST(Encoding_IllegalBytes_FallsBackToAnsi) {
    MARKAIR_MAKE_TEST_ARENA();
    const char bytes[] = {'A', static_cast<char>(0xFF), 'B'};
    StrSlice s{bytes, sizeof(bytes)};

    EncodingDetection d = DetectEncoding(s);
    MARKAIR_CHECK(d.encoding == DetectedEncoding::AnsiFallback);
    MARKAIR_CHECK_EQ(d.contentOffset, 0u);

    Utf16Slice w = DecodeToUtf16(s, d, &arena);
    MARKAIR_CHECK(w.data != nullptr);
    MARKAIR_CHECK(w.len >= 1u);
}
