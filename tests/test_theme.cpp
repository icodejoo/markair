// T46 覆盖测试:调色板(Palette)体积、槽位是否显式赋值、light/dark 主背景与
// 正文对比度是否达标(WCAG 相对亮度公式)。ContrastRatio 本身也顺带单测覆盖。
#include "mdvn_test.h"
#include "../src/render/theme.h"

using mdvn::ContrastRatio;
using mdvn::kDarkPalette;
using mdvn::kLightPalette;
using mdvn::LinearizeChannel;
using mdvn::MakeColor;
using mdvn::Palette;

namespace {

// 透明黑(D2D1_COLOR_F 的默认零值),用来判断某个槽位是否"忘了显式赋值"。
bool IsDefaultZero(const D2D1_COLOR_F& c) {
    return c.r == 0.0f && c.g == 0.0f && c.b == 0.0f && c.a == 0.0f;
}

// 依次检查一份调色板的每个槽位,返回第一个仍是默认零值的下标;全部合格返回 -1。
// 用数组而不是逐字段 if,避免漏检——新增槽位时这里也要跟着补一行,
// 漏补会让新槽位悄悄跳过检查(比逐字段断言更不容易"看似过了实则没测")。
int FirstUnassignedSlot(const Palette& p) {
    const D2D1_COLOR_F* slots[] = {
        &p.background,       &p.text,
        &p.quoteBar,          &p.codeBackground,
        &p.thematicBreak,     &p.link,
        &p.tableHeaderBackground, &p.tableGrid,
        &p.checkboxBorder,    &p.checkboxCheck,
        &p.imagePlaceholderBackground, &p.imagePlaceholderBorder,
        &p.imagePlaceholderIcon,
        &p.downsampledBadgeBackground, &p.downsampledBadgeText,
        &p.codeCopyIcon,      &p.codeCopyHoverBackground,
        &p.codeCopyPaper,     &p.codeCopyDone,
        &p.findHighlight,     &p.findCurrentHighlight,
        &p.overlayBarBackground, &p.overlayBarText,
    };
    for (int i = 0; i < static_cast<int>(sizeof(slots) / sizeof(slots[0])); ++i) {
        if (IsDefaultZero(*slots[i])) return i;
    }
    return -1;
}

}  // namespace

// 用例:sizeof(Palette) 不超过验收给的 512 字节上限。
MDVN_TEST(Theme_PaletteSizeWithinBudget) {
    size_t paletteSize = sizeof(Palette);
    MDVN_CHECK(paletteSize <= 512);
}

// 用例:浅色调色板每个槽位都已显式赋值,不残留透明黑默认值。
MDVN_TEST(Theme_LightPaletteFullyAssigned) {
    MDVN_CHECK_EQ(FirstUnassignedSlot(kLightPalette), -1);
}

// 用例:深色调色板同上。
MDVN_TEST(Theme_DarkPaletteFullyAssigned) {
    MDVN_CHECK_EQ(FirstUnassignedSlot(kDarkPalette), -1);
}

// 用例:MakeColor 换算是否正确(纯黑/纯白/中间值 + alpha)。
MDVN_TEST(Theme_MakeColorConvertsRgbHex) {
    D2D1_COLOR_F white = MakeColor(0xFFFFFFu);
    MDVN_CHECK(white.r == 1.0f && white.g == 1.0f && white.b == 1.0f && white.a == 1.0f);

    D2D1_COLOR_F black = MakeColor(0x000000u);
    MDVN_CHECK(black.r == 0.0f && black.g == 0.0f && black.b == 0.0f);

    D2D1_COLOR_F halfAlpha = MakeColor(0xFFFFFFu, 0.5f);
    MDVN_CHECK(halfAlpha.a == 0.5f);
}

// 用例:LinearizeChannel 的两个分段——低端线性段、高端幂函数段——都要覆盖到。
MDVN_TEST(Theme_LinearizeChannelBothBranches) {
    // 低端:c <= 0.03928 走 c / 12.92,零值映射到零值。
    MDVN_CHECK(LinearizeChannel(0.0f) == 0.0f);
    float low = LinearizeChannel(0.02f);
    MDVN_CHECK(low > 0.0f && low < 0.02f);

    // 高端:纯白(1.0)线性化后仍是 1.0。
    float high = LinearizeChannel(1.0f);
    MDVN_CHECK(high > 0.999f && high < 1.001f);
}

// 用例:同色对比度恒为 1:1(黑对黑、白对白)。
MDVN_TEST(Theme_ContrastRatioSameColorIsOne) {
    float ratio = ContrastRatio(MakeColor(0x000000u), MakeColor(0x000000u));
    MDVN_CHECK(ratio > 0.999f && ratio < 1.001f);
}

// 用例:黑白对比度应为 21:1(WCAG 极值)。
MDVN_TEST(Theme_ContrastRatioBlackWhiteIsMax) {
    float ratio = ContrastRatio(MakeColor(0x000000u), MakeColor(0xFFFFFFu));
    MDVN_CHECK(ratio > 20.9f && ratio < 21.1f);
}

// 用例:ContrastRatio 与传参顺序无关(取更亮的那个做分子)。
MDVN_TEST(Theme_ContrastRatioIsOrderIndependent) {
    D2D1_COLOR_F a = MakeColor(0x123456u);
    D2D1_COLOR_F b = MakeColor(0xABCDEFu);
    MDVN_CHECK(ContrastRatio(a, b) == ContrastRatio(b, a));
}

// 用例(验收硬指标):浅色主题背景/正文对比度 >= 4.5:1。
MDVN_TEST(Theme_LightPaletteMeetsWcagAA) {
    float ratio = ContrastRatio(kLightPalette.background, kLightPalette.text);
    MDVN_CHECK(ratio >= 4.5f);
}

// 用例(验收硬指标):深色主题背景/正文对比度 >= 4.5:1。
MDVN_TEST(Theme_DarkPaletteMeetsWcagAA) {
    float ratio = ContrastRatio(kDarkPalette.background, kDarkPalette.text);
    MDVN_CHECK(ratio >= 4.5f);
}
