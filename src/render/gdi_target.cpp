#include "gdi_target.h"

#include <cmath>

namespace markair {

namespace {

// 把 GdiBrush 的颜色转成 GDI+ 画笔/画刷复用的小工具。
Gdiplus::SolidBrush MakeSolidBrush(GdiBrush* brush) { return Gdiplus::SolidBrush(brush->ToGdiplus()); }
Gdiplus::Pen MakePen(GdiBrush* brush, float width) {
    return Gdiplus::Pen(brush->ToGdiplus(), width > 0.0f ? width : 1.0f);
}

Gdiplus::RectF ToGdiplusRect(const D2D1_RECT_F& r) {
    return Gdiplus::RectF(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

// 圆角矩形路径:4 段 90 度圆弧 + 隐式连接直线,corner 半径分别钳到不超过
// 矩形半宽/半高(避免 radius 过大时 GDI+ AddArc 画出畸形形状)。
void BuildRoundedRectPath(Gdiplus::GraphicsPath* path, const D2D1_RECT_F& rect, float radiusX,
                           float radiusY) {
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    float rx = radiusX;
    float ry = radiusY;
    if (rx > w * 0.5f) rx = w * 0.5f;
    if (ry > h * 0.5f) ry = h * 0.5f;
    if (rx < 0.0f) rx = 0.0f;
    if (ry < 0.0f) ry = 0.0f;
    float dx = rx * 2.0f;
    float dy = ry * 2.0f;

    path->Reset();
    if (dx <= 0.01f || dy <= 0.01f) {
        path->AddRectangle(ToGdiplusRect(rect));
        return;
    }
    path->AddArc(rect.left, rect.top, dx, dy, 180.0f, 90.0f);
    path->AddArc(rect.right - dx, rect.top, dx, dy, 270.0f, 90.0f);
    path->AddArc(rect.right - dx, rect.bottom - dy, dx, dy, 0.0f, 90.0f);
    path->AddArc(rect.left, rect.bottom - dy, dx, dy, 90.0f, 90.0f);
    path->CloseFigure();
}

// 把 D2D1_MATRIX_3X2_F(行主序,v' = v * M)转成 DirectWrite 同布局的 DWRITE_MATRIX。
DWRITE_MATRIX ToDWriteMatrix(const D2D1_MATRIX_3X2_F& m) {
    DWRITE_MATRIX dm;
    dm.m11 = m._11;
    dm.m12 = m._12;
    dm.m21 = m._21;
    dm.m22 = m._22;
    dm.dx = m._31;
    dm.dy = m._32;
    return dm;
}

Gdiplus::Matrix ToGdiplusMatrix(const D2D1_MATRIX_3X2_F& m) {
    return Gdiplus::Matrix(m._11, m._12, m._21, m._22, m._31, m._32);
}

// IDWriteTextLayout::Draw 的回调实现:把每个 glyph run 转发给
// IDWriteBitmapRenderTarget::DrawGlyphRun,本项目不用下划线/删除线/内联对象
// (删除线走的是 markair 自己在 layout 阶段算好的一条 D2D 直线,不是 DWrite
// 原生删除线特性),这三个回调直接空实现。彩色 emoji 走
// IDWriteFactory2::TranslateColorGlyphRun 拆层,每层各自调一次 DrawGlyphRun,
// 有 runColor 的层用该颜色,没有的层退化用调用方传入的文字色。
// 栈上短生命周期对象,AddRef/Release 不做真实计数(标准写法,MSDN 样例同款)。
class GdiTextRenderer : public IDWriteTextRenderer {
public:
    GdiTextRenderer(IDWriteBitmapRenderTarget* target, IDWriteFactory* factory,
                     IDWriteRenderingParams* params, COLORREF defaultColor)
        : target_(target), factory_(factory), params_(params), defaultColor_(defaultColor) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** obj) override {
        if (riid == __uuidof(IDWriteTextRenderer) || riid == __uuidof(IDWritePixelSnapping) ||
            riid == __uuidof(IUnknown)) {
            *obj = this;
            return S_OK;
        }
        *obj = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* isDisabled) override {
        *isDisabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
        return target_->GetCurrentTransform(transform);
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override {
        *pixelsPerDip = target_->GetPixelsPerDip();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void* /*ctx*/, FLOAT baselineOriginX,
                                            FLOAT baselineOriginY,
                                            DWRITE_MEASURING_MODE measuringMode,
                                            const DWRITE_GLYPH_RUN* glyphRun,
                                            const DWRITE_GLYPH_RUN_DESCRIPTION* /*desc*/,
                                            IUnknown* /*effect*/) override {
        // 彩色字形(如 emoji)尝试分层绘制;factory_ 为空或该 run 本就不是彩色
        // 字形时 TranslateColorGlyphRun 返回 DWRITE_E_NOCOLOR,退化走单层绘制。
        IDWriteFactory2* factory2 = nullptr;
        if (factory_ &&
            SUCCEEDED(factory_->QueryInterface(__uuidof(IDWriteFactory2),
                                                reinterpret_cast<void**>(&factory2))) &&
            factory2) {
            IDWriteColorGlyphRunEnumerator* enumerator = nullptr;
            HRESULT hr = factory2->TranslateColorGlyphRun(
                baselineOriginX, baselineOriginY, glyphRun, nullptr, measuringMode, nullptr, 0,
                &enumerator);
            if (SUCCEEDED(hr) && enumerator) {
                HRESULT drawHr = S_OK;
                for (;;) {
                    BOOL hasRun = FALSE;
                    if (FAILED(enumerator->MoveNext(&hasRun)) || !hasRun) break;
                    const DWRITE_COLOR_GLYPH_RUN* layer = nullptr;
                    if (FAILED(enumerator->GetCurrentRun(&layer)) || !layer) break;
                    COLORREF layerColor = defaultColor_;
                    if (layer->paletteIndex != 0xFFFF) {
                        layerColor = RGB(static_cast<BYTE>(layer->runColor.r * 255.0f + 0.5f),
                                          static_cast<BYTE>(layer->runColor.g * 255.0f + 0.5f),
                                          static_cast<BYTE>(layer->runColor.b * 255.0f + 0.5f));
                    }
                    RECT dummy{};
                    drawHr = target_->DrawGlyphRun(layer->baselineOriginX, layer->baselineOriginY,
                                                    measuringMode, &layer->glyphRun, params_,
                                                    layerColor, &dummy);
                }
                enumerator->Release();
                factory2->Release();
                return drawHr;
            }
            factory2->Release();
        }

        RECT dummy{};
        return target_->DrawGlyphRun(baselineOriginX, baselineOriginY, measuringMode, glyphRun,
                                      params_, defaultColor_, &dummy);
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*,
                                             IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*,
                                                 IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL,
                                                BOOL, IUnknown*) override {
        return S_OK;
    }

private:
    IDWriteBitmapRenderTarget* target_;
    IDWriteFactory* factory_;
    IDWriteRenderingParams* params_;
    COLORREF defaultColor_;
};

}  // namespace

GdiRenderTarget* GdiRenderTarget::Create(HWND hwnd, IDWriteFactory* dwriteFactory, float dpi) {
    if (!hwnd || !dwriteFactory) return nullptr;
    GdiRenderTarget* rt = new GdiRenderTarget();
    if (!rt->Init(hwnd, dwriteFactory, dpi)) {
        delete rt;
        return nullptr;
    }
    return rt;
}

bool GdiRenderTarget::Init(HWND hwnd, IDWriteFactory* dwriteFactory, float dpi) {
    hwnd_ = hwnd;
    dwriteFactory_ = dwriteFactory;

    if (FAILED(dwriteFactory_->GetGdiInterop(&gdiInterop_)) || !gdiInterop_) return false;
    if (FAILED(dwriteFactory_->CreateRenderingParams(&renderingParams_)) || !renderingParams_) {
        return false;
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    pixelWidth_ = static_cast<UINT32>(rc.right - rc.left);
    pixelHeight_ = static_cast<UINT32>(rc.bottom - rc.top);
    if (pixelWidth_ == 0) pixelWidth_ = 1;
    if (pixelHeight_ == 0) pixelHeight_ = 1;

    // dpi 传 0 表示跟随系统:GetDpiForWindow 是 Win10 1607+ API,与本项目
    // 现有的按窗口取 DPI 逻辑(main.cpp/window.cpp 的 Per-Monitor V2)同一口径。
    float effectiveDpi = dpi;
    if (effectiveDpi <= 0.0f) {
        UINT d = GetDpiForWindow(hwnd);
        effectiveDpi = d > 0 ? static_cast<float>(d) : 96.0f;
    }
    dpiScale_ = effectiveDpi / 96.0f;

    if (FAILED(gdiInterop_->CreateBitmapRenderTarget(nullptr, pixelWidth_, pixelHeight_,
                                                       &dwTarget_)) ||
        !dwTarget_) {
        return false;
    }
    dwTarget_->SetPixelsPerDip(dpiScale_);

    RebuildGraphics();
    return gfx_ != nullptr;
}

GdiRenderTarget::~GdiRenderTarget() {
    delete gfx_;
    if (dwTarget_) dwTarget_->Release();
    if (renderingParams_) renderingParams_->Release();
    if (gdiInterop_) gdiInterop_->Release();
}

void GdiRenderTarget::RebuildGraphics() {
    delete gfx_;
    gfx_ = nullptr;
    if (!dwTarget_) return;
    HDC dc = dwTarget_->GetMemoryDC();
    if (!dc) return;
    gfx_ = new Gdiplus::Graphics(dc);
    gfx_->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gfx_->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    gfx_->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    ApplyTransformToDWrite();
    Gdiplus::Matrix m = ToGdiplusMatrix(D2D1::Matrix3x2F::Scale(dpiScale_, dpiScale_) *
                                         userTransform_);
    gfx_->SetTransform(&m);
    clipDepth_ = 0;
}

void GdiRenderTarget::ApplyTransformToDWrite() {
    if (!dwTarget_) return;
    D2D1_MATRIX_3X2_F combined = D2D1::Matrix3x2F::Scale(dpiScale_, dpiScale_) * userTransform_;
    DWRITE_MATRIX dm = ToDWriteMatrix(combined);
    dwTarget_->SetCurrentTransform(&dm);
}

void GdiRenderTarget::Resize(D2D1_SIZE_U size) {
    if (!dwTarget_) return;
    UINT32 w = size.width > 0 ? size.width : 1;
    UINT32 h = size.height > 0 ? size.height : 1;
    if (w == pixelWidth_ && h == pixelHeight_) return;
    if (FAILED(dwTarget_->Resize(w, h))) return;
    pixelWidth_ = w;
    pixelHeight_ = h;
    RebuildGraphics();
}

D2D1_SIZE_F GdiRenderTarget::GetSize() const {
    float scale = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
    return D2D1_SIZE_F{static_cast<float>(pixelWidth_) / scale,
                        static_cast<float>(pixelHeight_) / scale};
}

void GdiRenderTarget::BeginDraw() {
    clipDepth_ = 0;
    if (gfx_) gfx_->ResetClip();
}

HRESULT GdiRenderTarget::EndDraw() {
    if (!dwTarget_ || !hwnd_) return E_FAIL;
    HDC windowDc = GetDC(hwnd_);
    if (!windowDc) return E_FAIL;
    BitBlt(windowDc, 0, 0, static_cast<int>(pixelWidth_), static_cast<int>(pixelHeight_),
           dwTarget_->GetMemoryDC(), 0, 0, SRCCOPY);
    ReleaseDC(hwnd_, windowDc);
    return S_OK;
}

void GdiRenderTarget::Clear(const D2D1_COLOR_F& color) {
    if (!gfx_) return;
    Gdiplus::Matrix saved;
    gfx_->GetTransform(&saved);
    gfx_->ResetTransform();
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, static_cast<BYTE>(color.r * 255.0f + 0.5f),
                                              static_cast<BYTE>(color.g * 255.0f + 0.5f),
                                              static_cast<BYTE>(color.b * 255.0f + 0.5f)));
    gfx_->FillRectangle(&brush, 0, 0, static_cast<int>(pixelWidth_),
                         static_cast<int>(pixelHeight_));
    gfx_->SetTransform(&saved);
}

void GdiRenderTarget::SetTransform(const D2D1_MATRIX_3X2_F& m) {
    userTransform_ = m;
    if (!gfx_) return;
    Gdiplus::Matrix gm =
        ToGdiplusMatrix(D2D1::Matrix3x2F::Scale(dpiScale_, dpiScale_) * userTransform_);
    gfx_->SetTransform(&gm);
    ApplyTransformToDWrite();
}

void GdiRenderTarget::DrawRectangle(const D2D1_RECT_F& rect, GdiBrush* brush, float strokeWidth) {
    if (!gfx_ || !brush) return;
    Gdiplus::Pen pen = MakePen(brush, strokeWidth);
    gfx_->DrawRectangle(&pen, ToGdiplusRect(rect));
}

void GdiRenderTarget::FillRectangle(const D2D1_RECT_F& rect, GdiBrush* brush) {
    if (!gfx_ || !brush) return;
    Gdiplus::SolidBrush b = MakeSolidBrush(brush);
    gfx_->FillRectangle(&b, ToGdiplusRect(rect));
}

void GdiRenderTarget::DrawRoundedRectangle(const D2D1_ROUNDED_RECT& rr, GdiBrush* brush,
                                            float strokeWidth) {
    if (!gfx_ || !brush) return;
    Gdiplus::GraphicsPath path;
    BuildRoundedRectPath(&path, rr.rect, rr.radiusX, rr.radiusY);
    Gdiplus::Pen pen = MakePen(brush, strokeWidth);
    gfx_->DrawPath(&pen, &path);
}

void GdiRenderTarget::FillRoundedRectangle(const D2D1_ROUNDED_RECT& rr, GdiBrush* brush) {
    if (!gfx_ || !brush) return;
    Gdiplus::GraphicsPath path;
    BuildRoundedRectPath(&path, rr.rect, rr.radiusX, rr.radiusY);
    Gdiplus::SolidBrush b = MakeSolidBrush(brush);
    gfx_->FillPath(&b, &path);
}

void GdiRenderTarget::DrawEllipse(const D2D1_ELLIPSE& e, GdiBrush* brush, float strokeWidth) {
    if (!gfx_ || !brush) return;
    Gdiplus::Pen pen = MakePen(brush, strokeWidth);
    gfx_->DrawEllipse(&pen, e.point.x - e.radiusX, e.point.y - e.radiusY, e.radiusX * 2.0f,
                       e.radiusY * 2.0f);
}

void GdiRenderTarget::FillEllipse(const D2D1_ELLIPSE& e, GdiBrush* brush) {
    if (!gfx_ || !brush) return;
    Gdiplus::SolidBrush b = MakeSolidBrush(brush);
    gfx_->FillEllipse(&b, e.point.x - e.radiusX, e.point.y - e.radiusY, e.radiusX * 2.0f,
                       e.radiusY * 2.0f);
}

void GdiRenderTarget::DrawLine(const D2D1_POINT_2F& p0, const D2D1_POINT_2F& p1, GdiBrush* brush,
                                float strokeWidth) {
    if (!gfx_ || !brush) return;
    Gdiplus::Pen pen = MakePen(brush, strokeWidth);
    gfx_->DrawLine(&pen, p0.x, p0.y, p1.x, p1.y);
}

void GdiRenderTarget::DrawBitmap(Gdiplus::Bitmap* bitmap, UINT32 srcWidthPx, UINT32 srcHeightPx,
                                  const D2D1_RECT_F& dest, float opacity,
                                  D2D1_BITMAP_INTERPOLATION_MODE /*interpolationMode*/) {
    if (!gfx_ || !bitmap) return;
    // 2026-09-22 崩溃修复(WinDbg dump 定位):GDI+ 的 DrawImage 插值拉伸(不管
    // Bilinear 还是 HighQualityBilinear)会在源矩形边缘附近多采样一点。真正的
    // 修复是 MakeOwnedBgraBitmap 分配时留出的 1 像素安全边距(见该函数
    // doc-comment)——这里只用双线性(不用 HighQualityBilinear)是因为二者画质
    // 肉眼差异极小,双线性更快,不是安全性考量(留着高质量版本也一样安全,
    // 只是没必要)。源矩形必须传 srcWidthPx/srcHeightPx(内容真实尺寸),不能用
    // bitmap->GetWidth()/GetHeight()(那两个会多报 1,把安全边距画进来)。
    gfx_->SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    Gdiplus::RectF destRect = ToGdiplusRect(dest);
    Gdiplus::REAL srcW = static_cast<Gdiplus::REAL>(srcWidthPx);
    Gdiplus::REAL srcH = static_cast<Gdiplus::REAL>(srcHeightPx);
    if (opacity >= 0.999f) {
        gfx_->DrawImage(bitmap, destRect, 0, 0, srcW, srcH, Gdiplus::UnitPixel);
        return;
    }
    Gdiplus::ColorMatrix cm = {{
        {1, 0, 0, 0, 0},
        {0, 1, 0, 0, 0},
        {0, 0, 1, 0, 0},
        {0, 0, 0, opacity, 0},
        {0, 0, 0, 0, 1},
    }};
    Gdiplus::ImageAttributes attr;
    attr.SetColorMatrix(&cm);
    gfx_->DrawImage(bitmap, destRect, 0, 0, srcW, srcH, Gdiplus::UnitPixel, &attr);
}

void GdiRenderTarget::DrawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* layout,
                                      GdiBrush* brush, D2D1_DRAW_TEXT_OPTIONS /*options*/) {
    if (!dwTarget_ || !layout || !brush) return;
    GdiTextRenderer renderer(dwTarget_, dwriteFactory_, renderingParams_, brush->ToColorRef());
    // GdiTextRenderer::DrawGlyphRun 通过 dwTarget_->DrawGlyphRun 直接在原始 GDI
    // 层面往 dwTarget_ 的内存 HDC 里写字形,而 gfx_ 是包着同一个 HDC 的
    // Gdiplus::Graphics——两边不经 GetHDC/ReleaseHDC 这套官方约定的"借用协议"
    // 直接交替操作同一个 HDC,会让 GDI+ 内部缓存的裁剪区/变换状态跟 HDC 实际
    // 状态错位,下一次 gfx_ 的调用(典型地是 PushAxisAlignedClip 之后的
    // FillRectangle,例如大纲侧栏蒙层)就会在 gdiplus.dll 内部访问越界崩溃
    // (真实 bug,2026-09-22 用大纲侧栏开关必现复现)。GetHDC 让 GDI+ 把当前状态
    // 落盘到 HDC 上再"借出"控制权,ReleaseHDC 让它重新接管并刷新内部缓存,
    // 这就是 GDI+ 官方文档里"与原始 GDI 交替操作同一 HDC"的标准写法。
    HDC hdc = gfx_ ? gfx_->GetHDC() : nullptr;
    layout->Draw(nullptr, &renderer, origin.x, origin.y);
    if (gfx_ && hdc) gfx_->ReleaseHDC(hdc);
}

void GdiRenderTarget::FillPathIcon(Gdiplus::GraphicsPath* path, GdiBrush* brush) {
    if (!gfx_ || !path || !brush) return;
    Gdiplus::SolidBrush b = MakeSolidBrush(brush);
    gfx_->FillPath(&b, path);
}

void GdiRenderTarget::PushAxisAlignedClip(const D2D1_RECT_F& rect,
                                           D2D1_ANTIALIAS_MODE /*antialiasMode*/) {
    if (!gfx_ || clipDepth_ >= kMaxClipDepth) return;
    gfx_->GetClip(&savedClips_[clipDepth_]);
    ++clipDepth_;
    gfx_->SetClip(ToGdiplusRect(rect), Gdiplus::CombineModeIntersect);
}

void GdiRenderTarget::PopAxisAlignedClip() {
    if (!gfx_ || clipDepth_ <= 0) return;
    --clipDepth_;
    gfx_->SetClip(&savedClips_[clipDepth_]);
}

}  // namespace markair
