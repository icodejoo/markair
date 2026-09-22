#include "image.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cmath>
#include <cstring>

namespace markair {

namespace {

// 把一个 ASCII 字符折叠成小写(只处理 A-Z,图片扩展名都是 ASCII)。
char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// 判断 slice 的末尾是否是给定的小写 ASCII 后缀(大小写不敏感)。
bool EndsWithIgnoreCase(StrSlice s, const char* suffix, u32 suffixLen) {
    if (s.len < suffixLen) return false;
    const char* tail = s.data + (s.len - suffixLen);
    for (u32 i = 0; i < suffixLen; ++i) {
        if (LowerAscii(tail[i]) != suffix[i]) return false;
    }
    return true;
}

// 判断 slice 是否以给定的小写 ASCII 前缀开头(大小写不敏感)。
bool StartsWithIgnoreCase(StrSlice s, const char* prefix, u32 prefixLen) {
    if (s.len < prefixLen) return false;
    for (u32 i = 0; i < prefixLen; ++i) {
        if (LowerAscii(s.data[i]) != prefix[i]) return false;
    }
    return true;
}

// 去掉 URL 尾部的 ?query 与 #fragment,只留路径部分,供扩展名判断用。
StrSlice StripQueryAndFragment(StrSlice href) {
    for (u32 i = 0; i < href.len; ++i) {
        if (href.data[i] == '?' || href.data[i] == '#') return StrSlice{href.data, i};
    }
    return href;
}

// 失败结果的快捷构造。
DecodedImage MakeStatus(ImageStatus status) {
    return DecodedImage{nullptr, 0, 0, status, false, 0, 0};
}

}  // namespace

bool IsSvgImageRef(StrSlice href) {
    if (!href.data || href.len == 0) return false;
    // data:image/svg+xml;... 这类 URI 不看扩展名,直接看 MIME 前缀。
    if (StartsWithIgnoreCase(href, "data:image/svg", 14)) return true;
    return EndsWithIgnoreCase(StripQueryAndFragment(href), ".svg", 4);
}

void ComputeDownscaledSize(u32 srcWidth, u32 srcHeight, u32 maxDim, u32* outWidth, u32* outHeight) {
    u32 w = srcWidth == 0 ? 1u : srcWidth;
    u32 h = srcHeight == 0 ? 1u : srcHeight;
    if (maxDim == 0 || (w <= maxDim && h <= maxDim)) {
        *outWidth = w;
        *outHeight = h;
        return;
    }
    // 按最长边等比缩放,用 double 避免大尺寸下的整数溢出与精度丢失。
    double longest = (w > h) ? static_cast<double>(w) : static_cast<double>(h);
    double ratio = static_cast<double>(maxDim) / longest;
    u32 nw = static_cast<u32>(static_cast<double>(w) * ratio);
    u32 nh = static_cast<u32>(static_cast<double>(h) * ratio);
    *outWidth = nw == 0 ? 1u : nw;
    *outHeight = nh == 0 ? 1u : nh;
}

void ComputeDownscaledSizeForBudget(u32 srcWidth, u32 srcHeight, u64 maxBytes,
                                     u32* outWidth, u32* outHeight) {
    u32 w = srcWidth == 0 ? 1u : srcWidth;
    u32 h = srcHeight == 0 ? 1u : srcHeight;
    u64 srcBytes = DecodedByteSize(w, h);
    if (maxBytes == 0 || srcBytes <= maxBytes) {
        *outWidth = w;
        *outHeight = h;
        return;
    }
    // 按面积等比缩放:scale^2 = maxBytes/srcBytes,再把 scale 套到宽高上,
    // 用 double 避免大尺寸下的整数溢出与精度丢失(与 ComputeDownscaledSize 同一处理方式)。
    double scale = std::sqrt(static_cast<double>(maxBytes) / static_cast<double>(srcBytes));
    u32 nw = static_cast<u32>(static_cast<double>(w) * scale);
    u32 nh = static_cast<u32>(static_cast<double>(h) * scale);
    *outWidth = nw == 0 ? 1u : nw;
    *outHeight = nh == 0 ? 1u : nh;
}

u64 DecodedByteSize(u32 width, u32 height) {
    return static_cast<u64>(width) * static_cast<u64>(height) * 4ull;
}

Gdiplus::Bitmap* MakeOwnedBgraBitmap(u32 width, u32 height, const void* pixels, u32 strideBytes) {
    if (width == 0 || height == 0 || !pixels || strideBytes == 0) return nullptr;

    // 物理尺寸故意比内容大 1×1(右边/下边各留 1 像素安全边距)——见 image.h 的
    // doc-comment:GDI+ 的 DrawImage 插值拉伸会在源矩形边缘多采样一点,如果
    // 物理内存刚好卡在 width×height,这次多采样就会读到分配区之外,直接崩溃。
    // 用 GDI+ 自己的 Bitmap(w,h,format) 构造函数分配(内部自行管理内存、自选
    // 对齐后的 stride),不是先包一层引用调用方指针的位图再 Clone——旧写法的
    // Clone 结果仍然是"紧贴 width×height"的物理尺寸,边距加在源头（调用方
    // 传入的 pixels 缓冲）无效,必须加在这个最终真正被 DrawImage 读取的位图上。
    Gdiplus::Bitmap* owned = new Gdiplus::Bitmap(
        static_cast<INT>(width) + 1, static_cast<INT>(height) + 1, PixelFormat32bppPARGB);
    if (!owned || owned->GetLastStatus() != Gdiplus::Ok) {
        delete owned;
        return nullptr;
    }

    Gdiplus::Rect lockRect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    Gdiplus::BitmapData bd{};
    if (owned->LockBits(&lockRect, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &bd) !=
        Gdiplus::Ok) {
        delete owned;
        return nullptr;
    }
    const u8* src = static_cast<const u8*>(pixels);
    u8* dst = static_cast<u8*>(bd.Scan0);
    u32 rowBytes = width * 4u;
    for (u32 y = 0; y < height; ++y) {
        memcpy(dst + static_cast<size_t>(y) * bd.Stride, src + static_cast<size_t>(y) * strideBytes,
               rowBytes);
    }
    owned->UnlockBits(&bd);
    return owned;
}

ImageDecoder::ImageDecoder() : factory_(nullptr), comInitialized_(false) {}

ImageDecoder::~ImageDecoder() {
    if (factory_) {
        factory_->Release();
        factory_ = nullptr;
    }
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
}

bool ImageDecoder::EnsureFactory() {
    if (factory_) return true;

    // 惰性初始化:只有真的要解码图片时才走到这里,无图文档永远不会触碰 COM/WIC。
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(hr)) {
        comInitialized_ = true;
    } else if (hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
        return false;  // COM 无法初始化,放弃图片功能,不影响文本渲染
    }

    IWICImagingFactory* factory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;
    factory_ = factory;
    return true;
}

DecodedImage ImageDecoder::DecodeFirstFrame(void* wicDecoder, bool createBitmap) {
    IWICBitmapDecoder* decoder = static_cast<IWICBitmapDecoder*>(wicDecoder);

    // 动图 GIF 只解第 0 帧(裁决):取完帧就把解码器释放掉,不留住整个文件流。
    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) return MakeStatus(ImageStatus::Failed);

    UINT srcW = 0, srcH = 0;
    if (FAILED(frame->GetSize(&srcW, &srcH)) || srcW == 0 || srcH == 0) {
        frame->Release();
        return MakeStatus(ImageStatus::Failed);
    }

    u32 dstW = 0, dstH = 0;
    ComputeDownscaledSizeForBudget(srcW, srcH, kMaxDecodedBytes, &dstW, &dstH);

    // 超尺寸时"边解码边缩小"(裁决 #5):IWICBitmapScaler 挂在帧解码器上游,
    // 不会先把原始整图展开成像素缓冲,严格限住单张图片的最坏内存。
    IWICBitmapSource* source = frame;
    IWICBitmapScaler* scaler = nullptr;
    bool downsampled = false;
    if (dstW != srcW || dstH != srcH) {
        if (SUCCEEDED(factory_->CreateBitmapScaler(&scaler)) && scaler &&
            SUCCEEDED(scaler->Initialize(frame, dstW, dstH, WICBitmapInterpolationModeFant))) {
            source = scaler;
            downsampled = true;  // 真的缩小过,T33 才画"已压缩"提示标签
        } else {
            if (scaler) { scaler->Release(); scaler = nullptr; }
            // 缩放器建不出来时退回原尺寸解码,尺寸信息仍然有效。
            dstW = srcW;
            dstH = srcH;
        }
    }

    IWICFormatConverter* converter = nullptr;
    HRESULT hr = factory_->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        if (scaler) scaler->Release();
        frame->Release();
        return MakeStatus(ImageStatus::Failed);
    }
    hr = converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) {
        converter->Release();
        if (scaler) scaler->Release();
        frame->Release();
        return MakeStatus(ImageStatus::Failed);
    }

    DecodedImage result{nullptr, dstW, dstH, ImageStatus::Ok, downsampled, srcW, srcH};

    if (createBitmap) {
        // 自己申请一块像素缓冲接住转换器输出,再包成 GDI+ 位图——不像原来的
        // D2D CreateBitmapFromWicBitmap 那样能让渲染后端直接拉,GDI+ 没有等价
        // 的"零拷贝从 WIC 建位图"接口,这一步比迁移前多一次内存拷贝(单图
        // 至多 kMaxDecodedBytes=100KB 级别,可忽略)。
        UINT32 stride = dstW * 4u;
        UINT32 bufSize = stride * dstH;
        BYTE* buf = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, bufSize));
        if (buf && SUCCEEDED(converter->CopyPixels(nullptr, stride, bufSize, buf))) {
            result.bitmap = MakeOwnedBgraBitmap(dstW, dstH, buf, stride);
            if (!result.bitmap) result.status = ImageStatus::Failed;
        } else {
            result.status = ImageStatus::Failed;
        }
        if (buf) HeapFree(GetProcessHeap(), 0, buf);
    } else {
        // 不建位图(单测/仅探测尺寸):仍然真实跑一次像素拷贝,确保"能解码"
        // 这个结论不是靠元数据猜出来的,损坏文件会在这一步暴露。
        WICRect rect{0, 0, 1, 1};
        BYTE probe[4] = {0, 0, 0, 0};
        if (FAILED(converter->CopyPixels(&rect, 4, 4, probe))) {
            result.status = ImageStatus::Failed;
        }
    }

    converter->Release();
    if (scaler) scaler->Release();
    frame->Release();
    return result;
}

DecodedImage ImageDecoder::DecodeFromMemory(const void* bytes, u32 len, bool createBitmap) {
    if (!bytes || len == 0) return MakeStatus(ImageStatus::Failed);
    if (len > kMaxEncodedBytes) return MakeStatus(ImageStatus::TooLarge);
    if (!EnsureFactory()) return MakeStatus(ImageStatus::Failed);

    IWICStream* stream = nullptr;
    if (FAILED(factory_->CreateStream(&stream)) || !stream) return MakeStatus(ImageStatus::Failed);
    // InitializeFromMemory 不拷贝字节,调用方缓冲区需活到解码结束——本函数内同步用完即止。
    HRESULT hr = stream->InitializeFromMemory(
        const_cast<BYTE*>(static_cast<const BYTE*>(bytes)), len);
    if (FAILED(hr)) {
        stream->Release();
        return MakeStatus(ImageStatus::Failed);
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory_->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        stream->Release();
        return MakeStatus(ImageStatus::Failed);  // 损坏文件/未知格式走占位
    }

    DecodedImage result = DecodeFirstFrame(decoder, createBitmap);
    decoder->Release();
    stream->Release();
    return result;
}

DecodedImage ImageDecoder::DecodeFromFile(const wchar_t* path, bool createBitmap) {
    if (!path || path[0] == 0) return MakeStatus(ImageStatus::Failed);
    if (!EnsureFactory()) return MakeStatus(ImageStatus::Failed);

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory_->CreateDecoderFromFilename(
        path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) return MakeStatus(ImageStatus::Failed);

    DecodedImage result = DecodeFirstFrame(decoder, createBitmap);
    decoder->Release();
    return result;
}

}  // namespace markair
