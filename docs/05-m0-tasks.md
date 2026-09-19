# M0 可执行任务清单(最小可渲染文本)

> **状态**:任务已拆解到文件级,**尚未开工**。本文档是 [04-delivery-plan.md](04-delivery-plan.md) 中 M0 阶段的细化,不引入任何新的范围决策。
>
> 范围约束来自 2026-09-16 的裁决(见 [01-requirements.md §8](01-requirements.md#8-决策裁决记录-2026-09-16-定稿)),各任务中标注 `[裁决 #N]` 的条目即由此而来。
>
> **本阶段唯一已存在的代码是 `spikes/s02_font_probe.cpp`**(spike,不进主构建)。除此之外尚未创建任何源文件、CMake 工程或 vcpkg 配置。

## M0 的边界

**做**:打开一个 md → 渲染标题 / 段落 / 粗斜体 / 行内代码 / 列表 / 引用 / 分割线 / 围栏代码块(无高亮)→ 滚动。
**不做**(留给 M1+):表格、任务列表、图片、链接点击、`Ctrl+F`、字号缩放、热重载、主题、文件关联、大纲侧栏、高亮。

M0 的真实目的**不是功能,而是把性能测量的闭环建起来** —— 任务 T0/T14/T15 的优先级高于任何渲染效果。

---

## 阶段 A:前置 Spike(阻塞 T1,不做完不动主工程)

| # | 任务 | 产出 | 验收标准 |
|---|---|---|---|
| **S1** | D2D 空壳基线 | `spikes/s01_d2d_baseline.cpp` | 单文件,Win32 窗口 + D2D 设备上下文 + DirectWrite 画一行 "Hello 中文",内置 `QueryPerformanceCounter` 埋点。**Go 判据:Private Working Set ≤ 8 MB 且进程入口→首个 `Present` 返回 ≤ 40 ms** |
| **S2** | 字体加载探测 | `spikes/s02_font_probe.cpp` ✅**已写** | 见文件头注释。**Go 判据:Q2b + Q3 的私有内存增量合计在百 KB 级**;若与 Q4(全量枚举对照组)同量级,架构 §4 第 3 条需改为只用 `CreateTextFormat` 逐 run 指定族名 `[裁决 #10]` |
| **S3** | md4c 体积与速度 | `spikes/s03_md4c.cpp` + `third_party/md4c/` | 把 md4c vendored 进来,回调里只计数。**Go 判据:解析 BENCH-A(100 KB)≤ 5 ms**;同时记录 exe 体积增量与 10 MB 文档的解析耗时/峰值内存 |

**No-Go 处理**:若 S1 基线就超 8 MB,**停止推进 M0**,回到 01-requirements.md §4 与用户重新协商指标 —— 这是裁决 #12 明确保留的决策点,不得自行放宽。

编译命令(开发者命令提示符,三个 spike 通用形式):
```
cl /nologo /EHsc /O2 /W4 spikes\s02_font_probe.cpp /link dwrite.lib psapi.lib
```

---

## 阶段 B:工程骨架与基础设施

| # | 任务 | 要创建的文件 | 验收标准 |
|---|---|---|---|
| **T0** | 构建系统 | `CMakeLists.txt`、`src/CMakeLists.txt`、`.gitignore`、`.editorconfig` | CMake ≥ 3.20 + MSVC;`/MT` 静态 CRT;`/EHs-c-`(禁异常)、`/GR-`(禁 RTTI)、`/O2 /GL /Gy /Gw`;链接 `/OPT:REF /OPT:ICF /LTCG`。**不引入 vcpkg/conan**。Debug/Release 两配置均能从干净目录一次配置成功 |
| **T1** | vendored md4c | `third_party/md4c/{md4c.c,md4c.h,LICENSE,VERSION.txt}` | 直接拷贝源码,不做 submodule;`VERSION.txt` 记录采用的上游版本与提交哈希。编译无警告(允许对该目录关闭 `/W4`) |
| **T2** | 编码规范文档 | `CONTRIBUTING.md` 或 `docs/coding-rules.md` | 把技术选型 §1 的硬性约束写成可检查条目:禁异常/RTTI/iostream/`std::regex`/全局构造函数;**禁止调用 `GetSystemFontCollection`**(裁决 #10 的代码评审检查项) |
| **T3** | 极简测试框架 | `tests/markair_test.h`、`tests/main.cpp` | `[裁决 #11]` 自写约 80 行:`MARKAIR_TEST(name)` 注册宏 + `MARKAIR_CHECK` / `MARKAIR_CHECK_EQ` / `MARKAIR_CHECK_STREQ`。产出 `markair_tests.exe`,失败时返回非零退出码(供 CI 用)。**不引入 gtest/catch2** |
| **T4** | Arena 分配器 | `src/util/arena.h` / `arena.cpp` | `VirtualAlloc` 预留 + 按需提交;`Alloc(size, align)` / `Reset()`;对齐正确、跨页边界正确、耗尽时返回 `nullptr` 而非崩溃。**测试**:`tests/test_arena.cpp` 覆盖对齐、越界、reset 复用 |
| **T5** | 字符串切片与小容器 | `src/util/str.h`、`src/util/span.h` | `StrSlice{const char*, u32}`(零拷贝,指向内存映射);UTF-8↔UTF-16 转换;`Vec<T>` 基于 arena 的追加数组。**测试**:UTF-8 边界、代理对、非法序列 |

---

## 阶段 C:文档管线(解析 → 模型)

| # | 任务 | 要创建的文件 | 验收标准 |
|---|---|---|---|
| **T6** | 文件读取与内存映射 | `src/doc/file_map.h/.cpp` | `CreateFileW` + `MapViewOfFile` 只读映射,零拷贝;文件不存在/无权限返回错误码而非崩溃;空文件、0 字节文件正确处理 |
| **T7** | 编码嗅探 | `src/doc/encoding.h/.cpp` | 识别 UTF-8 BOM / UTF-16LE BOM / 无 BOM UTF-8;非法 UTF-8 时回退系统 ANSI 代码页(避免中文乱码)。**测试**:`tests/test_encoding.cpp` 准备 6 个样本文件(UTF-8、UTF-8 BOM、UTF-16LE BOM、GBK、纯 ASCII、混入非法字节),全部判定正确 |
| **T8** | md4c 回调 → 文档模型 | `src/doc/model.h`、`src/doc/parser.cpp` | 实现架构 §7 的 `Block` / `Inline` 紧凑数组模型,全部走 arena。**M0 启用的 md4c flag 仅**:`MD_FLAG_TABLES` 之外的基础 CommonMark(表格留 M1)。**明确不启用 `MD_FLAG_LATEXMATHSPANS`** `[裁决 #2]`。设置节点数上限与嵌套深度上限,超限截断。**测试**:`tests/test_model.cpp` 用 8 份小样本做快照比对(块类型序列 + 行内 flag 序列) |

---

## 阶段 D:排版与渲染

| # | 任务 | 要创建的文件 | 验收标准 |
|---|---|---|---|
| **T9** | DirectWrite 封装 | `src/text/font.h/.cpp`、`src/text/layout_text.cpp` | 字体白名单(Segoe UI / Microsoft YaHei UI→Microsoft YaHei→SimSun / Cascadia Mono→Consolas→Courier New)`[裁决 #10]`;`IDWriteTextFormat` 复用池,启动只建正文一个,其余惰性;**代码中不得出现 `GetSystemFontCollection`**。实现方式按 S2 的实测结论选定 |
| **T10** | 块级布局 | `src/layout/layout.h/.cpp` | 文档模型 → 行盒/块盒;视口宽度变化只重跑布局不重解析;**只为可见区域 ± 1 屏生成 `IDWriteTextLayout`**,其余淘汰(架构 §5)。列表缩进、引用竖线、代码块背景区域的几何计算 |
| **T11** | D2D 渲染 | `src/render/renderer.h/.cpp` | 设备/交换链管理;硬件加速失败回退 `D2D1_RENDER_TARGET_TYPE_SOFTWARE`;`D2DERR_RECREATE_TARGET` 重建资源保留布局;引用竖线与分割线用 D2D 几何图元画,**不用图标字体**(架构 §4 第 5 条) |
| **T12** | 窗口与消息循环 | `src/shell/window.h/.cpp` | **标准 Windows 标题栏,不自绘** `[裁决 #9]`;Per-Monitor V2 DPI 感知;滚轮 / PageUp / PageDown / Home / End / `Ctrl+W` / `Esc`;窗口背景色设为主题背景以避免白闪(架构 §6) |
| **T13** | 进程入口 | `src/app/main.cpp` | 命令行解析(`markair <file>`);**每个文件一个独立窗口,不做单实例复用** `[裁决 #6]`;命名互斥体仅用于"同一文件重复双击时 `SetForegroundWindow` 前置已有窗口";无有副作用的全局构造函数 |

---

## 阶段 E:性能测量闭环(与阶段 B 并行开工,不排在最后)

| # | 任务 | 要创建的文件 | 验收标准 |
|---|---|---|---|
| **T14** | 内置 `--bench` 埋点 | `src/app/bench.h/.cpp` | `QueryPerformanceCounter` 记录:进程入口 → 窗口创建 → 解析完成 → 布局完成 → **第一次 `Present` 返回**;`GetProcessMemoryInfo` 的 `PrivateUsage`。`markair --bench <file>` 以机器可读的单行 KV 输出到 stderr(便于脚本采集) |
| **T15** | 基准语料与测量脚本 | `bench/BENCH-A.md`、`bench/run_bench.ps1` | `BENCH-A.md` ≈ 100 KB 纯文本 Markdown(含表格、代码块、若干行内链接,无图片);脚本跑 20 次取中位数与 P95,输出 CSV。冷启动模式调用 RAMMap 清 standby list |
| **T16** | CI 性能门禁 | `.github/workflows/ci.yml` 或 `ci/check_budget.ps1` | 构建 + 跑 `markair_tests.exe` + 跑 `run_bench.ps1`,对**首屏时间、PrivateUsage、exe 体积**三项做阈值门禁,超标即失败 |
| **T17** | 中英混排视觉回归语料 | `bench/mixed-cjk.md` | 中英混排 + 长 URL + 长代码行 + 嵌套列表的样例,供人工视觉比对(M0 风险项:断行位置) |

---

## M0 验收线与测量方法

沿用 04-delivery-plan.md 的门槛,**不预先放宽** `[裁决 #12]`:

| 指标 | M0 门槛 | 测量工具 | 具体命令 / 操作 |
|---|---|---|---|
| BENCH-A 暖启动首屏 | ≤ **80 ms**(M3 收紧到 60 ms) | 内置埋点(T14) | `powershell -File bench\run_bench.ps1 -Warm -N 20`,取**中位数**;同时记录 P95 |
| BENCH-A 冷启动首屏 | ≤ 250 ms(目标)/ 400 ms(上限) | 内置埋点 + RAMMap | `RAMMap64.exe -Et`(清 Empty Standby List)后跑单次,重复 10 轮 |
| Private Working Set(打开 BENCH-A 静置 10s) | ≤ **20 MB**(M3 收紧到 15 MB) | **VMMap** 为准,`PrivateUsage` 为自动化代理 | VMMap 附加进程,记录 Private Working Set / Private Bytes / **Mapped File** 三项分项 —— 必须能看到字体落在 Mapped File 而非 Private |
| 空文档常驻内存 | ≤ 8 MB(目标)/ 12 MB(上限) | 同上 | 不传文件参数启动 |
| exe 体积 | 记录基线即可(M3 才门禁 1.5 MB) | 构建产物 | `link /dump /headers markair.exe`;CI 中做趋势记录 |
| 滚动帧率 | 肉眼无卡顿 + 抽查 | **PresentMon** | `PresentMon.exe -process_name markair.exe -timed 15`,滚动期间采样,看 99 分位帧时间 |
| 单元测试 | 编码嗅探 / arena / 文档模型三块全绿 | `markair_tests.exe` | 退出码为 0 |
| 内存泄漏 | 反复打开关闭无单调上升 | Application Verifier + CRT 调试堆 | 脚本连开 100 个文档,前后对比 `PrivateUsage` |

> 所有 Sysinternals 工具(VMMap / RAMMap / Process Monitor)与 PresentMon 均需在开工前装好并记录版本号,写进 `bench/TOOLS.md`,保证测量可复现。

---

## 建议的执行顺序与依赖

```
S1 ─┐
S2 ─┼─→ (Go/No-Go 决策点) ─→ T0 ─→ T1 ─→ T4/T5 ─→ T6 ─→ T7 ─→ T8 ─→ T9 ─→ T10 ─→ T11 ─→ T12 ─→ T13
S3 ─┘                          │
                               └─→ T2、T3 (与 T4 并行)
                               └─→ T14/T15/T16/T17 (尽早,不排在最后)
```

关键点:
1. **三个 spike 全部跑完并给出数据之前不要建主工程** —— 否则一旦 No-Go,推倒重来的成本远高于三天 spike。
2. **T14/T15 与阶段 B 并行** —— 性能脚手架越早建好,后面每个任务都能立刻看到自己的代价;等到 M0 末尾再补,就只能事后归因。
3. T9 的实现细节**依赖 S2 的实测结论**,不要在 S2 出数之前预先定稿字体子系统的 API 形态。

## M0 阶段的已知风险(沿用 04 并细化)

| 风险 | 触发条件 | 验证/缓解 |
|---|---|---|
| 中英混排断行位置不正确 | T10 完成后 | 用 `bench/mixed-cjk.md` 人工视觉比对;DirectWrite 的 `zh-cn` locale 与 `DWRITE_WORD_WRAPPING` 设置需实测 |
| 滚动时重排导致掉帧 | T10/T11 | PresentMon 抓 1000 帧看 99 分位;若超标,把"可见 ± 1 屏"扩大到 ± 2 屏换稳定性 |
| 高 DPI / 多显示器不同 DPI 拖动 | T12 | 手动在 100% / 150% / 200% 与双屏拖动测试;`WM_DPICHANGED` 必须重建 D2D 资源 |
| S2 结论推翻架构 §4 方案 | S2 出数时 | 备选路径已写在 spike 文件尾部:改为只用 `CreateTextFormat` 逐 run 指定族名,不用 `FontFallbackBuilder` |
| M0 实测达不到门槛 | M0 结束 | 按裁决 #12,携实测数据与用户重新协商指标,不自行放宽、不无限期优化 |
