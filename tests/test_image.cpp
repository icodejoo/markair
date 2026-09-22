// T30 覆盖测试:WIC 解码封装。
//
// 六个样本(验收标准原文要求):PNG / JPEG / 多帧 GIF / 超大尺寸 / 损坏文件 / .svg。
// 前四个用 WIC 编码器在内存里现场生成(见 image_test_support.h 的理由说明),
// 损坏文件用一段随机字节,.svg 走"不进 WIC"的纯判定路径。
#include "markair_test.h"
#include "image_test_support.h"
#include "../src/assets/image.h"

#include <cstring>

using markair::ComputeDownscaledSize;
using markair::ComputeDownscaledSizeForBudget;
using markair::DecodedByteSize;
using markair::DecodedImage;
using markair::ImageDecoder;
using markair::ImageStatus;
using markair::IsSvgImageRef;
using markair::StrSlice;
using markair::u32;
using markair::u8;
using markair_test::EncodeTestImage;
using markair_test::ImageTestEnv;

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
MARKAIR_TEST(Image_SvgDetection) {
    MARKAIR_CHECK(IsSvgImageRef(StrSlice{"logo.svg", 8}));
    MARKAIR_CHECK(IsSvgImageRef(StrSlice{"LOGO.SVG", 8}));
    MARKAIR_CHECK(IsSvgImageRef(StrSlice{"a/b/c.svg?v=2", 13}));
    MARKAIR_CHECK(IsSvgImageRef(StrSlice{"data:image/svg+xml;base64,AAA", 29}));
    MARKAIR_CHECK(!IsSvgImageRef(StrSlice{"logo.png", 8}));
    MARKAIR_CHECK(!IsSvgImageRef(StrSlice{"svg", 3}));
    MARKAIR_CHECK(!IsSvgImageRef(StrSlice{nullptr, 0}));
}

// 用例:降采样尺寸计算(纯数字函数),等比且至少 1 像素。
MARKAIR_TEST(Image_DownscaledSizeMath) {
    u32 w = 0, h = 0;
    ComputeDownscaledSize(8000, 4000, 512, &w, &h);
    MARKAIR_CHECK_EQ(w, 512u);
    MARKAIR_CHECK_EQ(h, 256u);

    // 未超限时原样返回。
    ComputeDownscaledSize(100, 50, 512, &w, &h);
    MARKAIR_CHECK_EQ(w, 100u);
    MARKAIR_CHECK_EQ(h, 50u);

    // 极端长条图:短边缩到 0 时钳到 1,不产出 0 尺寸。
    ComputeDownscaledSize(100000, 1, 512, &w, &h);
    MARKAIR_CHECK_EQ(w, 512u);
    MARKAIR_CHECK_EQ(h, 1u);

    // maxDim 传 0 表示不限制。
    ComputeDownscaledSize(9000, 9000, 0, &w, &h);
    MARKAIR_CHECK_EQ(w, 9000u);
}

// 用例:体积预算口径的降采样计算(解码路径实际使用的公式)。
MARKAIR_TEST(Image_DownscaledSizeForBudgetMath) {
    u32 w = 0, h = 0;
    // 2048x1024 原图 = 8388608 字节,远超 100KB 预算,按面积等比缩小。
    ComputeDownscaledSizeForBudget(2048, 1024, 100u * 1024u, &w, &h);
    MARKAIR_CHECK(DecodedByteSize(w, h) <= 100u * 1024u);
    // 宽高比不应跑偏(允许取整误差 1)。
    double srcRatio = 2048.0 / 1024.0;
    double dstRatio = static_cast<double>(w) / static_cast<double>(h);
    MARKAIR_CHECK(dstRatio > srcRatio - 0.1 && dstRatio < srcRatio + 0.1);

    // 未超预算时原样返回。
    ComputeDownscaledSizeForBudget(100, 50, 100u * 1024u, &w, &h);
    MARKAIR_CHECK_EQ(w, 100u);
    MARKAIR_CHECK_EQ(h, 50u);

    // maxBytes 传 0 表示不限制。
    ComputeDownscaledSizeForBudget(9000, 9000, 0, &w, &h);
    MARKAIR_CHECK_EQ(w, 9000u);

    // 极端长条图:短边不会缩到 0。
    ComputeDownscaledSizeForBudget(100000, 1, 100u * 1024u, &w, &h);
    MARKAIR_CHECK(h >= 1u);
}

// 用例:LRU/预算计量口径 —— 解码后像素缓冲字节数 = w × h × 4。
MARKAIR_TEST(Image_DecodedByteSize) {
    MARKAIR_CHECK_EQ(DecodedByteSize(1920, 1080), 8294400ull);
    MARKAIR_CHECK_EQ(DecodedByteSize(512, 512), 1048576ull);  // 降采样上限下的单图 ~1MB
    MARKAIR_CHECK_EQ(DecodedByteSize(0, 0), 0ull);
}

// 用例:未解码过任何图片时,解码器不应初始化 WIC(惰性初始化的代码层证据)。
MARKAIR_TEST(Image_DecoderIsLazyBeforeFirstUse) {
    ImageDecoder decoder;
    MARKAIR_CHECK(!decoder.IsInitialized());
}

// 用例:PNG 样本解码成功,尺寸正确,未触发降采样。
MARKAIR_TEST(Image_DecodePngSample) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }  // 环境不具备时跳过,不误报失败

    u8* buf = AllocSampleBuffer();
    MARKAIR_CHECK(buf != nullptr);
    if (!buf) { env.Shutdown(); return; }

    u32 n = EncodeTestImage(env.wic, GUID_ContainerFormatPng, 64, 32, 1, buf, kSampleCapacity);
    MARKAIR_CHECK(n > 0);

    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromMemory(buf, n, env.target);
    MARKAIR_CHECK(img.status == ImageStatus::Ok);
    MARKAIR_CHECK_EQ(img.width, 64u);
    MARKAIR_CHECK_EQ(img.height, 32u);
    MARKAIR_CHECK(!img.wasDownsampled);
    MARKAIR_CHECK(img.bitmap != nullptr);
    MARKAIR_CHECK(decoder.IsInitialized());  // 解码之后 WIC 才被初始化
    if (img.bitmap) img.bitmap->Release();

    FreeSampleBuffer(buf);
    env.Shutdown();
}

// 用例:JPEG 样本解码成功。
MARKAIR_TEST(Image_DecodeJpegSample) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }

    u8* buf = AllocSampleBuffer();
    if (!buf) { env.Shutdown(); return; }

    u32 n = EncodeTestImage(env.wic, GUID_ContainerFormatJpeg, 48, 24, 1, buf, kSampleCapacity);
    MARKAIR_CHECK(n > 0);

    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromMemory(buf, n, nullptr);  // 不建位图,只验解码链路
    MARKAIR_CHECK(img.status == ImageStatus::Ok);
    MARKAIR_CHECK_EQ(img.width, 48u);
    MARKAIR_CHECK_EQ(img.height, 24u);

    FreeSampleBuffer(buf);
    env.Shutdown();
}

// 用例:多帧 GIF 只解第 0 帧 —— 返回单张位图与首帧尺寸,不报错、不解出多帧。
MARKAIR_TEST(Image_DecodeMultiFrameGifTakesFirstFrameOnly) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }

    u8* buf = AllocSampleBuffer();
    if (!buf) { env.Shutdown(); return; }

    u32 n = EncodeTestImage(env.wic, GUID_ContainerFormatGif, 32, 16, 3, buf, kSampleCapacity);
    MARKAIR_CHECK(n > 0);

    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromMemory(buf, n, env.target);
    MARKAIR_CHECK(img.status == ImageStatus::Ok);
    MARKAIR_CHECK_EQ(img.width, 32u);
    MARKAIR_CHECK_EQ(img.height, 16u);
    if (img.bitmap) img.bitmap->Release();

    FreeSampleBuffer(buf);
    env.Shutdown();
}

// 用例:超大尺寸图应"边解码边缩小"到 kMaxDecodedBytes 体积预算内,而不是整图拒绝;
// originalWidth/originalHeight 应保留降采样前的真实尺寸供布局层显示用。
MARKAIR_TEST(Image_OversizedSampleIsDownsampledNotRejected) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }

    u8* buf = AllocSampleBuffer();
    if (!buf) { env.Shutdown(); return; }

    const u32 kSrcW = 2048, kSrcH = 1024;  // 远超 100KB 体积预算
    u32 n = EncodeTestImage(env.wic, GUID_ContainerFormatPng, kSrcW, kSrcH, 1, buf, kSampleCapacity);
    MARKAIR_CHECK(n > 0);

    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromMemory(buf, n, env.target);
    MARKAIR_CHECK(img.status == ImageStatus::Ok);       // 关键:不是 TooLarge/Failed
    MARKAIR_CHECK(img.wasDownsampled);                   // T33 据此画"已压缩"标签
    MARKAIR_CHECK(DecodedByteSize(img.width, img.height) <= markair::kMaxDecodedBytes);
    MARKAIR_CHECK_EQ(img.originalWidth, kSrcW);
    MARKAIR_CHECK_EQ(img.originalHeight, kSrcH);
    if (img.bitmap) img.bitmap->Release();

    FreeSampleBuffer(buf);
    env.Shutdown();
}

// 用例:损坏文件(随机字节)走失败占位,不崩溃。
MARKAIR_TEST(Image_CorruptSampleFailsGracefully) {
    u8 corrupt[512];
    for (u32 i = 0; i < 512; ++i) corrupt[i] = static_cast<u8>((i * 131u + 7u) & 0xFF);
    // 故意带上 PNG 魔数前缀,让格式嗅探通过但后续数据是垃圾。
    const u8 kPngMagic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    memcpy(corrupt, kPngMagic, 8);

    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromMemory(corrupt, 512, nullptr);
    MARKAIR_CHECK(img.status != ImageStatus::Ok);
    MARKAIR_CHECK(img.bitmap == nullptr);
}

// 用例:空输入 / 零长度输入不崩溃,返回失败。
MARKAIR_TEST(Image_EmptyInputFails) {
    ImageDecoder decoder;
    MARKAIR_CHECK(decoder.DecodeFromMemory(nullptr, 0, nullptr).status == ImageStatus::Failed);
    u8 one = 0;
    MARKAIR_CHECK(decoder.DecodeFromMemory(&one, 0, nullptr).status == ImageStatus::Failed);
    MARKAIR_CHECK(decoder.DecodeFromFile(nullptr, nullptr).status == ImageStatus::Failed);
    MARKAIR_CHECK(decoder.DecodeFromFile(L"", nullptr).status == ImageStatus::Failed);
}

// 用例:不存在的文件路径返回 Failed,不崩溃。
MARKAIR_TEST(Image_MissingFileFails) {
    ImageDecoder decoder;
    DecodedImage img = decoder.DecodeFromFile(L"Z:\\markair_no_such_image_12345.png", nullptr);
    MARKAIR_CHECK(img.status == ImageStatus::Failed);
}
