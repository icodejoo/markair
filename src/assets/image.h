// mdvn 图片解码子系统(T30):WIC 解码封装。
//
// 设计要点:
//   - **惰性初始化**:文档里没有图片就不 CoInitializeEx、不创建 IWICImagingFactory
//     (架构 §1 第 4 条),配合根 CMakeLists 的 /DELAYLOAD:windowscodecs.dll,
//     无图文档全程不加载 windowscodecs.dll。
//   - 解码时若原始像素尺寸超过 kMaxDecodedDimension,用 IWICBitmapScaler
//     边解码边缩小,而不是整图拒绝(裁决 #5)。
//   - 动图 GIF 只解第 0 帧,GetFrame(0) 之后立即释放解码器。
//   - SVG 直接判定为不支持,不进 WIC。
//   - 解码失败一律返回占位状态,不崩溃、不抛异常(本工程禁异常)。
#pragma once

#include <d2d1.h>

#include "../util/str.h"
#include "../util/types.h"

struct IWICImagingFactory;

namespace mdvn {

// 单张图片的解码上限:任一边超过该像素数就按比例降采样,与 T32 缓存共用。
// 取 512 是 2026-09-17 裁决的结果:T32 改为"永久缓存、不做淘汰"之后,必须靠
// 更激进的降采样把单图解码后成本压到 ~1MB 级(512×512×4 ≈ 1MB),几十张图
// 累计才仍在内存预算内。高清需求由 T36b"点击查看原图"满足(打开原文件/原始
// 字节,不受此处降采样影响)。
constexpr u32 kMaxDecodedDimension = 512u;

/** 单张图片压缩字节数上限(16MB),超过直接判超限,不进 WIC。 */
constexpr u32 kMaxEncodedBytes = 16u * 1024u * 1024u;

/** 占位块默认尺寸(DIP),尺寸未知时先用它占位,解码出真实尺寸后再重排(T33)。 */
constexpr float kPlaceholderWidthDip = 240.0f;
constexpr float kPlaceholderHeightDip = 135.0f;

/**
 * 一张图片在界面上的状态。五种"非 Ok"状态共用同一个占位块绘制函数,只换文案(T33)。
 */
enum class ImageStatus : u8 {
    Ok = 0,            // 解码成功,位图可用
    NotLoaded = 1,     // 尚未尝试解码(首次出现在视口内之前)
    Failed = 2,        // 文件损坏 / 读不到 / WIC 解码失败
    Unsupported = 3,   // 格式不支持(SVG)
    TooLarge = 4,      // 压缩字节数超过 kMaxEncodedBytes
    RemoteNotLoaded = 5, // 网络图片且 load_remote_images=0,等用户点击加载(T34)
};

/**
 * 一次解码的结果。bitmap 可能为空:传入的渲染目标为空(单测场景)或
 * 创建 D2D 位图失败时,width/height/status 仍然有效。
 */
struct DecodedImage {
    ID2D1Bitmap* bitmap; // 解码得到的 D2D 位图,所有权转移给调用方;可为 nullptr
    u32 width;           // 降采样之后的像素宽
    u32 height;          // 降采样之后的像素高
    ImageStatus status;  // 解码结果状态
    bool wasDownsampled; // 原始尺寸超过 kMaxDecodedDimension、真的缩小过时为 true
                          // (T33 据此在图片右下角画"已压缩·点击看原图"提示标签)
};

/**
 * 判断一个图片地址是否是 SVG(按扩展名,忽略 ?query / #fragment 与大小写),
 * 或是 data:image/svg+xml 这种 MIME。SVG 不进 WIC,直接走占位(裁决)。
 *
 * @param href 图片地址(UTF-8 切片),可以是本地路径、URL 或 data: URI。
 * @return 判定为 SVG 返回 true。
 * @example bool svg = mdvn::IsSvgImageRef(mdvn::StrSlice{"logo.SVG", 8}); // true
 */
bool IsSvgImageRef(StrSlice href);

/**
 * 按 kMaxDecodedDimension 计算降采样之后的目标尺寸(等比,至少 1 像素)。
 * 纯数字函数,不依赖 WIC,可脱离 COM 单测。
 *
 * @param srcWidth 原始像素宽(0 视为 1)。
 * @param srcHeight 原始像素高(0 视为 1)。
 * @param maxDim 任一边的像素上限,传 0 视为不限制。
 * @param outWidth 输出:缩放后的宽,非空。
 * @param outHeight 输出:缩放后的高,非空。
 * @example
 *   mdvn::u32 w = 0, h = 0;
 *   mdvn::ComputeDownscaledSize(8000, 4000, 4096, &w, &h); // w=4096, h=2048
 */
void ComputeDownscaledSize(u32 srcWidth, u32 srcHeight, u32 maxDim, u32* outWidth, u32* outHeight);

/**
 * 解码后位图占用的像素缓冲字节数(width × height × 4),即 T32 LRU 的计量口径(裁决 #5)。
 *
 * @param width 像素宽。
 * @param height 像素高。
 * @return 字节数(用 u64 避免大图溢出)。
 * @example mdvn::u64 bytes = mdvn::DecodedByteSize(1920, 1080); // 8294400
 */
u64 DecodedByteSize(u32 width, u32 height);

/**
 * WIC 解码器封装:惰性持有 COM 初始化与 IWICImagingFactory。
 *
 * 只要不调用任何 Decode* 接口,本对象就不会 CoInitializeEx、不会创建 WIC 工厂,
 * 因此也不会触发 windowscodecs.dll 的延迟加载 —— 这是"无图文档零 WIC 开销"的实现保证。
 *
 * @example
 *   mdvn::ImageDecoder decoder;
 *   mdvn::DecodedImage img = decoder.DecodeFromMemory(bytes, len, renderTarget);
 *   if (img.status == mdvn::ImageStatus::Ok && img.bitmap) {
 *       renderTarget->DrawBitmap(img.bitmap, rect);
 *       img.bitmap->Release();
 *   }
 */
class ImageDecoder {
public:
    // 构造一个未初始化的解码器,不做任何 COM 调用。
    ImageDecoder();

    // 释放 WIC 工厂;若 COM 是本对象初始化的,再 CoUninitialize。
    ~ImageDecoder();

    ImageDecoder(const ImageDecoder&) = delete;
    ImageDecoder& operator=(const ImageDecoder&) = delete;

    /**
     * WIC 工厂是否已经真正创建过(即本进程是否已经碰过 windowscodecs.dll)。
     * @return 已惰性初始化返回 true。
     * @example MDVN_CHECK(!decoder.IsInitialized()); // 还没解码过任何图片
     */
    bool IsInitialized() const { return factory_ != nullptr; }

    /**
     * 从内存字节解码一张图片(PNG/JPEG/GIF/BMP 等 WIC 内建编解码器支持的格式)。
     * 多帧 GIF 只取第 0 帧,取完立即释放解码器。
     *
     * @param bytes 压缩图片字节,非空。
     * @param len 字节数;为 0 或超过 kMaxEncodedBytes 分别返回 Failed / TooLarge。
     * @param target D2D 渲染目标,用于创建 ID2D1Bitmap;传 nullptr 时只解码取尺寸、
     *               不创建位图(单元测试/仅探测尺寸场景)。
     * @return 解码结果;失败时 bitmap 为 nullptr 且 status 非 Ok,绝不崩溃。
     * @example mdvn::DecodedImage img = decoder.DecodeFromMemory(buf, n, nullptr);
     */
    DecodedImage DecodeFromMemory(const void* bytes, u32 len, ID2D1RenderTarget* target);

    /**
     * 从磁盘文件路径解码一张图片,语义同 DecodeFromMemory。
     * 路径以 .svg 结尾时直接返回 Unsupported,不进 WIC。
     *
     * @param path 绝对或相对的宽字符文件路径,非空。
     * @param target D2D 渲染目标,可为 nullptr(只取尺寸)。
     * @return 解码结果;文件不存在/损坏返回 Failed。
     * @example mdvn::DecodedImage img = decoder.DecodeFromFile(L"C:\\a\\b.png", target);
     */
    DecodedImage DecodeFromFile(const wchar_t* path, ID2D1RenderTarget* target);

private:
    // 惰性创建 COM + IWICImagingFactory,已创建时直接返回 true。
    bool EnsureFactory();

    // DecodeFromMemory/DecodeFromFile 共用的后半段:从 IWICBitmapDecoder 取第 0 帧、
    // 按需降采样、转 32bppPBGRA、按需创建 D2D 位图。decoder 由调用方释放。
    DecodedImage DecodeFirstFrame(void* wicDecoder, ID2D1RenderTarget* target);

    IWICImagingFactory* factory_; // 惰性创建的 WIC 工厂,未用过时恒为 nullptr
    bool comInitialized_;         // COM 是否由本对象初始化(决定析构时是否 CoUninitialize)
};

}  // namespace mdvn
