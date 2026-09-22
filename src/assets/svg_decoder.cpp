#include "svg_decoder.h"

#include <lunasvg.h>

namespace markair {

namespace {

// 失败结果的快捷构造,与 image.cpp 同一套口径。
DecodedImage MakeStatus(ImageStatus status) {
    return DecodedImage{nullptr, 0, 0, status, false, 0, 0};
}

}  // namespace

DecodedImage DecodeSvgFromMemory(const void* bytes, u32 len, bool createBitmap) {
    if (!bytes || len == 0) return MakeStatus(ImageStatus::Failed);
    if (len > kMaxEncodedBytes) return MakeStatus(ImageStatus::TooLarge);

    // lunasvg 的 XML 解析器不认 UTF-8 BOM(EF BB BF),带 BOM 的 SVG 文件会直接
    // 解析失败,这里跳过开头的 BOM 再喂给 lunasvg。
    const char* data = static_cast<const char*>(bytes);
    size_t size = static_cast<size_t>(len);
    if (size >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        data += 3;
        size -= 3;
    }

    auto document = lunasvg::Document::loadFromData(data, size);
    if (!document) return MakeStatus(ImageStatus::Failed);

    // 内在尺寸取自 width/height(缺失时退化用 lunasvg 自身默认值),与 WIC 路径
    // 的 GetSize() 语义对齐,再套用同一条降采样公式(裁决 #5)。上限钳到
    // kMaxEncodedBytes 同量级(16M)——畸形 SVG(如 width="1e30")转 u32 是
    // 未定义行为,这里先在 float 域夹住,不能等 cast 完再夹。
    constexpr float kMaxSvgDim = 16.0f * 1024.0f * 1024.0f;
    float rawW = document->width();
    float rawH = document->height();
    if (!(rawW > 0.0f) || !(rawH > 0.0f)) return MakeStatus(ImageStatus::Failed);  // 含 NaN(!(NaN>0)为真)
    if (rawW > kMaxSvgDim) rawW = kMaxSvgDim;
    if (rawH > kMaxSvgDim) rawH = kMaxSvgDim;
    u32 srcW = static_cast<u32>(rawW);
    u32 srcH = static_cast<u32>(rawH);
    if (srcW == 0 || srcH == 0) return MakeStatus(ImageStatus::Failed);

    u32 dstW = 0, dstH = 0;
    ComputeDownscaledSizeForBudget(srcW, srcH, kMaxDecodedBytes, &dstW, &dstH);
    bool downsampled = (dstW != srcW) || (dstH != srcH);

    lunasvg::Bitmap bitmap = document->renderToBitmap(static_cast<int>(dstW), static_cast<int>(dstH));
    if (bitmap.isNull()) return MakeStatus(ImageStatus::Failed);

    DecodedImage result{nullptr, dstW, dstH, ImageStatus::Ok, downsampled, srcW, srcH};

    if (createBitmap) {
        // lunasvg 输出 ARGB32_Premultiplied,内存字节序与 GDI+ 的
        // PixelFormat32bppPARGB 完全一致,与 WIC 路径共用同一个
        // MakeOwnedBgraBitmap 包装函数,不需要再中转一次像素格式转换。
        result.bitmap = MakeOwnedBgraBitmap(dstW, dstH, bitmap.data(),
                                            static_cast<u32>(bitmap.stride()));
        if (!result.bitmap) result.status = ImageStatus::Failed;
    }

    return result;
}

}  // namespace markair
