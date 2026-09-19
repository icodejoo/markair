// SVG 图片解码测试:lunasvg 离线栅格化封装(src/assets/svg_decoder.h)。
// 样本 SVG 直接以字符串字面量内嵌,不需要 WIC(与 WIC 样本走不同代码路径)。
#include "mdvn_test.h"
#include "image_test_support.h"
#include "../src/assets/svg_decoder.h"
#include "../src/assets/image.h"

#include <cstring>

using mdvn::DecodedImage;
using mdvn::DecodeSvgFromMemory;
using mdvn::ImageStatus;
using mdvn::kMaxDecodedDimension;
using mdvn_test::ImageTestEnv;

namespace {

const char kSimpleSvg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"32\">"
    "<rect width=\"64\" height=\"32\" fill=\"#ff0000\"/></svg>";

// viewBox 带 clip-path(lunasvg 支持,验证不只是纯 shape 解析)。
const char kClipSvg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"20\">"
    "<clipPath id=\"c\"><circle cx=\"20\" cy=\"10\" r=\"8\"/></clipPath>"
    "<rect width=\"40\" height=\"20\" fill=\"#00ff00\" clip-path=\"url(#c)\"/></svg>";

const char kOversizedSvg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2048\" height=\"1024\">"
    "<rect width=\"2048\" height=\"1024\" fill=\"#0000ff\"/></svg>";

}  // namespace

// 用例:基础矩形 SVG 解码成功,尺寸与源一致,不触发降采样。
MDVN_TEST(SvgDecoder_SimpleRectDecodesOk) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }  // 环境不具备时跳过,不误报失败

    DecodedImage img = DecodeSvgFromMemory(kSimpleSvg, sizeof(kSimpleSvg) - 1, env.target);
    MDVN_CHECK(img.status == ImageStatus::Ok);
    MDVN_CHECK_EQ(img.width, 64u);
    MDVN_CHECK_EQ(img.height, 32u);
    MDVN_CHECK(!img.wasDownsampled);
    MDVN_CHECK(img.bitmap != nullptr);
    if (img.bitmap) img.bitmap->Release();

    env.Shutdown();
}

// 用例:clip-path 这类 lunasvg 支持的高级特性不应导致解码失败。
MDVN_TEST(SvgDecoder_ClipPathDecodesOk) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }

    DecodedImage img = DecodeSvgFromMemory(kClipSvg, sizeof(kClipSvg) - 1, env.target);
    MDVN_CHECK(img.status == ImageStatus::Ok);
    MDVN_CHECK_EQ(img.width, 40u);
    MDVN_CHECK_EQ(img.height, 20u);
    if (img.bitmap) img.bitmap->Release();

    env.Shutdown();
}

// 用例:超大 SVG 应等比降采样到 kMaxDecodedDimension,而不是整图拒绝(与 WIC 路径同一口径)。
MDVN_TEST(SvgDecoder_OversizedIsDownsampledNotRejected) {
    ImageTestEnv env{};
    if (!env.Init()) { env.Shutdown(); return; }

    DecodedImage img = DecodeSvgFromMemory(kOversizedSvg, sizeof(kOversizedSvg) - 1, env.target);
    MDVN_CHECK(img.status == ImageStatus::Ok);
    MDVN_CHECK(img.wasDownsampled);
    MDVN_CHECK(img.width <= kMaxDecodedDimension);
    MDVN_CHECK(img.height <= kMaxDecodedDimension);
    MDVN_CHECK_EQ(img.width, kMaxDecodedDimension);
    if (img.bitmap) img.bitmap->Release();

    env.Shutdown();
}

// 用例:不带渲染目标时只探测尺寸,不创建位图(单测/仅尺寸探测场景)。
MDVN_TEST(SvgDecoder_NullTargetOnlyProbesSize) {
    DecodedImage img = DecodeSvgFromMemory(kSimpleSvg, sizeof(kSimpleSvg) - 1, nullptr);
    MDVN_CHECK(img.status == ImageStatus::Ok);
    MDVN_CHECK_EQ(img.width, 64u);
    MDVN_CHECK_EQ(img.height, 32u);
    MDVN_CHECK(img.bitmap == nullptr);
}

// 用例:损坏/非法 SVG 文本走失败占位,不崩溃。
MDVN_TEST(SvgDecoder_MalformedXmlFailsGracefully) {
    const char kBroken[] = "<svg><rect width=\"10\"";  // 未闭合标签
    DecodedImage img = DecodeSvgFromMemory(kBroken, sizeof(kBroken) - 1, nullptr);
    MDVN_CHECK(img.status != ImageStatus::Ok);
    MDVN_CHECK(img.bitmap == nullptr);
}

// 用例:空输入 / 超限输入不崩溃。
MDVN_TEST(SvgDecoder_EmptyAndOversizedInputFails) {
    MDVN_CHECK(DecodeSvgFromMemory(nullptr, 0, nullptr).status == ImageStatus::Failed);
    char one = 0;
    MDVN_CHECK(DecodeSvgFromMemory(&one, 0, nullptr).status == ImageStatus::Failed);
}
