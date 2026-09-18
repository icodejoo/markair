# M3-RENDER：软件渲染路径归因、修复与三环境验证（T76）

> 对应任务：`08-m3-tasks.md` 第 97 行 T76。裁决依据：M3 裁决记录 **#3**（渲染架构矛盾
> 闭合方式 = 方案 B，维持恒定软件渲染并修订上游文档）、**#5**（时间盒 2 个工作日）。
>
> 本文档的结论全部来自本机实测或可追溯的代码行，没有一条是推断出来的。每个结论后面
> 都标了证据来源（文件:行号 / 实测命令 / 实测数字）。

---

## 0. 结论速览

| 项 | 结论 |
|---|---|
| 异常 A（滚动帧率恒定 30.8 ms、掉帧率 99%） | **测量工具的 bug，不是渲染性能问题**。`bench/scroll_probe.ps1` 的 `Start-Sleep -Milliseconds 16` 受 Windows 15.625 ms 定时器粒度约束，实际每 31.25 ms 才投递一次滚轮消息；mdvn 每条消息画一帧、一帧不掉，于是 PresentMon 测到的"帧间隔"其实是**脚本自己的投递节奏**。已修脚本。 |
| 异常 B（10 MB 文档 `window_to_present` 占 96.6%、2108 ms） | **产品代码的真 bug，且首先是一个正确性 bug**。截断文档的 `childCount` 永远回填不上，布局塌成 `totalHeight=0`（**画面全空**），同时几何全零的块被虚拟化判成"全部可见"，一次首帧创建并绘制 7355 个 `IDWriteTextLayout`。已修。 |
| 修复效果（BENCH-D，20 轮） | 首屏 `t_process_to_present_ms` 中位数 **2182 ms → 124 ms**（17.6×）；`private_bytes` **176.9 MB → 21.6 MB**（8.2×）。01 §4 的"≤ 1.5 s"从**不达标**变为达标且余量 12×。 |
| 软件渲染能否满足 60 FPS | **能**。mdvn 自身单帧重绘 P50：BENCH-A **1.8 ms**、BENCH-C（高亮最坏情况）**7.4 ms**、BENCH-D **2.5 ms**，全部远在 16.6 ms 预算内。 |
| 是否需要"滚动时降低重绘精度" | **不需要**，该方案未实现、未采用。见 §7。 |
| 既有用例 | 改动前 **422** 个 0 失败；改动后 **423** 个（新增 1 条回归用例）0 失败；`/W4` 零警告。 |

---

## 1. 现状先讲清楚：软件渲染是主路径，不是回退分支

`src/render/renderer.cpp:370~392` 的 `Renderer::EnsureRenderTarget`：

- 第 **379** 行 `rtProps.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;` —— **无条件**，前面没有
  任何硬件探测，后面没有任何回退分支。
- 第 **384** 行用的是 `CreateHwndRenderTarget`，即 GDI 位图后端的 HWND 渲染目标，
  **mdvn 自己没有 DXGI 交换链**。

所以 `04-delivery-plan.md` 风险表里"软件渲染达不到 60FPS"说的是**主路径的帧率风险**，
不是一条边缘分支的风险。这条风险本轮已用实测关闭（§3）。

### 1.1 与上游文档的矛盾已闭合（裁决 #3，方案 B）

本任务按裁决 #3 同步修订了两份上游文档，**已随本次改动落盘**：

- **`02-tech-stack.md` §2**：删除"启动时尝试创建硬件加速的 D2D 设备上下文;失败则回退
  软件渲染"这段过时描述，改写为一条正式的架构决策"渲染目标类型：恒定软件渲染"，
  附 M0 实测对照表（硬件 **38 MB** vs 软件 **9.0~9.4 MB**，来源 `memory.md:103~108`）
  与本轮的帧率实测数据；§1 技术栈总表里"硬件加速"的表述、§6 风险表里"软件渲染回退
  路径的帧率"那一行也一并更新。
- **`03-architecture.md`**：§2 模块表第 6 行"软件渲染回退"改为"恒定软件渲染"；§9
  新增"架构决策：恒定软件渲染"小节，并把原来那条"D3D/D2D 硬件设备创建失败 → 回退
  软件渲染目标"标记为**不适用**（从不创建硬件设备）。

`08-m3-tasks.md` 第 254 行"需要用户另行修订的上游文档"清单里的第 ③ 项（02 §2 的
"先试硬件失败回退"）**至此可以勾除**。

---

## 2. 异常 A 归因：滚动帧率恒定 30.8 ms

### 2.1 反常点复述

T72 实测（`bench/M3-RENDER-FPS.md` §4）：

| 语料 | P50 | P95 | 掉帧率 |
|---|---|---|---|
| BENCH-A（100 KB 简单文本） | 30.86 ms | 34.29 ms | 99.3% |
| BENCH-C（高亮最坏情况） | 30.85 ms | 35.80 ms | 100% |

两份复杂度天差地别的文档测出几乎逐位相同的分布——这不符合"内容越复杂越慢"的常识。

### 2.2 归因：帧间隔 = 脚本的投递间隔，与 mdvn 无关

`bench/scroll_probe.ps1`（修订前）第 122 行用 `Start-Sleep -Milliseconds $IntervalMs`
控制投递节奏，`$IntervalMs` 默认 16。**Windows 的默认定时器粒度是 15.625 ms**，
`Start-Sleep` 会向上取整到整数个 tick，请求 16 ms 实际睡满两个 tick = **31.25 ms**。

**三条互相独立的证据同时指向这一点：**

1. **T72 自己的数据里就有答案**：`M3-RENDER-FPS.md` 第 67 行写着"10 秒内稳定投递
   320+ 次"。320 次 / 10 s = **31.25 ms/次**，与实测 P50 **30.86 ms** 逐位吻合。
2. **本轮复现**：用原口径（`Start-Sleep 16`）跑 BENCH-A，脚本自报
   `投递 321 次 / 实际 10030 ms => 平均间隔 31.25 ms`。
3. **改用自旋等待后节奏立刻变了**：同样请求 16 ms，实测
   `投递 625 次 / 实际 10000 ms => 平均间隔 16 ms`。

### 2.3 用新埋点把 mdvn 自己那一段单独测出来

要证明"慢的不是 mdvn"，必须把 mdvn 自身的重绘耗时与 DWM 合成节奏分开。本任务按裁决
#2 的**方案 B** 新增了逐帧埋点（只在 `--bench` 下生效，见 §6 的开销说明）：

```
MarkFrameBegin      -> PaintOnce 入口
MarkFrameLayoutDone -> 虚拟化(UpdateVisibleRange)/滚动条同步做完，D2D 绘制即将开始
MarkFrameEnd        -> RenderFrame 返回（D2D EndDraw 已返回）
```

**同一份 BENCH-A、同一个 10 秒窗口，两种投递口径的对照：**

| 投递口径 | 脚本实际投递间隔 | mdvn 画了几帧 | mdvn 自身单帧 P50 | P95 | P99 |
|---|---|---|---|---|---|
| `Start-Sleep 16`（T72 原口径） | 31.25 ms | 322 帧 / 321 条消息 | **2.30 ms** | 4.00 ms | 5.91 ms |
| `-SpinWait 16`（修正口径） | 16.00 ms | 626 帧 / 625 条消息 | **2.31 ms** | 3.61 ms | 4.40 ms |

两行的关键读法：

- **mdvn 自身单帧耗时在两种口径下完全一致（2.30 / 2.31 ms）** —— 它压根不是瓶颈，
  投递快一倍它就画快一倍。
- **帧数 = 消息数 + 1**，即**每条滚轮消息都画出了一帧，一帧没掉**。T72 报的"掉帧率
  99.3%"是把"没有输入事件所以不需要重绘"误判成了"掉帧"。
- mdvn 自身 2.3 ms 的重绘，相当于 **430 FPS 的能力**，对 16.6 ms 预算有 7 倍余量。

**结论：异常 A 是测量工具的 bug，不是渲染性能问题。** PresentMon 抓到的 `Composed:
Copy with GPU GDI` 事件是 DWM 把窗口位图合成上屏的时刻；在"只有收到输入才重绘"的
事件驱动应用上，这个节奏的上界由**输入事件到达速率**决定，PresentMon 无法区分
"渲染慢"和"没有东西要渲染"。

### 2.4 已做的修复：`bench/scroll_probe.ps1`

1. 新增 `-SpinWait` 开关，用自旋等待对齐到目标时刻（按绝对时刻对齐，不累积误差），
   实测能稳定压到 16.0 ms。`.PARAMETER SpinWait` 的文档里写明了 T72 那轮数据失真的
   全过程，避免后人再踩。
2. 收尾时打印**实际平均投递间隔**；当实际间隔超过请求值 1.25 倍时直接 `Write-Warning`
   告警——这正是 T72 那轮悄悄失真的地方，不能再让它无声通过。

---

## 3. 异常 B 归因：10 MB 文档 `window_to_present` 占 96.6%

### 3.1 反常点复述

T73 实测（`bench/M3-BENCHD.md` §5）：`process_to_parse` 40 ms、`parse_to_layout` 2.7 ms，
但 `window_to_present` 中位数 **2108 ms**，占总耗时 96.6%。架构 §5 承诺"可见 ± 1 屏
虚拟化"，首屏渲染耗时本应与文档总大小基本无关。

### 3.2 归因过程（逐层剥）

**第 1 层——先确认"慢"落在哪一段。** 用新埋点跑 BENCH-D：

```
frames_n=1  first_frame_total_ms=1573.010  first_frame_draw_ms=1516.434
```

首帧总计 1573 ms，其中 `RenderFrame` 段 **1516 ms（96.4%）**；虚拟化 +
滚动条同步那一段只有 57 ms。**瓶颈在绘制，不在布局。**

**第 2 层——第一个假设（错的，如实记录）。** `renderer.cpp` 的 `RenderFrame` 主循环
当时对**全部**块无条件调 `DrawBlock`，没有任何视口裁剪（`03-architecture.md:50` 明明
写着"render: 裁剪绘制可见块"，代码没做到）。假设：33299 个块的屏幕外图元提交拖垮了
首帧。加上视口裁剪后重测 —— **`first_frame_draw_ms` 1516 → 1558 ms，毫无改善，假设被
实测推翻。**

**第 3 层——往 `RenderFrame` 内部插桩。** 临时把 `RenderFrame` 拆成"建画笔 / BeginDraw+
Clear / 块循环 / 叠加层 / EndDraw"五段计时，并统计块的几何状态：

```
DIAG blocks=8192 drawn=8192 degenerate=8192 withlayout=7355 totalheight=0.0
     brushes_ms=0.00 beginclear_ms=0.01 blockloop_ms=1479.59 overlay_ms=0.01 enddraw_ms=1.45
```

三个数字同时炸出来：

- `totalheight=0.0` —— **整份布局的总高度是 0**，也就是说 mdvn 给 BENCH-D 画出来的是
  一个**全空的窗口**。这已经不是性能问题，是正确性问题。
- `degenerate=8192` —— **全部 8192 个块的几何都是 `top == bottom == 0`**，所以我加的
  视口裁剪一个都裁不掉（零几何与任何视口都"相交"），这解释了第 2 层为什么没效果。
- `withlayout=7355` —— 尽管画面是空的，仍然创建了 **7355 个 `IDWriteTextLayout`** 并
  逐个绘制，`blockloop_ms=1479.59` 就是这么来的。也解释了 `private_bytes` 为什么高达
  176.9 MB。

**第 4 层——为什么几何全零。** `src/layout/layout.cpp:227` 的布局入口是
`LayoutSubtree(0, ...)`，从下标 0 的根块开始按 `childCount` 递归。而
`src/doc/parser.cpp:242~254` 的 `OnLeaveBlock` 是**唯一**回填 `childCount` 的地方，
它第一行就是：

```cpp
int OnLeaveBlock(MD_BLOCKTYPE, void*, void* userdata) {
    ParseContext* ctx = static_cast<ParseContext*>(userdata);
    if (ctx->aborted) return 1;      // <-- 截断后直接返回，不回填
```

一旦触发截断（`ctx->Truncate()` 置位 `aborted`，`parser.cpp:66~70`），md4c 中止解析、
不再派发回调，`OnLeaveBlock` 自身也直接返回，于是**所有还开着的祖先块——包括最外层
的 Document 块（下标 0）——的 `childCount` 永远停在 0**。`LayoutSubtree(0, ...)` 看到
根块 `childCount == 0`，理解为"这份文档没有任何内容"，什么都不排，全部几何保持
`BlockGeometry{}` 的零初始化状态。

零几何再喂给虚拟化判据（`layout.cpp:854`）：

```cpp
bool inRange = g.bottom > extendedTop && g.top < extendedBottom;
```

`top == bottom == 0`，而首屏的 `extendedTop` 是负数、`extendedBottom` 是正数，于是
**每一个块都被判成"可见"**，虚拟化被完全击穿 → 7355 个 `IDWriteTextLayout` 被创建
并绘制 → 首帧 1.5 s。

**根因一句话**：截断文档的 `childCount` 回填缺失 → 布局塌成空 → 零几何击穿虚拟化
判据 → 首帧创建并绘制几千个本不该存在的文本布局。

> BENCH-D 确实会触发截断：`tests/test_benchd_smoke.cpp` 实测
> `truncated=true(触发 kMaxDocumentNodeCount 上限截断)`，T73 已如实记录在
> `M3-BENCHD.md` §4。也就是说这条路径**每次打开 BENCH-D 都会走到**，不是偶发。

### 3.3 已做的修复

**修复 1（正确性，`src/doc/parser.cpp`）**：`md_parse` 返回后，若 `ctx.aborted`，把
块栈上剩余的块按"提前闭合"语义逐个回填 `childCount = blocks.Size() - firstChildIdx`。
截断文档从此能正常显示**已解析出来的那部分前缀**，而不是一片空白。

**修复 2（渲染层视口裁剪，`src/render/renderer.cpp`）**：`RenderFrame` 主循环按
"可见 ± 1 屏"跳过屏幕外的块（与 `layout.cpp` 的 `UpdateVisibleRange` 用同一条边界，
不会误裁掉任何持有 `textLayout` 的块）。修复 1 把几何恢复成真实值之后，这层裁剪才
真正生效——它让代码兑现了 `03-architecture.md:50` 早就写下的"裁剪绘制可见块"。

**修复 3（逐 run 裁剪，`src/render/renderer.cpp`）**：见 §4，归因异常 A 的余量时顺带
发现的第三处"为屏幕外内容付费"。

### 3.4 修复效果（BENCH-D，`run_bench.ps1 -Target BENCH-D -N 20`）

| 字段 | 修复前（T73 实测） | 修复后 | 倍数 |
|---|---|---|---|
| `t_process_to_parse_ms` | 40.116 | 32.066 | — |
| `t_parse_to_layout_ms` | 2.709 | 5.404 | 略升（现在真的在排版了） |
| `t_layout_to_window_ms` | 29.260 | 16.556 | — |
| **`t_window_to_present_ms`** | **2108.445** | **67.912** | **31×** |
| **`t_process_to_present_ms`** | **2182.072**（P95 2588.859） | **124.18**（P95 151.6） | **17.6×** |
| **`private_bytes`** | **176 916 480**（176.9 MB） | **21 639 168**（21.6 MB） | **8.2×** |

01 §4 的"10 MB 文档 ≤ 1.5 s"：**从不达标（2182 ms）变为达标，余量 12×**。

> 注：`parse_to_layout` 从 2.7 ms 升到 5.4 ms 是**预期内**的——修复前布局函数因为根块
> `childCount == 0` 直接空转返回，那 2.7 ms 是"什么都没排"的耗时，不是一个可比的基线。

**BENCH-A 回归**（同样 20 轮）：`t_process_to_present_ms` 中位数 **64.134 ms**、
`private_bytes` 中位数 **14 821 376**（14.1 MB，P95 15 699 968 = 15.0 MB），均在现行 CI
门禁（首屏 P95 ≤ 400 ms、`private_bytes` P95 ≤ 16 MB）之内，**无回退**。

---

## 4. 归因过程中发现的第三处问题：逐 run 的全量 layout 重绘

修完异常 B 之后，用修正口径重测滚动，BENCH-C（语法高亮最坏情况）单帧 P50 **14.68 ms**、
P95 **17.65 ms**、P99 **19.53 ms** —— **P95/P99 已经越过 16.6 ms 的单帧预算**，而
BENCH-A 只要 2.31 ms。内容复杂度终于体现出来了（这正是 T72 原数据里看不到的信号），
但 6.4 倍的差距值得再挖一层。

`renderer.cpp` 的 `DrawLinkOverlays`（430~458 行）与 `DrawCodeHighlights`（460~491 行）
用的是同一个手法：**对每一个链接 run / 语法 token，裁剪到它的矩形，然后把整份
`IDWriteTextLayout` 重画一遍**（T24/T53 刻意用这个手法换掉自定义 `TextRenderer`）。
问题在于这次重绘的代价与裁剪框大小无关，**而当时它对滚出屏幕的 run 照画不误**——
一个大代码块里成百上千个 token，绝大多数并不在视口里。

**修复**：`RenderFrame` 开头记下本帧客户区高度（新增成员 `frameViewportHeight_`），
两个函数在 `PushAxisAlignedClip` 之前先判断该 run 的矩形是否整体在屏幕外，是则跳过。
**这是纯粹的"不画看不见的东西"，不改变任何一个可见像素**，不属于 04 风险表所说的
"降低重绘精度"。

| 语料 | 修复前 P50 | 修复后 P50 | 修复前 P99 | 修复后 P99 |
|---|---|---|---|---|
| BENCH-C | 14.68 ms | **7.44 ms** | 19.53 ms | **10.64 ms** |
| BENCH-D | 2.63 ms | **2.49 ms** | **512.64 ms** | **44.54 ms** |

BENCH-D 那个 512 ms 的 P99 尖刺（最大 528 ms）也是同一个成因：滚动经过大代码块时，
整块的 token 全部触发全量 layout 重绘。修复后最大值从 528 ms 降到 69 ms。

### 4.1 视口裁剪单独的贡献（A/B 对照）

为了不把三处修复的功劳混在一起，把 §3.3 修复 2 的块级裁剪单独开关做了一次 A/B
（其余修复均在位，10 秒滚动 / 自旋 16 ms）：

| 语料 | 关闭块级裁剪 P50 | 开启块级裁剪 P50 |
|---|---|---|
| BENCH-D | 7.72 ms | **2.63 ms** |
| BENCH-C | 15.59 ms | **14.68 ms** |

块级裁剪对大文档是 2.9× 的收益，对中等文档约 6%，代价是每帧 N 次浮点比较（BENCH-D
的 8192 次约 0.02 ms）。**保留**。

---

## 5. 三种环境各一轮 10 秒滚动

### 5.1 环境实地探测（先查能造出哪些场景，不预设"环境不具备"）

| 探测项 | 命令 | 结果 |
|---|---|---|
| 显卡 | `Get-CimInstance Win32_VideoController` | Intel(R) UHD Graphics 770，驱动 31.0.101.4953，1 GB，Status=OK，**唯一一块** |
| 刷新率 | 同上 `CurrentRefreshRate` | **59 Hz**（注意：不是 60 Hz，vsync 周期 16.95 ms） |
| 是否 RDP 会话 | `GetSystemMetrics(SM_REMOTESESSION=0x1000)` | **0**（当前是物理控制台会话） |
| RDP 服务 | `Get-Service TermService` / `fDenyTSConnections` | Running / **0**（允许连入） |
| RDP 监听 | `Get-NetTCPConnection -LocalPort 3389 -State Listen` | `0.0.0.0:3389` 与 `[::]:3389` **均在监听** |
| 会话列表 | `qwinsta` | `console`(ID 1, Active, Jelon) + `rdp-tcp`(Listen) |
| 已保存 RDP 凭据 | `cmdkey /list` 过滤 `TERMSRV` | **无** |
| Windows 沙盒 | `Get-WindowsOptionalFeature -FeatureName Containers-DisposableClientVM` | **Disabled**（启用需重启） |
| Hyper-V | `Get-WindowsOptionalFeature -FeatureName Microsoft-Hyper-V-All` | Enabled，`HypervisorPresent=True` |

### 5.2 档位一：本机（有 iGPU，但走软件渲染）—— ✅ 已测

两个口径分别列出。**两者测的不是一回事，必须分开读**：`mdvn 自身重绘`是
`--bench` 逐帧埋点测的"PaintOnce 入口 → D2D EndDraw 返回"；`DWM 合成上屏`是
PresentMon 的 `MsBetweenPresents`，其上界由输入事件到达速率决定。

**口径 ①：mdvn 自身单帧重绘耗时**（10 s / 自旋 16 ms 投递 625 条滚轮消息）

| 语料 | 帧数 | P50 | P95 | P99 | 最大 | 掉帧数（> 16.6 ms） |
|---|---|---|---|---|---|---|
| BENCH-A | 626 | **1.82 ms** | 3.27 ms | 4.38 ms | 28.59 ms（首帧） | 1（仅首帧） |
| BENCH-C（高亮最坏） | 626 | **7.44 ms** | 9.80 ms | 10.64 ms | 31.10 ms（首帧） | 1（仅首帧） |
| BENCH-D（10 MB） | 549 | **2.49 ms** | 31.86 ms | 44.54 ms | 69.04 ms | 见下注 |

BENCH-D 的 P95/P99 仍偏高（31.9 / 44.5 ms），成因是滚动经过大代码块时集中创建
`IDWriteTextLayout`；已从 512 ms 量级降到 45 ms 量级，**剩余部分属于 T77（超大文档
优化）的范围**，本文档只作为实测依据交接，不在 T76 内继续优化。

**口径 ②：DWM 合成上屏间隔**（PresentMon 2.5.1，同一时间窗口，修正后的投递口径）

| 语料 | 样本 n | P50 | P95 | P99 | 平均帧时间 | 平均 FPS | 掉帧数 | 掉帧率 |
|---|---|---|---|---|---|---|---|---|
| BENCH-A | 536 | 15.99 ms | 17.81 ms | 19.75 ms | 15.99 ms | **62.54** | 96 | 17.9% |
| BENCH-C | 543 | 15.98 ms | 17.52 ms | 18.41 ms | 15.99 ms | **62.53** | 144 | 26.5% |
| BENCH-D | 471 | 16.05 ms | 34.72 ms | 47.90 ms | 18.24 ms | **54.83** | 132 | 28.0% |

**读数须知（三条，缺一条都会误读）：**

1. **P50 恒等于 16.0 ms 不是巧合，是投递间隔的倒影**。脚本每 16 ms 投一条消息，mdvn
   每条消息画一帧，所以合成节奏就是 16 ms。这个数字**不是 mdvn 的能力上限**——能力
   上限看口径 ①（BENCH-A 1.82 ms ≈ 550 FPS 的余量）。
2. **本机面板是 59 Hz，vsync 周期 16.95 ms**，而 01 §4 的掉帧判据用的是 16.6 ms。也就是
   说**这块屏幕在物理上就不可能让每一帧都 ≤ 16.6 ms**，上表的"掉帧数"里有相当一部分
   是判据比硬件还严格造成的。这一点请 T83 在定门禁阈值时按实际刷新率处理。
3. 按 01 §4 的另一条判据"平均 < 55 FPS 即不通过"：**BENCH-A / BENCH-C 通过**
   （62.5 FPS）；**BENCH-D 54.83 FPS，差 0.17 卡在线上**，成因同上（大代码块的集中
   建 layout），交接 T77。

**对照 T76 开工前的数据**（`M3-RENDER-FPS.md`：BENCH-A 32.37 FPS / BENCH-C 32.25 FPS，
掉帧率 99.3% / 100%）：修正测量口径 + 三处修复之后，**BENCH-C 从 52.72 FPS（修复逐 run
重绘之前）提升到 62.53 FPS**，两份语料双双跨过 55 FPS 线。

### 5.3 档位二：RDP 会话中 —— ⚠️ 未执行，原因如实记录（不是"环境不具备"）

**本机技术上完全具备建立 RDP 会话的条件**，§5.1 已逐项实测确认：TermService 正在运行、
`fDenyTSConnections=0`、3389 端口在 IPv4/IPv6 双栈上监听、`qwinsta` 里 `rdp-tcp` 处于
Listen、`mstsc.exe` 在位。**缺的不是环境，是凭据**：

- `cmdkey /list` 显示**没有任何 `TERMSRV/*` 的已保存凭据**，`mstsc /v:localhost` 会弹出
  交互式口令框。
- 执行方**不会向用户索取、也不会代为输入账户口令**（安全标准：不经手用户凭据）。
- 另一条路（新建一个本地用户账户专供 RDP 登录）与 **M3 裁决 #6"不新建本地用户账户
  近似验证"** 的结论相抵触，不采用。
- 还有一点必须提示：Windows 10 Pro 只允许一个交互式会话，**从本机 RDP 连本机会把当前
  控制台会话接管过去**，用户的物理桌面会被断开并锁定。这属于会打断用户当前工作的操作，
  按响应行为标准需要用户本人知情并主动发起。

**请用户自行执行的完整复跑步骤**（三条命令，约 1 分钟）：

```powershell
# ① 从本机（或另一台机）发起 RDP 连到本机，在弹出的口令框里登录
mstsc /v:localhost

# ② 在 RDP 会话里确认确实处于远程会话（应输出 4096，即 SM_REMOTESESSION 置位）
powershell -c "Add-Type -Namespace T -Name M -MemberDefinition '[DllImport(\"user32.dll\")] public static extern int GetSystemMetrics(int i);'; [T.M]::GetSystemMetrics(0x1000)"

# ③ 在 RDP 会话里跑与 §5.2 完全相同的两个口径
#    口径①：mdvn 自身重绘（看进程 stderr 的 frames_* 一行）
E:\workspaces\mdvn\build\src\Release\mdvn.exe --bench E:\workspaces\mdvn\bench\BENCH-C.md
powershell -File E:\workspaces\mdvn\bench\scroll_probe.ps1 -DurationSeconds 10 -IntervalMs 16 -SpinWait
#    口径②：DWM 合成（PresentMon，需管理员）
E:\workspaces\mdvn\tools\PresentMon.exe --process_name mdvn.exe --timed 10 --terminate_after_timed --output_file bench\render_fps\rdp_BENCH-C.csv
```

**基于代码的预期（供对照，不冒充实测）**：mdvn 的绘制全部落在 CPU 上的 D2D 软件光栅器
里（`renderer.cpp:379/384`，无 DXGI 交换链），RDP 改变的只是**呈现环节**（位图经远程
桌面镜像驱动编码传输），不改变 mdvn 自身的重绘路径。因此**口径 ① 的数字预期与本机
基本一致，口径 ② 预期会明显变差且随网络带宽波动**。这个预期是否成立，必须由上面的
实跑来判定——本文档不替它下结论。

### 5.4 档位三：无 GPU / 虚拟机 —— ⚠️ 未执行，原因如实记录

逐条说明每一条可能路径为什么没走：

1. **禁用显卡驱动**（`Disable-PnpDevice` 掉 Intel UHD 770）：本机**只有这一块显示适配器**
   （§5.1 实测，`Get-PnpDevice -Class Display` 只有一行），禁用会让用户的物理桌面直接
   黑屏，且恢复可能需要重启。这是会中断用户工作、且不保证可自动恢复的操作，**未执行**。
2. **Windows 沙盒**（本来是最理想的低成本无 GPU 档：轻量、用基本显示适配器、用完即弃）：
   `Containers-DisposableClientVM` 实测状态为 **Disabled**，启用需要重启本机。**未执行**
   （重启用户机器需用户同意）。**这是三档里最值得补齐的一档**，建议用户择机启用后补测。
3. **Hyper-V 虚拟机**：Hyper-V 功能已启用（`HypervisorPresent=True`），但从零建一台
   Windows 客户机（安装介质 + 装系统 + 装 VC 运行时/工具链 + 拷语料）的工作量远超裁决
   #5 给的 2 个工作日时间盒，且与 **M3 裁决 #6"不专门搭建虚拟机"** 的既有结论一致。
   **未执行**。

**同样给出基于代码的预期**：与 §5.3 同理，mdvn 的渲染目标类型是编译期写死的软件光栅器，
**无 GPU 环境不会触发任何不同的代码分支**（这正是 §1 那条"软件渲染是主路径不是回退
分支"的直接推论），口径 ① 预期与本机同量级（仅随客户机 CPU 单核性能缩放）。

---

## 6. 新增埋点的开销（裁决 #2 方案 B 的成本约束）

方案 B 当时量化的约束是"体积 ≤ 4 KB、首屏 ≤ 0.5 ms"。实际落地情况：

- **Release 默认路径（不加 `--bench`）**：`main.cpp` 只在 `benchArgs.benchEnabled` 为真时
  才给 `WindowState` 的三个钩子赋值，否则保持空指针；`PaintOnce` 每帧只多 3 次空指针
  判断，**不产生任何 `QueryPerformanceCounter` 调用、不产生任何输出**。
- **`--bench` 路径**：每帧 3 次 QPC + 2 次浮点写入；样本环固定 4096 条（`float` × 2 =
  32 KB 静态零初始化数据，POD 全局、无构造函数副作用，符合 `docs/coding-rules.md` 第 6 条）。
- **对首屏的影响**：BENCH-A 首屏 20 轮中位数 64.134 ms，与埋点落地前的同口径数据同量级，
  未观察到可分辨的增量。
- **编码约束复核**：无异常 / RTTI / iostream / std::regex；`_snprintf_s` + `WriteFile`；
  `/W4` 构建**零警告**。

---

## 7. 是否需要"滚动时降低重绘精度"（04 缓解方案）

**不需要，且未实现。**

04 风险表里那条缓解方案的触发前提是"实测软件渲染达不到 60 FPS"。本轮实测的结论是：
达得到。mdvn 自身单帧重绘 P50 在 1.8 ms（BENCH-A）到 7.4 ms（BENCH-C 高亮最坏情况）
之间，对 16.6 ms 的单帧预算有 2.2× ~ 9.2× 的余量；此前看起来"达不到"的两组数据，一组
（异常 A）是测量工具 bug，另一组（异常 B、以及 BENCH-C 的 14.7 ms）是产品代码里三处
"为屏幕外内容付费"的真 bug，都已修复。

本轮做的三处修复**没有一处改变用户可见行为**：

| 修复 | 性质 | 可见行为变化 |
|---|---|---|
| 截断文档回填 `childCount` | 正确性修复 | 有，且是**修好**：从"全空白窗口"变成"正常显示已解析的前缀"。这是把坏行为改成对的，不是降低精度 |
| `RenderFrame` 块级视口裁剪 | 纯性能 | 无——跳过的块本来就在屏幕外 |
| 链接/高亮 run 级视口裁剪 | 纯性能 | 无——跳过的 run 本来就在屏幕外 |

**因此本任务没有任何需要用户单独裁决的"改变用户可见行为"的方案。** GDI 第二渲染路径
按 02 §2 仍然不做（现已把理由正式写进 02）。

---

## 8. 改动清单

**产品代码**

| 文件 | 改动 |
|---|---|
| `src/doc/parser.cpp` | `ParseMarkdown` 末尾：截断时回填块栈上剩余块的 `childCount`（异常 B 根因修复） |
| `src/render/renderer.cpp` | ① `RenderFrame` 主循环按"可见 ± 1 屏"裁剪；② `DrawLinkOverlays` / `DrawCodeHighlights` 跳过屏幕外的 run；③ 新增 `frameViewportHeight_` 的构造初始化 |
| `src/render/renderer.h` | 新增成员 `frameViewportHeight_`（本帧客户区高度） |
| `src/app/bench.h` / `src/app/bench.cpp` | 新增 `MarkFrameBegin` / `MarkFrameLayoutDone` / `MarkFrameEnd` / `EmitFrameReport`（逐帧耗时埋点，仅 `--bench` 生效） |
| `src/shell/window.h` | `WindowState` 末尾新增三个钩子 `onFrameBegin` / `onFrameLayoutDone` / `onFrameEnd`（沿用既有"shell 层不认识 bench 模块"的约定） |
| `src/shell/window.cpp` | `PaintOnce` 三个时机调用上述钩子 |
| `src/app/main.cpp` | 三个薄转发钩子；仅 `--bench` 时挂钩；消息循环结束后 `EmitFrameReport()` |

**测量脚本与文档**

| 文件 | 改动 |
|---|---|
| `bench/scroll_probe.ps1` | 新增 `-SpinWait`；收尾打印实际平均投递间隔；间隔超请求值 1.25× 时告警 |
| `02-tech-stack.md` | §1 技术栈表、§2 渲染目标类型架构决策（替换"先试硬件失败回退"）、§6 风险表 |
| `03-architecture.md` | §2 模块表第 6 行、§9 新增"架构决策：恒定软件渲染"小节并作废硬件回退行 |
| `tests/test_layout.cpp` | 新增回归用例 `Layout_TruncatedDocumentStillLaysOutParsedPrefix` |
| `bench/M3-RENDER.md` | 本文档 |

**测试**：改动前 **422** 个用例 0 失败 → 改动后 **423** 个用例 0 失败，`/W4` 零警告。
新增用例做过**双向验证**（沿用 T16/T44/T69 惯例）：临时把 `parser.cpp` 的修复条件改成
恒假后重跑，该用例的 3 条核心断言全部失败（`423 test(s) run, 3 failure(s)`），确认它
真的能抓住这个 bug，随后已还原。

---

## 9. 交接给后续任务的事项

1. **T77（超大文档优化）**：BENCH-D 滚动仍有 P95 31.9 ms / P99 44.5 ms 的尖刺，
   PresentMon 口径平均 54.83 FPS 卡在 55 FPS 线上。已定位到成因是滚动经过大代码块时
   集中创建 `IDWriteTextLayout`（已从 512 ms 量级降到 45 ms 量级）。另外，`M3-BENCHD.md`
   §6 那个"private_bytes 176.9 MB"的结论**已经作废**，修复后是 21.6 MB。
2. **T83（门禁收紧）**：① 本机面板是 **59 Hz**（vsync 16.95 ms），01 §4 的 16.6 ms 掉帧
   判据比硬件本身还严，定阈值时必须按实际刷新率折算；② 帧率门禁应当用 `--bench` 的
   **口径 ①**（mdvn 自身重绘）而不是 PresentMon 的口径 ②——后者的上界由输入事件速率
   决定，在 CI 的无 GPU 虚拟机上必然假红，T72 §7 已经预见到这一点。
3. **T85（发布前回归）**：§5.3 的 RDP 一档与 §5.4 的沙盒一档需要用户参与才能补齐，
   复跑命令已在对应小节给全。
4. **一条值得单独留意的产品事实**：截断（`kMaxDocumentNodeCount` / `kMaxNestingDepth`）
   此前会让窗口**整个变成空白**，而不是显示已解析的部分。这个 bug 在 10 MB 语料上必现，
   在任何触发截断的畸形/超大文档上也必现。修复后行为是"显示已解析的前缀"，但**目前
   仍然没有任何界面提示告诉用户"这份文档被截断了"**（`03-architecture.md` §9 写的是
   "超限截断并在文末提示"，该提示尚未实现）。建议由用户决定是否补一条提示，本任务
   不擅自增加用户可见的界面元素。

---

## 10. 环境信息（供数据可比性参考）

- 操作系统：Microsoft Windows 10 Pro 10.0.19045（64 位）
- CPU：13th Gen Intel(R) Core(TM) i5-13500，14 核 / 20 逻辑处理器
- 物理内存：31.7 GB
- 显卡：Intel(R) UHD Graphics 770，驱动 31.0.101.4953，1 GB；**刷新率 59 Hz**
- 会话类型：物理控制台会话（`SM_REMOTESESSION == 0`）
- 测量工具：PresentMon 2.5.1（`tools/PresentMon.exe`，管理员权限运行）
- 构建：`cmake --build build --config Release`，MSVC `/W4` 零警告
- 语料：`bench/BENCH-A.md`（102 620 B）、`bench/BENCH-C.md`（116 078 B）、
  `bench/BENCH-D.md`（10 499 788 B，不进 git，用 `bench/make_bench_d.ps1` 生成）
