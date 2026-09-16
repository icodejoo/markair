// =============================================================================
// spikes/s02_font_probe.cpp
//
// ⚠️ 这是一个 SPIKE,不是产品代码。
//    - 不进主构建,不受项目编码规范约束(这里允许 printf / 裸指针 / 不做错误恢复)
//    - 目的只有一个:回答"DirectWrite 按需用字体族名,会不会像 fontdb 那样把字体
//      数据拖进进程私有内存"这个**阻塞性假设**。架构文档 §4 的字体白名单方案、
//      以及 15MB 内存目标,全都建立在这个假设上。
//
// 对应:04-delivery-plan.md 的 M-1 `spikes/s02_font_probe`
//        01-requirements.md §8 裁决 #10 末尾标注的待验证前提
//
// 要回答的问题:
//   Q1 仅 DWriteCreateFactory,私有内存增量多少?
//   Q2 CreateTextFormat("Microsoft YaHei UI") + 真实排版一段中英混排文本,增量多少?
//   Q3 用 IDWriteFontFallbackBuilder 只登记 3 个字体族做自定义回退链,增量多少?
//   Q4 (对照组)调用 GetSystemFontCollection 并枚举全部字体族,增量多少?耗时多少?
//
// 判据:Q2/Q3 的私有内存增量应在**百 KB 级**。若 Q3 与 Q4 量级接近,说明
//       FontFallbackBuilder 内部仍触发了系统字体集加载,架构 §4 方案需要重做。
//
// 编译(开发者命令提示符):
//   cl /nologo /EHsc /O2 /W4 s02_font_probe.cpp /link dwrite.lib psapi.lib
// 运行:
//   s02_font_probe.exe
// 交叉验证:
//   用 VMMap 附加到本进程(它会在每步后暂停等回车),看 Private Working Set
//   与 Mapped File 的**分项**——psapi 的 PrivateUsage 只是代理指标,字体是否
//   走共享映射必须由 VMMap 的分项确认。
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <dwrite_2.h>
#include <stdio.h>
#include <string.h>

// ---- 测量工具 ---------------------------------------------------------------

// 取当前进程的私有内存字节数(代理指标,精确分项请用 VMMap)
static SIZE_T PrivateBytes()
{
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(),
                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                         sizeof(pmc));
    return pmc.PrivateUsage;
}

// 取当前进程的工作集字节数
static SIZE_T WorkingSet()
{
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(),
                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                         sizeof(pmc));
    return pmc.WorkingSetSize;
}

// 高精度计时:返回自某固定原点的毫秒数
static double NowMs()
{
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}

static SIZE_T g_lastPrivate = 0;

// 打印从上一个检查点到现在的内存增量与耗时
static void Report(const char* step, double elapsedMs)
{
    SIZE_T now = PrivateBytes();
    double deltaKB = (double)((__int64)now - (__int64)g_lastPrivate) / 1024.0;
    printf("  %-46s  Private=%7.0f KB  (%+8.1f KB)  WS=%7.0f KB  %8.2f ms\n",
           step,
           (double)now / 1024.0,
           deltaKB,
           (double)WorkingSet() / 1024.0,
           elapsedMs);
    g_lastPrivate = now;
}

// 暂停,便于用 VMMap 附加观察分项
static void PauseForVMMap(const char* what)
{
    printf("      >> [%s] 已就绪。可用 VMMap 观察分项,按回车继续...\n", what);
    (void)getchar();
}

// ---- 主流程 -----------------------------------------------------------------

int main(int argc, char** argv)
{
    // 传 --pause 才在每步暂停,默认一口气跑完
    bool pause = (argc > 1 && strcmp(argv[1], "--pause") == 0);

    printf("s02_font_probe — DirectWrite 字体加载开销探测 (SPIKE)\n");
    printf("%s\n", "--------------------------------------------------------------"
                   "--------------------------------------------");

    g_lastPrivate = PrivateBytes();
    Report("Q0 进程基线(未碰 DirectWrite)", 0.0);
    if (pause) PauseForVMMap("Q0 基线");

    // ---- Q1: 只创建工厂 ----
    double t0 = NowMs();
    IDWriteFactory2* factory = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                     __uuidof(IDWriteFactory2),
                                     reinterpret_cast<IUnknown**>(&factory));
    if (FAILED(hr) || !factory) {
        printf("  [FAIL] DWriteCreateFactory hr=0x%08lX\n", (unsigned long)hr);
        return 1;
    }
    Report("Q1 DWriteCreateFactory(IDWriteFactory2)", NowMs() - t0);
    if (pause) PauseForVMMap("Q1 工厂");

    // ---- Q2: 按族名建 TextFormat + 真实排版中英混排 ----
    // 注意:只有真正排版(取字形、度量)才会触发字体数据加载,
    //       仅 CreateTextFormat 可能是惰性的,所以这里必须走到 GetMetrics。
    t0 = NowMs();
    IDWriteTextFormat* fmt = nullptr;
    hr = factory->CreateTextFormat(L"Microsoft YaHei UI",
                                   nullptr,
                                   DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_FONT_STYLE_NORMAL,
                                   DWRITE_FONT_STRETCH_NORMAL,
                                   16.0f,
                                   L"zh-cn",
                                   &fmt);
    if (FAILED(hr) || !fmt) {
        printf("  [FAIL] CreateTextFormat hr=0x%08lX\n", (unsigned long)hr);
        return 1;
    }
    Report("Q2a CreateTextFormat(\"Microsoft YaHei UI\")", NowMs() - t0);

    t0 = NowMs();
    static const wchar_t kSample[] =
        L"mdvn 是一个 Windows 专用的只读 Markdown 查看器,"
        L"目标常驻内存 15MB。Mixed CJK/Latin line-breaking test: "
        L"https://github.com/mity/md4c —— 排版质量与断行位置需人工确认。";
    IDWriteTextLayout* layout = nullptr;
    hr = factory->CreateTextLayout(kSample,
                                   (UINT32)(sizeof(kSample) / sizeof(wchar_t) - 1),
                                   fmt, 800.0f, 600.0f, &layout);
    if (SUCCEEDED(hr) && layout) {
        DWRITE_TEXT_METRICS tm = {};
        layout->GetMetrics(&tm);  // 强制触发字形度量 → 真正加载字体数据
        printf("      (排版结果: %u 行, 宽 %.1f)\n", tm.lineCount, tm.width);
    }
    Report("Q2b CreateTextLayout + GetMetrics(中英混排)", NowMs() - t0);
    if (pause) PauseForVMMap("Q2 按族名排版后 —— 关键观察点");

    // ---- Q3: 自定义字体回退链(只登记白名单的 3 个族) ----
    // 这是架构文档 §4 第 3 条方案的真实 API 路径。
    t0 = NowMs();
    IDWriteFontFallbackBuilder* fb = nullptr;
    hr = factory->CreateFontFallbackBuilder(&fb);
    if (SUCCEEDED(hr) && fb) {
        DWRITE_UNICODE_RANGE ranges[] = {
            { 0x0020, 0x024F },   // 基本拉丁 + 拉丁扩展
            { 0x4E00, 0x9FFF },   // CJK 统一表意文字
            { 0x3000, 0x30FF },   // CJK 标点 + 假名
        };
        const wchar_t* latin[] = { L"Segoe UI" };
        const wchar_t* cjk[]   = { L"Microsoft YaHei UI" };

        fb->AddMapping(&ranges[0], 1, latin, 1, nullptr, nullptr, nullptr, 1.0f);
        fb->AddMapping(&ranges[1], 2, cjk,   1, nullptr, nullptr, nullptr, 1.0f);

        IDWriteFontFallback* fallback = nullptr;
        hr = fb->CreateFontFallback(&fallback);
        if (SUCCEEDED(hr) && fallback) {
            IDWriteTextLayout2* layout2 = nullptr;
            if (layout && SUCCEEDED(layout->QueryInterface(
                    __uuidof(IDWriteTextLayout2),
                    reinterpret_cast<void**>(&layout2))) && layout2) {
                layout2->SetFontFallback(fallback);
                DWRITE_TEXT_METRICS tm = {};
                layout2->GetMetrics(&tm);  // 用自定义回退链重排一次
                layout2->Release();
            }
            fallback->Release();
        }
        fb->Release();
    } else {
        printf("  [WARN] CreateFontFallbackBuilder hr=0x%08lX\n", (unsigned long)hr);
    }
    Report("Q3 自定义 FontFallback(仅 3 个族)+ 重排", NowMs() - t0);
    if (pause) PauseForVMMap("Q3 自定义回退链后 —— 关键观察点");

    // ---- Q4: 对照组 —— 架构文档明令禁止的全量枚举 ----
    // 跑这一步只是为了量出"被禁止的那条路"到底有多贵,好让禁令有数据支撑。
    t0 = NowMs();
    IDWriteFontCollection* coll = nullptr;
    hr = factory->GetSystemFontCollection(&coll, FALSE);
    if (SUCCEEDED(hr) && coll) {
        UINT32 n = coll->GetFontFamilyCount();
        UINT32 touched = 0;
        for (UINT32 i = 0; i < n; ++i) {
            IDWriteFontFamily* fam = nullptr;
            if (SUCCEEDED(coll->GetFontFamily(i, &fam)) && fam) {
                IDWriteLocalizedStrings* names = nullptr;
                if (SUCCEEDED(fam->GetFamilyNames(&names)) && names) {
                    wchar_t buf[128] = {};
                    names->GetString(0, buf, 128);   // 真的去读族名
                    ++touched;
                    names->Release();
                }
                fam->Release();
            }
        }
        printf("      (枚举到 %u 个字体族,成功读名 %u 个)\n", n, touched);
        coll->Release();
    }
    Report("Q4 [对照组] GetSystemFontCollection + 全量枚举", NowMs() - t0);
    if (pause) PauseForVMMap("Q4 全量枚举后 —— 对照组");

    if (layout)  layout->Release();
    if (fmt)     fmt->Release();
    if (factory) factory->Release();

    printf("%s\n", "--------------------------------------------------------------"
                   "--------------------------------------------");
    printf("判读方式:\n");
    printf("  * Q2b + Q3 的私有内存增量合计应在**百 KB 级** → 架构 §4 白名单方案成立\n");
    printf("  * 若 Q3 的增量与 Q4 同量级(MB 级) → FontFallbackBuilder 内部触发了\n");
    printf("    系统字体集加载,架构 §4 第 3 条需改为只用 CreateTextFormat 逐 run 指定族名\n");
    printf("  * 务必用 VMMap 确认字体落在 Mapped File 而非 Private Working Set\n");
    return 0;
}
