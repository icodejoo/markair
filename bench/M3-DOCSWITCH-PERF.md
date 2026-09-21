# M3-DOCSWITCH-PERF：运行期"换文档"分段耗时排查（用户委托任务）

> 背景：用户反馈"打开文档普遍慢 500ms~1s"。本文档记录用真实分段埋点（新增
> `markair::bench::MarkSwitch*` / `EmitSwitchReport`，见 `src/app/bench.h`
> `src/app/bench.cpp`）在真机（本机 2026-09-18 环境快照见 `bench/M3-METHOD.md`
> 末尾，Windows 10 专业版 10.0.19045，i5-13500）对 `src/app/main.cpp` 的
> `OpenDocumentInPlace`（"点击侧栏切换文档"这条运行期路径）实测的分段数据，
> **不是**启动期首屏五段埋点（那条测的是"进程启动 -> 首帧"，覆盖不到本任务
> 关心的"进程已运行、点侧栏换文档"场景，两者是完全独立的两条时间线）。

## 测量方法

1. `markair.exe --bench <path>` 启动后，通过真实鼠标点击（`SetCursorPos` +
   `mouse_event`，非模拟消息投递）操作侧栏，触发 `OpenDocumentInPlace`。
2. 每次换文档完成后，`EmitSwitchReport` 把以下几段耗时以单行 KV 输出到
   stderr：
   - `switch_release_ms`：释放旧状态（位图/图片 Arena/文件映射/文档 Arena）
   - `switch_open_ms`：新文件 `FileMap::Open`（读盘）
   - `switch_parse_ms`：`LoadMarkdownFile`（Markdown 解析）
   - `switch_relayout_ms`：`BlockLayoutEngine::Relayout`（块级重排）
   - `switch_scroll_reset_ms`：`ResetScrollToTop` 内部 `UpdateVisibleRange`
     （可见区间 `IDWriteTextLayout` 创建 + 图片解码，任务一新增的滚动复位
     调用点顺带覆盖了这一段，此前完全没有埋点覆盖）
   - `switch_folder_scan_ms` / `switch_total_ms`：仅目录变更时才有前者，
     后者恒为"从函数入口到本次换文档全部同步工作做完"的总计
3. 场景覆盖：同目录小文档→小文档（BENCH-A→BENCH-C，无图片）、同目录小文档
   →图片密集文档（BENCH-A→BENCH-B，50 张本地 PNG）、跨目录换文档（触发
   `OpenAndScanFolder`，corpus 子目录 40 个文件）、同目录小文档→10MB 超大
   文档（BENCH-A→BENCH-D）。

## 实测数据（2026-09-21，真实鼠标点击驱动，见 `bench/screenshots/docswitch/`）

| 场景 | release | open(读盘) | parse | relayout | scroll_reset(虚拟化+图片解码) | folder_scan | **total** |
|---|---|---|---|---|---|---|---|
| BENCH-A(104KB,无图) → BENCH-C(121KB,无图)，同目录 | 0.127ms | 0.292ms | 0.889ms | 0.897ms | — | — | **2.205ms** |
| BENCH-C → corpus\actions-checkout-readme.md，跨目录(触发扫描) | 0.101ms | 0.179ms | 0.100ms | 0.918ms | 0.132ms | 0.842ms | **2.140ms** |
| BENCH-A → BENCH-B(7.8KB，50张本地PNG图片密集)，同目录 | 0.066ms | 0.146ms | 0.077ms | 0.216ms | **7.772ms** | — | **8.277ms** |
| BENCH-B → 跨目录(误点，实际命中 electron-electron-readme.md)，触发扫描(40文件) | 0.269ms | 0.252ms | 0.070ms | 0.124ms | 0.132ms | 1.442ms | **2.288ms** |
| BENCH-A(104KB) → BENCH-D(10MB) ，同目录 | 0.092ms | 0.214ms | **33.813ms** | **15.753ms** | 0.393ms | — | **50.266ms** |

## 结论：现有代码路径测不出"500ms~1s"

- 小/中文档跨目录换文档：**总计 2~9ms**，即使目录扫描 40 个文件也只占
  0.8~1.4ms（`FindFirstFileW`/`FindNextFileW` 同步遍历本身很快，40 个文件
  规模下不是瓶颈）。
- 图片密集文档（50 张本地 PNG）：`scroll_reset`（虚拟化补齐可见区块 +
  `ImageCache` 同步解码可见范围内的图片）确实是这个场景里最大的一段
  （7.8ms/8.3ms ≈ 94%），但绝对值仍然只有个位数毫秒，不构成"卡顿"。
- 10MB 超大文档：`parse`（34ms）+`relayout`（16ms）明显变大，符合"跟文档大小
  线性相关"的预期，但总计 50ms，同样远达不到 500ms。
- **本次测量没有复现用户描述的"500ms~1s"**。已排查过的候选原因（`release`/
  `open`/`parse`/`relayout`/`scroll_reset`/`folder_scan`）在本机真实点击下
  全部是个位数到几十毫秒级别，没有一段单独或加总能解释报告的量级。

## 交给用户裁决的疑点（本轮未验证，不属于本次已排查范围）

1. **`IFileOpenDialog`（`src/shell/open_dialog.cpp`）的弹出延迟**：Windows
   现代文件选择对话框在有较多 Shell 扩展（图标叠加/云盘/输入法等，本机
   `bench/M3-METHOD.md` 已记录本机常驻 `SogouCloud`/`SogouImeBroker`/
   `SOGOUSmartAssistant`）时，首次弹出常见 300ms~1s 级别延迟，这是操作系统
   /Shell 扩展枚举的固有开销，不在 `OpenDocumentInPlace` 这条路径里，本轮
   未测量（如果用户反馈的"慢"是走"文件 > 打开"对话框而不是点侧栏/文档内
   链接，这是更可能的根因，但这是 Windows Shell 层面的成本，应用层基本
   无法消除，只能考虑是否有绕过/延迟初始化的余地）。
2. **真实使用场景里更大的目录**：本次测量用的目录规模是 40/75 个文件，
   `OpenAndScanFolder` 单次约 0.8~1.4ms，线性外推到几千个文件的目录（用户
   真实工作目录可能远大于测试语料）耗时会明显上升，但目前手头没有这个
   规模的语料复现，如实标注为未测。
3. **首次换文档 vs 后续换文档的差异**：本轮每个场景都只测了一次，没有做
   20 轮统计（P50/P95），不排除某些轮次因为系统抖动（搜狗输入法等常驻
   进程、页面调度）出现单次的离群延迟，这与 `M3-METHOD.md` 记录的"本机
   非干净环境"是同一类口径限制。

## 本轮结论

没有发现"轻量级、可以安全修掉"的瓶颈——测出来的几段耗时本身都很小，不存在
"某个不必要的同步操作"需要现在就动手删除。`OpenAndScanFolder` 目前是同步
遍历目录，但在测量到的规模下不是瓶颈，是否要为"可能存在的超大目录"场景
预先做异步化，**这是一个要不要为未验证到的场景做架构改动的问题，不动手，
留给用户裁决**：如果用户能提供或描述一个真实复现"500ms~1s"的具体操作步骤
（尤其是是否经过"文件 > 打开"对话框、目录里大概多少文件、是否有图片），
可以针对性地补测再决定优化方向。
