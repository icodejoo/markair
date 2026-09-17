# BENCH-A：mdvn 性能基准语料

> 本文件由脚本生成，用于 T15 性能基准测量（暖启动/冷启动首屏、常驻内存）。
> 内容为中英混排的合成文档文本，贴合 mdvn 实际使用场景（技术文档/设计说明），
> 含表格语法、代码块、若干行内链接，不含图片。


## 第 1 节：基准语料片段

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 The rendering pipeline targets a first-frame latency budget well under one hundred milliseconds. 为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 The benchmark harness captures five timestamps plus one memory counter per run. 中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 Window creation and first present are tracked as two distinct phases in the timeline. 渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。

滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。 Scrolling performance is validated separately using PresentMon frame time percentiles. 解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。

mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。 Layout computation walks the block tree once and produces a flat list of paint commands. 常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。

参考文档：[md4c 项目主页](https://github.com/mity/md4c) 以及 [Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap)，两者都对本节涉及的实现细节有帮助。See also [md4c 项目主页](https://github.com/mity/md4c) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 1，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```


## 第 2 节：基准语料片段

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 Reproducible measurements depend on recording exact tool versions alongside the results. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 The benchmark harness captures five timestamps plus one memory counter per run. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

参考文档：[Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) 以及 [Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap)，两者都对本节涉及的实现细节有帮助。See also [Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 2，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```


## 第 3 节：基准语料片段

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 The rendering pipeline targets a first-frame latency budget well under one hundred milliseconds. DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 Inline links are styled distinctly from body text but remain non-interactive in this milestone. 为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。

中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 Layout computation walks the block tree once and produces a flat list of paint commands. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. 表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。

参考文档：[DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/) 以及 [PresentMon 项目](https://github.com/GameTechDev/PresentMon)，两者都对本节涉及的实现细节有帮助。See also [DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 3，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #3 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #3 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #3 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #3 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 4 节：基准语料片段

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 Direct2D was chosen over GDI+ primarily for its hardware-accelerated text rendering path. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 Window creation and first present are tracked as two distinct phases in the timeline. 代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。

为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. 滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Layout computation walks the block tree once and produces a flat list of paint commands. 解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。

参考文档：[Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap) 以及 [QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/)，两者都对本节涉及的实现细节有帮助。See also [Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 4，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```


## 第 5 节：基准语料片段

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 Code blocks are rendered with a fixed-width font to preserve column alignment. 渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 Direct2D was chosen over GDI+ primarily for its hardware-accelerated text rendering path. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. 表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。

参考文档：[Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap) 以及 [GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/)，两者都对本节涉及的实现细节有帮助。See also [Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 5，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```


## 第 6 节：基准语料片段

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Window creation and first present are tracked as two distinct phases in the timeline. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 Window creation and first present are tracked as two distinct phases in the timeline. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。 Inline links are styled distinctly from body text but remain non-interactive in this milestone. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

参考文档：[PresentMon 项目](https://github.com/GameTechDev/PresentMon) 以及 [WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged)，两者都对本节涉及的实现细节有帮助。See also [PresentMon 项目](https://github.com/GameTechDev/PresentMon) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 6，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #6 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #6 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #6 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #6 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 7 节：基准语料片段

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。

在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 Private bytes should stay below the twenty megabyte ceiling for a mid-sized document. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

参考文档：[QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/) 以及 [CommonMark 规范](https://spec.commonmark.org/0.30/)，两者都对本节涉及的实现细节有帮助。See also [QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 7，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```


## 第 8 节：基准语料片段

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 Window creation and first present are tracked as two distinct phases in the timeline. 为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。

滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。 Scrolling performance is validated separately using PresentMon frame time percentiles. 滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。

在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。 Feature scope for M0 explicitly excludes tables of contents, theming, and file association. 任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。

参考文档：[GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/) 以及 [md4c 项目主页](https://github.com/mity/md4c)，两者都对本节涉及的实现细节有帮助。See also [GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 8，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```


## 第 9 节：基准语料片段

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Scrolling performance is validated separately using PresentMon frame time percentiles. 为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。

参考文档：[WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged) 以及 [Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal)，两者都对本节涉及的实现细节有帮助。See also [WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 9，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #9 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #9 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #9 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #9 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 10 节：基准语料片段

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 The rendering pipeline targets a first-frame latency budget well under one hundred milliseconds. 性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 Code blocks are rendered with a fixed-width font to preserve column alignment. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

参考文档：[CommonMark 规范](https://spec.commonmark.org/0.30/) 以及 [DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/)，两者都对本节涉及的实现细节有帮助。See also [CommonMark 规范](https://spec.commonmark.org/0.30/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 10，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```


## 第 11 节：基准语料片段

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 Feature scope for M0 explicitly excludes tables of contents, theming, and file association. DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 Direct2D was chosen over GDI+ primarily for its hardware-accelerated text rendering path. 代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 Reproducible measurements depend on recording exact tool versions alongside the results. 常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。

中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 Feature scope for M0 explicitly excludes tables of contents, theming, and file association. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

参考文档：[md4c 项目主页](https://github.com/mity/md4c) 以及 [Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap)，两者都对本节涉及的实现细节有帮助。See also [md4c 项目主页](https://github.com/mity/md4c) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 11，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```


## 第 12 节：基准语料片段

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 This corpus intentionally interleaves short and long sentences to resemble real documentation. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. 代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。 Layout computation walks the block tree once and produces a flat list of paint commands. 渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。

任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。 Layout computation walks the block tree once and produces a flat list of paint commands. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

参考文档：[Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) 以及 [Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap)，两者都对本节涉及的实现细节有帮助。See also [Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 12，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #12 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #12 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #12 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #12 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 13 节：基准语料片段

中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。 This corpus intentionally interleaves short and long sentences to resemble real documentation. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。

为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。 Scrolling performance is validated separately using PresentMon frame time percentiles. 常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。

参考文档：[DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/) 以及 [PresentMon 项目](https://github.com/GameTechDev/PresentMon)，两者都对本节涉及的实现细节有帮助。See also [DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 13，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```


## 第 14 节：基准语料片段

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Code blocks are rendered with a fixed-width font to preserve column alignment. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。

性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。 Direct2D was chosen over GDI+ primarily for its hardware-accelerated text rendering path. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 Layout computation walks the block tree once and produces a flat list of paint commands. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。 A read-only viewer can make aggressive assumptions that a full editor cannot. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

参考文档：[Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap) 以及 [QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/)，两者都对本节涉及的实现细节有帮助。See also [Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 14，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```


## 第 15 节：基准语料片段

滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。 The benchmark harness captures five timestamps plus one memory counter per run. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

参考文档：[Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap) 以及 [GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/)，两者都对本节涉及的实现细节有帮助。See also [Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 15，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #15 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #15 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #15 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #15 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 16 节：基准语料片段

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 The benchmark harness captures five timestamps plus one memory counter per run. 为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。

任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。 A read-only viewer can make aggressive assumptions that a full editor cannot. 滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。

参考文档：[PresentMon 项目](https://github.com/GameTechDev/PresentMon) 以及 [WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged)，两者都对本节涉及的实现细节有帮助。See also [PresentMon 项目](https://github.com/GameTechDev/PresentMon) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 16，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```


## 第 17 节：基准语料片段

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 This corpus intentionally interleaves short and long sentences to resemble real documentation. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Direct2D was chosen over GDI+ primarily for its hardware-accelerated text rendering path. 性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。

参考文档：[QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/) 以及 [CommonMark 规范](https://spec.commonmark.org/0.30/)，两者都对本节涉及的实现细节有帮助。See also [QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 17，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```


## 第 18 节：基准语料片段

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 Direct2D was chosen over GDI+ primarily for its hardware-accelerated text rendering path. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。 Private bytes should stay below the twenty megabyte ceiling for a mid-sized document. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

参考文档：[GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/) 以及 [md4c 项目主页](https://github.com/mity/md4c)，两者都对本节涉及的实现细节有帮助。See also [GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 18，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #18 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #18 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #18 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #18 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 19 节：基准语料片段

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。 Layout computation walks the block tree once and produces a flat list of paint commands. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

参考文档：[WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged) 以及 [Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal)，两者都对本节涉及的实现细节有帮助。See also [WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 19，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```


## 第 20 节：基准语料片段

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 This corpus intentionally interleaves short and long sentences to resemble real documentation. 任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. 解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

参考文档：[CommonMark 规范](https://spec.commonmark.org/0.30/) 以及 [DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/)，两者都对本节涉及的实现细节有帮助。See also [CommonMark 规范](https://spec.commonmark.org/0.30/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 20，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```


## 第 21 节：基准语料片段

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 Window creation and first present are tracked as two distinct phases in the timeline. 任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。

中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。

参考文档：[md4c 项目主页](https://github.com/mity/md4c) 以及 [Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap)，两者都对本节涉及的实现细节有帮助。See also [md4c 项目主页](https://github.com/mity/md4c) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 21，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #21 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #21 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #21 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #21 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 22 节：基准语料片段

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。

任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。 Feature scope for M0 explicitly excludes tables of contents, theming, and file association. 内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 Cold start measurements require clearing the standby list to avoid file cache warm effects. 渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。

参考文档：[Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) 以及 [Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap)，两者都对本节涉及的实现细节有帮助。See also [Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 22，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```


## 第 23 节：基准语料片段

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 The benchmark harness captures five timestamps plus one memory counter per run. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 A read-only viewer can make aggressive assumptions that a full editor cannot. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。 Reproducible measurements depend on recording exact tool versions alongside the results. 解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。

为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。 Layout computation walks the block tree once and produces a flat list of paint commands. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。 A read-only viewer can make aggressive assumptions that a full editor cannot. 内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。

参考文档：[DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/) 以及 [PresentMon 项目](https://github.com/GameTechDev/PresentMon)，两者都对本节涉及的实现细节有帮助。See also [DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 23，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```


## 第 24 节：基准语料片段

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。 Code blocks are rendered with a fixed-width font to preserve column alignment. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 Layout computation walks the block tree once and produces a flat list of paint commands. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

参考文档：[Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap) 以及 [QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/)，两者都对本节涉及的实现细节有帮助。See also [Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 24，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #24 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #24 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #24 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #24 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 25 节：基准语料片段

为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。 A read-only viewer can make aggressive assumptions that a full editor cannot. 性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 Window creation and first present are tracked as two distinct phases in the timeline. 常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。

表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。 Layout computation walks the block tree once and produces a flat list of paint commands. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

参考文档：[Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap) 以及 [GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/)，两者都对本节涉及的实现细节有帮助。See also [Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 25，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```


## 第 26 节：基准语料片段

代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。 Cold start measurements require clearing the standby list to avoid file cache warm effects. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 Code blocks are rendered with a fixed-width font to preserve column alignment. 为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 The rendering pipeline targets a first-frame latency budget well under one hundred milliseconds. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。 Direct2D was chosen over GDI+ primarily for its hardware-accelerated text rendering path. 中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。

参考文档：[PresentMon 项目](https://github.com/GameTechDev/PresentMon) 以及 [WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged)，两者都对本节涉及的实现细节有帮助。See also [PresentMon 项目](https://github.com/GameTechDev/PresentMon) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 26，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```


## 第 27 节：基准语料片段

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 Direct2D was chosen over GDI+ primarily for its hardware-accelerated text rendering path. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。

为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。

参考文档：[QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/) 以及 [CommonMark 规范](https://spec.commonmark.org/0.30/)，两者都对本节涉及的实现细节有帮助。See also [QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 27，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #27 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #27 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #27 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #27 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 28 节：基准语料片段

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。

mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。 Inline links are styled distinctly from body text but remain non-interactive in this milestone. 中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 Cold start measurements require clearing the standby list to avoid file cache warm effects. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

参考文档：[GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/) 以及 [md4c 项目主页](https://github.com/mity/md4c)，两者都对本节涉及的实现细节有帮助。See also [GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 28，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```


## 第 29 节：基准语料片段

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 Window creation and first present are tracked as two distinct phases in the timeline. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 Layout computation walks the block tree once and produces a flat list of paint commands. 中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。

滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。 Window creation and first present are tracked as two distinct phases in the timeline. 中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 Private bytes should stay below the twenty megabyte ceiling for a mid-sized document. 解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。

参考文档：[WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged) 以及 [Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal)，两者都对本节涉及的实现细节有帮助。See also [WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 29，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```


## 第 30 节：基准语料片段

表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 The benchmark harness captures five timestamps plus one memory counter per run. 性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。

表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。 Window creation and first present are tracked as two distinct phases in the timeline. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。 Private bytes should stay below the twenty megabyte ceiling for a mid-sized document. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

参考文档：[CommonMark 规范](https://spec.commonmark.org/0.30/) 以及 [DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/)，两者都对本节涉及的实现细节有帮助。See also [CommonMark 规范](https://spec.commonmark.org/0.30/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 30，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #30 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #30 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #30 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #30 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 31 节：基准语料片段

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Scrolling performance is validated separately using PresentMon frame time percentiles. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Inline links are styled distinctly from body text but remain non-interactive in this milestone. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。

参考文档：[md4c 项目主页](https://github.com/mity/md4c) 以及 [Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap)，两者都对本节涉及的实现细节有帮助。See also [md4c 项目主页](https://github.com/mity/md4c) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 31，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```


## 第 32 节：基准语料片段

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. 解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。

任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。 The benchmark harness captures five timestamps plus one memory counter per run. 常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。

性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. 代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 The benchmark harness captures five timestamps plus one memory counter per run. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

参考文档：[Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) 以及 [Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap)，两者都对本节涉及的实现细节有帮助。See also [Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 32，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```


## 第 33 节：基准语料片段

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 The rendering pipeline targets a first-frame latency budget well under one hundred milliseconds. 性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 Cold start measurements require clearing the standby list to avoid file cache warm effects. 滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。

参考文档：[DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/) 以及 [PresentMon 项目](https://github.com/GameTechDev/PresentMon)，两者都对本节涉及的实现细节有帮助。See also [DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 33，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #33 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #33 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #33 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #33 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 34 节：基准语料片段

解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。 Direct2D was chosen over GDI+ primarily for its hardware-accelerated text rendering path. 为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 Reproducible measurements depend on recording exact tool versions alongside the results. 内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. 滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。

参考文档：[Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap) 以及 [QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/)，两者都对本节涉及的实现细节有帮助。See also [Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 34，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```


## 第 35 节：基准语料片段

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。 Feature scope for M0 explicitly excludes tables of contents, theming, and file association. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

参考文档：[Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap) 以及 [GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/)，两者都对本节涉及的实现细节有帮助。See also [Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 35，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```


## 第 36 节：基准语料片段

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 Reproducible measurements depend on recording exact tool versions alongside the results. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。 Code blocks are rendered with a fixed-width font to preserve column alignment. 内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。

代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。 Scrolling performance is validated separately using PresentMon frame time percentiles. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

参考文档：[PresentMon 项目](https://github.com/GameTechDev/PresentMon) 以及 [WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged)，两者都对本节涉及的实现细节有帮助。See also [PresentMon 项目](https://github.com/GameTechDev/PresentMon) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 36，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #36 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #36 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #36 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #36 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 37 节：基准语料片段

代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。 Window creation and first present are tracked as two distinct phases in the timeline. 中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Scrolling performance is validated separately using PresentMon frame time percentiles. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Reproducible measurements depend on recording exact tool versions alongside the results. 滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。

代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

参考文档：[QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/) 以及 [CommonMark 规范](https://spec.commonmark.org/0.30/)，两者都对本节涉及的实现细节有帮助。See also [QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 37，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```


## 第 38 节：基准语料片段

代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。 Code blocks are rendered with a fixed-width font to preserve column alignment. 常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 A read-only viewer can make aggressive assumptions that a full editor cannot. 为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

参考文档：[GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/) 以及 [md4c 项目主页](https://github.com/mity/md4c)，两者都对本节涉及的实现细节有帮助。See also [GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 38，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```


## 第 39 节：基准语料片段

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Feature scope for M0 explicitly excludes tables of contents, theming, and file association. 任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Layout computation walks the block tree once and produces a flat list of paint commands. 代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 Reproducible measurements depend on recording exact tool versions alongside the results. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 The benchmark harness captures five timestamps plus one memory counter per run. 代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。

参考文档：[WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged) 以及 [Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal)，两者都对本节涉及的实现细节有帮助。See also [WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 39，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #39 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #39 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #39 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #39 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 40 节：基准语料片段

常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。 Private bytes should stay below the twenty megabyte ceiling for a mid-sized document. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。 The benchmark harness captures five timestamps plus one memory counter per run. 任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。

为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。 Layout computation walks the block tree once and produces a flat list of paint commands. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

参考文档：[CommonMark 规范](https://spec.commonmark.org/0.30/) 以及 [DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/)，两者都对本节涉及的实现细节有帮助。See also [CommonMark 规范](https://spec.commonmark.org/0.30/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 40，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```


## 第 41 节：基准语料片段

滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。 The rendering pipeline targets a first-frame latency budget well under one hundred milliseconds. 性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 The benchmark harness captures five timestamps plus one memory counter per run. DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。

参考文档：[md4c 项目主页](https://github.com/mity/md4c) 以及 [Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap)，两者都对本节涉及的实现细节有帮助。See also [md4c 项目主页](https://github.com/mity/md4c) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 41，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```


## 第 42 节：基准语料片段

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 The benchmark harness captures five timestamps plus one memory counter per run. 性能预算是 M0 阶段的第一优先级，功能完整性反而是次要目标。

性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。 Private bytes should stay below the twenty megabyte ceiling for a mid-sized document. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 Window creation and first present are tracked as two distinct phases in the timeline. 为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 The arena allocator trades flexibility for predictable, low-overhead allocation patterns. 为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。

参考文档：[Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) 以及 [Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap)，两者都对本节涉及的实现细节有帮助。See also [Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 42，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #42 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #42 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #42 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #42 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 43 节：基准语料片段

内存分配器使用了简单的 arena 策略，避免频繁的堆分配带来的抖动。 Scrolling performance is validated separately using PresentMon frame time percentiles. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。 Cold start measurements require clearing the standby list to avoid file cache warm effects. 任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。

性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。

为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。 Feature scope for M0 explicitly excludes tables of contents, theming, and file association. 任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

参考文档：[DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/) 以及 [PresentMon 项目](https://github.com/GameTechDev/PresentMon)，两者都对本节涉及的实现细节有帮助。See also [DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 43，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```


## 第 44 节：基准语料片段

代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。 The benchmark harness captures five timestamps plus one memory counter per run. 为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 Inline links are styled distinctly from body text but remain non-interactive in this milestone. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 Private bytes should stay below the twenty megabyte ceiling for a mid-sized document. 中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。

参考文档：[Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap) 以及 [QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/)，两者都对本节涉及的实现细节有帮助。See also [Sysinternals VMMap](https://learn.microsoft.com/sysinternals/downloads/vmmap) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 44，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```


## 第 45 节：基准语料片段

在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。 Private bytes should stay below the twenty megabyte ceiling for a mid-sized document. 常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 Scrolling performance is validated separately using PresentMon frame time percentiles. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。 Window creation and first present are tracked as two distinct phases in the timeline. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

参考文档：[Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap) 以及 [GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/)，两者都对本节涉及的实现细节有帮助。See also [Sysinternals RAMMap](https://learn.microsoft.com/sysinternals/downloads/rammap) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 45，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #45 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #45 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #45 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #45 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 46 节：基准语料片段

解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。 Mixed CJK and Latin text wrapping is one of the trickiest correctness problems in this project. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。 A read-only viewer can make aggressive assumptions that a full editor cannot. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. 任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。

mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。 Table layout requires a two-pass algorithm: measure column widths, then paint rows. 代码块的渲染需要保证等宽字体和正确的语法高亮边界，即便高亮本身不在 M0 范围内。

参考文档：[PresentMon 项目](https://github.com/GameTechDev/PresentMon) 以及 [WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged)，两者都对本节涉及的实现细节有帮助。See also [PresentMon 项目](https://github.com/GameTechDev/PresentMon) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 46，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```


## 第 47 节：基准语料片段

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 Window creation and first present are tracked as two distinct phases in the timeline. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

为了避免样本过于机械，语料中混合了长句、短句、列表和代码片段。 The benchmark harness captures five timestamps plus one memory counter per run. 任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

任务列表、图片、超链接点击等功能被有意推迟到 M1 及以后的版本。 The rendering pipeline targets a first-frame latency budget well under one hundred milliseconds. 在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。

参考文档：[QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/) 以及 [CommonMark 规范](https://spec.commonmark.org/0.30/)，两者都对本节涉及的实现细节有帮助。See also [QueryPerformanceCounter 文档](https://learn.microsoft.com/windows/win32/api/profileapi/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 47，用于人工核对语料生成是否重复。

```text
t_process_to_parse_ms=3.265 t_parse_to_layout_ms=0.012 t_layout_to_window_ms=7.299 t_window_to_present_ms=58.556 t_process_to_present_ms=69.132 private_bytes=10526720
```


## 第 48 节：基准语料片段

在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。 Cold start measurements require clearing the standby list to avoid file cache warm effects. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

中英混排文本的断行位置是本项目的一个高风险项，需要在 T10 阶段重点验证。 Reproducible measurements depend on recording exact tool versions alongside the results. 文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。

mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。 High DPI awareness means recomputing text metrics whenever the effective scale factor changes. 滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。

滚动时的重排开销是另一个需要用 PresentMon 抓帧来验证的风险点。 The rendering pipeline targets a first-frame latency budget well under one hundred milliseconds. 渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。

参考文档：[GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/) 以及 [md4c 项目主页](https://github.com/mity/md4c)，两者都对本节涉及的实现细节有帮助。See also [GetProcessMemoryInfo 文档](https://learn.microsoft.com/windows/win32/api/psapi/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 48，用于人工核对语料生成是否重复。

```cpp
void mdvn::bench::MarkFirstPresent() {
    if (!g_enabled || g_firstPresentRecorded) return;
    g_tFirstPresent = NowCounter();
    g_firstPresentRecorded = true;
}
```

| 指标 | 暖启动预算 | 冷启动预算 | 备注 |
|---|---|---|---|
| 首屏时间 #48 | <= 80 ms | <= 250 ms | 内置埋点采集 |
| 常驻内存 #48 | <= 20 MB | <= 20 MB | VMMap 为准 |
| 空文档内存 #48 | <= 8 MB | <= 12 MB | 不传文件启动 |
| exe 体积 #48 | 记录基线 | 记录基线 | M3 才门禁 |


## 第 49 节：基准语料片段

窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。 Private bytes should stay below the twenty megabyte ceiling for a mid-sized document. 性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。

DirectWrite 的 zh-cn locale 设置会影响标点符号与连续汉字的换行策略。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 为了让测量可复现，所有 Sysinternals 工具都需要记录版本号。

在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。 Inline links are styled distinctly from body text but remain non-interactive in this milestone. 常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。

性能基准语料应当贴近真实使用场景，而不是刻意堆砌极端案例。 Inline links are styled distinctly from body text but remain non-interactive in this milestone. 行内链接的样式应当与正文有明显区分，但点击行为在 M0 阶段暂不实现。

在高 DPI 屏幕上，文本度量必须重新计算，否则会出现锯齿或错位。 Percentile reporting, especially P95, matters more than the mean for interactive latency budgets. 窗口拖动到不同 DPI 的显示器之间时，D2D 资源需要在 WM_DPICHANGED 时重建。

参考文档：[WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged) 以及 [Direct2D 官方文档](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal)，两者都对本节涉及的实现细节有帮助。See also [WM_DPICHANGED 消息](https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 49，用于人工核对语料生成是否重复。

```cpp
LARGE_INTEGER freq{};
QueryPerformanceFrequency(&freq);
PROCESS_MEMORY_COUNTERS_EX pmc{};
pmc.cb = sizeof(pmc);
GetProcessMemoryInfo(GetCurrentProcess(),
                      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                      sizeof(pmc));
```


## 第 50 节：基准语料片段

渲染管线基于 Direct2D，窗口首次绘制的时间被内置埋点精确记录。 This corpus intentionally interleaves short and long sentences to resemble real documentation. mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。

文档解析完成后立即进入布局阶段，两者之间的耗时会被单独记录一条指标。 Markdown parsing is delegated to md4c, a small and fast CommonMark-compliant C parser. 表格语法在 Markdown 中较为特殊，需要单独的表格布局器来处理列宽与对齐。

冷启动场景下，操作系统的 standby list 会显著影响首次页面命中的耗时。 Layout computation walks the block tree once and produces a flat list of paint commands. 解析阶段使用 md4c 作为底层库，布局阶段由自研的块级布局引擎完成。

mdvn 是一个只读的 Markdown 查看器，专注于快速打开与流畅滚动。 The benchmark harness captures five timestamps plus one memory counter per run. 常驻内存必须控制在 20MB 以内，字体资源应当映射为 Mapped File 而不是 Private Bytes。

参考文档：[CommonMark 规范](https://spec.commonmark.org/0.30/) 以及 [DirectWrite 文本度量](https://learn.microsoft.com/windows/win32/directwrite/)，两者都对本节涉及的实现细节有帮助。See also [CommonMark 规范](https://spec.commonmark.org/0.30/) for the canonical reference.

- 要点一：性能优先于功能完整性。
- 要点二：Mixed CJK/Latin wrapping needs manual visual verification.
- 要点三：本节编号 50，用于人工核对语料生成是否重复。

```powershell
$p = Start-Process -FilePath $MdvnExe -ArgumentList "--bench", $BenchFile `
    -RedirectStandardError $errFile -PassThru
Wait-Process -Id $p.Id -Timeout 10 -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```

