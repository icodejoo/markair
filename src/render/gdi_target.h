// markair 渲染后端(2026-09-22 从 Direct2D 迁移到纯 GDI+/DirectWrite GDI
// Interop):不再触碰 D3D/D2D,彻底避免 D2D1_RENDER_TARGET_TYPE_SOFTWARE 在
// HWND 渲染目标上实际走 D3D10 WARP 软件光栅化驱动、窗口越大内存开销越高的
// 问题(实测最大化窗口下能吃掉 27~30MB 私有内存,见对应会话的 VMMap 排查记录)。
//
// 核心机制:`IDWriteGdiInterop::CreateBitmapRenderTarget` 拿到一个内部管理
// 32bpp DIB 的 `IDWriteBitmapRenderTarget`,它的 `GetMemoryDC()` 就是整帧的
// 画布——文字通过自定义 `IDWriteTextRenderer` 回调 `DrawGlyphRun` 直接画
// 到这块 DIB 上(DirectWrite 的排版/字体回退/ClearType 逻辑完全不变,只是
// 换了"画到哪");矩形/圆角矩形/椭圆/直线/位图这些图元改用 `Gdiplus::Graphics`
// 包住同一个 HDC 画(GDI+ 是纯 CPU 光栅化,不碰 D3D,且比原生 GDI 多了抗锯齿)。
// 一帧画完后 `EndDraw` 把这块 DIB `BitBlt` 到窗口 DC。
//
// GdiRenderTarget 的方法名/参数形状刻意贴近原来 ID2D1RenderTarget 的子集
// (BeginDraw/EndDraw/Clear/CreateSolidColorBrush/DrawRectangle/...),配合
// gdi_types.h 里同名的 D2D1_RECT_F 等值类型,renderer.cpp 里成百上千处已经
// 写好的绘制调用点因此绝大多数不用改一个字——这是刻意的移植策略,不是巧合。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwrite_2.h>
#include <objidl.h>
#include <gdiplus.h>

#include "gdi_types.h"

namespace markair {

/**
 * 纯色画笔:`ID2D1SolidColorBrush` 的极简替身,只存一个颜色值。
 *
 * 生命周期约定与原 D2D 画笔一致——`Release()` 即销毁自身(调用方原有的
 * `if (brush) brush->Release();` 收尾代码不用改),不做引用计数(本项目里
 * 每支画笔都是"每帧新建、用完就扔",不存在共享持有场景)。
 *
 * @example
 *   markair::GdiBrush* b = target->CreateSolidColorBrush(palette->text);
 *   target->FillRectangle(rect, b);
 *   b->Release();
 */
class GdiBrush {
public:
    explicit GdiBrush(const D2D1_COLOR_F& color) : color_(color) {}

    /** 转成 GDI+ 用的 ARGB 颜色(预乘无关——GDI+ SolidBrush/Pen 吃直接 alpha)。 */
    Gdiplus::Color ToGdiplus() const {
        auto Clamp255 = [](float v) -> BYTE {
            if (v <= 0.0f) return 0;
            if (v >= 1.0f) return 255;
            return static_cast<BYTE>(v * 255.0f + 0.5f);
        };
        return Gdiplus::Color(Clamp255(color_.a), Clamp255(color_.r), Clamp255(color_.g),
                               Clamp255(color_.b));
    }

    /** 转成 DirectWrite `DrawGlyphRun` 要的 COLORREF(文字向来不透明,丢弃 alpha)。 */
    COLORREF ToColorRef() const {
        auto Clamp255 = [](float v) -> int {
            if (v <= 0.0f) return 0;
            if (v >= 1.0f) return 255;
            return static_cast<int>(v * 255.0f + 0.5f);
        };
        return RGB(Clamp255(color_.r), Clamp255(color_.g), Clamp255(color_.b));
    }

    float Alpha() const { return color_.a; }

    /** 与 `ID2D1SolidColorBrush::Release` 同语义:销毁自身。 */
    void Release() { delete this; }

private:
    D2D1_COLOR_F color_;
};

/**
 * D2D `ID2D1RenderTarget`(HWND 版)的 GDI+/DirectWrite 替身。
 *
 * 只实现 renderer.cpp 实际用到的那个子集:清屏、纯色画笔、矩形/圆角矩形/
 * 椭圆/直线的描边与填充、位图绘制(逐像素透明)、文字排版绘制、轴对齐裁剪、
 * 仿射变换(本项目只用到平移与 1 处均匀缩放)。
 *
 * @example
 *   markair::GdiRenderTarget* rt = markair::GdiRenderTarget::Create(hwnd, dwriteFactory, dpi);
 *   rt->BeginDraw();
 *   rt->Clear(palette->background);
 *   // ... DrawXxx/FillXxx ...
 *   rt->EndDraw();
 */
class GdiRenderTarget {
public:
    /**
     * 按 hwnd 当前客户区尺寸创建渲染目标。
     * @param hwnd 目标窗口,非空。
     * @param dwriteFactory 共享的 DirectWrite 工厂(来自 FontSubsystem::Factory()),
     *        非空——文字排版对象本来就是用它创建的,GDI Interop 必须来自同一个
     *        工厂实例族系才能正确取到 `IDWriteGdiInterop`。
     * @param dpi 每英寸点数;传 0 表示跟随 hwnd 当前所在显示器的系统 DPI。
     * @return 创建成功返回新实例(调用方持有并负责 `delete`);失败返回 nullptr,
     *         不崩溃(内存不足等极端场景)。
     * @example auto* rt = GdiRenderTarget::Create(hwnd, fonts.Factory(), 0.0f);
     */
    static GdiRenderTarget* Create(HWND hwnd, IDWriteFactory* dwriteFactory, float dpi);

    ~GdiRenderTarget();

    GdiRenderTarget(const GdiRenderTarget&) = delete;
    GdiRenderTarget& operator=(const GdiRenderTarget&) = delete;

    /** 窗口客户区尺寸变化:内部 DIB 跟着 resize。@param w/h 新客户区像素尺寸。 */
    void Resize(D2D1_SIZE_U size);

    /** 当前渲染目标尺寸(DIP,已经换算过 DPI 缩放)。 */
    D2D1_SIZE_F GetSize() const;

    /** 开始一帧绘制;GDI+ 画布本身没有 BeginDraw/EndDraw 的事务概念,这里只
     *  是保持调用点形状一致,内部重置变换栈深度等每帧状态。 */
    void BeginDraw();

    /** 结束一帧绘制并把内部 DIB 一次性 BitBlt 到窗口——D2D 版本的 HWND 渲染
     *  目标在 EndDraw 时自动"呈现",这里手动做等价的事。
     *  @return S_OK(GDI 路径没有"设备丢失"这个概念,恒不返回
     *          D2DERR_RECREATE_TARGET,调用方 ShouldRecreateRenderTarget 判定
     *          恒为 false)。 */
    HRESULT EndDraw();

    void Clear(const D2D1_COLOR_F& color);

    GdiBrush* CreateSolidColorBrush(const D2D1_COLOR_F& color) { return new GdiBrush(color); }

    /** D2D 风格的出参版本(`target_->CreateSolidColorBrush(color, &brush)`),
     * 保留是为了不用改 renderer.cpp 里几十处这个调用形状的既有代码。 */
    void CreateSolidColorBrush(const D2D1_COLOR_F& color, GdiBrush** out) {
        if (out) *out = new GdiBrush(color);
    }

    void SetTransform(const D2D1_MATRIX_3X2_F& m);

    void DrawRectangle(const D2D1_RECT_F& rect, GdiBrush* brush, float strokeWidth = 1.0f);
    void FillRectangle(const D2D1_RECT_F& rect, GdiBrush* brush);
    void DrawRoundedRectangle(const D2D1_ROUNDED_RECT& rr, GdiBrush* brush, float strokeWidth = 1.0f);
    void FillRoundedRectangle(const D2D1_ROUNDED_RECT& rr, GdiBrush* brush);
    void DrawEllipse(const D2D1_ELLIPSE& e, GdiBrush* brush, float strokeWidth = 1.0f);
    void FillEllipse(const D2D1_ELLIPSE& e, GdiBrush* brush);
    void DrawLine(const D2D1_POINT_2F& p0, const D2D1_POINT_2F& p1, GdiBrush* brush,
                  float strokeWidth = 1.0f);

    /**
     * 画一张已解码的位图(逐像素透明,WIC/lunasvg 解码结果都是预乘 alpha,
     * `Gdiplus::Bitmap` 原生吃这个格式)。
     *
     * @param bitmap 已解码的位图,可为 nullptr(静默跳过,不崩溃)。由
     *        `MakeOwnedBgraBitmap` 建出,物理尺寸比内容大 1×1(见该函数
     *        doc-comment 里 GDI+ 越界采样崩溃的说明)——`srcWidthPx`/`srcHeightPx`
     *        必须传内容的真实尺寸,不能用 `bitmap->GetWidth()/GetHeight()`
     *        (那两个会多报 1,把安全边距那 1 像素也画进来)。
     * @param srcWidthPx 位图内容的真实像素宽(不含安全边距)。
     * @param srcHeightPx 位图内容的真实像素高(不含安全边距)。
     * @param dest 目标矩形(DIP),位图按此矩形拉伸绘制。
     * @param opacity 整体不透明度,本项目里恒为 1.0(保留参数只为兼容调用点签名)。
     * @param interpolationMode 保留参数,GDI+ 统一用双线性插值(D2D 原来
     *        也只用 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR 这一种,不存在行为差异)。
     */
    void DrawBitmap(Gdiplus::Bitmap* bitmap, UINT32 srcWidthPx, UINT32 srcHeightPx,
                     const D2D1_RECT_F& dest, float opacity = 1.0f,
                     D2D1_BITMAP_INTERPOLATION_MODE interpolationMode =
                         D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

    /**
     * 画一段已排版好的文字(`IDWriteTextLayout::Draw` 通过自定义
     * `IDWriteTextRenderer` 回调本目标内部的 `IDWriteBitmapRenderTarget`)。
     * 彩色 emoji(COLR/位图字形)通过 `IDWriteFactory2::TranslateColorGlyphRun`
     * 逐层解析后按层绘制——`options` 里的 `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`
     * 对应"尝试彩色分层",不传则退化为纯色单层绘制(本项目调用点几乎全部
     * 都传了这个标志,语义与迁移前一致)。
     * @param origin 文字排版左上角原点(DIP)。
     * @param layout 已经 CreateTextLayout 出来的排版对象,可为 nullptr(跳过)。
     * @param brush 文字颜色。
     * @param options 见上,仅识别 ENABLE_COLOR_FONT 这一个标志位。
     */
    void DrawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* layout, GdiBrush* brush,
                         D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE);

    /** 交给下一次 FillPathIcon 用的、按 nonzero 缠绕规则填充的手绘图标路径
     *  (替代原来的 ID2D1PathGeometry + FillGeometry,唯一调用方是缩放图标)。 */
    void FillPathIcon(Gdiplus::GraphicsPath* path, GdiBrush* brush);

    /** 轴对齐裁剪(与当前裁剪区求交,GDI+ Region 自然支持嵌套)。
     * @param antialiasMode 保留参数,不影响裁剪行为本身(GDI+ 裁剪不分这两档)。 */
    void PushAxisAlignedClip(const D2D1_RECT_F& rect, D2D1_ANTIALIAS_MODE antialiasMode);
    void PopAxisAlignedClip();

private:
    GdiRenderTarget() = default;
    bool Init(HWND hwnd, IDWriteFactory* dwriteFactory, float dpi);
    void RebuildGraphics();
    void ApplyTransformToDWrite();

    HWND hwnd_ = nullptr;
    IDWriteFactory* dwriteFactory_ = nullptr;   // 不拥有,来自 FontSubsystem
    IDWriteGdiInterop* gdiInterop_ = nullptr;   // 拥有(AddRef 过)
    IDWriteBitmapRenderTarget* dwTarget_ = nullptr;  // 拥有,内部持有 DIB
    IDWriteRenderingParams* renderingParams_ = nullptr;  // 拥有
    Gdiplus::Graphics* gfx_ = nullptr;          // 拥有,包住 dwTarget_ 的 HDC

    UINT32 pixelWidth_ = 0;
    UINT32 pixelHeight_ = 0;
    float dpiScale_ = 1.0f;  // dpi_/96

    // 当前用户变换(平移/缩放,DIP 空间),与 dpiScale_ 组合后同时喂给
    // gfx_(世界变换)与 dwTarget_(SetCurrentTransform),保持文字与图形对齐。
    D2D1_MATRIX_3X2_F userTransform_ = D2D1_MATRIX_3X2_F{1, 0, 0, 1, 0, 0};

    // 轴对齐裁剪栈,项目里从未嵌套超过 1 层,给足余量到 8 层,超出静默不再裁剪
    // (不崩溃,退化成"这一层裁剪没生效",比栈溢出安全)。
    static constexpr int kMaxClipDepth = 8;
    Gdiplus::Region savedClips_[kMaxClipDepth];
    int clipDepth_ = 0;
};

}  // namespace markair
