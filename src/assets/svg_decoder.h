// markair SVG 图片解码子系统:lunasvg 离线栅格化封装。
//
// 设计要点:
//   - 与 ImageDecoder(WIC 路径)平行、独立的解码入口,复用同一套 DecodedImage/
//     ImageStatus/kMaxDecodedBytes 口径,解码结果同样经 ImageCache 常驻。
//   - lunasvg 输出 ARGB32_Premultiplied,内存字节序与现有 WIC 路径统一用的
//     32bppPBGRA + 预乘 alpha 完全一致,创建 ID2D1Bitmap 时无需再转换格式。
//   - 惰性:不解码 SVG 就不会构造 lunasvg::Document,不产生额外常驻开销。
//   - 解码失败(格式错误/尺寸为 0)一律返回 Failed 状态,不崩溃、不抛异常出本模块
//     (lunasvg 内部允许用异常,但调用全部包在本文件内,不越过本模块边界)。
#pragma once

#include <d2d1.h>

#include "image.h"
#include "../util/types.h"

namespace markair {

/**
 * 从内存字节解码一张 SVG 图片,栅格化到位图。
 *
 * 中文:按 SVG 内在尺寸(width/height,缺失时退化用 lunasvg 默认值)计算目标像素,
 * 超过 kMaxDecodedBytes 时按 ComputeDownscaledSizeForBudget 等比降采样后再栅格化
 * ——与 WIC 路径的降采样口径一致,不会先展开一份超大像素缓冲。
 *
 * @param bytes SVG 源文本/字节,非空。
 *
 *   Raw SVG bytes (UTF-8 XML text), must not be null.
 *
 * @param len 字节数;为 0 或超过 kMaxEncodedBytes 分别返回 Failed / TooLarge。
 *
 *   Byte length; returns Failed for 0, TooLarge when exceeding kMaxEncodedBytes.
 *
 * @param target D2D 渲染目标,用于创建 ID2D1Bitmap;传 nullptr 时只解码取尺寸、
 *               不创建位图(单元测试/仅探测尺寸场景)。
 *
 *   D2D render target used to create the ID2D1Bitmap; pass nullptr to only
 *   probe dimensions without creating a bitmap (unit tests / size-only probes).
 *
 * @return 解码结果;失败时 bitmap 为 nullptr 且 status 非 Ok,绝不崩溃。
 *
 *   The decode result; on failure bitmap is nullptr and status is non-Ok,
 *   this function never crashes or throws past its own boundary.
 *
 * @example markair::DecodedImage img = markair::DecodeSvgFromMemory(buf, n, target);
 */
DecodedImage DecodeSvgFromMemory(const void* bytes, u32 len, ID2D1RenderTarget* target);

}  // namespace markair
