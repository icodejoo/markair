# M1 真实文档回归比对记录(T41)

语料清单与来源见 `bench/corpus/SOURCES.md`(39 份)。

## 测试方式说明

由于 `mdvn.exe` 目前只有 GUI 交互式打开一条路径,没有命令行批量/无窗口模式,
"打开不崩溃"这条验收线改走 `tests/test_corpus_smoke.cpp` 新增的
`CorpusSmoke_AllRealWorldFilesOpenParseLayoutWithoutCrash` 用例:对 39 份语料
逐一跑一次与 `src/app/main.cpp::LoadMarkdownFile` 完全一致的链路——
`FileMap::Open -> DetectEncoding -> SkipFrontMatter -> ParseMarkdown ->
BlockLayoutEngine::Relayout`(不含渲染/窗口,因为渲染需要真实 HWND/D2D 目标,
且本任务范围是"解析/排版不出岔子",不是像素级渲染验证)。

结论:**39/39 全部成功打开、解析、布局,进程不崩溃**(`mdvn_tests.exe` 退出码
0,185 个用例全绿,其中就包含这条新用例;M0 原有 61 个用例、T40 新增用例均
未改动)。此外该用例还断言了 `doc.truncated == false`——**39 份真实文档没有
一份触发 `kMaxNestingDepth`(64 层)/`kMaxDocumentNodeCount`(20 万节点)截断**,
真实 README/CHANGELOG 的规模与嵌套深度远低于这两条安全兜底线。

"人工比对 GitHub 网页版"这一步按项目历史记录里的既定裁决(见 memory.md)
做不到像素级真机验证,改为"检查已知架构限制是否触发 + 检查有没有明显解析
异常(空文档/章节丢失/表格错位/图片链接抓取失败)+ 检查安全截断是否误触发"
这三件事,逐份记录如下。

## 比对记录表

| 文件 | 差异描述 | 分类 | 处理状态 |
|---|---|---|---|
| facebook-react-readme.md | 无异常;`<details>`/`<picture>` 等 HTML 片段按纯文本原样显示(未渲染成折叠面板/多分辨率图片) | 可接受 | 无需处理(既定裁决:M1 明确 `MD_FLAG_NOHTML`,HTML 一律当纯文本) |
| facebook-react-changelog.md | 无异常;超大文件(298KB,2567 块)解析/布局均正常完成,兼作大文档压力样本 | 可接受 | 无需处理 |
| microsoft-vscode-readme.md | 无异常;含 `<picture>`/`<img>` 的深色模式适配图片按纯文本处理 | 可接受 | 同上(HTML 按纯文本) |
| nodejs-node-readme.md | 无异常;332 处 `<https://...>` 尖括号自动链接均被正确识别为 autolink | 可接受 | 无需处理 |
| nodejs-release-readme.md | 无异常;2 张 7 列宽表格(发布计划表)列数与源文件一致,`tableColWidths` 长度匹配 | 可接受 | 无需处理 |
| rust-lang-rust-readme.md | 无异常 | 可接受 | 无需处理 |
| golang-go-readme.md | 无异常(最小基线样本,16 块) | 可接受 | 无需处理 |
| vuejs-core-readme.md | 无异常;`- [X](https://x.com/vuejs)` 是普通链接(链接文字恰为大写字母 X),未被误判为任务列表复选框 | 可接受 | 无需处理(验证了任务列表判定没有对"文字恰好是 X/x"的普通链接产生误报) |
| angular-angular-readme.md | 无异常;含大量 `<picture>`/`<a>` HTML 片段,按纯文本处理 | 可接受 | 同上 |
| sveltejs-svelte-readme.md | 无异常 | 可接受 | 无需处理 |
| tensorflow-tensorflow-readme.md | 无异常;2 张表格(Python/构建矩阵表)解析正常 | 可接受 | 无需处理 |
| pytorch-pytorch-readme.md | 无异常;22 处围栏代码块(含 shell/python 混排)均完整保留 | 可接受 | 无需处理 |
| kubernetes-kubernetes-readme.md | 无异常 | 可接受 | 无需处理 |
| docker-compose-readme.md | 无异常 | 可接受 | 无需处理 |
| expressjs-express-readme.md | 无异常;11 处围栏代码块含多语言混排 | 可接受 | 无需处理 |
| axios-axios-readme.md | 无异常;5 列浏览器兼容表格(含图片列)解析正常,106 处代码块(超大文件,108KB)均正常 | 可接受 | 无需处理 |
| pages-themes-minimal-readme.md | 无异常 | 可接受 | 无需处理 |
| actions-checkout-readme.md | 无异常;15 处围栏代码块(YAML workflow 示例)完整保留 | 可接受 | 无需处理 |
| sindresorhus-awesome-readme.md | 无异常;795 块、706 处链接的超大纯链接列表文档,无解析异常、无截断 | 可接受 | 无需处理(压力样本,验证大量兄弟节点场景下前序数组结构不出错) |
| vitejs-vite-readme.md | 无异常;1 张表格 | 可接受 | 无需处理 |
| webpack-webpack-readme.md | 无异常;7 张表格、58 处图片(徽章为主)、超大文件(80KB)均解析正常 | 可接受 | 无需处理 |
| electron-electron-readme.md | 无异常 | 可接受 | 无需处理 |
| flutter-flutter-readme.md | 无异常;8 处图片(徽章) | 可接受 | 无需处理 |
| ohmyzsh-ohmyzsh-readme.md | 无异常;3 张表格、31 处代码块 | 可接受 | 无需处理 |
| redis-redis-readme.md | 无异常;3 处 `<https://...>` 尖括号自动链接被正确识别 | 可接受 | 无需处理 |
| pandas-dev-pandas-readme.md | 无异常;1 张表格、12 处图片(徽章为主) | 可接受 | 无需处理 |
| github-docs-get-started-index.md | **跳过 front matter 之后正文长度为 0,解析出 0 个块** | 可接受 | 无需处理——核对原文后确认这是一份"纯元数据/正文完全由 Jekyll/Liquid 站点模板渲染"的索引页(`children`/`carousels` 等字段驱动外部渲染),`---...---` 之后原文本身就没有 Markdown 正文,`doc.blocks.Size() == 0` 是**正确**解析结果而非丢内容;`test_corpus_smoke.cpp` 已按"正文非空白才要求块数 > 0"的口径处理,不误判为异常 |
| github-docs-onboarding-getting-started-account.md | 无异常;front matter 被正确跳过,正文 109 块 | 可接受 | 无需处理 |
| reactjs-react.dev-blog-react19.md | 无异常;front matter 被正确跳过,21 处代码块(React 19 迁移代码示例)完整保留 | 可接受 | 无需处理 |
| remarkjs-remark-gfm-readme.md | 文中演示脚注/任务列表/表格语法的 ` ```markdown ` 围栏示例块被**当作纯代码文本**显示,`[^1]`/`- [x]`/`\| a \| b \|` 等语法**未被解析**成真正的脚注/任务列表/表格 | 可接受 | 无需处理——这些语法字面量本来就写在围栏代码块里,是"演示语法长什么样"的示例文本,GitHub 网页版同样把它们渲染成代码块而非活跃元素,mdvn 的行为与预期完全一致,不是围栏代码块解析的缺陷 |
| micromark-gfm-footnote-readme.md | 同上:多处围栏代码块内的脚注示例语法未被解析,按纯文本正确保留 | 可接受 | 无需处理,理由同上 |
| microsoft-playwright-readme.md | 无异常;2 张表格 | 可接受 | 无需处理 |
| n8n-io-n8n-readme.md | 无异常 | 可接受 | 无需处理 |
| mermaid-js-mermaid-readme.md | ` ```mermaid ` 围栏代码块被当作**普通围栏代码块**显示(展示 mermaid 源码文本,不渲染成流程图/时序图),2 处尖括号自动链接被正确识别 | 可接受 | 无需处理——mdvn 需求范围内从未包含"mermaid 图形渲染"这一功能(01-requirements.md/06-m1-tasks.md 均未列出),按代码块处理是唯一合理且与项目裁决一致的行为,GitHub 网页版才会额外渲染成图,这属于"该项目主动裁决不做"的能力差异,不是解析缺陷 |
| mermaid-js-mermaid-cli-readme.md | 同上:3 处 mermaid 围栏代码块按纯代码块显示 | 可接受 | 无需处理,理由同上 |
| prettier-prettier-changelog.md | 文中演示任务列表格式化效果的 ` ```markdown ` 围栏示例块里的 `- [x]` 未被解析成任务列表(与 remark-gfm 情况相同);超大文件(160KB,1754 块、178 处代码块)解析/布局均正常 | 可接受 | 无需处理,理由同 remark-gfm |
| pandoc-manual.md | 无异常;手册正文里 4 处真实(非围栏)脚注定义/引用均被正确解析为 `FootnoteDef`/`kInlineFlagFootnoteRef`(`footnoteDefs=4 footnoteRefs=4`),304 处代码块(超大文件,300KB)全部正常 | 可接受 | 无需处理——这是当前语料集里**唯一**验证到"脚注在真实正文中被正确解析"的文件,细节见下方"遗留问题" |
| electron-electron-pull-request-template.md | 无异常;7 项真实(非围栏)PR checklist 被正确解析为任务列表项(`taskItems=7`) | 可接受 | 无需处理 |
| angular-angular-pull-request-template.md | 无异常;14 项真实(非围栏)PR checklist 被正确解析为任务列表项(`taskItems=14`) | 可接受 | 无需处理 |

## "必修"项统计

**本轮比对未发现任何"必修"分类项。** 39 份真实文档全部打开/解析/布局成功,
无崩溃、无空指针/越界迹象(否则测试进程会直接崩溃退出而非走到断言),无
`kMaxNestingDepth`/`kMaxDocumentNodeCount` 误触发,唯一一次"解析出 0 个块"
(`github-docs-get-started-index.md`)经核对原文确认是文档本身正文为空、
不是解析缺陷。凡是"内容看起来没有被渲染成 GitHub 网页版那样的活跃元素"的
情况(HTML 片段、mermaid 图、围栏代码块里的语法示例文本),经核对均属于
mdvn 已在需求/架构文档里明确裁决的行为边界(HTML 按纯文本 / 不做 mermaid
图形渲染 / 围栏代码块内容不做二次解析),按任务说明的口径统一分类为"可接受"。

## 遗留问题(需要主对话决策)

**脚注(脚注定义/引用,GFM `[^label]` 语法)类别目前只有 1 份文件
(`pandoc-manual.md`)验证到"脚注在真实正文中被正确解析"这条路径**,详见
`bench/corpus/SOURCES.md` 的"特殊说明"第一条:经过对 30 余个知名开源项目
(remark-gfm、micromark 系列扩展、rustc-dev-guide、rust-lang/reference、
tokio、zig、多份规范文档、机器学习论文类仓库等)的搜索,GFM 脚注语法在真实
项目文档里几乎只作为"演示该语法本身"的围栏代码示例出现(remark-gfm、
micromark-extension-gfm-footnote 的 README 均属此类),而非作为正文被真正
解析消费。`pandoc-manual.md` 是目前找到的唯一反例。

这不属于"必修"缺陷(解析器对已找到的脚注语法处理完全正确,
`footnoteDefs=4 footnoteRefs=4` 与手册原文一致),而是**语料覆盖广度**的
遗留问题,有三种处理方向,需要主对话选择:
1. 接受现状——脚注类别 1 份真实命中 + 2 份围栏示例佐证"围栏代码块内的脚注
   语法被正确当作纯文本、不会被误解析",作为该类别的验收证据;
2. 继续投入搜索寻找第二份真实命中的知名开源项目文档;
3. 放宽"知名开源项目"的口径(如接受某个中小型但活跃的开源项目文档)以更快
   凑齐第二份。

在主对话给出决定前,本任务的其余 7 个类别(宽表格/任务列表/徽章图片/自动
链接/front matter/HTML 片段/mermaid)均已各自有 ≥ 2 份真实命中,验收标准里
"必修项全部关闭"这一条已经满足(因为没有发现任何必修项)。

**决策(2026-09-17,用户裁决)**:采纳方向 1——接受现状。GFM 脚注语法在真实
开源项目文档里本就罕见(围栏代码示例远多于真实生产用法),`pandoc-manual.md`
这 1 份真实命中已足以证明脚注定义/引用的解析链路正确
(`footnoteDefs=4 footnoteRefs=4` 与原文一致),不再额外投入搜索第二份。
脚注类别的验收口径就此收敛为"至少 1 份真实命中 + 已确认围栏示例不会被误
解析",T41 到此全部关闭,无遗留问题。
