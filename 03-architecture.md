# mdvn 技术架构

## 1. 总体原则

1. **单进程、单线程主循环 + 一个可选后台 IO 线程**。没有子进程,没有线程池。
2. **流式解析 → 一次成型的文档模型**:md4c 的 SAX 回调直接写入我们自己的紧凑文档模型,不建中间 AST、不建 HTML 字符串。
3. **内存靠 Arena(线性分配器)托管**:文档相关的全部数据结构分配在一块连续 arena 上,换文档时整块 `reset`,而不是成千上万次 `free`。零碎片、零析构成本。
4. **一切初始化都是惰性的**:没有图片就不初始化 WIC;没有代码块就不加载等宽字体;没有滚动出屏幕的内容就不做布局细化。
5. **绝不枚举系统字体**(见 §4)。

## 2. 模块划分

| # | 模块 | 目录建议 | 职责 | 依赖 |
|---|---|---|---|---|
| 1 | `app` | `src/app/` | 进程入口、命令行解析、单实例判定、崩溃兜底 | 2,3 |
| 2 | `shell` | `src/shell/` | Win32 窗口创建、消息循环、DPI 感知、拖放、键盘/滚轮、文件关联注册 | 6 |
| 3 | `doc` | `src/doc/` | 文件读取 + 编码嗅探;调用 md4c;把回调流转成"文档模型" | md4c |
| 4 | `layout` | `src/layout/` | 文档模型 → 行盒/块盒的布局结果(按宽度排版,支持增量重排) | 5 |
| 5 | `text` | `src/text/` | DirectWrite 封装:字体解析与缓存、`IDWriteTextLayout` 生成、度量 | DirectWrite |
| 6 | `render` | `src/render/` | Direct2D 渲染目标管理(**恒定软件渲染,见 §9 的架构决策**)、视口裁剪绘制、主题色 | Direct2D,5 |
| 7 | `assets` | `src/assets/` | 图片加载(WIC)、LRU 位图缓存、占位符 | WIC |
| 8 | `watch` | `src/watch/` | `ReadDirectoryChangesW` 监听 + 去抖 + 重载通知 | — |
| 9 | `hl` | `src/hl/` | 代码块极简词法高亮(M2) | — |
| 10 | `util` | `src/util/` | Arena 分配器、字符串切片、小容器、路径、ini 配置 | — |
| — | `third_party/md4c/` | | vendored md4c(`md4c.c`/`md4c.h`/`LICENSE`/`VERSION.txt`) | — |

**依赖方向**(单向,无环):

```
app → shell → render → text → (DirectWrite)
 ↓       ↓        ↓
doc → layout → text
 ↓       ↓
watch   assets → (WIC)
        hl
util ← 所有模块
```

## 3. 数据流

```
双击 .md
  ↓  ① app: 解析命令行 → 绝对路径
  ↓  ② shell: CreateWindowEx + ShowWindow(先出窗口,再填内容 → 感知延迟最低)
  ↓  ③ doc: CreateFile + 内存映射(MapViewOfFile)整文件,零拷贝
  ↓  ④ doc: BOM/UTF-8 合法性嗅探 → 必要时转 UTF-16
  ↓  ⑤ doc: md_parse() —— SAX 回调边走边往 arena 写"文档模型"
  ↓         (块:类型+层级+子节点区间;行内:样式 run + 文本切片,切片直接指向映射内存)
  ↓  ⑥ layout: 按当前视口宽度,为**可见区域 + 上下各一屏**生成 IDWriteTextLayout
  ↓  ⑦ render: BeginDraw → 裁剪绘制可见块 → EndDraw/Present
  ↓  ⑧ watch: 启动目录监听(此步可延后到首帧之后,不阻塞首屏)
```

### 关键点

- **首帧优先**:③~⑦ 必须在主线程同步完成以避免闪白;但 ⑥ 只对首屏做,其余块的布局在空闲时(`WM_TIMER` / 消息队列空闲)分批补齐。
- **文本切片零拷贝**:文档模型里的文本不复制,只存 `{offset, length}` 指向内存映射的原文件。这是内存目标的核心手段之一(100KB 文档的文本部分内存增量 ≈ 0)。UTF-16 转换的文档除外(需要一份转换后缓冲)。
- **窗口宽度变化**:只重跑 layout(⑥),不重跑解析(⑤)。
- **热重载**:watch 通知 → 重跑 ③~⑦,arena reset,按"重载前首个可见块的标题/序号"恢复滚动位置。

## 4. 字体子系统:如何避免 fontdb 式反模式

**反模式是什么**:egui/fontdb 那条路会在初始化时遍历 `C:\Windows\Fonts` 下所有字体文件、逐个解析字体表(有的还全量栅格化字形集),中文字体单个就是几十 MB(等线/雅黑/思源),这是 440MB 的主因。

**mdvn 的做法**:

1. **只用 `IDWriteFactory::CreateTextFormat` 按字体族名创建格式**,让 DirectWrite 的字体服务(`FontCache` 系统服务)去做匹配。DirectWrite 的系统字体集是**进程间共享的、按需内存映射的**,不会把字体数据复制进我们的私有工作集。
2. **显式禁止调用 `GetSystemFontCollection` 去做枚举**(写进代码评审检查项)。我们不需要"字体选择下拉框",不需要字体列表。
3. **固定字体族白名单(按需回退链)**,而不是动态发现:
   - 正文西文:`Segoe UI`
   - 正文中文:`Microsoft YaHei UI` → `Microsoft YaHei` → `SimSun`
   - 等宽:`Cascadia Mono` → `Consolas` → `Courier New`
   用 `IDWriteTextLayout::SetFontFamilyName` 对不同 run 指定,或用自定义 `IDWriteFontFallback`(`IDWriteFontFallbackBuilder`)只登记这几个族,避免系统默认 fallback 链在遇到罕见字符时去扫描更多字体。
4. **最多 3 种字号 × 3 种字重的 `IDWriteTextFormat` 复用池**,启动时只创建正文一个,其余惰性创建。
5. **图标不用图标字体**(Segoe MDL2 等),用 D2D 几何图元直接画勾选框/引用竖线等少量装饰,避免额外字体加载。
6. **不内嵌自带字体文件**(内嵌 = exe 膨胀 + 私有内存驻留)。

**验证方法**:VMMap 中观察 `Private Working Set` 与 `Mapped File` 的区分 —— 字体应当只出现在共享/映射部分,私有部分增量应在百 KB 级。这是 M0 spike 必须实测的第一条结论。

## 5. 内存优化手段清单

| 手段 | 说明 | 预期收益 |
|---|---|---|
| Arena 线性分配器 | 文档模型全部分配在一块 `VirtualAlloc` 的 arena;换文档整体 reset | 消除碎片与 malloc 元数据开销;换文档不产生内存爬升 |
| 文件内存映射 + 文本切片 | 不复制原文,`MapViewOfFile` | 文本部分私有内存 ≈ 0 |
| 不建 AST / 不生成 HTML | md4c SAX 直接入模型 | 避免节点级堆分配与中间字符串 |
| 视口外不生成 `IDWriteTextLayout` | `IDWriteTextLayout` 是相对昂贵的对象,只保留可见 ± 1 屏,其余淘汰 | 大文档内存与文档长度近似解耦 |
| 图片 LRU 缓存 + 上限 | 默认上限(建议 64MB 可配置);滚出很远的图片释放位图,保留尺寸信息以免布局跳动 | 防止图片密集文档爆内存 |
| 静态链接 CRT(`/MT`) + `/OPT:REF /OPT:ICF /LTCG` | 单 exe 无运行时依赖,链接期裁剪死代码 | exe 体积、加载 DLL 数 |
| 禁用异常/RTTI/iostream/`std::regex` | 见技术选型 §1 | exe 体积、启动期静态初始化 |
| 零全局构造函数 | 禁止有副作用的全局对象;所有子系统显式 `Init()` | 启动时间 |
| 惰性初始化 WIC / 等宽字体 / 高亮器 | 文档里没有才不付费 | 常见文档(纯文本 README)启动最快 |
| 不用 COM 单元套间外的任何 COM 组件 | `CoInitializeEx(COINIT_APARTMENTTHREADED)` 仅在需要 WIC/Shell 时调用 | 启动时间 |

## 6. 启动时间优化手段清单

| 手段 | 说明 |
|---|---|
| **先显示窗口再排版** | `CreateWindow` + 首次 `Present` 之间做最少的事;窗口背景色用主题背景,避免白闪 |
| 延迟加载 DLL(`/DELAYLOAD`) | 对 WIC、shell32 等非首屏必需的 DLL 用延迟加载 |
| 首屏只排一屏 | layout 分块化,首帧只处理可见块 |
| 避免启动期读配置的同步 IO | `state.ini` 很小,但仍放在窗口创建前一次性读完(单次 <1KB 读) |
| 不做启动画面、不做动画 | 任何过场动画都是纯粹的感知延迟 |
| **可选**:预热(`/Prefetch`)与 exe 打包优化 | 不引入自定义加壳/压缩(UPX 会拖慢冷启动的解压时间,明确不用) |

## 7. 文档模型(草案)

```
Block { type, level, firstInlineIdx, inlineCount, firstChildIdx, childCount, layoutCache* }
Inline { flags(bold/italic/code/strike/link), textOffset, textLen, linkTargetIdx }
```
- 全部以 **索引 + 连续数组**(SoA/紧凑 AoS)组织,不用指针链表 —— 缓存友好且天然适配 arena。
- `layoutCache*` 是可空的、可被淘汰的 `IDWriteTextLayout` 句柄。

## 8. 线程模型

- **主线程**:消息循环、解析、布局、渲染。目标是让它们都足够快到不需要异步。
- **IO 线程(1 个,惰性创建)**:`ReadDirectoryChangesW` 阻塞等待;图片解码(大图);超大文档(>2MB)的后台预排版。通过 `PostMessage` 回主线程。
- 无锁设计:跨线程只传递"已完成的不可变结果"的所有权。

## 9. 错误与降级路径

### 架构决策:恒定软件渲染(M3 裁决 #3 定稿)

`src/render/renderer.cpp:379` **无条件**使用 `D2D1_RENDER_TARGET_TYPE_SOFTWARE`
(`CreateHwndRenderTarget`),**没有硬件探测、没有回退分支、也没有 mdvn 自建的 DXGI
交换链**。依据是 M0 实测(`memory.md:103~108`):硬件加速目标会拉起 GPU 驱动模块,
进程私有内存 **38 MB**;软件渲染目标的裸窗口基线是 **9.0~9.4 MB**。01 §4 的常驻内存
硬指标 15 MB 只有后者做得到,且 38 MB 里的大头是驱动私有页,mdvn 侧无从优化。

由此派生的两条架构事实,写在这里避免后续任务再次误判:

1. **"软件渲染"对 mdvn 而言是主路径,不是降级路径。** 无 GPU / RDP / 老驱动这些
   场景走的是同一条代码路径,不存在"另一条分支"需要单独验证功能正确性——需要
   单独验证的只有帧率(T76 已做,见 `bench/M3-RENDER.md`)。
2. **绘制成本全部落在 CPU 上**,所以"每帧只画可见的东西"是硬约束而不是优化偏好:
   §5 的"可见 ± 1 屏"虚拟化必须在**布局层与渲染层两处**同时成立(T76 曾实测到
   渲染层缺了这一层裁剪,10 MB 文档首帧因此花掉 1.5 s)。

| 情况 | 行为 |
|---|---|
| ~~D3D/D2D 硬件设备创建失败~~ | **不适用**:从不创建硬件设备,见上方架构决策 |
| D2D 软件渲染目标创建失败(极端内存不足) | 保持目标为空,静默跳过本帧,下次绘制重试(不崩溃) |
| 设备丢失(`D2DERR_RECREATE_TARGET`) | 重建设备资源,保留布局 |
| 文件不存在/无权限 | 窗口内显示错误态,不弹 MessageBox |
| 文件超大(> 建议 50MB) | 提示并提供"仍然打开"选项,或只加载前 N MB |
| 畸形 Markdown(深嵌套) | md4c 自带深度上限;我们另加节点数上限,超限截断并在文末提示 |
| 图片解码失败 | 显示占位框 + alt 文本 |
