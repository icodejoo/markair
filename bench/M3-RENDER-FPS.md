# T72：滚动帧率测量能力（PresentMon 可行性探测与实测结果）

> 对应任务：`08-m3-tasks.md` 第 84 行 T72。选型依据：M3 裁决记录 #2「先探测，再选型」。
> 本文档只记录测量能力本身与实测数据，**不做任何渲染路径的优化**（那是 T76 的范围）。

## 1. 技术前提（开工前已核实，未重新调研）

`src/render/renderer.cpp:379` 无条件把 `D2D1_RENDER_TARGET_TYPE_SOFTWARE`
设为渲染目标类型，走的是 `CreateHwndRenderTarget`，**没有 markair 自己的 DXGI
交换链**。PresentMon 本质是订阅 ETW 里的 DXGI/D3D Present 事件，理论上"抓
不到软件渲染窗口的帧"是大概率事件——这正是 T72 要求先探测而不是先选型/先
写代码的原因。

## 2. 可行性探测过程

### 2.1 前置

- `tools/PresentMon.exe`（2.5.1，T71 已下载）、`bench/scroll_probe.ps1`（本任务新增）均已就位。
- 本机以管理员权限运行（`net session` 探测确认），PresentMon 的 ETW 采集需要此权限。

### 2.2 探测命令

```powershell
# 1) 启动 markair 打开基准语料
E:\workspaces\markair\build\src\Release\markair.exe bench\BENCH-A.md

# 2) 并行：PresentMon 采集 10 秒 + 脚本化滚动 10 秒
tools\PresentMon.exe --process_name markair.exe --timed 10 --terminate_after_timed `
    --output_file bench\render_fps\frametimes_BENCH-A_20260918.csv
powershell -File bench\scroll_probe.ps1 -DurationSeconds 10 -IntervalMs 16
```

### 2.3 探测结果：**能抓到帧，出乎预期**

PresentMon 对 markair.exe 产出了 268 行有效帧记录（BENCH-A）/ 253 行（BENCH-C），
`PresentMode` 列的值是 **`Composed: Copy with GPU GDI`**。

**原因分析**：markair 自己确实没有 DXGI 交换链，但 Windows 的桌面窗口管理器
（DWM）在合成桌面时，会把每个顶层窗口的位图作为一次"Present"提交进
DWM 自己的合成交换链——这条 DWM 合成层的 Present 事件同样能被 PresentMon
的 ETW 订阅捕获到，且以 markair.exe 的进程名/PID 归类。也就是说 PresentMon 在
这里测到的不是"markair 自己发起的 D3D Present"，而是**"markair 这个窗口内容被
合成上屏的频率"**——这恰好就是 01 §4 想要的"用户实际看到的刷新节奏"，
比自建埋点（只能测到 D2D `EndDraw` 返回，不知道 DWM 合成层还要再等多久
才真正上屏）口径更贴近真实体感。

**结论：直接采用 PresentMon（方案 A），零代码改动**，不需要触发方案 B
（`bench.h`/`window.cpp` 埋点）。因此本任务未修改任何 `src/` 下的文件，
体积增量与首屏增量均为 0，方案 B 的"≤4KB / ≤0.5ms"约束不适用。

## 3. 脚本化滚动实现

新增 `bench/scroll_probe.ps1`：

- 用 `EnumWindows + GetClassNameW` 按窗口类名 `markair_main_window` 定位目标窗口
  （详见脚本内注释：**本机实测 `FindWindowW` 对本项目窗口稳定返回 NULL**，
  `GetLastError` 也不是"未找到"该有的错误码，但同一进程里 `EnumWindows`
  能正常枚举到该窗口并核对类名一致，怀疑是本机某个 hook/安全软件拦截了
  `FindWindow` 这一个更常被自动化脚本滥用的 API；`EnumWindows` 定位后仍
  用 `PostMessage` 投递，语义等价，同样不使用 `SendInput`）。
- 用 `PostMessageW(hwnd, WM_MOUSEWHEEL, wParam, lParam)` 投递，`wParam`
  高 16 位是滚轮增量（固定 `-120`，标准一格向下），低 16 位固定 0（不
  模拟修饰键）。
- 固定间隔 16ms、固定总时长（默认 10s，参数可调）。
- **未使用 `SendInput`**，不抢占真实鼠标焦点。

实测：10 秒内稳定投递 320+ 次消息，误差在 ±1% 以内（10014ms / 10031ms）。

## 4. BENCH-A 与 BENCH-C 各一轮 10 秒滚动实测

统计口径：`MsBetweenPresents` 列（两次 Present 的间隔，即 DWM 合成上屏的
帧间隔）。掉帧判据：单帧 > 16.6ms 计一帧掉帧（01 §4 定义）；同时给出
平均 FPS（01 §4 的另一判据：平均 < 55 FPS 即不通过）。

| 语料 | 样本数 n | P50 (ms) | P95 (ms) | P99 (ms) | 平均帧时间 (ms) | 平均 FPS | 掉帧数 | 掉帧率 |
|---|---|---|---|---|---|---|---|---|
| BENCH-A | 268 | 30.86 | 34.29 | 43.07 | 30.89 | **32.37** | 266 | 99.3% |
| BENCH-C（高亮最坏情况） | 253 | 30.85 | 35.80 | 45.34 | 31.01 | **32.25** | 253 | 100% |

原始逐帧 CSV：`bench/render_fps/frametimes_BENCH-A_20260918.csv`、
`bench/render_fps/frametimes_BENCH-C_20260918.csv`（已加入 `.gitignore`，
不进版本控制——体量大且不可跨机复现，复跑方法见本文档第 2.2 节，
按需重新生成）。

## 5. 与 01 §4 判据的对照结论

01 §4 判据："60 FPS 无掉帧,平均 < 55 FPS 即不通过"。

**两条语料均不通过**：

- 掉帧率接近 100%（几乎每一帧都超过 16.6ms 的单帧预算）；
- 平均 FPS 约 32.3，远低于 55 FPS 的不通过线，更远低于 60 FPS 的目标线。

⚠️ **注意口径边界**：本次测的是"持续投递滚轮消息期间 markair 窗口被 DWM
合成上屏的频率"，混杂了两部分开销——① markair 收到 `WM_MOUSEWHEEL` 后自己
的滚动重绘耗时（`InvalidateRect` → `WM_PAINT` → D2D `EndDraw`）；②
DWM 把该位图合成上屏的固有延迟。**这正是 T76（软件渲染路径验证）要接手
分析的下一步**：需要拆解这 30ms 左右的帧时间里，markair 自己的重绘占多少、
DWM 合成占多少，才能判断"达不到 60 FPS"是重绘慢还是合成慢——本任务
（T72）只负责把测量能力立起来，不做归因和优化，归因与优化留给 T76。

## 6. 验收对照（T72 验收标准逐项）

| 验收项 | 结果 |
|---|---|
| 先做 PresentMon 可行性探测,不先写代码 | 已做，见第 2 节 |
| 脚本化滚动,`PostMessage` 而非 `SendInput` | `bench/scroll_probe.ps1` 已实现并实测通过 |
| 产出 10 秒滚动的逐帧耗时 CSV | 已产出（BENCH-A/BENCH-C 各一份） |
| P50/P95/P99/掉帧数四个数都算得出来 | 见第 4 节表格 |
| 若选方案 B：体积 ≤4KB、首屏 ≤0.5ms | **不适用**——探测结果选定方案 A，零代码改动，无体积/首屏增量 |

## 7. 遗留与后续

- 本任务未改动任何 `src/` 下文件。
- 掉帧率数据已经足以支撑 T76 的"软件渲染是否达到 60 FPS"这一实测结论；
  T76 需要进一步拆解 markair 自身重绘耗时与 DWM 合成耗时的占比。
- CI 上跑帧率门禁大概率假红（`windows-latest` 是无 GPU 虚拟机，DWM 合成
  行为在虚拟机上与本机不同），此结论留给 T83 处理"本机门禁 vs CI 门禁"
  两套阈值。
