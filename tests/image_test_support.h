// T30/T32 测试用的 WIC 脚手架:在内存里现场编码出 PNG / JPEG / 多帧 GIF /
// 超大尺寸图等样本字节。
//
// 为什么现场编码而不是往仓库里塞二进制样本文件:
//   - 样本字节完全可复现、随测试一起走,不需要 .gitattributes / LFS;
//   - 用的就是被测系统同一套 WIC 编解码器,不会出现"样本文件在别的机器上解不开"。
// 唯一的例外是"损坏文件"与 ".svg" 两个样本 —— 它们本来就不需要是合法图片。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

#include "../src/util/types.h"

namespace markair_test {

/**
 * 测试用的 WIC 环境:COM + WIC 工厂。
 *
 * `target` 字段(2026-09-22 GDI+ 迁移后)只是个 bool——解码函数签名已经改成
 * `bool createBitmap`,Init() 成功后置为 true,失去了原来"真实渲染目标指针"
 * 的含义,但字段名保留不改,这样调用点 `decoder.DecodeFromMemory(buf, n,
 * env.target)` 不用逐个改。
 */
struct ImageTestEnv {
    IWICImagingFactory* wic;
    bool target;
    bool comInitialized;

    /**
     * 初始化全部环境。
     * @return 全部就绪返回 true;任一环节失败返回 false(调用方应跳过相关断言)。
     * @example ImageTestEnv env{}; if (env.Init()) { ... env.Shutdown(); }
     */
    bool Init() {
        wic = nullptr;
        target = false;
        comInitialized = false;

        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(hr)) comInitialized = true;
        else if (hr != RPC_E_CHANGED_MODE && hr != S_FALSE) return false;

        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&wic))) || !wic) {
            return false;
        }
        target = true;
        return true;
    }

    // 释放全部 COM 对象(与 Init 成对调用,允许 Init 半途失败后调用)。
    void Shutdown() {
        target = false;
        if (wic) { wic->Release(); wic = nullptr; }
        if (comInitialized) { CoUninitialize(); comInitialized = false; }
    }
};

/**
 * 在内存里编码一张(或多帧)纯色测试图。
 *
 * @param wic 已创建的 WIC 工厂。
 * @param containerFormat 目标容器格式(GUID_ContainerFormatPng / Jpeg / Gif)。
 * @param width 图像像素宽。
 * @param height 图像像素高。
 * @param frameCount 帧数(GIF 传 >1 即得到多帧动图;其余格式传 1)。
 * @param out 输出缓冲。
 * @param outCap 输出缓冲容量(字节)。
 * @return 实际写入的字节数;失败返回 0。
 * @example
 *   markair::u32 n = EncodeTestImage(env.wic, GUID_ContainerFormatPng, 8, 8, 1, buf, sizeof(buf));
 */
inline markair::u32 EncodeTestImage(IWICImagingFactory* wic, const GUID& containerFormat,
                                  markair::u32 width, markair::u32 height, markair::u32 frameCount,
                                  markair::u8* out, markair::u32 outCap) {
    if (!wic || !out || outCap == 0 || width == 0 || height == 0 || frameCount == 0) return 0;

    IWICStream* stream = nullptr;
    if (FAILED(wic->CreateStream(&stream)) || !stream) return 0;
    if (FAILED(stream->InitializeFromMemory(out, outCap))) { stream->Release(); return 0; }

    IWICBitmapEncoder* encoder = nullptr;
    if (FAILED(wic->CreateEncoder(containerFormat, nullptr, &encoder)) || !encoder) {
        stream->Release();
        return 0;
    }
    if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) {
        encoder->Release();
        stream->Release();
        return 0;
    }

    bool ok = true;
    for (markair::u32 f = 0; f < frameCount && ok; ++f) {
        // 先把这一帧画进一张 32bppBGRA 的 WIC 位图,再用 WriteSource 交给编码器。
        // 走 WriteSource 而不是 WritePixels,是因为 GIF 编码器只接受 8bppIndexed,
        // WriteSource 会自动完成格式转换(配合下面按帧生成的调色板)。
        IWICBitmap* source = nullptr;
        if (FAILED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppBGRA,
                                      WICBitmapCacheOnLoad, &source)) || !source) {
            ok = false;
            break;
        }
        IWICBitmapLock* lock = nullptr;
        WICRect full{0, 0, static_cast<INT>(width), static_cast<INT>(height)};
        if (FAILED(source->Lock(&full, WICBitmapLockWrite, &lock)) || !lock) {
            source->Release();
            ok = false;
            break;
        }
        UINT stride = 0, bufferSize = 0;
        BYTE* pixels = nullptr;
        if (SUCCEEDED(lock->GetStride(&stride)) &&
            SUCCEEDED(lock->GetDataPointer(&bufferSize, &pixels)) && pixels) {
            for (markair::u32 y = 0; y < height; ++y) {
                BYTE* row = pixels + static_cast<size_t>(y) * stride;
                for (markair::u32 x = 0; x < width; ++x) {
                    // 渐变纹样:让 JPEG 这类有损编码器也有可压缩的内容,
                    // 不同帧之间加一点偏移,便于区分"只解了第 0 帧"。
                    row[x * 4 + 0] = static_cast<BYTE>((x + f * 37u) & 0xFF);
                    row[x * 4 + 1] = static_cast<BYTE>((y + f * 11u) & 0xFF);
                    row[x * 4 + 2] = static_cast<BYTE>((x + y) & 0xFF);
                    row[x * 4 + 3] = 0xFF;
                }
            }
        } else {
            ok = false;
        }
        lock->Release();
        if (!ok) { source->Release(); break; }

        IWICBitmapFrameEncode* frame = nullptr;
        IPropertyBag2* props = nullptr;
        if (FAILED(encoder->CreateNewFrame(&frame, &props)) || !frame) {
            source->Release();
            ok = false;
            break;
        }
        if (props) props->Release();
        if (FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(width, height))) {
            frame->Release();
            source->Release();
            ok = false;
            break;
        }

        if (IsEqualGUID(containerFormat, GUID_ContainerFormatGif)) {
            // GIF:必须给出 8bppIndexed 格式与调色板,否则 WriteSource 会失败。
            WICPixelFormatGUID indexed = GUID_WICPixelFormat8bppIndexed;
            frame->SetPixelFormat(&indexed);
            IWICPalette* palette = nullptr;
            if (SUCCEEDED(wic->CreatePalette(&palette)) && palette) {
                if (SUCCEEDED(palette->InitializeFromBitmap(source, 256, FALSE))) {
                    frame->SetPalette(palette);
                }
                palette->Release();
            }
        }

        if (FAILED(frame->WriteSource(source, nullptr))) ok = false;
        if (ok && FAILED(frame->Commit())) ok = false;
        frame->Release();
        source->Release();
    }

    if (ok && FAILED(encoder->Commit())) ok = false;

    markair::u32 written = 0;
    if (ok) {
        STATSTG stat{};
        if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME))) {
            written = static_cast<markair::u32>(stat.cbSize.QuadPart);
        }
    }

    encoder->Release();
    stream->Release();
    return written;
}

}  // namespace markair_test
