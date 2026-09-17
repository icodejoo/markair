// T9 覆盖测试:字体子系统里不依赖窗口/D2D 渲染目标的部分——
// DirectWrite 工厂创建本身不需要 HWND，可以在无窗口环境下验证。
#include <cwchar>

#include "mdvn_test.h"
#include "../src/text/font.h"

using mdvn::FontRole;
using mdvn::FontSubsystem;

// Init 应成功创建 DirectWrite 工厂，并把正文角色的文本格式建好（非惰性）。
MDVN_TEST(FontSubsystem_InitCreatesBodyFormatEagerly) {
    FontSubsystem fonts;
    bool ok = fonts.Init();
    MDVN_CHECK(ok);
    MDVN_CHECK(fonts.Factory() != nullptr);
    MDVN_CHECK(fonts.GetTextFormat(FontRole::Body) != nullptr);
}

// 正文文本格式的主族名必须是裁决 #10 原文的 "Segoe UI"。
MDVN_TEST(FontSubsystem_BodyFormatFamilyNameMatchesWhitelist) {
    FontSubsystem fonts;
    MDVN_CHECK(fonts.Init());

    IDWriteTextFormat* body = fonts.GetTextFormat(FontRole::Body);
    MDVN_CHECK(body != nullptr);
    if (body) {
        wchar_t name[64] = {};
        HRESULT hr = body->GetFontFamilyName(name, 64);
        MDVN_CHECK(SUCCEEDED(hr));
        MDVN_CHECK(wcscmp(name, L"Segoe UI") == 0);
    }
}

// 等宽文本格式在未请求前不应存在；首次 GetTextFormat(Mono) 才惰性创建，
// 且主族名必须是裁决 #10 原文的 "Cascadia Mono"。
MDVN_TEST(FontSubsystem_MonoFormatIsLazyAndMatchesWhitelist) {
    FontSubsystem fonts;
    MDVN_CHECK(fonts.Init());

    IDWriteTextFormat* mono = fonts.GetTextFormat(FontRole::Mono);
    MDVN_CHECK(mono != nullptr);
    if (mono) {
        wchar_t name[64] = {};
        HRESULT hr = mono->GetFontFamilyName(name, 64);
        MDVN_CHECK(SUCCEEDED(hr));
        MDVN_CHECK(wcscmp(name, L"Cascadia Mono") == 0);
    }

    // 再次获取应返回同一个已缓存的对象（复用池语义），不重新创建。
    IDWriteTextFormat* mono2 = fonts.GetTextFormat(FontRole::Mono);
    MDVN_CHECK(mono == mono2);
}

// T29:SetScale 应钳制到最近的离散档位,超出上下限时钳制到端点。
MDVN_TEST(FontSubsystem_ScaleClampsToDiscreteLevels) {
    FontSubsystem fonts;
    MDVN_CHECK(fonts.Init());
    MDVN_CHECK_EQ(fonts.Scale(), 1.0f);

    MDVN_CHECK_EQ(fonts.SetScale(0.1f), 0.8f);   // 低于最小档,钳制到 0.8
    MDVN_CHECK_EQ(fonts.SetScale(10.0f), 2.0f);  // 高于最大档,钳制到 2.0
    MDVN_CHECK_EQ(fonts.SetScale(1.2f), 1.15f);  // 就近取档

    MDVN_CHECK_EQ(fonts.ResetZoom(), 1.0f);
}

// T29:ZoomIn/ZoomOut 按档位表逐档移动,越界时停在端点不崩溃。
MDVN_TEST(FontSubsystem_ZoomInOutStepThroughLevelsAndClamp) {
    FontSubsystem fonts;
    MDVN_CHECK(fonts.Init());
    MDVN_CHECK_EQ(fonts.Scale(), 1.0f);

    MDVN_CHECK_EQ(fonts.ZoomIn(), 1.15f);
    MDVN_CHECK_EQ(fonts.ZoomOut(), 1.0f);

    for (int i = 0; i < 20; ++i) fonts.ZoomIn();
    MDVN_CHECK_EQ(fonts.Scale(), 2.0f);  // 顶到最大档,不越界

    for (int i = 0; i < 20; ++i) fonts.ZoomOut();
    MDVN_CHECK_EQ(fonts.Scale(), 0.8f);  // 顶到最小档,不越界
}

// T29:缩放变更应真的改变正文 IDWriteTextFormat 的字号(否则布局/渲染看不到效果)。
MDVN_TEST(FontSubsystem_ScaleAffectsBodyFormatFontSize) {
    FontSubsystem fonts;
    MDVN_CHECK(fonts.Init());

    IDWriteTextFormat* body1 = fonts.GetTextFormat(FontRole::Body);
    MDVN_CHECK(body1 != nullptr);
    float size1 = body1 ? body1->GetFontSize() : 0.0f;

    fonts.SetScale(2.0f);
    IDWriteTextFormat* body2 = fonts.GetTextFormat(FontRole::Body);
    MDVN_CHECK(body2 != nullptr);
    float size2 = body2 ? body2->GetFontSize() : 0.0f;

    MDVN_CHECK(size2 > size1);
}

// T29:连续缩放(放大/缩小各 50 次)不崩溃,正文格式始终保持可用
// (对应验收标准里"连续缩放 50 次 PrivateUsage 无单调上升"里"不崩溃/不泄漏句柄"
// 这部分可脱离外部测量工具验证的子集)。
MDVN_TEST(FontSubsystem_RepeatedZoomDoesNotCrashAndKeepsFormatValid) {
    FontSubsystem fonts;
    MDVN_CHECK(fonts.Init());
    for (int i = 0; i < 50; ++i) {
        fonts.ZoomIn();
        fonts.ZoomOut();
    }
    MDVN_CHECK(fonts.GetTextFormat(FontRole::Body) != nullptr);
}

// CreateTextLayout 应能用中英混排文本成功创建布局对象，并触发一次真实排版
// （GetMetrics）证明白名单回退链在无窗口环境下也能正常工作。
MDVN_TEST(FontSubsystem_CreateTextLayoutWithMixedCjkText) {
    FontSubsystem fonts;
    MDVN_CHECK(fonts.Init());

    static const wchar_t kSample[] = L"mdvn 是一个只读 Markdown 查看器 hello";
    IDWriteTextLayout* layout = fonts.CreateTextLayout(
        kSample, static_cast<mdvn::u32>(wcslen(kSample)), FontRole::Body,
        600.0f, 200.0f);
    MDVN_CHECK(layout != nullptr);
    if (layout) {
        DWRITE_TEXT_METRICS metrics = {};
        HRESULT hr = layout->GetMetrics(&metrics);
        MDVN_CHECK(SUCCEEDED(hr));
        MDVN_CHECK(metrics.lineCount >= 1u);
        layout->Release();
    }
}
