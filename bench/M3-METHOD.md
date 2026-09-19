# M3-METHOD：五类硬指标的测量口径与可复跑命令（T71）

> 本文档把 `01-requirements.md` §4 的性能指标表、`04-delivery-plan.md` 第 109~120 行的
> "测量方法"表，落成一条一条可复制粘贴执行的命令。每条命令已在本机（Windows 10
> 专业版 10.0.19045，见 `bench/env_*.txt`）实跑一次，输出贴在对应小节下方。
> **口径纪律**：凡是用代理指标顶替权威值、或口径与 01 §4 严格定义有出入的地方，
> 都在小节内用"已知口径偏差"单独标注，不用一句"已测量"糊过去。

## 0. 前置：工具安装状态

见 `bench/TOOLS.md`。本机已装：`tools\VMMap\vmmap64.exe`（3.4）、
`tools\RAMMap\RAMMap64.exe`（1.63）、`tools\PresentMon.exe`（2.5.1）。
未装：Process Monitor（04 表格未直接依赖，暂缓）。

`dumpbin` 未在当前 PowerShell PATH 中（需要 "Developer Command Prompt for VS"
或手动加入 `<VS 安装目录>\VC\Tools\MSVC\<版本>\bin\Hostx64\x64`），T80/T83 用到时
需在该环境下跑，本文档先如实记录这一条环境限制。

---

## 1. 首屏可见时间（暖启动，BENCH-A）—— 对应 01 §4 表格第 1 行

- **目标**：≤ 60 ms（中位数），上限 120 ms
- **工具**：内置埋点 `t_process_to_present_ms`（`src/app/bench.cpp`），`bench/run_bench.ps1` 驱动
- **统计量**：中位数 + P95
- **样本量**：20 轮
- **命令**：
  ```powershell
  powershell -File bench\run_bench.ps1 -Target BENCH-A -N 20
  ```
- **本机实测输出（2026-09-18，见 `bench/results_20260918_124519.csv` / `bench/env_20260918_124519.txt`）**：
  ```
  field                     median      p95
  -----                     ------      ---
  t_process_to_present_ms   93.475  116.296
  private_bytes           15624192 16179200
  ```
- **已知口径偏差**：01 §4 定义的"暖启动"是"同一进程已运行过一次、文件已在缓存"
  （对应"第二次及以后打开"），本命令测的是"每轮全新进程 + 文件系统缓存已热"
  （第 1 轮读盘把文件读入系统缓存，第 2~20 轮命中缓存），**不是**"同一进程复用"。
  这是 M3 裁决记录 #4 采纳的口径（进程内复用在 markair 架构下不存在 —— 每个文档
  是独立进程，见 01 §5.3 第 4 条"不做单实例"），因此"暖启动"在 markair 语境下
  唯一可行的解释就是"文件已缓存的新进程"。当前实测中位数 93.475 ms **超过
  60 ms 目标**（但未过 120 ms 上限），这是 T74 的输入数据，不在本任务范围内
  处理。

---

## 2. 首屏可见时间（冷启动，清 standby）—— 对应 01 §4 表格第 2 行

- **目标**：≤ 250 ms，上限 400 ms
- **工具**：内置埋点 + RAMMap 清 Empty Standby List
- **统计量**：中位数 + P95
- **样本量**：20 轮
- **命令**：
  ```powershell
  powershell -File bench\run_bench.ps1 -Target BENCH-A -N 20 -Cold -RamMapPath tools\RAMMap\RAMMap64.exe
  ```
- **口径说明（T71 新拆分，见 `-Cold` 参数文档块）**：每轮运行前调用
  `RAMMap64.exe -Et` 清空 Empty Standby List，模拟"文件从未被读过、系统缓存
  为空"的真冷启动场景，与上面第 1 行的"暖启动"形成对照。
- **已知口径偏差**：`RAMMap64.exe -Et` 清的是**系统级** Empty Standby List，
  会影响当前登录会话里其它进程的缓存状态（不限于 markair 相关文件），这是
  RAMMap 工具本身的行为边界，非本脚本引入的偏差，测量时应关闭其它占用磁盘
  I/O 的程序以减少串扰。本次 T71 验收只验证命令能跑通、`-Cold` 分支能正确
  调用到 RAMMap 可执行文件，**完整 20 轮冷启动实测数据留给 T74**（阶段 S
  性能优化任务，本任务只负责固定口径与脚本能力）。

---

## 3. 常驻内存 Private Working Set（BENCH-A，静置 10s）—— 对应 01 §4 表格第 3 行

- **目标**：≤ 15 MB，上限 25 MB
- **工具**：VMMap（权威）/ `GetProcessMemoryInfo` 的 `PrivateUsage`（自动化代理，`--bench` 内置输出为 `private_bytes`）
- **统计量**：中位数 + P95
- **样本量**：20 轮（自动化代理）；VMMap 权威值为单次人工快照，非统计量
- **代理指标命令**：
  ```powershell
  powershell -File bench\run_bench.ps1 -Target BENCH-A -N 20
  ```
  （输出见上面第 1 节，`private_bytes` 中位数 ≈ 14.90 MB，P95 ≈ 15.43 MB）
- **VMMap 权威值命令（人工，无法脚本化批量跑）**：
  ```powershell
  build\src\Release\markair.exe bench\BENCH-A.md   # 先正常打开一次，静置 10 秒
  tools\VMMap\vmmap64.exe -p <markair.exe 的 PID>   # 在 Process 页签查看 Private Bytes / Private WS
  ```
  或用 VMMap 的命令行快照模式落盘（避免逐次手动截图）：
  ```powershell
  tools\VMMap\vmmap64.exe -p <PID> "bench\vmmap_snapshot.mtl"
  ```
- **已知口径偏差**：`PrivateUsage`（Private Bytes）**含已提交未驻留的部分**，
  通常比 VMMap 的 Private Working Set（真正驻留物理内存的私有页）更大，
  两者不是同一个数——这是 08-m3-tasks.md 明确要求拆分的差值，具体差值的
  实测与模块级拆解是 T75 的范围（`bench/M3-MEMORY.md`），本任务只固定两条
  命令的可执行性。

---

## 4. 常驻内存（空文档/刚启动）—— 对应 01 §4 表格第 4 行

- **目标**：≤ 8 MB，上限 12 MB
- **工具**：同上（VMMap 权威 / `private_bytes` 代理），语料换成新增的 `bench/EMPTY.md`（单行标题）
- **统计量**：中位数 + P95
- **样本量**：20 轮
- **命令**：
  ```powershell
  powershell -File bench\run_bench.ps1 -Target EMPTY -N 20
  ```
- **本机实测输出（2026-09-18，见 `bench/results_20260918_124424.csv` / `bench/env_20260918_124424.txt`）**：
  ```
  field            median      p95
  -----            ------      ---
  private_bytes  9404416  9654272
  ```
  即中位数 ≈ 8.97 MB，P95 ≈ 9.21 MB —— **已超过 8 MB 目标**（未过 12 MB 上限），
  与 M0 记录的"软件渲染 + 禁 IME 裸窗口基线 9.0~9.4 MB"（memory.md）一致，
  说明这不是本次测量的偶然波动，而是架构级的系统开销（D2D/DWrite 私有页）。
  这条数据是 T75 的输入，本任务不做优化。
- **已知口径偏差**：与第 3 节相同（代理指标 vs VMMap 权威值）。

---

## 5. 可执行文件体积 —— 对应 01 §4 表格第 5 行

- **目标**：≤ 1.5 MB，上限 3 MB
- **工具**：文件属性 + `dumpbin /headers`（后者需 VS Developer 环境，见前置说明）
- **统计量**：单值（非统计分布）
- **样本量**：1（体积是确定性值，不需要多轮）
- **命令**：
  ```powershell
  (Get-Item build\src\Release\markair.exe).Length
  ```
- **本机实测输出（2026-09-18）**：`352256` 字节 ≈ 0.336 MB，距 1.5 MB 硬线约 4.5 倍余量。
- **已知口径偏差**：无（文件大小是确定性度量，无采样误差）。`dumpbin /headers`
  的子系统/时间戳记录留给 T80（`ci/verify_release.ps1`），需要先进入 VS
  Developer 命令行环境。

---

## 6. 滚动帧率（BENCH-A）—— 对应 01 §4 表格第 6 行

- **目标**：60 FPS 无掉帧，平均 < 55 FPS 即不通过
- **工具**：PresentMon（`tools\PresentMon.exe`），**可行性未知**
- **统计量**：P50/P95/P99 帧耗时 + 掉帧数（单帧 > 16.6 ms）
- **样本量**：10 秒滚动窗口
- **命令（可行性探测，T72 范围）**：
  ```powershell
  build\src\Release\markair.exe bench\BENCH-A.md &
  tools\PresentMon.exe --process_name markair.exe --output_file bench\presentmon_probe.csv --timed 10
  ```
- **已知口径偏差 / 阻塞项**：`src/render/renderer.cpp:379` 无条件使用
  `D2D1_RENDER_TARGET_TYPE_SOFTWARE`，**没有 DXGI 交换链**，而 PresentMon 挂的
  是 DXGI/D3D 的 Present 事件——**很可能一帧都抓不到**。这条指标的测量手段
  选型明确标注为 `[裁决 #2]`，本任务（T71）不做选型决策，只确认 PresentMon
  二进制本身可执行（见 `bench/TOOLS.md`）。真正的可行性探测与埋点方案属于
  T72 的范围。**此行阻塞于 `[裁决 #2]`**。

---

## 7. 10 MB 超大文档打开时间 —— 对应 01 §4 表格第 7 行

- **目标**：≤ 1.5 s 且 UI 不卡死，上限 3 s
- **工具**：内置埋点 `t_process_to_present_ms` + 消息循环阻塞探针
- **统计量**：中位数
- **样本量**：20 轮
- **命令（语料未就绪，T73 范围）**：
  ```powershell
  powershell -File bench\run_bench.ps1 -Target BENCH-D -N 20
  ```
- **已知口径偏差 / 阻塞项**：`bench/BENCH-D.md` 尚不存在（10 MB 结构真实语料
  是 T73 的产出物），`run_bench.ps1` 目前也没有 `BENCH-D` 这个 `-Target` 枚举
  值——**此行阻塞于 T73**，本任务不新增该语料（超出 T71 范围），如实标注
  未测。

---

## 8. 子进程数 —— 对应 01 §4 表格第 8 行

- **目标**：恒为 0
- **工具**：`Get-CimInstance Win32_Process -Filter "ParentProcessId=<pid>"` 轮询
- **统计量**：布尔判定（是否出现任何子进程）
- **样本量**：覆盖打开/点外链/点图片/F5/`--register` 等场景各一次
- **命令**：
  ```powershell
  $p = Start-Process build\src\Release\markair.exe -ArgumentList "bench\BENCH-A.md" -PassThru
  Start-Sleep -Seconds 2
  Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)"
  Stop-Process -Id $p.Id -Force
  ```
- **本机实测输出（2026-09-18）**：打开 BENCH-A 静置场景下，`Win32_Process`
  查询返回空集合（无子进程）。
- **已知口径偏差**：本次只验证"打开文档"这一个场景，**"点外链/点图片/F5/
  `--register`"的完整覆盖是 T80 `ci/verify_release.ps1` 的范围**，本任务只
  确认查询命令本身可执行、语法正确。

---

## 环境信息采集（贯穿全部测量）

`bench/run_bench.ps1` 每次运行会自动在 CSV 同目录写一份 `env_<timestamp>.txt`，
采集内容：操作系统版本、CPU、物理内存、显卡（`Get-CimInstance
Win32_VideoController`）、是否 RDP 会话（`GetSystemMetrics(SM_REMOTESESSION)`）、
疑似全局注入/常驻模块进程（关键词匹配，非精确列表，用于提醒"数字受环境干扰"，
精确定位需要 VMMap 对 markair.exe 做模块级快照）。

本机 2026-09-18 实测环境快照（`bench/env_20260918_124424.txt`）：
```
操作系统：Microsoft Windows 10 专业版 10.0.19045 (64-bit)
CPU：13th Gen Intel(R) Core(TM) i5-13500，核心数 14，逻辑处理器 20
物理内存：31.7 GB
显卡：Intel(R) UHD Graphics 770，驱动版本 31.0.101.4953，显存 1024 MB
是否 RDP 会话（SM_REMOTESESSION）：否
疑似全局注入/常驻模块相关进程：dartaotruntime, RuntimeBroker, SogouCloud, SogouImeBroker, SOGOUSmartAssistant
```
可见本机确实存在搜狗输入法相关的常驻进程（`SogouCloud`/`SogouImeBroker`/
`SOGOUSmartAssistant`），与 memory.md 记录的先例一致——**本机测出的内存数字
不能直接当作"干净环境"的权威基线**，这条事实必须随每份报告一起附上，是
`M3-PERF.md`（T84）撰写时的强制引用项。

---

## 小结：本任务（T71）范围内已完成 / 明确阻塞的项

| 01 §4 行 | 状态 |
|---|---|
| 1. 暖启动首屏 | 已测（口径已固定，数值超线交给 T74） |
| 2. 冷启动首屏 | 命令已固定并跑通，20 轮完整数据交给 T74 |
| 3. 常驻内存（BENCH-A） | 代理指标已测；VMMap 权威值命令已固定，差值拆解交给 T75 |
| 4. 空文档内存 | 已测（EMPTY.md 新增语料，数值超线交给 T75） |
| 5. exe 体积 | 已测 |
| 6. 滚动帧率 | **阻塞于 `[裁决 #2]`**（T72 范围） |
| 7. 10 MB 文档打开时间 | **阻塞于 T73**（语料未就绪） |
| 8. 子进程数 | 已验证命令可行，完整场景覆盖交给 T80 |
