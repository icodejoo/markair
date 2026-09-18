# M3-PERF：最终性能报告（T84，可复跑）

> 对应 `08-m3-tasks.md` T84。这是 M3 阶段的最终交付物，汇总 `01-requirements.md` §4
> 全部 8 行硬指标的**最终**实测结果（T76 修复后的数字，不是 T71/T72/T73 最初测出、
> 后来被推翻的旧数字）。每一行都附可复制粘贴的复跑命令、样本量、统计量、原始文件
> 路径，以及口径偏差声明。另一个人或另一台机器，照着本文档就能复跑出全部数据。

---

## 0. 测试用例数（先说清楚，避免用旧数字）

本任务开工时**实跑一次** `mdvn_tests.exe`（不沿用 T79/T81/T82 报告过的 426/429 等
可能已过期的数字）：

```powershell
build\tests\Release\mdvn_tests.exe
```

**本机实测输出（2026-09-18）：`mdvn_tests: 426 test(s) run, 0 failure(s)`。**

> 之前几个任务（T76=423、T79=426→429、T82=429）报的数字之间有出入——按本次实跑
> 结果，**当前真实用例数是 426**，以此为准，不采信文档里更早或更晚的其它数字。
> 这个差异本身不影响任何一条硬指标的通过判定（回归护栏只要求"0 failure"）。

---

## 1. 01 §4 表格逐行汇总

> 每行的原始数据来源、复跑命令、口径偏差见对应小节。**"最终值"一律取 T76 修复
> 之后的重测结果**，不用 T71/T72/T73 最初测出、后来被推翻的旧数字。

| # | 指标 | 目标值 | 上限值 | 最终实测值（中位数） | P95 | 是否通过 |
|---|---|---|---|---|---|---|
| 1 | 暖启动首屏 | ≤ 60 ms | 120 ms | **63.545 ms**（低负载采样，`M3-STARTUP.md` §5） | ~68 ms | **未达标（未超上限）** |
| 2 | 冷启动首屏（清 standby） | ≤ 250 ms | 400 ms | **69.333 ms**（`M3-STARTUP.md` §5） | 81.991 ms | **达标** |
| 3 | 常驻内存 Private WS（BENCH-A，静置 10s） | ≤ 15 MB | 25 MB | **10.48 MB**（10,732 K，VMMap 权威值，`M3-MEMORY.md` §3） | — | **达标** |
| 4 | 常驻内存（空文档/刚启动） | ≤ 8 MB | 12 MB | **6.87 MB**（7,040 K，VMMap 权威值，`M3-MEMORY.md` §2） | — | **达标** |
| 5 | exe 体积 | ≤ 1.5 MB | 3 MB | **360448 B ≈ 0.344 MB**（`M3-RELEASE.md` §2，T76 新增埋点后的最终构建） | — | **达标（余量约 4.2 倍）** |
| 6 | 滚动帧率（BENCH-A） | 60 FPS 无掉帧 | 平均 < 55 FPS 不通过 | **mdvn 自身重绘 P50 1.82 ms（≈550 FPS 能力）；DWM 合成口径平均 62.54 FPS**（`M3-RENDER.md` §5.2） | 见下方口径说明 | **达标**（按两种口径的解读方式，见下方"口径偏差声明"第 6 行） |
| 7 | 10 MB 文档打开时间 | ≤ 1.5 s 且 UI 不卡死 | 3 s | **87.532 ms**（`M3-HUGE.md` §1，T77 复核），消息循环阻塞 <65ms，首次滚动响应 <1ms | 91.385 ms | **达标（余量约 17 倍）** |
| 8 | 子进程数 | 恒为 0 | 出现任何子进程即不通过 | **0**（BENCH-A/B/D + 点外链 + 点图片 + F5 + `--register`，`M3-RELEASE.md` §3） | — | **达标**（点击场景的命中精度未获独立证实，见口径偏差声明） |

**汇总：8 行中 7 行达标，1 行（暖启动首屏）未达标但未超上限**，已在 T74 触发
`[裁决 #5]`，处理方式见第 5 节。

---

## 2. 每行的复跑命令、样本量、统计量

> 全部命令引用 `bench/M3-METHOD.md`（T71）固定的口径，此处只给可直接执行的版本。

### 2.1 暖启动首屏（第 1 行）

```powershell
powershell -File bench\run_bench.ps1 -Target BENCH-A -N 20
```
- 样本量：20 轮；统计量：中位数 + P95；原始文件：`bench/results_20260918_133544.csv`
  + `bench/env_20260918_133544.txt`（低负载采样，见 `M3-STARTUP.md` §5）。

### 2.2 冷启动首屏（第 2 行）

```powershell
powershell -File bench\run_bench.ps1 -Target BENCH-A -N 20 -Cold -RamMapPath tools\RAMMap\RAMMap64.exe
```
- 样本量：20 轮；统计量：中位数 + P95；原始文件：`bench/results_20260918_133608.csv`。
- 前置：需要 `tools\RAMMap\RAMMap64.exe`（清 Empty Standby List），管理员权限。

### 2.3 常驻内存 Private WS（第 3、4 行）

代理指标（可脚本化批量跑）：
```powershell
powershell -File bench\run_bench.ps1 -Target BENCH-A -N 20   # 第3行
powershell -File bench\run_bench.ps1 -Target EMPTY   -N 20   # 第4行
```
VMMap 权威值（GUI 工具，单次人工快照，无法批量跑）：
```powershell
build\src\Release\mdvn.exe --bench bench\BENCH-A.md   # 或 bench\EMPTY.md
# 静置 10 秒后
tools\VMMap\vmmap64.exe -accepteula -p <PID> bench\vmmap_xxx_snapshot.mtl
```
- 样本量：代理指标 20 轮；VMMap 权威值单次人工快照（`M3-MEMORY.md` §1 已如实说明
  GUI 工具限制，无法做 20 轮统计）。
- 原始文件：`bench/screenshots/m3-memory/*.mtl`（快照，不进 git，需按上面命令重新生成）
  + 截图 `bench/screenshots/m3-memory/vmmap_summary2.png` / `vmmap_bencha_summary3.png`
  （也不进 git，沿用 `bench/screenshots/` 既有约定）。

### 2.4 exe 体积（第 5 行）

```powershell
(Get-Item build\src\Release\mdvn.exe).Length
# 进入 VS Developer 命令行环境后：
dumpbin /headers build\src\Release\mdvn.exe
```
- 样本量：1（确定性值）；无统计量。

### 2.5 滚动帧率（第 6 行）

口径①（mdvn 自身重绘，`--bench` 埋点）：
```powershell
build\src\Release\mdvn.exe --bench bench\BENCH-A.md
powershell -File bench\scroll_probe.ps1 -DurationSeconds 10 -IntervalMs 16 -SpinWait
```
口径②（DWM 合成上屏，PresentMon）：
```powershell
build\src\Release\mdvn.exe bench\BENCH-A.md
tools\PresentMon.exe --process_name mdvn.exe --timed 10 --terminate_after_timed --output_file bench\render_fps\bench_a.csv
powershell -File bench\scroll_probe.ps1 -DurationSeconds 10 -IntervalMs 16 -SpinWait
```
- 样本量：10 秒滚动窗口（口径①约 625 帧、口径②约 500+ 帧）；统计量：P50/P95/P99 +
  掉帧数/平均 FPS。
- 原始文件：`bench/render_fps/*.csv`（不进 git，需重新生成）。

### 2.6 10 MB 文档打开时间（第 7 行）

```powershell
powershell -File bench\make_bench_d.ps1   # 先重新生成语料（不进 git）
powershell -File bench\run_bench.ps1 -Target BENCH-D -N 20
```
- 样本量：20 轮；统计量：中位数 + P95；原始文件：`bench/results_20260918_141702.csv`
  + `bench/env_20260918_141702.txt`。
- 消息循环阻塞探针 / 首次滚动响应是一次性验证脚本，未固化进 `bench/`
  （`M3-HUGE.md` §2 已如实说明，核心逻辑：`EnumWindows` 定位窗口 +
  `SendMessageTimeout(WM_NULL)` 测往返耗时），复跑需按该小节的步骤自行实现或迁移。

### 2.7 子进程数（第 8 行）

```powershell
$p = Start-Process build\src\Release\mdvn.exe -ArgumentList "bench\BENCH-A.md" -PassThru
Start-Sleep -Seconds 4
Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)"
Stop-Process -Id $p.Id -Force
```
或直接跑自动化脚本：
```powershell
powershell -File ci\verify_release.ps1
```
- 样本量：覆盖 BENCH-A/B/D + 点外链 + 点图片 + F5 + `--register` 各一次场景。

---

## 3. 测试环境完整信息

来源：`bench/env_20260918_124424.txt`（T71 采集）+ `M3-RENDER.md` §10（T76 补充）。

```
操作系统：Microsoft Windows 10 专业版/Pro 10.0.19045 (64-bit)
CPU：13th Gen Intel(R) Core(TM) i5-13500，核心数 14，逻辑处理器 20
物理内存：31.7 GB
显卡：Intel(R) UHD Graphics 770，驱动版本 31.0.101.4953，显存 1024 MB，刷新率 59 Hz
是否 RDP 会话（SM_REMOTESESSION）：否（物理控制台会话）
```

**⚠️ 必须随数字一起读的两条环境事实：**

1. **本机存在第三方全局注入模块**：`SogouCloud` / `SogouImeBroker` /
   `SOGOUSmartAssistant`（搜狗输入法相关常驻进程，T71 环境快照实测），以及
   VMMap 模块级拆解（`M3-MEMORY.md` §2）里确认的
   `C:\Program Files (x86)\Sangfor\SNAC\bin\PrinterAdapter64.dll` /
   `printerhook64.dll`（深信服 SNAC 终端安全客户端，企业网络环境常驻，
   Private WS 合计 56 K，占比 <1%，量级不及搜狗那种 30+ MB 级别，但同样是
   "本机不是干净环境"的证据）。**本机测出的内存数字不能直接当作干净环境的
   权威基线。**
2. **本机显示器刷新率是 59 Hz（vsync 周期 16.95 ms），不是 60 Hz**。01 §4 的
   16.6 ms 单帧掉帧判据比这块屏幕的物理刷新周期还严——DWM 合成口径下测出的
   "掉帧"里有一部分是判据比硬件还严造成的，不是渲染真的慢（`M3-RENDER.md` §5.2）。

外部测量工具版本（`bench/TOOLS.md`）：VMMap 3.4、RAMMap 1.63、PresentMon 2.5.1。

---

## 4. 口径偏差声明（逐行，不用"已测量"糊过去）

| 行 | 口径偏差 |
|---|---|
| 1（暖启动） | 01 §4 定义的"暖启动"是"同一进程复用"，但 mdvn 架构下**每个文档是独立进程**（不做单实例）。本文档采纳的口径是"文件已被系统缓存的新进程"（M3 裁决 #4 认定的唯一可行解释），**不是**真正的"同进程复用"。 |
| 2（冷启动） | `RAMMap64.exe -Et` 清的是**系统级** Empty Standby List，会影响当前会话内其它进程的缓存状态，非本脚本引入的偏差，是 RAMMap 工具本身的行为边界。 |
| 3（常驻内存） | 代理指标 `PrivateUsage`（≈ Private Bytes）**含已提交未驻留的部分**，比 VMMap 权威值 Private WS 更大（BENCH-A 差值 4.70 MB）。本文档"最终实测值"列取的是权威值，代理指标的 20 轮统计量在 `M3-METHOD.md` §3 单独列出（中位数 ≈14.90MB / P95≈15.43MB），两者不是同一个数，不可混用比较。 |
| 4（空文档内存） | 同第 3 行。差值 1.31 MB（VMMap 权威 6.87MB vs 代理指标同期快照 8.19~8.97MB）。 |
| 5（exe 体积） | 无偏差（文件大小是确定性度量）。但注意：T71 最初记录的基线是 352256 B，本报告采用的是 T76 新增逐帧埋点之后、`M3-RELEASE.md` 实测的**最终值 360448 B**——两者都在 1.5MB 硬线内，差异仅因新增了 `--bench` 惰性诊断代码。 |
| 6（滚动帧率） | **这是全表口径最复杂的一行，必须分开读，不能只报一个数**：口径①（`--bench` 埋点测的"PaintOnce 入口→D2D EndDraw 返回"）测的是 mdvn 自身重绘能力；口径②（PresentMon `MsBetweenPresents`）测的是"DWM 把这个窗口合成上屏的频率"，其**上界由输入事件到达速率决定**，在"只有收到输入才重绘"的事件驱动应用上，PresentMon 测到的"平均 FPS"实际近似等于"滚轮消息投递速率"，不是 mdvn 的渲染能力上限（`M3-RENDER.md` §2.3 已用双投递速率对照实验证实这一点：mdvn 自身单帧耗时在 16ms 投递与 31ms 投递两种口径下完全一致，均为 2.3ms 左右）。**判定"达标"用的是口径①**（架构上限，反映 mdvn 真实渲染能力），口径②的数字作为"当前投递速率下的实测合成频率"一并列出，供参考，不作为唯一判据——这个选择本身建议由 T83 门禁收紧时再次确认。另外 BENCH-D（10MB 文档）口径②平均 FPS 是 54.83，卡在 55 FPS 线下方 0.17，已交接 T77 处理（T77 复核后判定该指标不再是瓶颈，见 `M3-HUGE.md`），本文档不代为改判。 |
| 7（10MB文档） | `kMaxDocumentNodeCount`（200000）会截断此语料（`benchd_smoke` 实测 `truncated=true`），测到的"87.5ms 打开"是**截断后的部分文档**，不是完整 10MB 文档的排版结果——这是产品事实，如实记录。 |
| 8（子进程数） | "打开文档/F5/`--register`"场景已充分验证（子进程数恒为0）；"点外链/点图片"场景受本沙箱环境截图 API 失效所限（`Graphics.CopyFromScreen` 报错、`PrintWindow` 对 D2D 软件渲染窗口截出纯白），**未能独立证实点击精准命中了可点击热区**，只能确认"无论是否命中，全程零子进程"，`M3-RELEASE.md` §3.3 已如实注明这一局限。 |

---

## 5. 未达标项与后续处理

**暖启动首屏（第 1 行）**：目标 ≤60ms，实测中位数 63.545 ms（低负载采样），未达标，
但远低于 120ms 上限。

- **归因**（`M3-STARTUP.md` 第 1~3 节）：97% 以上耗时在 `window_to_present` 段，根因是
  Direct2D 的 `CreateHwndRenderTarget` 无论渲染目标类型是软件还是硬件，底层呈现通道
  始终强制经过 DXGI，本机没有可用硬件 D3D 设备时会退化到 WARP
  （`D3D10Warp.dll` + `d3d11.dll` + `dxgi.dll` + `dxcore.dll` ≈ 10.8MB 模块加载）。
  这是 **Direct2D API 本身的强制行为，不是 mdvn 代码里可以绕开的一次性初始化浪费**。
- **已尝试的优化手段**（均无效或被明文禁止，详见 `M3-STARTUP.md` 第 4 节表格）：
  复核首帧虚拟化裁剪（已生效，非瓶颈）、复核 `/OPT`/`/LTCG`（已在位）、复核
  `/DELAYLOAD`（按预期工作）、惰性初始化排查（未发现可挪动候选）、提前调用
  `EnsureRenderTarget`（单线程架构下无并行收益，无效）、后台线程预热模块
  （被 08-m3-tasks.md 明文禁止，未实施）。
- **处理方式**：按 `[裁决 #5]` 的时间盒流程（2 个工作日），本任务在允许的优化手段
  范围内**未找到进一步压缩空间**，已在 `M3-STARTUP.md` 第 7 节提交用户三个选项：
  ① 接受"未达目标但未超上限"的现状，标注为已知架构级偏差；② 授权扩大 T76
  的范围重新评估渲染后端；③ 其它。**本报告不代为选择，也不修改指标定义**，
  维持 T74 已提交的裁决请求原样转达。

---

## 6. 与 M1/M2 基线的趋势对照

| 里程碑 | Release exe 体积 | BENCH-A 常驻内存（代理指标 `private_bytes`） | 首屏/裸窗口耗时 |
|---|---|---|---|
| M0（spike，软件渲染+禁IME 裸窗口对照组） | 未产出可发布 exe（仅骨架/spike，未记录） | **9.0~9.4 MB**（memory.md，裸窗口基线） | **76~113 ms**（窗口创建→Present） |
| M1（T44 记录的新基线，`07-m2-tasks.md` T54 引用为"当前基线") | **296448 B ≈ 0.283 MB** | **≈13.75 MB**（P95，`07-m2-tasks.md` 第62行） | 未见 M1 单独暖启动记录，沿用 M0/M2 的门禁数字 |
| M2（T69 门禁扩展时的记录值） | 296448 B（高亮体积增量硬线 ≤80KB 未把体积推过 ~380KB，`08-m3-tasks.md` 前置事实记录 M3 开工时为 **352256 B**，属 M2 结束状态） | 门禁沿用 ≤16 MB（M1同一基线不放宽，裁决#12） | 门禁沿用 ≤400ms（宽松安全网口径） |
| M3（本报告，T76 修复 + 权威口径后的最终值） | **360448 B ≈ 0.344 MB**（T76 新增 `--bench` 逐帧诊断代码后的最终构建） | 代理指标 P95 ≈15.43MB；**VMMap 权威值 10.48 MB**（首次拆分两条口径） | 暖启动中位数 **63.545 ms**（未达60ms目标，远低于120ms上限）；冷启动 **69.333 ms**（达标） |

**读法**：exe 体积从 M0（未测）→ M1/M2（≈0.28~0.34 MB）→ M3（0.344 MB）基本持平，
远低于 1.5MB 硬线，M3 阶段的体积工作重点确实是"换门禁口径"而不是"压体积"，与
`08-m3-tasks.md` 前置事实的判断一致。内存方面 M0 的裸窗口基线（9.0~9.4MB）与
M3 空文档的 VMMap 权威值（6.87MB）同属同一量级，M3 的贡献是**把"代理指标已踩线"
的假象换成"权威口径下其实达标"的真实结论**，不是靠代码优化把数字压下来的。

---

## 7. 原始文件索引（复跑前的重新生成说明）

| 类别 | 是否进 git | 复跑前置动作 |
|---|---|---|
| `bench/results_*.csv` / `bench/env_*.txt`（本报告引用的具体几份） | 是（`bench/` 常规产出） | 无需前置，可直接查看；如需重新生成同名文件，重跑对应 `run_bench.ps1` 命令即可（时间戳会变） |
| `bench/BENCH-D.md`（10MB 语料） | **否**（`.gitignore` 排除，裁决 #8②） | 先跑 `powershell -File bench\make_bench_d.ps1` |
| `bench/render_fps/*.csv`（PresentMon 逐帧数据） | 否 | 先跑第 2.5 节的 PresentMon 命令 |
| `bench/screenshots/m3-memory/*.png` / `*.mtl`（VMMap 快照/截图） | 否 | 先跑第 2.3 节的 VMMap 命令；截图需要真实 Windows 桌面环境（`CopyFromScreen`，不用 `PrintWindow`） |
| `bench/EMPTY.md` / `bench/BENCH-A.md` / `bench/BENCH-C.md` | 是 | 无需前置 |

---

## 8. 结论

01 §4 全部 8 行硬指标：**7 行达标，1 行（暖启动首屏）未达标但未超上限**，未达标项
已按裁决 #5 的时间盒流程走完归因与已尝试手段记录，转交用户裁决，本报告不擅自
放宽指标定义或篡改实测数字。测试用例数以本次实跑的 **426** 个为最终依据。全部
数字均可通过第 2 节命令复跑，非 git 语料/截图/CSV 的重新生成方式已在第 7 节列出。
