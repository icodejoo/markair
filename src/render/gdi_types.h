// markair 渲染后端从 Direct2D 迁移到 GDI+/DirectWrite GDI Interop(2026-09-22)
// 之后的几何值类型层。
//
// 这些类型/同名 namespace 帮助函数与原来 <d2d1.h> 里的同名符号布局/语义完全
// 一致(D2D1_RECT_F/D2D1_POINT_2F/D2D1_ROUNDED_RECT/... + D2D1::RectF()/
// D2D1::Point2F()/...),目的是让 renderer.cpp 里成百上千处已经写好的几何
// 计算调用点保持原样、不用逐个改——只有真正调用 D2D COM 接口的那些调用点
// (target_->DrawXxx / factory_->CreateXxx)才需要改成本次新增的 GdiRenderTarget
// (见 gdi_target.h)。
//
// 全项目已经不再 #include <d2d1.h>,因此这里的名字与真正的 D2D1 SDK 类型
// 不会在同一个翻译单元里同时出现,不存在重定义冲突。
#pragma once

#include <windows.h>

namespace markair {

struct D2D1_POINT_2F {
    float x;
    float y;
};

struct D2D1_RECT_F {
    float left;
    float top;
    float right;
    float bottom;
};

struct D2D1_ROUNDED_RECT {
    D2D1_RECT_F rect;
    float radiusX;
    float radiusY;
};

struct D2D1_ELLIPSE {
    D2D1_POINT_2F point;
    float radiusX;
    float radiusY;
};

struct D2D1_SIZE_F {
    float width;
    float height;
};

struct D2D1_SIZE_U {
    UINT32 width;
    UINT32 height;
};

struct D2D1_COLOR_F {
    float r;
    float g;
    float b;
    float a;

    constexpr D2D1_COLOR_F() : r(0), g(0), b(0), a(0) {}
    constexpr D2D1_COLOR_F(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}
};

// 仿射变换矩阵(2x3),本项目只用到纯平移与 1 处均匀缩放,字段布局与
// D2D1_MATRIX_3X2_F 一致(行主序,v' = v * M)。
struct D2D1_MATRIX_3X2_F {
    float _11, _12;
    float _21, _22;
    float _31, _32;
};

// D2D1_ANTIALIAS_MODE 的子集:GdiRenderTarget::PushAxisAlignedClip 接收这个
// 参数只是为了不改调用点签名,GDI+ 裁剪本身不区分这两种抗锯齿模式。
enum D2D1_ANTIALIAS_MODE {
    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE = 0,
    D2D1_ANTIALIAS_MODE_ALIASED = 1,
};

enum D2D1_BITMAP_INTERPOLATION_MODE {
    D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR = 0,
    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR = 1,
};

enum D2D1_DRAW_TEXT_OPTIONS {
    D2D1_DRAW_TEXT_OPTIONS_NONE = 0,
    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT = 4,
};

enum D2D1_FILL_MODE {
    D2D1_FILL_MODE_ALTERNATE = 0,
    D2D1_FILL_MODE_WINDING = 1,
};

enum D2D1_FIGURE_BEGIN {
    D2D1_FIGURE_BEGIN_FILLED = 0,
    D2D1_FIGURE_BEGIN_HOLLOW = 1,
};

enum D2D1_FIGURE_END {
    D2D1_FIGURE_END_OPEN = 0,
    D2D1_FIGURE_END_CLOSED = 1,
};

namespace D2D1 {

inline D2D1_POINT_2F Point2F(float x, float y) { return D2D1_POINT_2F{x, y}; }

inline D2D1_RECT_F RectF(float left, float top, float right, float bottom) {
    return D2D1_RECT_F{left, top, right, bottom};
}

inline D2D1_ROUNDED_RECT RoundedRect(const D2D1_RECT_F& rect, float radiusX, float radiusY) {
    return D2D1_ROUNDED_RECT{rect, radiusX, radiusY};
}

inline D2D1_ELLIPSE Ellipse(const D2D1_POINT_2F& center, float radiusX, float radiusY) {
    return D2D1_ELLIPSE{center, radiusX, radiusY};
}

inline D2D1_SIZE_U SizeU(UINT32 width, UINT32 height) { return D2D1_SIZE_U{width, height}; }
inline D2D1_SIZE_F SizeF(float width, float height) { return D2D1_SIZE_F{width, height}; }

// 与 D2D1::ColorF(UINT32, float) 换算公式一致(theme.h::MakeColor 已经手写
// 了一份等价逻辑,这里补一份是给极少数直接用 D2D1::ColorF(...) 字面量的调用点用)。
inline D2D1_COLOR_F ColorF(UINT32 rgb, float alpha = 1.0f) {
    return D2D1_COLOR_F{
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        alpha,
    };
}

// Matrix3x2F::Identity()/Translation()/Scale() 静态工厂,与 D2D1 SDK 同名
// 用法(D2D1::Matrix3x2F::Translation(x, y))保持一致,调用点不用改。
struct Matrix3x2F {
    static D2D1_MATRIX_3X2_F Identity() { return D2D1_MATRIX_3X2_F{1, 0, 0, 1, 0, 0}; }
    static D2D1_MATRIX_3X2_F Translation(float x, float y) {
        return D2D1_MATRIX_3X2_F{1, 0, 0, 1, x, y};
    }
    static D2D1_MATRIX_3X2_F Scale(float sx, float sy) {
        return D2D1_MATRIX_3X2_F{sx, 0, 0, sy, 0, 0};
    }
};

}  // namespace D2D1

// 矩阵乘法(行主序,a 在前 b 在后,即"先应用 a 变换再应用 b 变换")。放在
// markair 命名空间(而不是内层 D2D1 命名空间)是因为 D2D1_MATRIX_3X2_F 本身
// 声明在 markair 里——ADL 只按操作数类型所在的命名空间找重载,放错命名空间
// 会导致 renderer.cpp/gdi_target.cpp 里的 `matA * matB` 找不到这个重载。
inline D2D1_MATRIX_3X2_F operator*(const D2D1_MATRIX_3X2_F& a, const D2D1_MATRIX_3X2_F& b) {
    D2D1_MATRIX_3X2_F r;
    r._11 = a._11 * b._11 + a._12 * b._21;
    r._12 = a._11 * b._12 + a._12 * b._22;
    r._21 = a._21 * b._11 + a._22 * b._21;
    r._22 = a._21 * b._12 + a._22 * b._22;
    r._31 = a._31 * b._11 + a._32 * b._21 + b._31;
    r._32 = a._31 * b._12 + a._32 * b._22 + b._32;
    return r;
}

}  // namespace markair
