# mdvn 技术选型

> 本文所有第三方项目均经过实际检索核实,来源链接列在文末。凡未能核实到的数据(如具体发布日期)一律注明"未核实",不编造。

## 0. 选型总览

| 层 | 结论 | 一句话理由 |
|---|---|---|
| 语言 | **C++17(C++ 子集,类 C 风格)+ 必要处纯 C** | Win32/Direct2D 是 COM API,C++ 用起来自然;运行时开销可控 |
| GUI / 渲染 | **Win32 原生窗口 + Direct2D(恒定软件渲染目标)+ DirectWrite** | 系统自带、零额外依赖、文字排版质量最佳;**刻意不用硬件加速**,理由见 §2 末尾的架构决策(GPU 驱动模块会把内存推到 38 MB) |
| Markdown 解析 | **md4c(mity/md4c,MIT)** —— 采用现成库,不自己写 | 单 .c + 单 .h、零依赖、SAX 回调、Qt 官方捆绑使用,成熟度有背书 |
| 代码高亮 | **MVP 不做;M2 做自研极简词法高亮**,Lexilla 作为备选 | 没有找到"小而无依赖"的现成 C 高亮库;Lexilla 可用但偏重 |
| 图片解码 | **WIC(Windows Imaging Component,系统自带)** | 零依赖,覆盖 PNG/JPEG/GIF/BMP/TIFF,Win10 起含 WebP/HEIF |
| 文件监听 | **ReadDirectoryChangesW(系统 API)** | 零依赖 |
| 构建 | **CMake + MSVC(clang-cl 备选)** | 生态标准;不引入包管理器(vcpkg/conan),md4c 直接 vendored 进源码树 |

---

## 1. 语言选型

| 选项 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| **C++17(受限子集)** | Direct2D/DirectWrite 是 COM,C++ 有 RAII/`ComPtr` 管理引用计数最自然;可完全不用异常/RTTI/iostream/大部分 STL,运行时几乎为零;静态链接 CRT 后 exe 可控制在 1MB 级 | 容易"不小心"引入重型 STL/异常导致体积膨胀,需要纪律约束(编码规范里明令禁用) | ✅ **推荐** |
| 纯 C11 | 更极致的最小运行时;md4c 本身就是 C | 手写 COM 调用(`lpVtbl->Method(p, ...)`)极其啰嗦且易错,Direct2D 大量接口在 C 下可用性差;开发效率损失远大于收益 | ❌ 排除 |
| Rust | 内存安全;生态好 | 前期实测的两个 Rust 方案(mdr gui 440MB、mdviewer 95MB)说明问题不在语言而在依赖生态,但 Rust 的 windows-rs 绑定会带来可观的编译产物体积,且极易顺手拉进 crate 依赖树;本项目核心收益恰恰来自"贴着系统 API 写、不依赖任何生态" | ❌ 排除(非否定 Rust,而是本项目的收益点不在 Rust 的强项上) |
| C# / .NET NativeAOT | 开发效率高 | NativeAOT 产物通常 3~10MB 起,GC 常驻内存与 15MB 目标冲突;WinUI/WPF 更是重型 | ❌ 排除 |
| Zig / Odin 等新语言 | 体积控制好 | Win32 COM 互操作生态不成熟,长期维护风险高 | ❌ 排除 |

**硬性约束(写进编码规范)**:禁用 C++ 异常(`/EHs-c-`)、禁用 RTTI(`/GR-`)、禁用 iostream/locale、禁用 `std::regex`、慎用 `std::string`(改用自有 arena + 切片视图)。这些是体积与启动时间的主要污染源。

---

## 2. GUI / 渲染层选型

| 选项 | 内存/启动预期 | 排除理由 |
|---|---|---|
| **Win32 + Direct2D + DirectWrite** | 最优。`d2d1.dll`/`dwrite.dll` 是系统组件,多数情况下已被系统预加载;GPU 加速绘制;DirectWrite 提供工业级文字整形(连字、复杂脚本、中文断行) | ✅ **推荐** |
| Win32 + GDI/GDI+ | 内存更低一点点,启动也快 | GDI 文字渲染质量差(ClearType 控制弱、无亚像素定位、缺少现代排版特性),中英混排与缩放场景明显劣化;GDI+ 反而更重且已停滞。**保留为无 GPU / 远程桌面环境的降级路径** |
| Direct2D + Direct3D 自绘全部 | 可控性最高 | 相比纯 D2D 没有实质收益,复杂度高 |
| WebView2 | ~66MB 主进程 + 多个子进程(实测) | 多进程、浏览器内核,直接违背目标 |
| Qt / wxWidgets | Ghostwriter 实测 ~150MB | 框架体积与依赖 DLL 与目标冲突 |
| egui / imgui 等即时模式 GUI | mdviewer 实测 95MB、mdr-gui 440MB | 每帧重建 UI、自带字体栅格化子系统(常见坑就是全量加载系统字体),且文本排版能力弱于 DirectWrite |
| Electron / Tauri | 100MB+ | 同上,直接排除 |

**渲染目标类型:恒定软件渲染(架构决策,M3 裁决 #3 定稿)**

`src/render/renderer.cpp:379` **无条件**把渲染目标设为 `D2D1_RENDER_TARGET_TYPE_SOFTWARE`,
走 `CreateHwndRenderTarget`,**不做"先试硬件、失败回退"的探测**,也没有 mdvn 自建的
DXGI 交换链。

依据是 M0 的实测对照(`memory.md:103~108`):

| 渲染目标类型 | 进程私有内存 | 说明 |
|---|---|---|
| 硬件加速 | **38 MB** | 会拉起 Intel iGPU 的 `igc64.dll` 等驱动模块 |
| 软件渲染 | **9.0 ~ 9.4 MB** | 裸窗口基线(含禁 IME) |

01 §4 的常驻内存硬指标是 15 MB。硬件路径单是驱动模块就把基线推到 38 MB,**直接击穿
该指标且无从优化**(那是驱动的私有页,不在 mdvn 的掌控内)。因此这里选择内存,放弃
GPU 加速——这不是"降级路径",而是 **mdvn 的唯一主路径**。

帧率代价已在 M3 T76 实测验证(`bench/M3-RENDER.md`):软件渲染下 mdvn 自身单帧重绘
BENCH-A P50 **1.8 ms**、BENCH-C(语法高亮最坏情况)P50 **7.4 ms**,均远在 16.6 ms 的
60 FPS 预算之内,**帧率不构成放弃软件渲染的理由**。

**GDI 第二渲染路径仍然不做**:软件渲染的实测帧率有充分余量,维护第二套渲染路径的
收益为零。上表"Win32 + GDI/GDI+"那一行里"保留为无 GPU / 远程桌面环境的降级路径"
的措辞已随本决策作废——无 GPU / RDP 场景下走的同样是这条软件渲染主路径,不另设分支。

---

## 3. Markdown 解析层选型(重点:先调研现成方案)

### 3.1 现成库调研结果

| 库 | 语言/依赖 | 规格核实情况 | 评价 |
|---|---|---|---|
| **md4c**(github.com/mity/md4c) | 纯 C,除标准 C 库外**零依赖**;实现为 1 个 `.c` + 1 个 `.h` | CHANGELOG 最新条目为 **0.6.0**;声称完全兼容 **CommonMark 0.31**;MIT 许可;SAX 式回调(`md_parse()` 单函数 API);支持 UTF-8 / UTF-16 / ASCII 三种编码模式;扩展含表格、任务列表、删除线、permissive autolink、wiki link、LaTeX math span、underline、脚注、admonition、highlight、上下标、spoiler;官方强调"线性或近线性解析时间,无退化为二次方的病态输入" | ✅ **推荐**。可信度背书:**Qt 6 官方在 `qtbase/src/3rdparty/md4c` 捆绑 md4c** 用于 `QTextMarkdownImporter`(Qt 6.11 文档标注捆绑版本为 0.5.3)。也就是说它经过了 Qt 级别的长期生产验证 |
| cmark-gfm(github/cmark-gfm) | C99,无外部依赖,GitHub 官方 GFM 实现 | 构建 AST(非流式),需要完整节点树 + 自带渲染器;源码文件数与构建复杂度明显高于 md4c | ❌ 排除:AST 会为每个节点单独堆分配,与内存目标冲突;且我们要的是"解析→自有布局",cmark 的 HTML/AST 渲染器都用不上 |
| Hoedown / sundown | C | 已基本停止维护,非 CommonMark 合规 | ❌ 排除(维护状态) |
| pulldown-cmark / comrak / ferromark | Rust | 优秀,但要求语言切到 Rust 或跨 FFI | ❌ 排除(与语言选型冲突) |
| md4qt(KDE/md4qt) | C++,依赖 Qt 或 ICU | 依赖太重 | ❌ 排除 |

### 3.2 md4c vs 自己手写解析器

| 维度 | 采用 md4c | 自己手写 |
|---|---|---|
| 正确性 | CommonMark 0.31 全套一致性测试通过(上游维护) | CommonMark 规范的 emphasis/link reference/lazy continuation 等规则出了名的反直觉,手写要达到同等正确性是数人月量级 |
| 体积 | 单 .c,编译产物预估几十 KB 级(**待 M0 spike 实测**) | 理论上能更小,但差值对 1.5MB 的总目标无意义 |
| 内存 | SAX 回调,不建 AST,契合"边解析边布局" | 同等设计才能打平 |
| 可控性 | 回调里我们完全自由地构造自己的布局结构 | 更自由,但自由的代价是自己背规范 |
| 维护 | 上游修 bug / 跟进规范版本;MIT 许可,vendored 进源码树即可,无构建期依赖 | 全部自己背 |
| 风险 | 需要按需裁剪扩展 flag;若某个特性(如 front matter 隐藏)不支持,需在回调层/预处理层自己补 | 无外部风险,但整体风险更高 |

**结论:采用 md4c。** 符合 CLAUDE.md 的"优先采用成熟现成库"原则,且它恰好是"小而无依赖、专为嵌入设计"的典型代表 —— 不存在"为了用库而被迫拖进一整套生态"的问题。做法是把 `md4c.c` / `md4c.h` 直接 vendored 到 `third_party/md4c/`(附 LICENSE 与版本号记录),不引 vcpkg,不做子模块,保证构建零外部依赖。

**自己写的部分**是 md4c 之上的:回调 → 自有文档模型 → 排版布局 → D2D 绘制。这部分本来就没有现成库可用(现成的 Markdown 渲染库都直接产出 HTML)。

---

## 4. 代码块语法高亮选型 **[已裁决 2026-09-16:做,M2 自研极简方案,见 01-requirements.md §8 #3]**

| 选项 | 评估 | 结论 |
|---|---|---|
| **Lexilla**(ScintillaOrg/lexilla) | Scintilla 的词法分析器库,已从 Scintilla 独立成项目;完整源码,许可允许自由/商业使用;可编译为静态库;覆盖上百种语言;要求 C++17 | ⚠️ **备选**。缺点:词法器数量庞大(整库静态链接会明显撑大体积,需要按语言裁剪只编译需要的 lexer);API 面向 Scintilla 的文档模型,接入需要写适配层 |
| tree-sitter | C 语言、MIT、无依赖,核心库确实小;但**每种语言的 grammar 是一个独立的、体积可观的生成式 C 文件**(常见几百 KB ~ 数 MB 源码),要覆盖 10 种语言体积代价很大;还需要 highlight query 文件 | ❌ 排除(体积) |
| 现成"单文件小型 C 高亮库" | 实际检索**未找到**维护良好、可直接嵌入的通用方案(检索到的多是编辑器插件、libclang 语义高亮、或玩具级 lexer) | —— 这是"没有合适现成方案"的证据 |
| **自研极简词法高亮** | 只做 5 类 token:关键字、字符串、数字、注释、其他。每种语言只需一张关键字表 + 注释/字符串定界符配置,几百行代码覆盖 C/C++/C#/Java/JS/TS/Python/Go/Rust/JSON/YAML/Shell | ✅ **推荐**(M2)。理由:阅读场景只需要"看得舒服",不需要语义级准确;体积 ≈ 0;无第三方风险 |

**推荐路线**:MVP(M0/M1)代码块只做等宽字体 + 背景色块 + 横向滚动;M2 加自研极简高亮;如果用户后续明确要求"高亮必须专业级",再评估引入裁剪版 Lexilla。

---

## 5. 其他配套选型

| 需求 | 方案 | 排除的其他选项 |
|---|---|---|
| 图片解码 | **WIC**(`IWICImagingFactory`),D2D 原生互操作,解码后直接转 `ID2D1Bitmap` | stb_image(需自己处理色彩管理/EXIF,且系统已自带)、libpng/libjpeg(额外依赖) |
| 文件监听 | **ReadDirectoryChangesW** + 去抖 | 轮询 `GetFileTime`(功耗差)、FileSystemWatcher(非原生) |
| 配置存储 | `%LOCALAPPDATA%\mdvn\state.ini`,自写 20 行 KV 解析 | 注册表(污染)、JSON 库(依赖)、TOML 库(依赖) |
| 命令行/路径 | Win32 `PathCchCanonicalize` 等 shlwapi/pathcch | 自写路径规范化(易错) |
| 单元测试 | **自写极简断言宏 + 一个 `mdvn_tests.exe`**(已裁决 2026-09-16) | 引入 gtest/catch2 需要 vcpkg 或 submodule,会破坏本表最后一行"构建零外部依赖"的结论。见 01-requirements.md §8 #11 |
| 构建 | CMake ≥ 3.20 + MSVC,`/MT` 静态 CRT,`/O2 /GL /Gy /Gw`,链接 `/OPT:REF /OPT:ICF /LTCG` | vcpkg/conan(项目零外部依赖,不需要) |

---

## 6. 关键风险与验证方式

| 风险 | 验证方式 | 何时验证 |
|---|---|---|
| 15MB 内存目标是否现实(D2D/DWrite 本身的常驻开销未知) | 写一个最小 spike:仅创建窗口 + D2D 上下文 + DirectWrite 画一行文字,用 VMMap 量 Private Working Set 基线 | **M0 第一件事** |
| DirectWrite 是否会在初始化时隐式枚举全部系统字体(即 fontdb 同款问题) | spike 中对比:只 `CreateTextFormat` 指定单个字体族 vs 调用 `GetSystemFontCollection`,分别量内存与耗时 | M0 |
| md4c 编译产物体积与解析速度 | 把 md4c 编译进 spike,量 exe 体积增量,并对 10MB 文档计时 | M0 |
| ~~软件渲染回退路径的帧率~~ **已验证(T76)**:软件渲染是主路径而非回退路径,本机实测 mdvn 自身单帧重绘 P50 1.8~7.4 ms,远在 60 FPS 预算内;RDP / 无 GPU 两档的实际可构造性与结论见 `bench/M3-RENDER.md` §5 | PresentMon(DWM 合成口径)+ `--bench` 逐帧埋点(mdvn 自身重绘口径),两个口径必须分开看 | M3 **已完成** |

---

## 7. 参考来源

- md4c: https://github.com/mity/md4c
- md4c CHANGELOG: https://raw.githubusercontent.com/mity/md4c/master/CHANGELOG.md
- Qt 6 捆绑 md4c 的第三方许可声明: https://doc.qt.io/qt-6/qtgui-attribution-md4c.html
- cmark-gfm: https://github.com/github/cmark-gfm
- Lexilla: https://scintilla.org/Lexilla.html / https://github.com/ScintillaOrg/lexilla
- tree-sitter 语法高亮文档: https://tree-sitter.github.io/tree-sitter/3-syntax-highlighting.html
