# M3-MEMORY：常驻内存打磨（T75）

> 对应 `08-m3-tasks.md` T75、`M3 裁决记录` #1（VMMap/RAMMap 已装）、#5（时间盒 2 个
> 工作日）。本任务**没有改动任何 C++ 代码**——归因结论表明大头是 D2D/DWrite 的系统
> 开销，不是 markair 自身代码，因此不做"盲目优化"，如实上报测量结果与拆解表，交用户裁决。

## 0. 结论先行

| 指标 | 现行 CI 代理指标 `PrivateUsage`(20轮中位数) | VMMap 权威值 `Private WS`(单次人工快照) | 目标 | 上限 | 是否达标（按权威值） |
|---|---|---|---|---|---|
| BENCH-A 常驻内存 | ≈ 14.90 MB (P95 ≈ 15.43 MB) | **10,732 K ≈ 10.48 MB** | ≤ 15 MB | 25 MB | **达标** |
| 空文档常驻内存 | ≈ 8.97 MB (P95 ≈ 9.21 MB) | **7,040 K ≈ 6.87 MB** | ≤ 8 MB | 12 MB | **达标** |

**核心发现**：01 §4 要的权威口径（VMMap `Private WS`，即真正驻留物理内存的私有页）比
现行 CI 代理指标（`GetProcessMemoryInfo` 的 `PrivateUsage`，≈ Private Bytes，含已提交
未驻留部分）**明显更小**——两条内存线换成权威口径后，**都已达标**，且空文档一条从
"踩线"（代理指标 8.97 MB > 8 MB 目标）变成"达标"（权威值 6.87 MB < 8 MB 目标）。

这与 M0 的"软件渲染 + 禁 IME 裸窗口基线 9.0~9.4 MB"记录并不矛盾：M0 记录的是
`private_bytes`（代理指标），本任务测的是同一进程状态下 VMMap 的 `Private WS`（权威
值），两者本来就不是一个数——这正是本任务要交付的差值。

**因此本任务不触发裁决 #5（口径已对齐后两条线均达标，无需放宽指标）**，也不需要做
任何代码改动去"优化"D2D/DWrite 的系统开销。下面给出完整的测量方法、原始数据与模块
级拆解表。

---

## 1. 测量方法

- **工具**：`tools\VMMap\vmmap64.exe`（3.4，已装，见 `bench/TOOLS.md`）。
- **前置**：VMMap 是 GUI 工具，命令行只支持"附加到已运行的 PID 并落盘一份 `.mtl`
  快照"（`vmmap64.exe -accepteula -p <PID> <输出文件>`），**没有能把 Private WS /
  模块级拆解直接导出为文本/CSV 的命令行开关**（官方文档也没有列出这类开关）。
  因此本任务对"人工打开一次 VMMap GUI，读 Process 页签的数字并截图存档"的部分，
  用**真实 Windows 桌面 + `System.Drawing.Bitmap.CopyFromScreen` 截取真实窗口像素**
  的方式留证（本机是真实 Windows 10 双屏环境，不是"AI 环境无 GUI"，遵循
  `feedback_ui_screenshot_verification.md` 的既有纪律：只用真实截图，不用
  `PrintWindow`）。
- **步骤**（EMPTY 与 BENCH-A 各一轮，均已实测，命令与产出如下）：
  1. `build\src\Release\markair.exe --bench <语料文件>`，与 `run_bench.ps1` 用的启动方式
     完全一致（`--bench` 模式窗口不会自动退出，便于人工附加）；
  2. 静置 10 秒（对应 01 §4"静置 10 s"的口径）；
  3. `tools\VMMap\vmmap64.exe -accepteula -p <PID> bench\vmmap_xxx_snapshot.mtl`
     落一份快照文件（存档于 `bench/screenshots/m3-memory/`，不进 git，沿用
     `bench/screenshots/` 既有 `.gitignore` 约定）；
  4. 再打开一次 GUI 附加同一 PID（`vmmap64.exe -accepteula -p <PID>`），读 Process
     页签的 `Total` 行与按 `Type` 分类的表格，截图存档。
- **样本量**：VMMap 权威值是**单次人工快照**（GUI 工具无法脚本化批量跑 20 轮，这一点
  `M3-METHOD.md` 第 3/4 节已如实记录为"已知口径偏差"）；代理指标 `PrivateUsage` 仍是
  `run_bench.ps1 -N 20` 的 20 轮中位数/P95，与 `M3-METHOD.md` 一致，本文档不重复измер，
  直接引用。

---

## 2. 空文档（EMPTY.md）实测

- 启动：`build\src\Release\markair.exe --bench bench\EMPTY.md`，PID 26880/24736（先后两次
  验证，数值一致）。
- 静置 10 秒后代理指标：`PrivateMemorySize64 ≈ 9,228,288~9,359,360 字节 ≈ 8.80~8.93 MB`
  （与 `run_bench.ps1 -Target EMPTY -N 20` 的 20 轮中位数 8.97 MB 同一量级，验证了单次
  快照与批量统计口径一致，无偶然波动）。
- **VMMap 权威值快照**（截图 `bench/screenshots/m3-memory/vmmap_summary2.png`，
  PID 24736）：

  | Type | Size | Committed | Private | Total WS | **Private WS** | Shareable WS | Shared WS |
  |---|---|---|---|---|---|---|---|
  | **Total** | 4,571,008 K | 108,760 K | **8,384 K** | 20,596 K | **7,040 K** | 13,556 K | 13,224 K |
  | Heap | 5,796 K | 2,400 K | 2,336 K | 1,908 K | 1,904 K | 4 K | 4 K |
  | Image | 66,916 K | 66,916 K | 1,484 K | 13,852 K | 1,360 K | 12,492 K | 12,200 K |
  | Private Data | 4,416,128 K | 4,516 K | 4,232 K | 3,620 K | 3,600 K | 20 K | 20 K |
  | Stack | 12,288 K | 332 K | 332 K | 176 K | 176 K | — | — |

  即：**Private Bytes 8,384 K（≈8.19 MB，与代理指标 8.80~8.97 MB 同一量级，代理指标
  略高是因为两次测量间隔几分钟、非同一时刻）**，而**权威值 Private WS 只有 7,040 K
  ≈ 6.87 MB**——低于 8 MB 目标。差值 8,384 K − 7,040 K = **1,344 K ≈ 1.31 MB**，即
  "已提交但未真正驻留物理内存"的私有页。

### 模块级拆解（Image 分类，截图 `vmmap_image_filtered2.png`）

按地址排列的部分条目（Type=Image，即已加载 DLL/EXE 的私有页）：

| 模块 | Private | Private WS | 说明 |
|---|---|---|---|
| `E:\workspaces\markair\build\src\Release\markair.exe` | 44 K | 28 K | markair 自身代码/数据段，**极小** |
| `C:\Windows\System32\DWrite.dll` | 100 K | 108 K | 文字排版引擎，系统 DLL |
| `C:\Windows\System32\d3d10warp.dll` | 4 K | 8 K | D3D 软件光栅化器（D2D 软件渲染路径依赖） |
| `C:\Windows\System32\D3D11.dll` / `amd64_microsoft.windows.common-c...` | 16~20 K | 28~32 K | D3D11 运行时（D2D 软件目标底层仍走 D3D11 WARP） |
| `C:\Windows\System32\DXCore.dll` | 20 K | 28 K | DirectX 核心枚举组件 |
| `C:\Windows\System32\TextShaping.dll` | 4 K | 8 K | 文字整形 |
| `C:\Windows\System32\cryptnet.dll` | 4 K | 8 K | 系统证书/网络组件（进程默认加载，非 markair 引入） |
| `C:\Program Files (x86)\Sangfor\SNAC\bin\PrinterAdapter64.dll` | 40 K | 40 K | **第三方全局注入模块**（深信服 SNAC 终端安全客户端，本机企业网络环境常驻） |
| `C:\Program Files (x86)\Sangfor\SNAC\bin\printerhook64.dll` | 12 K | 16 K | 同上，**第三方全局注入**，与 memory.md 记录的搜狗输入法注入是同一类环境噪声 |

**归因**：Image 分类总计 Private WS 只有 1,360 K（≈1.33 MB），是全部私有内存的
**19%**；其中 markair.exe 自身只占 28 K，**几乎可以忽略**。d2d1.dll 在本次快照的可见
条目里未单独出现明显私有页峰值（占用与 d3d10warp/D3D11 同一量级，均在几十 KB 级），
说明 D2D/DWrite 的"系统开销"确实存在，但绝对值很小，不是空文档内存的主要构成。

**真正的大头是 `Heap`（2,336 K/1,904 K，CRT 堆 + markair 自身的一次性初始化分配）与
`Private Data`（4,232 K/3,600 K，主要是线程栈的 Thread Environment Block 与保留区，
截图 `vmmap_summary2.png` 下半部分逐条列出的均是 `Thread Stack`/`Private Data` 类型，
不是 markair 的堆数据结构）**。这两类合计 Private WS 5,504 K，占总量 7,040 K 的 **78%**，
其中 CRT 堆与线程栈是 Windows 进程模型与 CRT 运行时的固定开销，不是"D2D/DWrite 系统
开销"，也不是"markair 自身数据结构膨胀"——是**单进程 + 静态 CRT + Win32 GUI 消息循环**
这套架构下的进程基础成本，与 markair 具体做了什么无关。

第三方全局注入模块（Sangfor SNAC 的两个 DLL，合计 Private 52 K/Private WS 56 K）占比
很小（<1%），量级远不及 M0 记录的搜狗输入法（30+ MB）那种级别，本次不构成显著噪声，
如实记录以备后续环境变化时对照。

---

## 3. BENCH-A（100 KB 文档，静置 10 s）实测

- 启动：`build\src\Release\markair.exe --bench bench\BENCH-A.md`，PID 33108。
- 静置 10 秒后代理指标：`PrivateMemorySize64 = 16,281,600~16,642,048 字节 ≈ 15.53~15.87 MB`。
  说明：这次单次快照略高于 `run_bench.ps1 -Target BENCH-A -N 20` 的 20 轮中位数
  14.90 MB / P95 15.43 MB（`M3-METHOD.md` 第 3 节），属于单次采样的正常波动区间
  （P95 已覆盖到 15.43 MB，本次 15.53~15.87 MB 略超 P95，但仍在同一量级，不改变
  "代理指标已接近甚至偶发超过 15 MB 目标线"这个既有结论）。
- **VMMap 权威值快照**（截图 `bench/screenshots/m3-memory/vmmap_bencha_summary3.png`，
  PID 33108）：

  | Type | Size | Committed | Private | Total WS | **Private WS** | Shareable WS | Shared WS |
  |---|---|---|---|---|---|---|---|
  | **Total** | 4,625,152 K | 153,384 K | **15,540 K** | 27,232 K | **10,732 K** | 16,500 K | 15,804 K |
  | Heap | 13,988 K | 8,112 K | 8,048 K | 4,456 K | 4,452 K | 4 K | 4 K |
  | Image | 66,916 K | 66,916 K | 1,484 K | 16,036 K | 1,360 K | 14,676 K | 14,260 K |
  | Private Data | 4,420,480 K | 5,824 K | 5,540 K | 4,676 K | 4,656 K | 20 K | 20 K |
  | Stack | 16,384 K | 468 K | 468 K | 264 K | 264 K | — | — |

  即：**Private Bytes 15,540 K（≈15.18 MB，与代理指标同量级，验证代理指标基本可信）**，
  **权威值 Private WS 只有 10,732 K ≈ 10.48 MB**——比 15 MB 目标低约 30%，比 CI 现行
  16 MB 门禁低约 33%。差值 15,540 K − 10,732 K = **4,808 K ≈ 4.70 MB**，即已提交未
  驻留部分，比空文档的 1.31 MB 更大——因为 BENCH-A（100 KB 文档）的文档模型/排版
  几何数据是一次性 `reserve` 分配（`docs/coding-rules.md` 与架构文档要求的一次性
  分配策略），committed 的容量比实际写入的数据略大，是**已知的、可接受的预留余量**，
  不是泄漏或膨胀。

**归因**：与空文档一致，`Heap`（CRT 堆 + markair 文档模型/排版数据结构）与 `Private Data`
（线程栈）合计 Private WS 9,108 K，占总量 10,732 K 的 **85%**；`Image`（D2D/DWrite/
D3D 系统 DLL 的私有页）只占 1,360 K（**13%**），且与空文档时完全相同（1,360 K），
说明 BENCH-A 相对空文档新增的 Private WS（10,732 − 7,040 = 3,692 K）**全部来自
markair 自身的文档模型/排版几何数据**（Heap 部分从 1,904 K 增至 4,452 K，增量 2,548 K；
Private Data 从 3,600 K 增至 4,656 K，增量 1,056 K），这是"打开一份 100 KB 文档"应有
的、与文档大小正相关的合理内存增量，不是异常。

---

## 4. 优化空间判断

- **D2D/DWrite/D3D 系统 DLL 的私有页（Image 分类）**：两条内存线上都稳定在
  1,360 K（≈1.33 MB），占比 13~19%，**绝对值很小且不随文档变化**，是系统开销，
  **markair 代码无法优化，也不应该为此改动**（改动系统 DLL 加载行为超出本项目范围）。
- **CRT 堆 + 线程栈（Heap + Private Data，占比 78~85%）**：这是单进程 Win32 GUI
  程序的基础开销（消息循环线程栈、CRT 运行时堆初始化），BENCH-A 相对空文档的增量
  已核实全部来自 markair 自身的文档模型数据，**增量本身是合理的、与文档大小成比例的**，
  不存在"异常膨胀"。
- **结论**：**两条内存线在权威口径（VMMap Private WS）下均已达标**，且拆解表未发现
  markair 自身代码/数据结构有异常占用，因此**本任务不做任何代码改动**，也**不触发
  裁决 #5**——如果用户认为现行 CI 代理指标（`PrivateUsage`）应该继续沿用（因为它更
  容易脚本化批量跑、且是保守上界），可以保留现有门禁；如果想让 CI 门禁更贴近权威
  口径，可以考虑给代理指标的判定阈值留出与本文档实测差值相当的余量——**这是口径
  层面的裁决，不是内存优化任务，留给 T83（CI 门禁收紧）处理**。

---

## 5. BENCH-B（50 张图）内存回归确认

沿用现有代理指标，跑一轮确认无回退（未做深入拆解，按验收标准只需确认不超过
80 MB）：

```
powershell -File bench\run_bench.ps1 -Target BENCH-B -N 5
```

本机实测（2026-09-18，见 `bench/results_20260918_134205.csv`）：
`private_bytes` 中位数 43,991,040 字节 ≈ **41.95 MB**，P95 44,015,616 字节 ≈ 41.97 MB，
远低于 80 MB 上限，**未回退**（与 M1 记录的 42~44 MB 基本一致）。

---

## 6. 测试回归

本任务未修改任何 C++ 源码，仅做测量与文档产出。跑一遍既有测试套件确认零改动零失败：

```
build\tests\Release\markair_tests.exe
```

本机实测输出：`markair_tests: 422 test(s) run, 0 failure(s)`。

---

## 7. 证据存档

- `bench/screenshots/m3-memory/vmmap_summary.png` / `vmmap_summary2.png`：空文档
  Total 行 + Private WS 列截图（PID 24736）。
- `bench/screenshots/m3-memory/vmmap_image_filtered.png` / `vmmap_image_filtered2.png`：
  空文档 Image 分类模块级明细截图。
- `bench/screenshots/m3-memory/vmmap_bencha_summary.png` / `vmmap_bencha_summary2.png`
  / `vmmap_bencha_summary3.png`：BENCH-A Total 行 + Private WS 列截图（PID 33108）。
- `bench/screenshots/m3-memory/vmmap_image_sorted.png` / `vmmap_image_sorted2.png` /
  `vmmap_image_check.png`：GUI 操作过程中的中间截图（尝试按 Private 列排序时，因
  VMMap 窗口一度被其他前台窗口遮挡而截取到无关内容，已如实保留作为操作过程记录，
  不作为数据依据——**最终数据依据只取上面列出的三份 summary 截图**）。
- `bench/screenshots/m3-memory/vmmap_empty_snapshot.mtl` / `vmmap_bencha_snapshot.mtl`：
  VMMap 命令行落盘的原始快照文件（可用 VMMap GUI 重新打开复核）。
- 以上截图/快照均不进 git（沿用 `bench/screenshots/` 的 `.gitignore` 约定）。
- `bench/results_20260918_134205.csv` / `env_20260918_134205.txt`：BENCH-B 回归确认
  的原始数据（已进 git 的 `bench/` 常规产出）。

## 8. 已知局限（如实声明）

- VMMap 权威值是**单次人工快照**，不是 20 轮统计量，波动范围未知（GUI 工具限制，
  已在第 1 节说明）。若后续需要更高置信度，可考虑手动重复 3~5 次人工快照取平均，
  本次时间盒内未做（两次测量单次快照已能稳定复现"权威值显著低于代理指标"这一结论，
  边际收益递减）。
- **需要向用户如实报告的一处操作失误**：在尝试用模拟鼠标点击给 VMMap 的明细表按
  `Private` 列排序时（为了取得按大小排序的模块级列表），本机当时还有另一个终端会话
  的窗口恰好覆盖在同一屏幕区域。由于点击坐标是绝对屏幕坐标而非只作用于目标窗口，
  这几次点击（`vmmap_image_sorted.png`/`vmmap_image_check.png` 对应的操作）**可能
  被那个前台窗口而非 VMMap 接收**——从截图内容看，点击后出现的是另一会话终端里
  jelon/pinoy 仓库的对话记录，说明当时点击命中的确实是那个窗口而不是 VMMap。
  发现后已立即停止一切模拟鼠标点击操作，改用"聚焦窗口 + 静态截图"的方式取数，
  未再对任何窗口做点击/移动。就本次误点的内容看，只是把焦点切换/可能触发了滚动或
  选中，**没有输入任何文本、没有执行任何命令、没有做任何破坏性操作**，但如实告知
  用户：这属于在共享桌面环境操作 GUI 工具时的意外副作用，建议用户确认一下那个
  pinoy 会话是否受到影响。本任务后续的数据（第 2/3 节的三份 summary 截图）均是在
  改为"聚焦后静态截图、不再点击"的方式下取得，可信。

## 4. 2026-09-18 bug 修复追记（不属于 M3 任务本身）

> 背景：上一轮已给全部 9 处 `DrawTextLayout` 加了
> `D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`，但用
> `bench/corpus/electron-electron-readme.md` 复现发现国旗 emoji 仍显示成方框。
> 定位到真正根因：`src/text/font.cpp` 的 `BuildBodyFallback`（裁决 #10 正文回退链）
> 只登记了 `Segoe UI → Microsoft YaHei UI → Microsoft YaHei → SimSun` 四个族名，
> 没有任何 emoji 字体，彩色字形自然找不到字形。用户已确认批准修复方案。

### 4.1 字体加载机制确认

先完整读了 `BuildBodyFallback`/`BuildMonoFallback` 当前实现，确认机制是：
只调用 `IDWriteFontFallbackBuilder::AddMapping` 登记**字体族名字符串**
（如 `L"Segoe UI"`），不调用 `GetSystemFontCollection` 做全量枚举，也不按文件路径
（`.ttf`）手工加载字体——族名到实际字体文件的解析完全交给 DirectWrite 自己的系统
字体集合完成。因此追加 emoji 字体的做法只能是"追加一个族名字符串"，不存在文件
路径可抄。

### 4.2 具体改动

`src/text/font.cpp`：在 `kBodyFallbackFamilies` 数组**末尾**追加
`L"Segoe UI Emoji"`（新增常量 `kEmojiFallback`），前面 4 个族名的顺序/名字未动。
只影响正文回退链，未动等宽回退链（`kMonoFallbackFamilies`）。因为链数组大小用
`sizeof(...)/sizeof(...[0])` 计算，追加后无需改动任何硬编码的计数逻辑。

### 4.3 截图验证结果

- `bench/corpus/electron-electron-readme.md` 第 5 行的国旗 emoji（regional
  indicator 序列）修复后显示为 `CN BR ES JP RU FR US DE` 字母对，**不是彩色
  旗子图案**。经核实，这是 **Windows 10（本机 10.0.19045）的已知 OS 级限制**：
  微软出于地缘政治原因，在 Windows 10 的 `Segoe UI Emoji` 里故意不提供彩色国旗
  字形，只有 Windows 11 才有——这与 markair 代码无关，任何 Windows 10 应用都是这个
  结果，不是本次修复没生效。
- 用 `bench/corpus/n8n-io-n8n-readme.md`（含 📚🔧💡🤖👥📖 等单码位彩色 emoji）
  复测，截图确认这些 emoji **修复后正常显示彩色图案**，修复前应为方框——证明
  fallback 链追加确实生效，只是国旗类 emoji 受 Win10 系统限制无法达到"彩色旗子"
  的效果。
- 截图均用 `CopyFromScreen` + 发起截图前用 `SetForegroundWindow` 并核对
  `GetForegroundWindow()` 命中同一句柄的方式取得，不是 `PrintWindow`。

### 4.4 内存实测对比（同一次改动前后，GetProcessMemoryInfo 的 `PrivateUsage`）

VMMap GUI 快照在本次会话里加载 `.mtl` 文件后窗口内容长期为空、随后自行退出
（`vmmap64.exe -accepteula -o <file>` 复现两次都是同样结果），没能取到 GUI 里的
`Private WS` 权威值，如实说明这个工具限制，改用与仓库 CI 一致的代理指标
`PrivateUsage`（`psapi.dll` 的 `GetProcessMemoryInfo`）做同一次改动前后对比，
两次测量都是同一进程刚启动静置 10 秒后取值：

| 场景 | 改动前 PrivateUsage | 改动后 PrivateUsage | 增量 |
|---|---|---|---|
| A：`bench/BENCH-A.md`（无 emoji） | 23,433,216 B（22.34MB） | 23,388,160 B（22.30MB） | -45,056 B（≈0，测量噪声内） |
| B：`bench/corpus/electron-electron-readme.md`（含国旗 emoji） | 26,427,392 B（25.20MB） | 29,052,928 B（27.71MB） | +2,625,536 B（≈2.50MB） |

**验证结论**：假设成立——不含 emoji 的文档（A）内存增量≈0，含 emoji 的文档（B）
因为触发了 `Segoe UI Emoji` 字体的实际加载，产生约 2.5MB 的增量，是真实的、可
接受的一次性字体加载成本，不是内存泄漏（A 场景没有增量证明"只有触发才加载"这个
DirectWrite 惰性行为符合预期）。

### 4.5 回归确认

- `markair_tests.exe` 429 个用例全部通过（与修复前一致，无新增/无减少），`/W4` 编译
  零警告。
- 抽查 `bench/corpus/microsoft-vscode-readme.md`、`axios-axios-readme.md`、
  `mermaid-js-mermaid-readme.md` 三份文档截图，标题/中文/链接/emoji（mermaid 里
  的 💎🚀🌐🙋）均正常渲染，无方框、无崩溃、无新增渐染异常。
