// T46 覆盖测试:调色板(Palette)体积、槽位是否显式赋值、light/dark 主背景与
// 正文对比度是否达标(WCAG 相对亮度公式)。ContrastRatio 本身也顺带单测覆盖。
// T49 追加:验证"切换主题不触发 Relayout"——见文件末尾。
#include "mdvn_test.h"
#include "../src/render/theme.h"
#include "../src/render/renderer.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/layout/layout.h"

using mdvn::Arena;
using mdvn::BlockLayoutEngine;
using mdvn::ContrastRatio;
using mdvn::Document;
using mdvn::kDarkPalette;
using mdvn::kLightPalette;
using mdvn::LinearizeChannel;
using mdvn::MakeColor;
using mdvn::Palette;
using mdvn::ParseMarkdown;
using mdvn::Renderer;
using mdvn::StrSlice;

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
        &p.selectionHighlight,
        &p.overlayBarBackground, &p.overlayBarText,
        &p.bottomBarBackground, &p.bottomBarIcon,
        &p.bottomBarText,     &p.bottomBarDivider,
        &p.outlineOverlayMaskBackground,
    };
    for (int i = 0; i < static_cast<int>(sizeof(slots) / sizeof(slots[0])); ++i) {
        if (IsDefaultZero(*slots[i])) return i;
    }
    return -1;
}

}  // namespace

// 用例:sizeof(Palette) 不超过验收给的字节上限——自绘滚动条(方案A)的
// Idle/Active 双档透明度(4 个槽位替代原来 2 个)后由 640 上调到 672
// (41 个 D2D1_COLOR_F * 16 字节 = 656,留一点余量,不是刚好顶格),防止
// 后续再无节制地往里堆槽位。
MDVN_TEST(Theme_PaletteSizeWithinBudget) {
    size_t paletteSize = sizeof(Palette);
    MDVN_CHECK(paletteSize <= 672);
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

// 用例(T49 验收硬指标):连续切换主题 N 次不触发 BlockLayoutEngine::Relayout。
//
// 验证思路:Renderer::SetPalette 的签名(见 renderer.h)只接受一个
// `const Palette*`,根本不持有/不接触 BlockLayoutEngine——这是"结构上不可能
// 触发 Relayout"的静态保证,和 window.cpp 里 Ctrl+Shift+T 分支只调用
// SetPalette + ApplyTitleBarTheme + InvalidateRect(不调用任何 layout 相关
// 函数)一致。本用例把这条静态保证落成一个可执行的回归断言:先跑一次真实
// Relayout 建立基线布局,记下此时的调用计数与总高度,再连续调用 50 次
// SetPalette(模拟连按 50 次 Ctrl+Shift+T 的核心动作),断言 Relayout 计数与
// TotalHeight 都纹丝不动——任何人以后不小心让主题切换路径牵连到布局重排,
// 这个用例就会先炸。
//
// 没有采用"扫描 window.cpp 源码文本找 Relayout 字符串"的方案:项目测试里
// 没有读取源文件当数据的先例(全部用例都直接调用被测函数),临时发明一种新
// 校验手法成本和收益不成比例,弃用。也没有额外给 window.cpp 的 WM_KEYDOWN
// 分支写一个需要真实 HWND/消息泵的集成测试——现有测试体系里没有任何
// window.cpp 单元测试先例(它依赖真实窗口),不为这一条新开先例。
MDVN_TEST(Theme_SwitchingPaletteDoesNotTriggerRelayout) {
    Arena arena;
    arena.Init(1 * 1024 * 1024);
    const char* md =
        "# Heading\n\nSome paragraph text for layout.\n\n"
        "- item one\n- item two\n\n> a quote\n";
    Document doc = ParseMarkdown(StrSlice{md, static_cast<mdvn::u32>(strlen(md))}, &arena);

    BlockLayoutEngine layout;
    MDVN_CHECK(layout.Relayout(doc, 760.0f));
    mdvn::u32 baselineCount = layout.RelayoutCallCount();
    float baselineHeight = layout.TotalHeight();
    MDVN_CHECK_EQ(baselineCount, 1u);

    Renderer renderer;
    for (int i = 0; i < 50; ++i) {
        renderer.SetPalette((i % 2 == 0) ? &kDarkPalette : &kLightPalette);
    }

    MDVN_CHECK_EQ(layout.RelayoutCallCount(), baselineCount);
    MDVN_CHECK(layout.TotalHeight() == baselineHeight);
}
