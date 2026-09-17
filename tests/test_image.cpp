// T30 覆盖测试:WIC 解码封装。
//
// 六个样本(验收标准原文要求):PNG / JPEG / 多帧 GIF / 超大尺寸 / 损坏文件 / .svg。
// 前四个用 WIC 编码器在内存里现场生成(见 image_test_support.h 的理由说明),
// 损坏文件用一段随机字节,.svg 走"不进 WIC"的纯判定路径。
#include "mdvn_test.h"
#include "image_test_support.h"
#include "../src/assets/image.h"

#include <cstring>

using mdvn::ComputeDownscaledSize;
using mdvn::DecodedByteSize;
using mdvn::DecodedImage;
using mdvn::ImageDecoder;
using mdvn::ImageStatus;
using mdvn::IsSvgImageRef;
using mdvn::StrSlice;
using mdvn::u32;
using mdvn::u8;
using mdvn_test::EncodeTestImage;
using mdvn_test::ImageTestEnv;

namespace {

// 样本编码缓冲上限:超大尺寸样本(2048x1024 渐变 PNG)也放得下。
constexpr u32 kSampleCapacity = 8u * 1024u * 1024u;

// 从进程堆借一块编码缓冲(测试用,函数内用完即还)。
u8* AllocSampleBuffer() {
    return static_cast<u8*>(HeapAlloc(GetProcessHeap(), 0, kSampleCapacity));
}

void FreeSampleBuffer(u8* p) {
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

}  // namespace

// 用例:SVG 判定是纯字符串逻辑,不进 WIC —— 扩展名大小写、带 query 都要认出来。
MDVN_TEST(Image_SvgDetection) {
    MDVN_CHECK(IsSvgImageRef(StrSlice{"logo.svg", 8}));
    MDVN_CHECK(IsSvgImageRef(StrSlice{"LOGO.SVG", 8}));
    MDVN_CHECK(IsSvgImageRef(StrSlice{"a/b/c.svg?v=2", 13}));
    MDVN_CHECK(IsSvgImageRef(StrSlice{"data:image/svg+xml;base64,AAA", 29}));
    MDVN_CHECK(!IsSvgImageRef(StrSlice{"logo.png", 8}));
    MDVN_CHECK(!IsSvgImageRef(StrSlice{"svg", 3}));
    MDVN_CHECK(!IsSvgImageRef(StrSlice{nullptr, 0}));
}

// 用例:降采样尺寸计算(纯数字函数),等比且至少 1 像素。
MDVN_TEST(Image_DownscaledSizeMath) {
    u32 w = 0, h = 0;
    ComputeDownscaledSize(8000, 4000, 512, &w, &h);
    MDVN_CHECK_EQ(w, 512u);
    MDVN_CHECK_EQ(h, 256u);

    // 未超限时原样返回。
    ComputeDownscaledSize(100, 50, 512, &w, &h);
    MDVN_CHECK_EQ(w, 100u);
    MDVN_CHECK_EQ(h, 50u);

    // 极端长条图:短边缩到 0 时钳到 1,不产出 0 尺寸。
    ComputeDownscaledSize(100000, 1, 512, &w, &h);
    MDVN_CHECK_EQ(w, 512u);
    MDVN_CHECK_EQ(h, 1u);

    // maxDim 传 0 表示不限制。
    ComputeDownscaledSize(9000, 9000, 0, &w, &h);
    MDVN_CHECK_EQ(w, 9000u);
}

// 用例:LRU/预算计量口径 —— 解码后像素缓冲字节数 = w × h × 4。
MDVN_TEST(Image_DecodedByteSize) {
    MDVN_CHECK_EQ(DecodedByteSize(1920, 1080), 8294400ull);
    MDVN_CHECK_EQ(DecodedByteSize(512, 512), 1048576ull);  // 降采样上限下的单图 ~1MB
    MDVN_CHECK_EQ(DecodedByteSize(0, 0), 0ull);
}

// 用例:未解码过任何图片时,解码器不应初始化 WIC(惰性初始化的代码层证据)。
MDVN_TEST(Image_DecoderIsLazyBeforeFirstUse) {
    ImageDecoder decoder;
    MDVN_CHECK(!decoder.IsInitialized());
}

// 用例:PNG 样本解码成功,尺寸正确,未触发降采样。
MDVN_TEST(Image_DecodePngSample) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }  // 环境不具备时跳过,不误报失败

    u8* buf = AllocSampleBuffer();
    MDVN_CHECK(buf != nullptr);
    if (!buf) { env.Shutdown(); return; }

    u32 n = EncodeTestImage(env.wic, GUID_ContainerFormatPng, 64, 32, 1, buf, kSampleCapacity);
    MDVN_CHECK(n > 0);

    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromMemory(buf, n, env.target);
    MDVN_CHECK(img.status == ImageStatus::Ok);
    MDVN_CHECK_EQ(img.width, 64u);
    MDVN_CHECK_EQ(img.height, 32u);
    MDVN_CHECK(!img.wasDownsampled);
    MDVN_CHECK(img.bitmap != nullptr);
    MDVN_CHECK(decoder.IsInitialized());  // 解码之后 WIC 才被初始化
    if (img.bitmap) img.bitmap->Release();

    FreeSampleBuffer(buf);
    env.Shutdown();
}

// 用例:JPEG 样本解码成功。
MDVN_TEST(Image_DecodeJpegSample) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }

    u8* buf = AllocSampleBuffer();
    if (!buf) { env.Shutdown(); return; }

    u32 n = EncodeTestImage(env.wic, GUID_ContainerFormatJpeg, 48, 24, 1, buf, kSampleCapacity);
    MDVN_CHECK(n > 0);

    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromMemory(buf, n, nullptr);  // 不建位图,只验解码链路
    MDVN_CHECK(img.status == ImageStatus::Ok);
    MDVN_CHECK_EQ(img.width, 48u);
    MDVN_CHECK_EQ(img.height, 24u);

    FreeSampleBuffer(buf);
    env.Shutdown();
}

// 用例:多帧 GIF 只解第 0 帧 —— 返回单张位图与首帧尺寸,不报错、不解出多帧。
MDVN_TEST(Image_DecodeMultiFrameGifTakesFirstFrameOnly) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }

    u8* buf = AllocSampleBuffer();
    if (!buf) { env.Shutdown(); return; }

    u32 n = EncodeTestImage(env.wic, GUID_ContainerFormatGif, 32, 16, 3, buf, kSampleCapacity);
    MDVN_CHECK(n > 0);

    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromMemory(buf, n, env.target);
    MDVN_CHECK(img.status == ImageStatus::Ok);
    MDVN_CHECK_EQ(img.width, 32u);
    MDVN_CHECK_EQ(img.height, 16u);
    if (img.bitmap) img.bitmap->Release();

    FreeSampleBuffer(buf);
    env.Shutdown();
}

// 用例:超大尺寸图应"边解码边缩小"到 kMaxDecodedDimension,而不是整图拒绝。
MDVN_TEST(Image_OversizedSampleIsDownsampledNotRejected) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }

    u8* buf = AllocSampleBuffer();
    if (!buf) { env.Shutdown(); return; }

    const u32 kSrcW = 2048, kSrcH = 1024;  // 长边远超 kMaxDecodedDimension(512)
    u32 n = EncodeTestImage(env.wic, GUID_ContainerFormatPng, kSrcW, kSrcH, 1, buf, kSampleCapacity);
    MDVN_CHECK(n > 0);

    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromMemory(buf, n, env.target);
    MDVN_CHECK(img.status == ImageStatus::Ok);       // 关键:不是 TooLarge/Failed
    MDVN_CHECK(img.wasDownsampled);                   // T33 据此画"已压缩"标签
    MDVN_CHECK(img.width <= mdvn::kMaxDecodedDimension);
    MDVN_CHECK(img.height <= mdvn::kMaxDecodedDimension);
    MDVN_CHECK_EQ(img.width, mdvn::kMaxDecodedDimension);
    // 单图解码后内存被死死限住在 ~1MB 级。
    MDVN_CHECK(DecodedByteSize(img.width, img.height) <= 4ull * 1024ull * 1024ull);
    if (img.bitmap) img.bitmap->Release();

    FreeSampleBuffer(buf);
    env.Shutdown();
}

// 用例:损坏文件(随机字节)走失败占位,不崩溃。
MDVN_TEST(Image_CorruptSampleFailsGracefully) {
    u8 corrupt[512];
    for (u32 i = 0; i < 512; ++i) corrupt[i] = static_cast<u8>((i * 131u + 7u) & 0xFF);
    // 故意带上 PNG 魔数前缀,让格式嗅探通过但后续数据是垃圾。
    const u8 kPngMagic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    memcpy(corrupt, kPngMagic, 8);

    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromMemory(corrupt, 512, nullptr);
    MDVN_CHECK(img.status != ImageStatus::Ok);
    MDVN_CHECK(img.bitmap == nullptr);
}

// 用例:空输入 / 零长度输入不崩溃,返回失败。
MDVN_TEST(Image_EmptyInputFails) {
    ImageDecoder decoder;
    MDVN_CHECK(decoder.DecodeFromMemory(nullptr, 0, nullptr).status == ImageStatus::Failed);
    u8 one = 0;
    MDVN_CHECK(decoder.DecodeFromMemory(&one, 0, nullptr).status == ImageStatus::Failed);
    MDVN_CHECK(decoder.DecodeFromFile(nullptr, nullptr).status == ImageStatus::Failed);
    MDVN_CHECK(decoder.DecodeFromFile(L"", nullptr).status == ImageStatus::Failed);
}

// 用例:不存在的文件路径返回 Failed,不崩溃。
MDVN_TEST(Image_MissingFileFails) {
    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromFile(L"Z:\\mdvn_no_such_image_12345.png", nullptr);
    MDVN_CHECK(img.status == ImageStatus::Failed);
}
