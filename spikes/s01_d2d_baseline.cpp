// S1 spike: D2D + DirectWrite 空壳窗口基线。
// 目的：测量“进程入口 -> 窗口创建 -> 首次 Present 返回”耗时，以及静置后的
// Private Working Set，作为 M0 内存/启动速度预算的 Go/No-Go 依据（见 05-m0-tasks.md）。
//
// 编译（开发者命令提示符）：
//   cl /nologo /EHsc /O2 /W4 spikes\s01_d2d_baseline.cpp /link d2d1.lib dwrite.lib psapi.lib user32.lib gdi32.lib
//
// 运行：窗口弹出后立即绘制一行 "Hello 中文" 并把 T0..T4 计时结果打到 stderr，
// 然后保持窗口存在，方便用 VMMap/任务管理器观察静置 10s 后的 Private Working Set。
// 按 ESC 或关闭窗口退出。

#include <windows.h>
#include <psapi.h>
#include <imm.h>
#include <d2d1.h>
#include <dwrite.h>

#include <cstdio>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "psapi.lib")

namespace {

LARGE_INTEGER g_freq{};
LARGE_INTEGER g_tProcessStart{};
LARGE_INTEGER g_tWindowCreated{};
LARGE_INTEGER g_tFirstPresent{};
bool g_firstPresentDone = false;

ID2D1Factory* g_d2dFactory = nullptr;
IDWriteFactory* g_dwriteFactory = nullptr;
ID2D1HwndRenderTarget* g_renderTarget = nullptr;
IDWriteTextFormat* g_textFormat = nullptr;

double MsBetween(const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
    return static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(g_freq.QuadPart);
}

void ReportMemory(const char* tag) {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        fprintf(stderr, "mem_tag=%s private_bytes=%zu working_set=%zu\n",
                tag, static_cast<size_t>(pmc.PrivateUsage), static_cast<size_t>(pmc.WorkingSetSize));
    }
}

void EnsureRenderTarget(HWND hwnd) {
    if (g_renderTarget) return;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
#if defined(MARKAIR_D2D_SOFTWARE)
    rtProps.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
#endif
    g_d2dFactory->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(hwnd, size),
        &g_renderTarget);
}

void PaintOnce(HWND hwnd) {
    EnsureRenderTarget(hwnd);
    if (!g_renderTarget) return;

    ID2D1SolidColorBrush* brush = nullptr;
    g_renderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush);

    g_renderTarget->BeginDraw();
    g_renderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));

    if (brush && g_textFormat) {
        const wchar_t text[] = L"Hello 中文";
        D2D1_RECT_F layoutRect = D2D1::RectF(20.0f, 20.0f, 400.0f, 80.0f);
        g_renderTarget->DrawText(text, static_cast<UINT32>(wcslen(text)), g_textFormat, layoutRect, brush);
    }

    HRESULT hr = g_renderTarget->EndDraw();
    if (brush) brush->Release();

    if (!g_firstPresentDone) {
        QueryPerformanceCounter(&g_tFirstPresent);
        g_firstPresentDone = true;

        fprintf(stderr, "t_process_to_window_ms=%.3f\n", MsBetween(g_tProcessStart, g_tWindowCreated));
        fprintf(stderr, "t_window_to_present_ms=%.3f\n", MsBetween(g_tWindowCreated, g_tFirstPresent));
        fprintf(stderr, "t_process_to_present_ms=%.3f\n", MsBetween(g_tProcessStart, g_tFirstPresent));
        ReportMemory("first_present");
    }

    if (hr == D2DERR_RECREATE_TARGET) {
        if (g_renderTarget) { g_renderTarget->Release(); g_renderTarget = nullptr; }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_PAINT: {
        PaintOnce(hwnd);
        ValidateRect(hwnd, nullptr);
        return 0;
    }
    case WM_SIZE: {
        if (g_renderTarget) {
            D2D1_SIZE_U size = D2D1::SizeU(LOWORD(lparam), HIWORD(lparam));
            g_renderTarget->Resize(size);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_tProcessStart);

    // markair 是只读的 markdown 查看器，不需要文字输入，禁用 IME/TSF 激活可
    // 避免第三方输入法（如搜狗）把自己的 TSF 模块注入进来，实测能省下
    // 二三十 MB 级别的 private bytes（与我们自己的渲染逻辑无关）。
    ImmDisableIME(static_cast<DWORD>(-1));

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2dFactory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                         reinterpret_cast<IUnknown**>(&g_dwriteFactory));
    if (g_dwriteFactory) {
        g_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-us", &g_textFormat);
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"markair_s01_d2d_baseline";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"markair S1 spike", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);

    QueryPerformanceCounter(&g_tWindowCreated);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_textFormat) g_textFormat->Release();
    if (g_renderTarget) g_renderTarget->Release();
    if (g_dwriteFactory) g_dwriteFactory->Release();
    if (g_d2dFactory) g_d2dFactory->Release();

    return 0;
}
