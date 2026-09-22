// markair 图片解码子系统(T30):WIC 解码封装。
//
// 设计要点:
//   - **惰性初始化**:文档里没有图片就不 CoInitializeEx、不创建 IWICImagingFactory
//     (架构 §1 第 4 条),配合根 CMakeLists 的 /DELAYLOAD:windowscodecs.dll,
//     无图文档全程不加载 windowscodecs.dll。
//   - 解码时若原始像素缓冲超过 kMaxDecodedBytes,用 IWICBitmapScaler
//     边解码边缩小,而不是整图拒绝(裁决 #5,2026-09-22 改为体积预算口径)。
//   - 动图 GIF 只解第 0 帧,GetFrame(0) 之后立即释放解码器。
//   - SVG 不进本解码器:由调用方(ImageResidencyManager::DecodeNow)按
//     IsSvgImageRef 分流到 svg_decoder.h(lunasvg 离线栅格化),WIC 本身
//     不认识 svg 格式。
//   - 解码失败一律返回占位状态,不崩溃、不抛异常(本工程禁异常)。
#pragma once

#include <windows.h>

#include "../util/str.h"
#include "../util/types.h"

struct IWICImagingFactory;

namespace Gdiplus {
class Bitmap;
}

namespace markair {

// 单张图片的解码上限:任一边超过该像素数就按比例降采样,与 T32 缓存共用。
// 取 512 是 2026-09-17 裁决的结果:T32 改为"永久缓存、不做淘汰"之后,必须靠
// 更激进的降采样把单图解码后成本压到 ~1MB 级(512×512×4 ≈ 1MB),几十张图
// 累计才仍在内存预算内。高清需求由 T36b"点击查看原图"满足(打开原文件/原始
// 字节,不受此处降采样影响)。
// 仍保留给 ComputeDownscaledSize 这个通用工具函数用(有独立单测),但解码路径
// 已改用下面的体积预算口径,不再引用这个常量。
constexpr u32 kMaxDecodedDimension = 512u;

// 单张图片解码后像素缓冲的体积预算(2026-09-22 裁决):按此反推降采样后的
// 像素尺寸,而不是卡任一边的像素数上限——布局层用"原始尺寸(封顶到可用宽度)"
// 定显示矩形,解码出来的小位图再靠渲染层的双线性插值拉伸画,用清晰度换内存。
// 100KB 意味着典型图片解码后只有一两百像素见方,拉伸到整屏宽度会明显模糊,
// 这是本次裁决主动接受的取舍。
constexpr u64 kMaxDecodedBytes = 100u * 1024u;

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
    RemoteNotLoaded = 5, // 网络图片但尚未下载完成,或 load_remote_images=0(用户手动
                          // 关闭)时等用户点击加载(T34)
};

/**
 * 一次解码的结果。bitmap 可能为空:调用方传 createBitmap=false(单测/仅探测
 * 尺寸场景)或创建位图失败时,width/height/status 仍然有效。
 */
struct DecodedImage {
    Gdiplus::Bitmap* bitmap; // 解码得到的位图,所有权转移给调用方;可为 nullptr
    u32 width;           // 降采样之后的像素宽(解码位图的真实像素宽)
    u32 height;          // 降采样之后的像素高
    ImageStatus status;  // 解码结果状态
    bool wasDownsampled; // 原始像素缓冲超过 kMaxDecodedBytes、真的缩小过时为 true
                          // (T33 据此在图片右下角画"已压缩·点击看原图"提示标签)
    u32 originalWidth;   // 降采样前的原始像素宽;未降采样时与 width 相同,
                          // 解码失败/探测失败时为 0。布局层用它(而不是 width)
                          // 算显示矩形,配合渲染层拉伸,实现"体积按预算砍,
                          // 显示按原尺寸(封顶到可用宽度)"。
    u32 originalHeight;  // 降采样前的原始像素高,语义同上。
};

/**
 * 判断一个图片地址是否是 SVG(按扩展名,忽略 ?query / #fragment 与大小写),
 * 或是 data:image/svg+xml 这种 MIME。SVG 不进 WIC,改走 svg_decoder.h
 * (lunasvg 离线栅格化)。
 *
 * @param href 图片地址(UTF-8 切片),可以是本地路径、URL 或 data: URI。
 * @return 判定为 SVG 返回 true。
 * @example bool svg = markair::IsSvgImageRef(markair::StrSlice{"logo.SVG", 8}); // true
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
 *   markair::u32 w = 0, h = 0;
 *   markair::ComputeDownscaledSize(8000, 4000, 4096, &w, &h); // w=4096, h=2048
 */
void ComputeDownscaledSize(u32 srcWidth, u32 srcHeight, u32 maxDim, u32* outWidth, u32* outHeight);

/**
 * 按 kMaxDecodedBytes 这类体积预算计算降采样之后的目标尺寸(等比,至少 1 像素)。
 * 与 ComputeDownscaledSize 的区别:后者卡的是"任一边的像素数上限",这个卡的是
 * "解码后像素缓冲的总字节数上限"——解码路径(image.cpp / svg_decoder.cpp)
 * 用的是这个。纯数字函数,不依赖 WIC,可脱离 COM 单测。
 *
 * @param srcWidth 原始像素宽(0 视为 1)。
 * @param srcHeight 原始像素高(0 视为 1)。
 * @param maxBytes 解码后像素缓冲(宽×高×4)的字节数上限,传 0 视为不限制。
 * @param outWidth 输出:缩放后的宽,非空。
 * @param outHeight 输出:缩放后的高,非空。
 * @example
 *   markair::u32 w = 0, h = 0;
 *   markair::ComputeDownscaledSizeForBudget(2048, 1024, 100 * 1024, &w, &h);
 *   // w×h×4 <= 100*1024,且 w:h 与原始比例一致
 */
void ComputeDownscaledSizeForBudget(u32 srcWidth, u32 srcHeight, u64 maxBytes,
                                     u32* outWidth, u32* outHeight);

/**
 * 解码后位图占用的像素缓冲字节数(width × height × 4),即 T32 LRU 的计量口径(裁决 #5)。
 *
 * @param width 像素宽。
 * @param height 像素高。
 * @return 字节数(用 u64 避免大图溢出)。
 * @example markair::u64 bytes = markair::DecodedByteSize(1920, 1080); // 8294400
 */
u64 DecodedByteSize(u32 width, u32 height);

/**
 * 把一段 32bpp BGRA 预乘 alpha 的像素缓冲(WIC/lunasvg 解码结果都是这个格式)
 * 包成一个 GDI+ 自己独立拥有内存的 `Gdiplus::Bitmap`,调用方拿到返回值后可以
 * 立即释放 `pixels` 缓冲,不产生悬空引用。
 *
 * 实际分配的位图物理尺寸是 (width+1)×(height+1)——右边、下边各留 1 像素安全
 * 边距,内容仍是原样的 width×height(边距像素未初始化,不会被画出来)。这是
 * 2026-09-22 WinDbg 定位到的一个真实 GDI+ 引擎坑的修复:`Graphics::DrawImage`
 * 做插值拉伸时(不管 Bilinear 还是 HighQualityBilinear)会在源矩形边缘多采样
 * 一点,如果源位图的物理内存刚好只有 width×height 那么大,这次多采样就会读
 * 到分配区之外的未映射页,直接 access violation——命中率随拉伸倍数增大而
 * 升高,markair 现在"体积预算降采样后拉伸到显示尺寸"的策略经常是 5~10 倍拉伸,
 * 稳定复现。加这 1 像素边距后,GDI+ 的越界采样落在自己确实拥有的内存里,不再
 * 崩溃;调用方(`GdiRenderTarget::DrawBitmap`)必须显式传入 width/height 而不是
 * 读 `bitmap->GetWidth()/GetHeight()`(那两个会报出带边距的 (width+1)/(height+1),
 * 用它们当绘制源矩形会画出半像素的垃圾边)。
 *
 * @param width 像素宽,须大于 0。
 * @param height 像素高,须大于 0。
 * @param pixels 源像素数据,按行从上到下、每像素 4 字节 BGRA 预乘,非空。
 * @param strideBytes 每行字节数(通常是 width * 4),须大于 0。
 * @return 新建的位图(调用方持有,用 `delete` 释放;物理尺寸是 (width+1)×(height+1),
 *         画图时源矩形要用原始 width×height,见上文);内存不足等失败场景返回 nullptr。
 * @example
 *   Gdiplus::Bitmap* bmp = markair::MakeOwnedBgraBitmap(64, 32, pixels, 64 * 4);
 *   // 画的时候:DrawImage(bmp, destRect, 0, 0, 64, 32, UnitPixel) —— 不要用
 *   // bmp->GetWidth()/GetHeight(),那会是 65/33。
 */
Gdiplus::Bitmap* MakeOwnedBgraBitmap(u32 width, u32 height, const void* pixels, u32 strideBytes);

/**
 * WIC 解码器封装:惰性持有 COM 初始化与 IWICImagingFactory。
 *
 * 只要不调用任何 Decode* 接口,本对象就不会 CoInitializeEx、不会创建 WIC 工厂,
 * 因此也不会触发 windowscodecs.dll 的延迟加载 —— 这是"无图文档零 WIC 开销"的实现保证。
 *
 * @example
 *   markair::ImageDecoder decoder;
 *   markair::DecodedImage img = decoder.DecodeFromMemory(bytes, len, true);
 *   if (img.status == markair::ImageStatus::Ok && img.bitmap) {
 *       renderTarget->DrawBitmap(img.bitmap, img.width, img.height, rect);
 *       delete img.bitmap;
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
     * @example MARKAIR_CHECK(!decoder.IsInitialized()); // 还没解码过任何图片
     */
    bool IsInitialized() const { return factory_ != nullptr; }

    /**
     * 从内存字节解码一张图片(PNG/JPEG/GIF/BMP 等 WIC 内建编解码器支持的格式)。
     * 多帧 GIF 只取第 0 帧,取完立即释放解码器。
     *
     * @param bytes 压缩图片字节,非空。
     * @param len 字节数;为 0 或超过 kMaxEncodedBytes 分别返回 Failed / TooLarge。
     * @param createBitmap true 时额外建出 Gdiplus::Bitmap;传 false 时只解码取
     *               尺寸、不建位图(单元测试/仅探测尺寸场景)。
     * @return 解码结果;失败时 bitmap 为 nullptr 且 status 非 Ok,绝不崩溃。
     * @example markair::DecodedImage img = decoder.DecodeFromMemory(buf, n, false);
     */
    DecodedImage DecodeFromMemory(const void* bytes, u32 len, bool createBitmap);

    /**
     * 从磁盘文件路径解码一张图片,语义同 DecodeFromMemory。调用方需先用
     * IsSvgImageRef 判断,SVG 不应传入本函数(应走 svg_decoder.h)。
     *
     * @param path 绝对或相对的宽字符文件路径,非空。
     * @param createBitmap true 时额外建出 Gdiplus::Bitmap,false 只取尺寸。
     * @return 解码结果;文件不存在/损坏返回 Failed。
     * @example markair::DecodedImage img = decoder.DecodeFromFile(L"C:\\a\\b.png", true);
     */
    DecodedImage DecodeFromFile(const wchar_t* path, bool createBitmap);

private:
    // 惰性创建 COM + IWICImagingFactory,已创建时直接返回 true。
    bool EnsureFactory();

    // DecodeFromMemory/DecodeFromFile 共用的后半段:从 IWICBitmapDecoder 取第 0 帧、
    // 按需降采样、转 32bppPBGRA、按需创建 Gdiplus::Bitmap。decoder 由调用方释放。
    DecodedImage DecodeFirstFrame(void* wicDecoder, bool createBitmap);

    IWICImagingFactory* factory_; // 惰性创建的 WIC 工厂,未用过时恒为 nullptr
    bool comInitialized_;         // COM 是否由本对象初始化(决定析构时是否 CoUninitialize)
};

}  // namespace markair
