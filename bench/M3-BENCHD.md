# M3-BENCHD：10 MB 超大文档语料与首屏时间实测（T73）

## 1. 背景

`01-requirements.md` §4 有一条硬指标"10 MB 超大文档打开时间 ≤ 1.5 s 且 UI 不卡死"，
此前没有**结构真实**的语料 —— `bench/fuzz/long-line-10mb.md` 是畸形语料（10MB 单行
无换行），测的是稳健性（不崩溃、不越界）而不是真实排版性能，不能拿它冒充这条硬
指标的验收依据。

本任务产出一份结构真实的 10 MB 文档（BENCH-D），并用它跑一次首屏时间实测。

## 2. 生成脚本设计（`bench/make_bench_d.ps1`）

按 M3 裁决记录 #8②：**BENCH-D.md 本身不进 git**，只提交生成脚本；语料在
`.gitignore` 里排除（`/bench/BENCH-D.md`）。复跑前先执行一次：

```
powershell -File bench\make_bench_d.ps1
```

设计要点：

1. **拼接来源**：`bench/corpus/` 下的 39 份真实 README（排除 `SOURCES.md`，它是
   抓取来源记录文件，不是语料正文）。
2. **确定性**：文件名字典序固定排序，循环整篇拼接直到总大小 ≥ 目标（默认 10MB），
   不做任何随机化。两次实测确认了**字节级相同**（同一份 SHA256）：

   ```
   SHA256: 6D898AAFD0B355CC6EDB6CFBFBA43F2C60E8027598AAB8BAF067EE820AB08314
   ```

3. **不含图片**：正则过滤三种 Markdown 图片语法（行内式 `![alt](url)`、引用式
   `![alt][ref]`，常见于徽章形式 `[![img][ref]][link]`、快捷引用式 `![ref]`）以及
   裸露的 `<img>` HTML 标签，只删图片语法本身、保留同行其余文本。生成后用
   `grep -c '!\[' / '<img'` 复核均为 0。
4. **块类型分布**：整篇拼接原始 README，不打散/不重排内部结构，标题/段落/列表/
   表格/代码块/链接/脚注的分布就是 39 份真实文档自身的分布，循环拼接不破坏任何
   块的完整性；每篇之间插入一个空行，避免上一篇末尾与下一篇开头的文本被解析成
   同一个块。

## 3. 生成文件实测

- 输出路径：`bench/BENCH-D.md`
- **实际大小：10,499,788 字节 ≈ 10.013 MB**（目标 10MB ± 0.5MB，落在容差区间内）
- 循环拼接轮数：8 轮（每轮遍历全部 39 份语料）
- 参与拼接语料文件数：39
- 图片引用残留：0（`![...]` 与 `<img>` 均已过滤干净）
- 确定性：重复运行两次，输出文件 diff 完全一致，SHA256 相同

## 4. 是否触发节点数上限截断

**触发了**。新增的冒烟测试 `tests/test_benchd_smoke.cpp`（复刻
`test_corpus_smoke.cpp` 的"打开 -> 编码嗅探 -> 跳过 front matter -> 解析 -> 布局"
链路，注册进 `tests/CMakeLists.txt` 的 `markair_tests` 目标）实测输出：

```
benchd_smoke: file_bytes=10499788 blocks=33299 truncated=true(触发kMaxDocumentNodeCount上限截断)
```

**如实记录，不回避**：`doc.truncated == true`，即触发了 `src/doc/parser.h` 里
`kMaxDocumentNodeCount = 200000` 的节点数上限截断。注意顶层块数（`blocks=33299`）
远小于 20 万这个数字本身——`nodeCount` 统计的是包括行内节点（inline span：链接、
强调、代码 span 等）在内的**全部**节点，39 份真实 README 反复拼接后行内节点密度
足够高，8 轮循环下来总节点数超过 20 万的上限，因此在第 8 轮中途被截断。

这是**产品事实**，不是测量瑕疵：真实世界一份 10MB、链接/格式密度接近这批真实
README 的文档，会被现有安全边界截断，测到的"打开时间"测的是**截断后的部分文档**，
不是完整 10MB 文档的排版结果。这一点已按 `08-m3-tasks.md` 表格里 T73 的验收要求
和"新增风险行"的要求（#194 行）写入本文档，供 T77（若涉及节点数上限复核）与用户
决策参考。

冒烟测试本身**通过**（`doc.blocks.Size() > 0`、`Relayout` 成功、几何数组长度与块数
一致，全部 422 个用例 0 failure），验证的是"截断后仍能安全解析并布局，不崩溃"，
而不是"没有截断"。

## 5. `run_bench.ps1 -Target BENCH-D` 首屏时间实测

`bench/run_bench.ps1` 已有的 `-Target` 语法糖机制（T42/T69/T71 建立）里新增了
`"BENCH-D"` 分支（`ValidateSet` 与 `switch` 两处），复用现有的暖启动测量逻辑，
未新增独立代码路径。

实测命令与结果（暖启动模式，N=20，本机环境见下文"环境信息"）：

```
powershell -File bench\run_bench.ps1 -Target BENCH-D -N 20
```

```
field                      median       p95
-----                      ------       ---
t_process_to_parse_ms      40.116    62.248
t_parse_to_layout_ms        2.709      4.260
t_layout_to_window_ms       29.260    51.722
t_window_to_present_ms   2108.445  2534.693
t_process_to_present_ms  2182.072  2588.859
private_bytes           176916480 178536448
```

- 原始数据：`bench/results_20260918_132404.csv`
- 环境快照：`bench/env_20260918_132404.txt`

## 6. 是否达标

**未达标**。`t_process_to_present_ms` 中位数 **2182.072 ms**，P95 **2588.859 ms**，
均**超出** 01 §4 的"≤ 1.5 s（1500 ms）"这条硬指标（超出约 45%~73%）。

耗时大头集中在 `t_window_to_present_ms`（中位数 2108.445 ms，占总耗时的
96.6%），而不是 `t_process_to_parse_ms`（40.116 ms）或 `t_parse_to_layout_ms`
（2.709 ms）——解析和布局本身很快，瓶颈在"布局完成到首帧真正 Present 出来"之间
这一段（渲染/首屏绘制路径）。私有内存 median 约 176.9 MB，也远超 01 §4 对
"稳态峰值内存"的量级预期，但本任务范围只负责测量与如实记录，不做归因和优化
（那是阶段 S 里 T74/T77 等任务的范围，且 08-m3-tasks.md 明确"阶段 S 数据出来前
不许开工"）。

**本任务（T73）的产出范围到此为止**：给出可复现的语料、如实的截断状态、如实的
首屏时间实测数据。是否优化、往哪个方向优化、`kMaxDocumentNodeCount` 是否需要
调整，均待用户/后续任务（T74/T77 及 `[裁决 #5]` 流程）决策，本文档不代为决策。

## 7. 环境信息（供数据可比性参考）

- 操作系统：Microsoft Windows 10 Pro 10.0.19045 (64-bit)
- CPU：13th Gen Intel(R) Core(TM) i5-13500，核心数 14，逻辑处理器 20
- 物理内存：31.7 GB
- 显卡：Intel(R) UHD Graphics 770，驱动版本 31.0.101.4953，显存 1024 MB
- 是否 RDP 会话：否
- 疑似全局注入/常驻模块（间接证据，非精确列表）：`RuntimeBroker`、`SogouCloud`、
  `SogouImeBroker`、`SOGOUSmartAssistant`（搜狗输入法相关进程存在，
  `memory.md` 记录过其全局注入导致私有内存 +30MB 的先例，本次 private_bytes
  的绝对值可能受此影响，需要与其他 BENCH 语料的同环境测量结果比较才有意义，
  不能孤立解读这一个数字）。

## 8. 涉及的文件变更清单

- 新增 `bench/make_bench_d.ps1`：确定性生成脚本
- 新增 `bench/BENCH-D.md`：生成的语料（**不进 git**，`.gitignore` 已排除）
- 新增 `tests/test_benchd_smoke.cpp`：BENCH-D 专用冒烟测试（文件不存在时跳过，
  不判失败），已登记进 `tests/CMakeLists.txt`
- 改 `bench/run_bench.ps1`：`-Target` 新增 `"BENCH-D"` 分支（`ValidateSet` +
  `switch` + 文档字符串 + `.EXAMPLE`），复用既有语法糖机制，未改动其余逻辑
- 改 `.gitignore`：新增 `/bench/BENCH-D.md` 排除规则
- 新增本文档 `bench/M3-BENCHD.md`
