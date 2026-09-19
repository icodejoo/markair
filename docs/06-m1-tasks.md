# M1 可执行任务清单(基础 GFM)

> **状态**:**已全部完成**(阶段 F~J,T18~T44,含新增的 T36b,2026-09-17)。`markair_tests.exe` 186 个用例全绿,已提交并推送至 [github.com/icodejoo/markair](https://github.com/icodejoo/markair)。本文档是 [04-delivery-plan.md](04-delivery-plan.md) 中 M1 阶段的细化,**不引入任何新的范围决策** —— 做什么 / 不做什么全部沿用 04 与 [01-requirements.md §6 / §8](01-requirements.md#6-gfm-支持范围-已裁决-2026-09-16全节定稿) 已裁决的结论。
>
> 拆解到文件级过程中新暴露出来的 7 个歧义点,已于 **2026-09-17** 由用户授权按"**内存占用为第一目的,怎么省内存怎么来**"这一原则裁决完毕,结论与论证见文末「M1 歧义点裁决记录」。以下任务表已按裁决结论写死,不再是"见文末待裁决"的占位。
>
> **前置事实(与 M0 不同)**:`05-m0-tasks.md` 的 T0~T17 已全部实现并实测验收通过(见 [memory.md](memory.md)),本阶段是在**已有的真实工程代码**上做增量,不是从零起步。当前代码结构:
> `src/util/`(arena/str/span/types)、`src/doc/`(file_map / encoding / model.h / parser)、`src/text/`(font)、`src/layout/`(layout)、`src/render/`(renderer)、`src/shell/`(window / scroll)、`src/app/`(main / bench),测试在 `tests/`(61 个用例全绿),语料与脚本在 `bench/`,门禁在 `ci/`。
> 架构 §2 中规划但**尚未创建**的目录:`src/assets/`(图片)、`src/watch/`(热重载,M2)、`src/hl/`(高亮,M2)。M1 会首次创建 `src/assets/`。

## M1 的边界

**做**:表格、任务列表、删除线、自动链接、脚注、图片(本地路径 + `data:` URI)、链接点击行为(外链 / 相对 `.md` 跳转 / `#锚点`)、`Ctrl+F` 查找、字号缩放、YAML front matter 隐藏。

**不做**(沿用已裁决结论,不在本阶段重开讨论):
- **mermaid** —— ```` ```mermaid ```` 按普通围栏代码块渲染 `[裁决 #1]`
- **LaTeX 数学** —— `$...$` / `$$...$$` 原样等宽显示,**`MD_FLAG_LATEXMATHSPANS` 继续保持不启用** `[裁决 #2]`
- **SVG 图片** —— 占位框 + alt 文本
- **emoji 短码** —— `:smile:` 不做映射表(Unicode emoji 字符本身由系统字体正常显示)
- **HTML 内联/块** —— 按等宽纯文本原样显示,不解释执行
- **网络图片默认不加载** —— 占位块 + 点击加载,`state.ini` 的 `load_remote_images` 开关,WinHTTP 走 `/DELAYLOAD` `[裁决 #5]`
- **动图 GIF** —— 只渲染首帧
- 留给 M2+:主题、代码高亮、大纲侧栏、历史前进后退、文件关联、热重载、拖放打开

**硬性约束不变**(02-tech-stack.md §1、`docs/coding-rules.md`):禁异常(`/EHs-c-`)、禁 RTTI(`/GR-`)、禁 iostream、禁 `std::regex`、零有副作用的全局构造函数、**禁止 `GetSystemFontCollection`**、构建零外部依赖(不引 vcpkg/conan/新第三方库)。M1 新增的代码同样要在 `/W4` 下零警告。

---

## 阶段 F:解析层扩展(阻塞本阶段几乎所有后续任务)

| # | 任务 | 要创建/修改的文件 | 验收标准 |
|---|---|---|---|
| **T18** | md4c flag 调整 | 改 `src/doc/parser.cpp`(`kParserFlags` 常量)、`src/doc/parser.h`(文档注释) | 启用:`MD_FLAG_TABLES`、`MD_FLAG_STRIKETHROUGH`、`MD_FLAG_TASKLISTS`、`MD_FLAG_PERMISSIVEAUTOLINKS`(= URL+EMAIL+WWW 三个位)、`MD_FLAG_FOOTNOTES`(vendored 版本已确认有此宏,`md4c.h:405`)。**继续保持不启用**:`MD_FLAG_LATEXMATHSPANS` `[裁决 #2]`、`MD_FLAG_WIKILINKS`、`MD_FLAG_UNDERLINE`、`MD_FLAG_SPOILERS`、`MD_FLAG_SUPER/SUBSCRIPTS`、`MD_FLAG_ADMONITIONS`、`MD_FLAG_HIGHLIGHT`、`MD_FLAG_INSERT`。现有的 `static_assert((kParserFlags & MD_FLAG_LATEXMATHSPANS) == 0)` **保留不动**,并**补一条** `static_assert` 覆盖上面整组"M1 明确不启用"的位,防止日后顺手加回。`MD_FLAG_TABLES` 那条 `static_assert` 删除并在注释里说明原因(M1 已纳入表格)。`MD_FLAG_NOHTML` **保持启用**(裁决,见文末 #1):HTML 继续按普通比例字体文本原样显示,不新增 `HtmlBlock` 块类型/`kInlineFlagHtml`,零额外内存与代码路径 |
| **T19** | 文档模型扩展 | 改 `src/doc/model.h` | `BlockType` 新增:`Table` / `TableHead` / `TableBody` / `TableRow` / `TableHeadCell` / `TableCell` / `FootnoteDefSection` / `FootnoteDef`(**不新增 `HtmlBlock`**——裁决 #1 保持 `MD_FLAG_NOHTML`,HTML 走既有的普通文本路径)。`InlineFlag` 新增:`kInlineFlagStrike`(1<<3)、`kInlineFlagLink`(1<<4)、`kInlineFlagImage`(1<<5)、`kInlineFlagAutolink`(1<<6)、`kInlineFlagFootnoteRef`(1<<7)(**不新增 `kInlineFlagHtml`**,同上)。`Block` 新增一个 `u32 detailIdx`(指向按类型分表的侧表下标,`kInvalidIndex` 表示无),**不要在 `Block` 里逐类型堆字段** —— 保持 `Block` 紧凑数组的缓存友好性(架构 §7)。`Inline::linkTargetIdx` 从"恒为 `kInvalidIndex`"改为真正指向 T20 的链接目标表。**验收**:`sizeof(Block)` 增量 ≤ 4 字节、`sizeof(Inline)` 不变;`tests/test_model.cpp` 原 26 个用例不修改即全过(向后兼容) |
| **T20** | 链接/图片目标表 | 改 `src/doc/model.h`、`src/doc/parser.cpp`;新增 `src/doc/attr.h/.cpp` | `MD_ATTRIBUTE` 展开成 arena 上的连续 UTF-8 缓冲:按 `substr_types` / `substr_offsets` 逐段拼接,`MD_TEXT_ENTITY` 段做实体解码(至少覆盖 `&amp; &lt; &gt; &quot; &#39;` 与数字实体),`MD_TEXT_NULLCHAR` 段替换为 U+FFFD。产出 `struct LinkTarget { StrSlice href; StrSlice title; u8 kind; }`(kind:外链 / 相对路径 / 锚点 / data URI / 未知),存进 `Document` 的新 `Vec<LinkTarget> linkTargets`。**这是唯一需要复制文本的地方**(href 不一定连续在源文件里),必须走 arena,不得 `malloc`。**验收**:`tests/test_attr.cpp` 覆盖实体解码、空 title、带空格的 href、`data:` 前缀识别;100KB BENCH-A 解析后 `linkTargets` 的 arena 增量 ≤ 8KB |
| **T21** | 表格 / 脚注 / 任务列表的 detail 落地 | 改 `src/doc/parser.cpp`;新增 `src/doc/detail.h` | `MD_BLOCK_TABLE_DETAIL`(col_count / head_row_count / body_row_count)→ `TableDetail`;`MD_BLOCK_TD_DETAIL::align`(`MD_ALIGN_*`)→ `CellDetail`;`MD_BLOCK_LI_DETAIL::is_task` / `task_mark` → `ListItemDetail`;`MD_BLOCK_FOOTNOTE_DEF_DETAIL::{id,ref_count,label}` 与 `MD_SPAN_FOOTNOTE_REF_DETAIL::{id,ref_id,label}` → `FootnoteDetail`。全部走 `Block::detailIdx` 侧表,各自一个 `Vec<T>`。现有的节点数上限 `kMaxDocumentNodeCount` / 深度上限 `kMaxNestingDepth` **保持不变且对新节点类型同样生效**。**验收**:`tests/test_model_gfm.cpp` 用 10 份小样本做快照比对(块类型序列 + 对齐方式 + 任务勾选态 + 脚注 id 配对) |
| **T22** | YAML front matter 隐藏 | 新增 `src/doc/front_matter.h/.cpp`;改 `src/app/main.cpp`(调用点) | 纯预处理:源文本首行恰为 `---`(允许 BOM 后)时,找到下一个单独成行的 `---` 或 `...`,把这段**从传给 `ParseMarkdown` 的切片里跳过**(只改起始 offset,零拷贝、不复制文本)。文件不以 `---` 开头、或没有闭合分隔符时原样返回,不吃掉正文。**注意**:跳过后 `Inline::textOffset` 仍相对整个源文件,不能改成相对裁剪后的切片,否则 T35 查找与 T20 的偏移都会错位。**验收**:`tests/test_front_matter.cpp` 覆盖 6 种输入(无 front matter / 正常 / 未闭合 / `---` 但其实是分割线 / CRLF / 带 BOM),渲染结果里不出现 YAML 文本 |

---

## 阶段 G:排版与渲染扩展

| # | 任务 | 要创建/修改的文件 | 验收标准 |
|---|---|---|---|
| **T23** | 富行内样式排版 | 改 `src/layout/layout.cpp`(`CreateLayoutForBlock`) | 当前实现把一个块的全部 inline 拼成一段纯文本套一个 `IDWriteTextFormat`;改为按 run 应用样式:`SetFontWeight`(粗)、`SetFontStyle`(斜)、`SetStrikethrough`(删除线,**这是删除线的全部实现成本,零新增内存**)、行内代码 run 用 `SetFontFamilyName` 切等宽族(不新建 layout 对象)。链接 run 的着色/下划线用 `SetUnderline` + 颜色(颜色画法见 T24)。**验收**:`tests/test_layout.cpp` 新增用例断言 run 区间与源 inline 区间一一对应;BENCH-A 首屏时间增量 ≤ 3ms |
| **T24** | 链接着色与命中信息 | 改 `src/layout/layout.h/.cpp` | 链接颜色不用 `SetDrawingEffect`(会引入自定义渲染器,成本不划算),改为:布局阶段把每个链接 run 的 `{blockIdx, textRange, linkTargetIdx}` 记进 `Vec<LinkBox>`,渲染时对这些 range 单独 `DrawTextLayout` 前设色 —— 或用 `IDWriteTextLayout::HitTestTextRange` 拿矩形,供 T33 命中测试与 T24 着色共用。**验收**:命中矩形与实际绘制位置误差 ≤ 1 DIP(用 `tests/test_layout.cpp` 对固定文本断言) |
| **T25** | 表格列宽算法 | 新增 `src/layout/table.h/.cpp` | "内容测宽 + 上限约束"(04 已定方向):① 每列按单元格内容的**无换行理想宽度**取 max(用 `IDWriteTextLayout` 的 `DetermineMinWidth` / metrics,或按字符宽度估算,择一并在文件头注释写明取舍);② 单列宽度上限 = 视口宽度 × `kMaxColumnWidthRatio`;③ 总宽超视口时按各列理想宽度**等比压缩**,压到列的最小可读宽度(`kMinColumnWidthDip`)为止;④ 仍超宽时**单元格内文本换行**(裁决,见文末 #2):表格变高但不丢信息;换行产生的多行版式只发生在当前"可见 ± 1 屏"范围内的表格(架构 §5 虚拟化天然覆盖),滚出视口即释放,**不产生额外常驻内存**,比横向滚动(需要每表格一份滚动状态)更省。列宽数组按裁决 §6 "列数 × 4 字节"存在布局 arena 上。**验收**:`tests/test_table.cpp` 覆盖单列超长、空表头、列数不齐、100 列宽表;算法是纯数字函数,可脱离 D2D 测 |
| **T26** | 表格渲染 | 改 `src/render/renderer.cpp`、`src/layout/layout.cpp` | 单元格文本按 `MD_ALIGN` 对齐(`SetTextAlignment`);表格边框/网格线用 **D2D 几何图元**(`DrawLine` / `DrawRectangle`)画,**不用图标字体、不用边框字符**(架构 §4 第 5 条);表头行加背景填充。表格整体参与已有的"可见 ± 1 屏"虚拟化,不得为不可见表格生成单元格 layout。**验收**:`bench/` 里 3 份真实 README 的宽表格人工比对 GitHub 网页版,列宽与对齐无明显偏差 |
| **T27** | 任务列表勾选框 | 改 `src/layout/layout.cpp`、`src/render/renderer.cpp` | `ListItemDetail::isTask` 为真时,列表项标记位置画勾选框:D2D 画一个圆角矩形 + 勾选时两条 `DrawLine` 组成的对勾(§6 已定"零字体、零位图")。**不可点击**(只读查看器)。**验收**:`tests/test_layout.cpp` 断言勾选框矩形几何随 DPI/字号缩放正确;视觉上与正文基线对齐 |
| **T28** | 脚注区渲染 | 改 `src/layout/layout.cpp`、`src/render/renderer.cpp` | `FootnoteDefSection` 作为文档末尾的独立区块:上方一条分割线 + 小一号字体 + 每条前缀 `[n]`;正文里的 `FootnoteRef` 渲染成上标样式的 `[n]`(用较小字号 + 基线偏移,不启用 `MD_FLAG_SUPERSCRIPTS`)。**ref ↔ def 不做点击互跳**(裁决,见文末 #3):M1 只渲染,不需要额外的"跳转来源"状态与命中/事件处理路径,与任务列表勾选框"不可点击"口径一致。**验收**:脚注编号与 md4c 给的 `id` 一致,多次引用同一脚注不重复生成 def 块 |
| **T29** | 字号缩放 | 改 `src/text/font.h/.cpp`、`src/shell/window.cpp`、`src/layout/layout.cpp` | `FontSubsystem` 增加 `SetScale(float)`,缩放档位固定为 `{0.8, 0.9, 1.0, 1.15, 1.3, 1.5, 1.75, 2.0}`(离散档位,避免任意浮点导致的 layout 缓存抖动);`Ctrl+=` / `Ctrl+-` / `Ctrl+0` 复位 / `Ctrl+滚轮`。缩放变更 → 释放全部 `IDWriteTextFormat` 与 `IDWriteTextLayout` → `Relayout` → 保持**当前视口顶部的块**仍在顶部(按块下标恢复,不按像素)。**M1 不持久化**(裁决,见文末 #4):缩放级别只存内存态,退出即丢,避免 M1 阶段提前引入 `state.ini` 的常驻配置对象与写盘路径,持久化随 M2 窗口状态记忆一起做。**验收**:`tests/test_font.cpp` 断言档位钳制与复位;连续缩放 50 次 `PrivateUsage` 无单调上升(复用 `run_bench.ps1` 的测量手法) |

---

## 阶段 H:图片子系统(新建 `src/assets/`)

| # | 任务 | 要创建/修改的文件 | 验收标准 |
|---|---|---|---|
| **T30** | WIC 解码封装 | 新增 `src/assets/image.h/.cpp`;改 `src/CMakeLists.txt`(加 `windowscodecs`)、根 `CMakeLists.txt`(`/DELAYLOAD:windowscodecs.dll`) | **惰性初始化**:文档里没有图片就不 `CoInitializeEx`、不创建 `IWICImagingFactory`(架构 §1 第 4 条)。解码 → `ID2D1Bitmap`。**解码时若原始像素尺寸超过 `kMaxDecodedDimension`(与 T32 共用的降采样上限)则用 WIC 的 `IWICBitmapScaler` 边解码边缩小**,不整图拒绝(与 T32 裁决 #5 的降采样策略一致,严格限住单张图片的最坏内存,同时尽量保留可读性);仅当图片本身损坏/格式不支持导致 WIC 直接解码失败时才走"超限/失败占位"状态。**动图 GIF 只解第 0 帧**(`GetFrame(0)` 后立即释放解码器)。**SVG 直接判定为不支持**,返回占位状态(不进 WIC)。解码失败同样走占位(架构 §9)。**验收**:`tests/test_image.cpp` 用 6 个样本(PNG / JPEG / 多帧 GIF / 超大尺寸 / 损坏文件 / `.svg`)断言状态码正确、无崩溃;**无图片的 BENCH-A 打开后模块列表里不出现 `windowscodecs.dll`**(用 `Get-Process.Modules` 验证 `/DELAYLOAD` 真的生效) |
| **T31** | `data:` URI 解码 | 新增 `src/assets/data_uri.h/.cpp` | 解析 `data:[<mime>][;base64],<payload>`,base64 解码到 arena 后交给 T30 同一条解码路径(§6 已定"零新增代码路径")。非法 base64 / 超长 payload(上限与单图上限一致)返回失败而非崩溃。**验收**:`tests/test_data_uri.cpp` 覆盖标准 base64、带换行的 base64、非 base64 的 URL-encoded、缺 `,`、超长输入 |
| **T32** | 图片解码位图跟随可见区域(2026-09-17 二次裁决:从"永久缓存"改为"可见性驱动") | 新增 `src/assets/cache.h/.cpp` | **裁决(2026-09-17,替代上一版"永久缓存到文档关闭"的设计)**:真正占内存的大头是解码后的 `ID2D1Bitmap`(`width×height×4`),这个**不做任何跨滚动周期的常驻缓存**——直接复用现有的"可见±1屏"块级虚拟化生命周期(与 `IDWriteTextLayout` 同一条释放/重建路径):块滚出该范围时随之释放解码位图,块再次进入可见范围时按需重新解码,**关闭文档前也不会有任何图片解码结果常驻超出当前可见范围**。真正常驻文档整个生命周期(直到文档关闭)的只有:①图片尺寸 `{width, height}`(几字节,防止布局跳动,这条不变);②本地路径 / `data:` URI 图片**不缓存原始字节**——本地路径重新进入可见范围时直接重新打开文件解码,`data:` URI 的 base64 原文本来就零拷贝引用在 `Document::source` 里,重新解码零额外内存成本;③网络图片(T34)已下载的**原始压缩字节**需要常驻(避免同一张图反复触发网络请求,压缩字节远小于解码后位图,内存代价可控)。查找/去重结构不使用 STL 容器(禁 `std::map`),用 arena 上的简单哈希表。高清需求由 T36b"点击查看原图"满足,与此处解码位图的生命周期无关。**验收**:`tests/test_cache.cpp` 断言"块进入可见范围触发解码、滚出后解码位图被释放(尺寸信息保留)、再次滚入触发重新解码"这条生命周期;网络图片原始字节的去重单独测一条用例 |
| **T33** | 图片布局与占位块 + 降采样提示标签(2026-09-17 追加) | 改 `src/layout/layout.h/.cpp`、`src/render/renderer.cpp` | 图片 inline 参与块高度计算:**先有尺寸再有位图** —— 解码前用"已缓存的尺寸 / 默认占位尺寸"占位,解码完成后若尺寸不同才触发一次重排;**这一条现在不止发生一次**(见 T32 的可见性驱动裁决:块每次重新进入可见范围都会重新解码,但由于尺寸信息本身常驻不变,重新解码不会再触发第二次重排,只有首次解码那一次可能改尺寸)。占位块统一画法(2026-09-17 追加图标):灰底圆角矩形 + 居中"图片"图标(相框+太阳+山峰,纯 D2D 几何,零图标字体零位图,矩形太小时跳过图标只留文案)+ 图标下方 alt 文本(未加载 / 加载失败 / SVG / 超限 / 网络未加载 五种状态共用同一个绘制函数,只换文案)。**降采样提示标签(新增,裁决样式:角落小文字标签)**:T30 解码时若触发了降采样(原始像素尺寸 > 降采样后尺寸),在图片右下角画一个半透明圆角小标签,文字"已压缩·点击看原图"(字号参考脚注小字号,零图标字体、零位图,与 T27 任务勾选框同一套 D2D 几何画法);未被降采样的图片不画这个标签。标签本身不需要独立命中区域——点击整张图片(T36b)已经是"查看原图"这一个行为,标签只是视觉提示,不新增交互状态。**验收**:`bench/BENCH-B.md` 从头滚到尾再滚回来,**滚动位置不发生跳动**(录屏 + 对比滚动偏移日志);另外人工验证一次"故意放一张超过 `kMaxDecodedDimension` 的大图,标签正确出现;放一张本来就很小的图,标签不出现" |
| **T34** | 网络图片(默认关闭) | 新增 `src/assets/remote.h/.cpp`;改根 `CMakeLists.txt`(`/DELAYLOAD:winhttp.dll`)、`src/shell/window.cpp`(点击占位块) | `load_remote_images` 默认 `0`:此时**完全不触碰 WinHTTP**,只显示"点击加载"占位块 `[裁决 #5]`。用户点击某个占位块 → 仅该图发起一次请求(架构 §8 的 IO 线程,`PostMessage` 回主线程)。开关为 `1` 时首屏后统一加载,**不得阻塞首帧**。**验收**:开关为 0 且不点击时,用 Process Monitor / `Get-Process.Modules` 确认**全程零网络行为、`winhttp.dll` 未被加载**(这是 §7 "不做任何主动网络请求"承诺的可验证形式);点击后单图加载失败走占位而非崩溃 |

---

## 阶段 I:交互(`src/shell/`)

| # | 任务 | 要创建/修改的文件 | 验收标准 |
|---|---|---|---|
| **T35** | 命中测试 | 新增 `src/shell/hit_test.h/.cpp`(纯函数部分)或并入 `src/layout/layout.cpp` | 屏幕坐标 →(滚动偏移换算)→ 块下标 →(`HitTestPoint`)→ inline run → `linkTargetIdx` / 图片 idx。鼠标移到链接/可点击占位块上时 `SetCursor(IDC_HAND)`。命中计算与 Win32 解耦,像 `scroll.h` 一样可单测。**验收**:`tests/test_hit_test.cpp` 断言边界条件(块间隙、行尾空白、滚动后坐标) |
| **T36** | 链接点击行为 | 新增 `src/shell/navigate.h/.cpp`;改 `src/shell/window.cpp`、`src/app/main.cpp` | 三类目标各自行为:① `http(s)://` → `ShellExecuteW(nullptr, L"open", ...)` 调系统默认浏览器,**不内嵌任何浏览**;② 相对/绝对 `.md` 路径 → 用 `PathCchCombineEx` + `PathCchCanonicalize` 规范化后打开,**在当前窗口内替换文档**(裁决,见文末 #6:不新开进程)——释放旧文档的解析/布局/渲染状态(`Document`/`BlockLayoutEngine`/图片缓存全部重建)后加载新文档,避免"点几个链接跳几个进程"导致内存随点击次数累积;路径不存在时窗口内提示、不弹 MessageBox(架构 §9);③ `#锚点` → 在当前文档的标题块里按 GitHub 式 slug 规则匹配,滚动到该块顶部。**安全边界**:非 `http/https/mailto/file` 的 scheme(如 `javascript:`)一律拒绝执行。**验收**:`tests/test_navigate.cpp` 覆盖 scheme 判定、slug 生成、路径穿越(`../../`)、锚点未命中;外链行为人工验证一次 |
| **T36b** | 图片点击查看原图(2026-09-17 新增) | 改 `src/shell/navigate.h/.cpp`、`src/shell/window.cpp` | **默认行为**(不额外画"查看原图"按钮,直接复用图片点击手势,符合 §9"内容区保持极简"):点击任意已加载图片 → 打开其原始数据。本地路径图片直接 `ShellExecuteW` 打开原文件路径;`data:` URI 图片将 base64 payload 解码写入 `%TEMP%\markair\` 下一个会话内自增编号的临时文件后 `ShellExecuteW` 打开;网络图片(T34)沿用同一模式,用已下载的原始字节写临时文件。**临时文件生命周期裁决**:不追踪外部查看器进程是否退出(`CreateProcess` + 候退线程对单实例转发型看图工具会误判"已关闭"导致过早删除、看图窗口报错或白图,弊大于利);改为进程内维护一个"本次会话创建过的临时文件路径"清单,`wWinMain` 正常退出前统一 `DeleteFileW` 清理,异常终止的残留由 %TEMP% 系统级清理兜底,不做额外保证。被降采样的图片查看到的是原始未降采样数据(因为打开的是原文件/原始 payload,不是解码后的位图),天然满足"降采样只影响内存占用、不影响用户能看到原图"的诉求。**验收**:`tests/test_navigate.cpp` 补充用例覆盖本地路径/data URI 两种打开分支与临时文件清单登记;人工验证一次"点击 PNG 图片能拉起系统照片查看器看到原图" |
| **T37** | `Ctrl+F` 查找 | 新增 `src/shell/find.h/.cpp`(UI/消息)+ `src/doc/search.h/.cpp`(纯算法) | 算法层:在 `Document` 的 inline 文本上做**大小写不敏感**的子串查找(裁决,见文末 #7:范围含正文/代码块/链接 URL/图片 alt,均直接复用已有的 `Inline`/`LinkTarget` 数据做一次性扫描,**不额外建索引缓冲**,零常驻内存增量;不含 front matter,因其未进入模型;**不做全字匹配/大小写开关**,省一份 UI 状态和分支),返回 `Vec<Match{blockIdx, byteOffset, byteLen}>`。**禁止 `std::regex`**(硬性约束),用朴素/Boyer-Moore 手写。UI 层:顶部浮出的轻量查找条(无菜单栏/工具栏,§9 #9 已定),增量输入即时重搜、`Enter` / `Shift+Enter` / `F3` 跳上一个/下一个、`Esc` 关闭。**验收**:`tests/test_search.cpp` 覆盖中英混排、跨 inline run 的匹配、空串、超长关键词;10MB 文档上一次全文搜索 ≤ 50ms |
| **T38** | 查找结果高亮与跳转 | 改 `src/layout/layout.cpp`、`src/render/renderer.cpp`、`src/shell/window.cpp` | 命中处用 `HitTestTextRange` 拿矩形,渲染时在文本**下方**画半透明填充(不改文本颜色,避免与链接色冲突);当前命中用另一种底色区分。跳转到命中项时若目标块不在"可见 ± 1 屏"内,需**先滚动再等虚拟化补齐 layout**,不得为此破坏虚拟化策略(架构 §5)。**验收**:在 BENCH-A 上连续按 50 次 `F3` 跨全文跳转,`PrivateUsage` 增量 ≤ 1MB(证明没有把全文 layout 都实例化了) |
| **T39** | `state.ini` 配置模块 | 新增 `src/util/ini.h/.cpp`;改 `src/app/main.cpp` | 自写约 20 行 KV 解析(02-tech-stack §5 已定,不引 JSON/TOML 库),路径 `%LOCALAPPDATA%\markair\state.ini`。M1 需要的键:`load_remote_images`(默认 0)、`font_body_*` / `font_mono_*` 族名覆盖 `[裁决 #10]`。**不含 `image_cache_mb`**(2026-09-17 随 T32 二次裁决作废:图片缓存改为"跟随可见区域走",不再有一个需要配置的总量上限)。**不含 `zoom` 键**(裁决,见文末 #4:M1 缩放不持久化,`zoom` 键留给 M2)。**启动期一次性读完、单次 < 1KB**(架构 §6),文件不存在按默认值走、**不创建目录也不写盘**(除非确有需要持久化的值)。**验收**:`tests/test_ini.cpp` 覆盖缺失文件、非法值、超范围值钳制、注释行、尾随空白 |

---

## 阶段 J:测试、语料与门禁(与阶段 F/G 并行开工,不排在最后)

| # | 任务 | 要创建/修改的文件 | 验收标准 |
|---|---|---|---|
| **T40** | GFM 快照测试 | 新增 `tests/test_model_gfm.cpp`、`test_attr.cpp`、`test_front_matter.cpp`、`test_table.cpp`、`test_search.cpp`、`test_navigate.cpp`、`test_hit_test.cpp`、`test_image.cpp`、`test_data_uri.cpp`、`test_cache.cpp`、`test_ini.cpp`;改 `tests/CMakeLists.txt` | 沿用 `tests/markair_test.h` 的自写断言宏 `[裁决 #11]`,**不引入 gtest/catch2**。快照口径与 M0 一致:块类型序列 + 行内 flag 序列 + 新增的 detail 字段。**验收**:`markair_tests.exe` 退出码 0,用例总数从 61 增长到 ≥ 110,且 M0 的 61 个用例**一个都不许改动**(改动即意味着破坏了向后兼容,需要在 PR 里单独说明) |
| **T41** | 真实文档回归语料 | 新增 `bench/corpus/*.md`(≥ 20 份)、`bench/corpus/SOURCES.md`、`bench/M1-REGRESSION.md` | 从知名开源项目取 ≥ 20 份 README/CHANGELOG(覆盖:宽表格、任务列表、徽章图片、脚注、自动链接、front matter、HTML 片段、mermaid 代码块各至少 2 份)。`SOURCES.md` 记录每份的来源 URL、抓取日期与许可,**不得改动原文**(改了就不是回归语料)。`M1-REGRESSION.md` 是比对记录表:`文件 / 差异描述 / 分类(可接受 / 必修)/ 处理状态`。**验收**:20 份全部打开不崩溃,差异逐条分类完毕,"必修"项全部关闭 |
| **T42** | 图片密集语料 | 新增 `bench/BENCH-B.md` + `bench/images/`(50 张 PNG)、改 `bench/run_bench.ps1`(加 `-Target` 参数) | 50 张本地 PNG + 若干 `data:` URI + 若干网络图片 URL(用于验证"默认不加载")+ 1 张 SVG + 1 张多帧 GIF + 1 张超 4096 尺寸图。**验收**见下方验收线表 |
| **T43** | 畸形文档语料与稳健性 | 新增 `bench/fuzz/*.md`、`ci/run_fuzz.ps1` | 手工构造 + 脚本生成:超深嵌套(> 64 层,触发已有的 `kMaxNestingDepth` 截断)、百万级链接(触发 `kMaxDocumentNodeCount`)、超长单行(10MB 无换行)、未闭合表格/围栏/front matter、非法 UTF-8 混入、伪造的超大 `data:` URI、图片路径穿越(`![](../../../windows/system32/...)`)。**验收**:全部**不崩溃、不卡死(单文件处理 ≤ 3s)、不越界**;在 clang-cl ASAN 配置下跑一遍零报告(ASAN 只用于这条验证,不进正式构建) |
| **T44** | CI 门禁扩展 | 改 `ci/check_budget.ps1`、`.github/workflows/ci.yml` | 在现有三项(首屏 P95 / `private_bytes` P95 / exe 体积)之外,增加:① `BENCH-B` 图片密集场景的内存门禁;② `run_fuzz.ps1` 的非零退出即失败。exe 体积基线要**重新记录**(M1 新增了 WIC/WinHTTP 的 delayload 桩与图片/查找代码),并在脚本注释里诚实注明"基线已从 M0 抬升,不是同一个数"。**验收**:本地跑一次 `ci/check_budget.ps1` exit=0;故意调低阈值能触发 exit=1(与 M0 的 T16 同样的双向验证) |

---

## M1 验收线与测量方法

沿用 04-delivery-plan.md 的 M1 验收标准,具体化成可执行步骤。**M0 已达成的门槛在 M1 不得回退**(这是新增功能不许偷走性能预算的硬约束)。

| 指标 | M1 门槛 | 测量工具 | 具体命令 / 操作 | 产出物 |
|---|---|---|---|---|
| 真实文档回归 | 20 份语料逐项人工比对 GitHub 网页版,差异全部分类,"必修"项清零 | 人工 + 截图 | 逐份 `markair.exe bench\corpus\<name>.md`,与浏览器打开的 GitHub 渲染结果并排比对 | `bench/M1-REGRESSION.md`(比对记录表)、`bench/corpus/SOURCES.md` |
| 图片密集文档峰值内存 | **≤ 80MB**(50 张 PNG 全部滚过一遍,`kMaxDecodedDimension` 已下调为低默认值) | `--bench` 的 `PrivateUsage`(自动化代理)+ **VMMap**(权威值) | `markair.exe --bench bench\BENCH-B.md`,滚到底部后读数;VMMap 附加进程记录 Private Working Set / Private Bytes / Mapped File 三项 | CSV(`run_bench.ps1 -Target BENCH-B`)+ VMMap 截图 |
| ~~图片滚走后内存回落~~(2026-09-17 裁决作废) | T32 已改为"降采样后永久缓存、不做淘汰"([裁决,见 T32])，不存在"滚走后回落"这件事,本行验收线不再适用,不需要验证 | — | — | — |
| 滚动不跳动 | 滚动位置不因图片加载完成后的一次性重排而变化(T33"先有尺寸再有位图"仍然有效;这条现在只需验证首次解码这一次性重排不产生跳动,不再涉及淘汰/重新加载场景) | 录屏 + 滚动偏移日志 | `BENCH-B` 从顶滚到底再滚回,记录每帧 `scrollY`,不应出现非用户触发的突变 | 录屏文件 + 日志(放 `bench/` 下,不进 git) |
| 解析→模型一致性 | 快照测试全绿,M0 的 61 个用例零改动 | `markair_tests.exe` | `build\Release\markair_tests.exe`,退出码 0 | 控制台输出 |
| BENCH-A 暖启动首屏(不回退) | **≤ 80ms 中位数**(与 M0 同线,新功能不得吃掉预算) | 内置埋点 | `powershell -File bench\run_bench.ps1 -Warm -N 20` | CSV + 中位数/P95 |
| BENCH-A 常驻内存(不回退) | **≤ 20MB**(M0 实测 ~11MB,M1 后不应显著抬升) | 同上 | 同上 | 同上 |
| exe 体积 | 记录 M1 新基线并写入 `ci/check_budget.ps1`(M3 才门禁 1.5MB) | 构建产物 | `link /dump /headers build\Release\markair.exe` | CI 日志的趋势记录 |
| 零网络行为 | `load_remote_images=0` 且不点击时,无任何出站连接、`winhttp.dll` 未加载 | Process Monitor + `Get-Process.Modules` | 打开含网络图片的语料,ProcMon 过滤 `markair.exe` 的 Network 类事件应为空 | ProcMon 导出的 CSV |
| 无图文档零 WIC 开销 | 打开 BENCH-A 时 `windowscodecs.dll` 未被加载 | `Get-Process.Modules` | `(Get-Process markair).Modules \| Where-Object { $_.ModuleName -like '*codecs*' }` 应为空 | 控制台输出 |
| 畸形文档稳健性 | 不崩溃 / 不卡死(≤ 3s)/ ASAN 零报告 | `ci/run_fuzz.ps1` + clang-cl ASAN | 逐份打开 `bench/fuzz/*.md`,超时即判失败 | 脚本退出码 + 日志 |
| 内存泄漏 | 反复打开关闭无单调上升 | 脚本 + `PrivateUsage` | 沿用 M0 手法:连续开关 30 次,比较前 5 次与后 5 次均值 | 控制台统计 |

> **工具前置**:VMMap / RAMMap / Process Monitor / PresentMon 在 M0 阶段**尚未安装**(见 `bench/TOOLS.md` 与 memory.md 的遗留项)。上表中标"权威值"的几行依赖 VMMap 与 ProcMon,**开工前需要先解决工具安装这件事**(往系统装外部工具属于用户决策,不由执行方单方面决定)。在工具到位之前,只能用 `PrivateUsage` 代理指标,且须在报告里注明口径。

---

## 建议的执行顺序与依赖

```
                    ┌─→ T23(富行内样式)─→ T24(链接着色/命中矩形)─┬─→ T35(命中测试)─→ T36(链接点击)
                    │                                                │
T18(flag)─→ T19(模型)┼─→ T21(detail)─→ T25(列宽)─→ T26(表格渲染)   ├─→ T38(查找高亮)
      │             │              └─→ T27(任务列表)                 │
      │             └─→ T20(链接目标表)─→ T30(WIC 解码)─→ T32(LRU)─→ T33(图片布局/占位)─→ T34(网络图片)
      │                             └─→ T31(data URI)                        ↑
      └─→ T22(front matter,可与 T19 并行)                          T39(state.ini)┘
                                                                     │
                    T29(字号缩放,依赖 T23)                           └─→ 供 T32 读 image_cache_mb、T34 读 load_remote_images

T37(查找算法,只依赖 T19,可很早并行)─→ T38

T40~T44(测试/语料/门禁)与阶段 F/G 并行开工,不排在最后
```

关键点:

1. **T18 + T19 是全阶段的瓶颈**,它们一改,`parser.cpp` / `model.h` 的所有下游都要跟着动。**先把这两个做完、测试全绿,再开并行**,否则多个任务会在同一批文件上持续冲突。
2. **T20(链接目标表)被三条线共用** —— 链接点击(T36)、图片(T30)、脚注(T28)都要它,优先级高于任何渲染效果。
3. **T39(`state.ini`)要早于 T32/T34** —— 缓存上限与网络开关都从它读;否则这两个任务只能先写死常量,回头再改一遍。
4. **T40~T44 与开发并行**,理由同 M0 的 T14/T15:语料越早到位,每个任务合并时就能立刻看到自己在真实文档上的效果与代价,而不是到阶段末尾再事后归因。
5. **T37 的算法层(`src/doc/search.h/.cpp`)只依赖文档模型**,不依赖布局/渲染,可以在 T23~T28 还在做的时候就并行完成并单测。
6. M0 的 61 个测试是本阶段的**回归护栏**:任何一次改动后先跑 `markair_tests.exe`,红了就不要继续往下叠功能。

---

## M1 阶段的已知风险(沿用 04 并细化验证方法)

| 风险 | 触发条件 | 验证/缓解 |
|---|---|---|
| **表格列宽算法(内容自适应 vs 等分)难做好看** | T25/T26 完成后 | 先按 04 已定的"内容测宽 + 上限约束"实现;拿 `bench/corpus/` 里的真实宽表格(至少 3 份不同风格的 README)人工比对 GitHub 网页版。等比压缩后仍不可读时按裁决 #2 换行(受±1屏虚拟化约束,不产生额外常驻内存) |
| **图片解码完成后一次性重排导致滚动跳动**(2026-09-17 缩小范围:T32 已改为不淘汰,原"淘汰后尺寸不一致"这条风险随之消失,只剩"首次解码"这一种触发场景) | T33 完成后 | 验证用 `BENCH-B` 从顶滚到底再滚回,记录每帧 `scrollY`,出现非用户触发的突变即判失败 |
| (已大幅降级)数百张图片的文档内存增长 | 图片数量远超正常 README 量级的文档 | **2026-09-17 二次裁决后风险已基本消解**:T32 改为解码位图跟随"可见±1屏"释放,不再随文档总图片数累积;唯一仍会累积的是网络图片(T34)的原始压缩字节,但字节量远小于解码位图,且仅限"已点击加载过的网络图片"这个更小的子集,不需要额外验证 |
| **恶意文档(超深嵌套/百万链接)** | T43,以及任何解析层改动之后 | `bench/fuzz/` 语料 + `ci/run_fuzz.ps1` 进 CI;配合 **clang-cl ASAN** 与 **Application Verifier** 各跑一遍。已有的 `kMaxNestingDepth`(64)与 `kMaxDocumentNodeCount`(200000)截断机制必须对新增的表格/脚注/HTML 节点同样生效 —— 这是 T21 的验收项,不是可选项 |
| (细化)新功能吃掉 M0 的性能预算 | 每个任务合并时 | 每个 PR 都跑一次 `ci/check_budget.ps1`;首屏时间与常驻内存**沿用 M0 的门槛不放宽** `[裁决 #12]`。单个任务的增量超过阈值时就地定位,不攒到阶段末尾 |
| (细化)`/DELAYLOAD` 没真正生效,无图文档也加载了 WIC/WinHTTP | T30/T34 | 用 `Get-Process.Modules` 直接验证模块列表,**不采信"代码里没调用"这种静态推断**(M0 的 S1 spike 已经证明模块注入会在意料之外发生) |

---

## M1 歧义点裁决记录(2026-09-17)

拆解到文件级时暴露的 7 个歧义,前面 01~04 文档都没覆盖到。用户于 2026-09-17 授权裁决原则:**"内存占用为第一目的,怎么省内存怎么来"**,由此逐条拍板(#2/#7 差异实为可忽略的 KB 级、跟内存无关时,退而按"不丢信息/不加状态"的最简方案定,已在各条注明)。已同步落到上面各任务表格。

1. **`MD_FLAG_NOHTML` —— 保持启用(维持 M0 现状)**
   代价对比:去掉该 flag 走"真等宽"需要新增 `HtmlBlock` 块类型 + `kInlineFlagHtml` + 一条独立文本路径,还要重新确认畸形 HTML 不会突破节点数上限;保持现状是零增量。选**零增量**一侧,代价是 HTML 显示为"原样但非等宽"(与裁决 #2 原文字面稍有出入,但視覺影响很小)。

2. **表格超宽兜底 —— 单元格内换行**
   本条的三个选项在内存上其实都是 KB 级差异,**不是内存主导的决策**:换行产生的多行文本仍然只存在于"可见 ± 1 屏"范围内(架构 §5 的虚拟化机制天然覆盖),滚出视口即释放,不产生额外常驻内存;而横向滚动需要每张表格多一份滚动状态,裁切会丢信息。三者内存代价相近时,选不丢信息、不加交互状态的换行。

3. **脚注 ref ↔ def 互跳 —— 不做**
   M1 只渲染,不加"跳转来源"状态与命中/事件处理路径,零增量,并与"任务列表勾选框不可点击"的既有口径保持一致。

4. **字号缩放持久化 —— M1 不做,纯内存态**
   避免 M1 阶段提前引入 `state.ini` 的常驻配置对象和写盘路径(哪怕只多一个键,也是提前把 M2 的模块拉到 M1)。缩放随进程退出即丢,持久化随 M2 窗口状态记忆一起做。

5. **图片 LRU 计量口径 —— 解码后像素缓冲字节数(`width×height×4`),且解码时按上限降采样**
   这是本轮对内存影响最直接的一条:按压缩文件大小计量会被"小 JPEG 解码出巨幅位图"击穿预算,按解码后像素字节数计才是软件渲染架构下的真实私有内存口径。同时不等解码完再判断超限,而是解码阶段就用 `IWICBitmapScaler` 边解码边缩小到 `kMaxDecodedDimension`,用固定上限严格封死单张图片的最坏内存,而不是靠事后淘汰兜底。

6. **相对 `.md` 链接跳转 —— 复用当前窗口(不新开进程)**
   本轮对内存影响第二大的一条:新开进程意味着内存随点击次数线性累积(在互相链接的文档集里点几次链接就是几个 markair 进程同时占着内存);复用当前窗口在跳转时释放旧文档的 `Document`/`BlockLayoutEngine`/图片缓存,任意时刻只有一份文档状态常驻。**这不推翻裁决 #6**:裁决 #6 讲的是"从资源管理器双击文件"这个入口场景,继续保持每次双击一个独立窗口/进程;本条只管"已经打开的文档内部点链接跳转"这一种更细的场景,两者并存不冲突,顺带让 M2 的"历史前进后退"有了明确的落地对象(单窗口内的导航栈)。

7. **`Ctrl+F` 查找范围 —— 正文/代码块/链接 URL/alt 文本均参与,大小写不敏感且无开关**
   这几类文本都已经在 `Inline`/`LinkTarget` 里,查找是一次性扫描不建索引,参与与否对内存**零差异**;选"参与"是因为这样更符合"查找找得到"的直觉,而不是省内存的结果。真正体现"怎么省内存怎么来"的是后半句:**不做全字匹配/区分大小写的开关**——省一份 UI 状态和一次分支判断,也符合 §9"内容区保持极简"的口径。YAML front matter 因为从未进入文档模型,天然不参与,不需要额外处理。
