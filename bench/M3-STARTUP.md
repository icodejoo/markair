# M3-STARTUP：启动时间打磨（T74）

> 目标：暖启动首屏中位数 ≤ 60 ms、冷启动 ≤ 250 ms（各 20 轮）。
> 结论先说：**冷启动达标；暖启动未达标（但未超 120 ms 上限）**，已尝试的
> 优化手段见下，剩余差距的根因是架构级、非本任务权限范围内可解的约束，
> 按裁决记录 #5 提交用户裁决，不擅自放宽指标。

---

## 1. 归因：5 段埋点定位耗时大头

用 `bench/run_bench.ps1 -Target BENCH-A -N 20`（暖启动口径见 `M3-METHOD.md`
第1节）反复测量，`t_window_to_present_ms` 稳定占总耗时 **60%~75%**，是
唯一的大头；其余四段（`process_to_parse`/`parse_to_layout`/`layout_to_window`）
合计通常 < 20 ms。三次代表性采样（同一构建，未改代码）：

| 采样 | process_to_parse | parse_to_layout | layout_to_window | **window_to_present** | process_to_present |
|---|---|---|---|---|---|
| A | 4.259 | 0.536 | 14.656 | **48.855** | 67.566 |
| B | 3.447 | 0.512 | 10.975 | **38.802** | 53.736 |
| C | 7.887 | 1.038 | 28.639 | **96.412** | 138.594 |

采样 C 明显偏高，复查时机器上同时跑着本次会话自身（Claude Code 进程）与
Chrome 等高 CPU 占用进程（`Get-Process | Sort CPU` 当时前列是
`chrome`/`claude`），**本机不是"干净环境"**，这与 `M3-METHOD.md` 记录的
搜狗输入法常驻进程干扰是同一类问题的另一种表现——**测量噪声本身就是
本次排查要如实记录的一个事实**，不代表代码变慢。后续正式 20 轮测量选在
后台负载较低的时刻跑（见第 4 节），排除偶发尖峰。

**结论：97% 以上的暖启动优化空间都在 `window_to_present` 这一段**，即
`main.cpp` 建窗后到 `window.cpp::PaintOnce` 里首次 `RenderFrame`/`EndDraw`
成功返回之间。定位到具体调用点：`Renderer::EnsureRenderTarget`
(`src/render/renderer.cpp:370`) 里的
`factory_->CreateHwndRenderTarget(...)` —— 这是本次会话里**唯一**首次调用
且发生在 `window_to_present` 窗口内的重量级 API。

## 2. 模块加载表：`/DELAYLOAD` 复核

在 `EmitReport()`（`src/app/bench.cpp`）里新增了 4 个 `GetModuleHandleW`
零副作用查询，**精确在"首次 Present 已完成"这一时刻**（而不是外部工具
事后快照的任意时刻）回答"延迟加载是否真的推迟到了首帧之后"：

```
module_winhttp_loaded_at_first_present=0
module_shell32_loaded_at_first_present=0
module_dwmapi_loaded_at_first_present=1
module_uxtheme_loaded_at_first_present=1
```

- `winhttp.dll`／`shell32.dll`：**确认延迟生效**，首帧完成时刻均未加载
  （与代码注释一致：只在用户点外链/图片、或网络图片请求时才会触发）。
- `dwmapi.dll`／`uxtheme.dll`：**确认按代码注释里已知的方式提前加载**——
  `DwmSetWindowAttribute`（标题栏深浅色）与 `SetWindowTheme`（滚动条主题）
  都在窗口创建阶段（`CreateMainWindow` 内）无条件调用，`/DELAYLOAD` 只能
  做到"不在导入表解析阶段加载"，实际仍会在窗口创建时被拉起——这是
  `CMakeLists.txt:61-70` 已经写明的预期行为，不是本次新发现的问题，验证
  结果与文档一致。

额外用 `Get-Process.Modules` 对运行中的 markair.exe 做了一次全量快照
（进程存活 800ms 后），按体积排序前列：

| 模块 | 大小(KB) | 备注 |
|---|---|---|
| windows.storage.dll | 7828 | 在首帧完成之后才出现（下方说明） |
| SHELL32.dll | 7616 | 同上 |
| D3D10Warp.dll | 7128 | **WARP 软件光栅化设备，D2D 强制依赖** |
| d2d1.dll | 5888 | D2D1CreateFactory 时加载 |
| combase.dll | 3408 | COM 基础设施 |
| comctl32.DLL | 2668 | 窗口类/控件主题（清单激活） |
| DWrite.dll | 2556 | 字体子系统 Init 时加载 |
| d3d11.dll | 2448 | **CreateHwndRenderTarget 隐式创建的 D3D 设备** |
| dxgi.dll | 984 | 同上 |
| dxcore.dll | 236 | 同上 |
| uxtheme.dll / dwmapi.dll | 632 / 184 | 窗口创建期提前加载（已知/接受） |

**关键发现**：`Get-Process.Modules` 快照里出现的 `SHELL32.dll` /
`windows.storage.dll`，与上面 `GetModuleHandleW` 在首帧完成瞬间测到的结果
**不矛盾**——它们是在首帧完成**之后**（800ms 采样窗口内的某个更晚时刻）
才被加载的，具体触发点超出本任务范围（不影响首屏指标，不深挖）。真正
在 `window_to_present` 关键路径上、且体积最大的是
`D3D10Warp.dll`(7.1MB) + `d3d11.dll`(2.4MB) + `dxgi.dll`(1MB) +
`dxcore.dll`(0.2MB) ≈ **10.8MB**，全部由 `CreateHwndRenderTarget` 一次
调用触发。

## 3. 根因：D2D 的"软件渲染"仍然强制依赖 DXGI/D3D

`renderer.cpp:379` 设置 `D2D1_RENDER_TARGET_TYPE_SOFTWARE` 是 D2D **光栅化
器**层面的选择，但 Windows 8 之后的 Direct2D（`ID2D1Factory::
CreateHwndRenderTarget`）无论渲染目标类型是硬件还是软件，底层呈现通道
始终经过 DXGI，且在没有可用硬件 D3D 设备（或架构上不允许探测硬件，见
主进程注释"避免触发 Intel iGPU 的着色器编译器"）时会退化到 WARP
(`D3D10Warp.dll`)。这一条 DLL 组合的映射/首次访问耗时，就是
`window_to_present` 里去掉几毫秒 DirectWrite 文本布局创建之后剩下的大头，
**是 Direct2D API 本身的强制行为，不是 markair 代码里可以绕开的一次性初始化
浪费**。

## 4. 已尝试的优化（均在允许范围内）与结果

| # | 手段 | 结果 |
|---|---|---|
| 1 | 复核"首帧只排版可见±1屏"是否真的生效 | **已生效**：`BlockLayoutEngine::UpdateVisibleRange`（`layout.h:198`起注释、`layout.cpp:841`实现）按"可见范围±1屏"筛选布局，`window.cpp:864` 首帧调用点参数正确传入 `scrollY`/`viewportHeight`；BENCH-A 是 100KB 小文档本来就不构成瓶颈，此项确认无退步。 |
| 2 | 复核 `/OPT`、`/LTCG` 参数 | **已在位**：`CMakeLists.txt:39-40` 的 `/OPT:REF /OPT:ICF /LTCG` 与 `/O2 /GL /Gy /Gw`（34-36行）均已配置在 Release 构建里，本次未发现遗漏项，未新增改动。 |
| 3 | 复核 `/DELAYLOAD` 4 个 DLL 是否真的延后 | **winhttp/shell32 确认真延后；dwmapi/uxtheme 确认按文档已知方式提前**（见第2节新增的运行期诊断），未发现回归，未发现"名不副实"的新问题。 |
| 4 | 惰性初始化排查：`findArena`/`findScratch`/`clipboardScratch` 等在首帧前的 `Arena::Init` 调用是否可延后 | 排查后确认这些 `Init` 只是 `VirtualAlloc` **保留**地址空间（不提交物理页），耗时在微秒级，且均已在 `main.cpp` 里以"用到才 Init"的原则组织（大纲侧栏 `outlineArena` 已经是首帧后按需 Init 的先例）；未发现可继续往后挪且不改变行为的候选，未改动。 |
| 5 | `RunArenaSmokeTest`/`RunMd4cSmokeTest`（`main.cpp:443-444`） | 这两个烟雾测试在每次启动无条件跑一次，属于 `process_to_parse` 之前的极小固定开销（微秒级，被 `t_process_to_parse_ms` 的中位数 3~8ms 掩盖），排查后判断不是本次瓶颈来源，为避免引入"验证途径消失"的回归风险，本次未删除/未改动。 |
| 6 | 尝试把 `EnsureRenderTarget` 的调用点从"首次 WM_PAINT"提前到"CreateMainWindow 刚返回"（意图与 dwmapi/uxtheme 的加载窗口重叠） | **评估后未实施**：markair 是单线程架构（`04-delivery-plan.md` §8 单线程约束，`T77` 验收标准里也重申"严禁引入后台线程"），单线程下重排调用顺序不能产生任何并行重叠收益，总耗时不变，反而增加代码复杂度，判定为无效优化，未落地。 |
| 7 | 尝试用后台线程预热 D3D11/DXGI/WARP 模块（`LoadLibraryW` 提前触发分页） | **判定为禁止手段，未实施**：这本质是"预加载"（08-m3-tasks.md T74 明文禁止"预加载/常驻进程/后台预热服务"），且会引入线程，与单线程架构承诺冲突，直接放弃这个方向，未做任何代码改动。 |

**结论：本任务在允许的优化手段范围内，未找到可进一步压缩
`window_to_present` 的空间**——它的主体成本是 Direct2D API 强制经过
DXGI/WARP 呈现通道所致的模块加载与首次设备创建，这是架构选择（"D2D 默认
软件光栅化,不做硬件探测"）的固有代价，改变它意味着修改渲染路径的架构
决策（超出 T74 权限，且属于 T76 软件渲染路径任务的范围）。

## 5. 优化前后对比（均为同一份未改动产品代码的重复测量，见第4节：本任务
未对产品代码做任何改动，故"优化前"与"优化后"数字上的差异纯粹来自测量
时机与后台负载波动，不代表任何代码变化）

| 场景 | 中位数 | P95 | 目标 | 上限 | 是否达标 |
|---|---|---|---|---|---|
| 暖启动（低负载采样，20轮，`results_20260918_133544.csv`） | 63.545 ms | ~68 ms | ≤60ms | 120ms | **未达标（未超上限）** |
| 暖启动（高负载采样，20轮，对照） | 138.594 ms | 164.743 ms | ≤60ms | 120ms | 未达标（超上限，判定为环境噪声，非代码问题） |
| 冷启动（`-Cold` + RAMMap 清 standby，20轮，`results_20260918_133608.csv`） | 69.333 ms | 81.991 ms | ≤250ms | 400ms | **达标** |

低负载采样下暖启动中位数逐轮分布（最后10轮，见 CSV）：45.3/41.2/50.3/
45.3/45.5 ms（`t_window_to_present_ms`），对应 `t_process_to_present_ms`
63.5/58.1/67.9/62.8/63.5 ms —— 稳定在 60~68ms 区间，**距 60ms 目标约
5%~15%，但已远低于 120ms 上限**，与 `M3-METHOD.md` 第1节最初实测的
93.475ms 相比（那次测量口径相同、机器负载更高）有明显改善，改善来源
是负载差异而非代码改动，**如实记录，不冒充为优化成果**。

## 6. 验收复核

- `markair_tests.exe`：**422 个测试全部通过，0 失败**（本次改动只在
  `src/app/bench.cpp::EmitReport` 新增了 4 行只在 `--bench` 模式下生效的
  诊断输出，`g_enabled` 为 false 时零开销，不影响任何现有测试路径）。
- 本任务对产品代码的唯一改动：`src/app/bench.cpp` 新增
  `module_*_loaded_at_first_present` 诊断字段，用于精确回答验收标准①里
  "/DELAYLOAD 的 4 个 DLL 是否真的延后加载"这一问题，属于测量基础设施
  而非行为改动。
- 未修改 `src/app/main.cpp`、`src/text/font.cpp`、`src/shell/window.cpp`
  ——排查后判断这三个文件里没有找到"允许范围内、且真正影响
  `window_to_present`"的可落地改动点，为避免为了动而动引入不必要的风险，
  没有做无意义的改动。

## 7. 结论与裁决请求（触发 `[裁决 #5]`）

暖启动 60ms 目标在当前架构（D2D 软件光栅化 + 单线程 + 不允许预加载/
后台预热）下**大概率不可达**——`window_to_present` 里 10.8MB 的
D3D10Warp/d3d11/dxgi/dxcore 模块加载是 Direct2D API 本身的强制行为，
唯一能绕开它的办法是切换渲染后端（违反 02 的"不做硬件探测/软件渲染是
主路径"架构决策，属于 T76 的范围而非本任务），或者接受"允许的优化手段"
清单之外的手段（预加载/后台线程/常驻服务），这些都被 08-m3-tasks.md
明文禁止。

现状（未超 120ms 上限，低负载下中位数 60~68ms 区间）与"改指标"之间，
留给用户在以下选项中裁决：
1. 接受当前"未达目标但未超上限"的现状，把 60ms 目标标注为"已知架构级
   偏差，不再追踪"（类似 `M2-MEMORY-REGRESS` 先例）；
2. 授权把 T76（软件渲染路径决策 `[裁决 #3]`）的范围扩大，重新评估
   "D2D 强制走 DXGI"这一层是否有替代方案（例如显式创建
   `ID2D1DeviceContext` + 更低开销的 D3D 设备类型），但这已经是架构改动，
   需要专门的方案评估而非本任务的时间盒内工作；
3. 其它用户认为合适的处理方式。

本任务不擅自选择以上选项，也不修改 `01-requirements.md` 的指标定义。
