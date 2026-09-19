#include "theme.h"

#include <cmath>

namespace markair {

// sRGB -> 线性空间的单通道换算(WCAG 2.x 公式),用 std::pow 实现 2.4 次方,
// 因此不是 constexpr——theme.h 里两份调色板的常量构造不依赖本函数。
float LinearizeChannel(float c) {
    return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// 相对亮度 = 0.2126*R + 0.7152*G + 0.0722*B(线性空间),WCAG 2.x 公式系数。
float ContrastRatio(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) {
    float la = 0.2126f * LinearizeChannel(a.r) + 0.7152f * LinearizeChannel(a.g) +
               0.0722f * LinearizeChannel(a.b);
    float lb = 0.2126f * LinearizeChannel(b.r) + 0.7152f * LinearizeChannel(b.g) +
               0.0722f * LinearizeChannel(b.b);
    float lighter = la > lb ? la : lb;
    float darker = la > lb ? lb : la;
    return (lighter + 0.05f) / (darker + 0.05f);
}

}  // namespace markair
