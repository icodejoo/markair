// T11 覆盖测试:D2D 渲染模块里唯一能脱离真实设备单独测的逻辑——
// "根据 HRESULT 判断是否需要重建渲染目标"这条纯函数(markair::ShouldRecreateRenderTarget)。
//
// Renderer 本体(EnsureRenderTarget/RenderFrame)强依赖真实 HWND + D2D 设备上下文,
// 在无窗口/无显示的 CI 环境下无法可靠构造,因此不为其编造假测试;这条纯函数
// 判断逻辑是本模块里唯一不依赖真实设备、值得写单元测试的部分。
#include "markair_test.h"
#include "../src/render/renderer.h"

// 用例:D2DERR_RECREATE_TARGET 应判定为需要重建。
MARKAIR_TEST(Renderer_RecreateTargetErrorTriggersRecreate) {
    MARKAIR_CHECK(markair::ShouldRecreateRenderTarget(D2DERR_RECREATE_TARGET));
}

// 用例:成功码(S_OK)与其他失败码不应触发重建。
MARKAIR_TEST(Renderer_OtherResultsDoNotTriggerRecreate) {
    MARKAIR_CHECK(!markair::ShouldRecreateRenderTarget(S_OK));
    MARKAIR_CHECK(!markair::ShouldRecreateRenderTarget(E_FAIL));
    MARKAIR_CHECK(!markair::ShouldRecreateRenderTarget(E_OUTOFMEMORY));
}

// ---- T33:占位块文案与降采样提示标签(纯函数,可脱离 D2D 测)----

#include <cwchar>

// 用例:五种非 Ok 状态共用一个占位块绘制函数,但文案各不相同、均非空;
// Ok 状态不画占位块,返回空串。
MARKAIR_TEST(Renderer_ImagePlaceholderTextPerStatus) {
    const wchar_t* notLoaded = markair::ImagePlaceholderText(markair::ImageStatus::NotLoaded);
    const wchar_t* failed = markair::ImagePlaceholderText(markair::ImageStatus::Failed);
    const wchar_t* unsupported = markair::ImagePlaceholderText(markair::ImageStatus::Unsupported);
    const wchar_t* tooLarge = markair::ImagePlaceholderText(markair::ImageStatus::TooLarge);
    const wchar_t* remote = markair::ImagePlaceholderText(markair::ImageStatus::RemoteNotLoaded);

    MARKAIR_CHECK(wcslen(notLoaded) > 0);
    MARKAIR_CHECK(wcslen(failed) > 0);
    MARKAIR_CHECK(wcslen(unsupported) > 0);
    MARKAIR_CHECK(wcslen(tooLarge) > 0);
    MARKAIR_CHECK(wcslen(remote) > 0);

    // 五种文案两两不同(否则用户分不清占位块为什么出现)。
    const wchar_t* all[5] = {notLoaded, failed, unsupported, tooLarge, remote};
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            MARKAIR_CHECK(wcscmp(all[i], all[j]) != 0);
        }
    }

    MARKAIR_CHECK_EQ(wcslen(markair::ImagePlaceholderText(markair::ImageStatus::Ok)), 0u);
}

// 用例:降采样提示标签文案固定为"已压缩·点击看原图"。
MARKAIR_TEST(Renderer_DownsampledBadgeText) {
    MARKAIR_CHECK(wcscmp(markair::DownsampledBadgeText(), L"已压缩·点击看原图") == 0);
}
