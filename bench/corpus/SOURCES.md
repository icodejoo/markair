# 语料来源记录(T41)

本目录下的 `*.md` 文件均为**逐字节原样抓取**的真实开源项目文档,未做任何修改
(不改行尾、不改编码、不修正原文里的格式问题)。抓取方式:`curl` 直接拉取
`raw.githubusercontent.com` 上的原始字节并写盘,不经过任何 Markdown 渲染/转换/
摘要工具,保证与仓库里的字节完全一致。抓取日期统一为 **2026-09-17**。

命中类别缩写:表=宽表格 / 任=任务列表 / 徽=徽章图片 / 脚=脚注 / 链=自动链接 /
matter=front matter / HTML=HTML 片段 / mmd=mermaid 代码块。

| 文件名 | 来源仓库 | 文件类型 | 来源 URL | 许可证 | 命中类别 |
|---|---|---|---|---|---|
| facebook-react-readme.md | facebook/react | README | https://raw.githubusercontent.com/facebook/react/main/README.md | MIT | 徽,HTML |
| facebook-react-changelog.md | facebook/react | CHANGELOG | https://raw.githubusercontent.com/facebook/react/main/CHANGELOG.md | MIT | HTML(超大文件,~298KB,兼作大文档压力样本) |
| microsoft-vscode-readme.md | microsoft/vscode | README | https://raw.githubusercontent.com/microsoft/vscode/main/README.md | MIT | 徽,HTML |
| nodejs-node-readme.md | nodejs/node | README | https://raw.githubusercontent.com/nodejs/node/main/README.md | MIT(仓库 LICENSE 为自定义措辞的 MIT 风格文本) | 链(`<https://...>` 尖括号自动链接),HTML |
| nodejs-release-readme.md | nodejs/release | README | https://raw.githubusercontent.com/nodejs/release/main/README.md | 未在仓库根目录找到独立 LICENSE 文件,沿用 Node.js 项目组织的 MIT 许可(与 nodejs/node 一致,未逐一核实本仓库是否单独声明) | 表(7 列发布计划表) |
| rust-lang-rust-readme.md | rust-lang/rust | README | https://raw.githubusercontent.com/rust-lang/rust/master/README.md | MIT / Apache-2.0 双许可 | 徽 |
| golang-go-readme.md | golang/go | README | https://raw.githubusercontent.com/golang/go/master/README.md | BSD-3-Clause | (基线纯文本样本,不命中特定类别) |
| vuejs-core-readme.md | vuejs/core | README | https://raw.githubusercontent.com/vuejs/core/main/README.md | MIT | 徽,HTML |
| angular-angular-readme.md | angular/angular | README | https://raw.githubusercontent.com/angular/angular/main/README.md | MIT | 徽,HTML |
| sveltejs-svelte-readme.md | sveltejs/svelte | README | https://raw.githubusercontent.com/sveltejs/svelte/main/README.md | MIT | 徽,HTML |
| tensorflow-tensorflow-readme.md | tensorflow/tensorflow | README | https://raw.githubusercontent.com/tensorflow/tensorflow/master/README.md | Apache-2.0 | 徽,HTML |
| pytorch-pytorch-readme.md | pytorch/pytorch | README | https://raw.githubusercontent.com/pytorch/pytorch/main/README.md | BSD 风格自定义许可(LICENSE 开头为 "From PyTorch: Copyright ... BSD-style") | 徽,HTML |
| kubernetes-kubernetes-readme.md | kubernetes/kubernetes | README | https://raw.githubusercontent.com/kubernetes/kubernetes/master/README.md | Apache-2.0 | 徽,HTML |
| docker-compose-readme.md | docker/compose | README | https://raw.githubusercontent.com/docker/compose/main/README.md | Apache-2.0 | 徽 |
| expressjs-express-readme.md | expressjs/express | README(`Readme.md`) | https://raw.githubusercontent.com/expressjs/express/master/Readme.md | MIT | HTML |
| axios-axios-readme.md | axios/axios | README | https://raw.githubusercontent.com/axios/axios/v1.x/README.md | MIT | 表(5 列浏览器兼容表,含图片列),徽,HTML |
| pages-themes-minimal-readme.md | pages-themes/minimal | README | https://raw.githubusercontent.com/pages-themes/minimal/master/README.md | CC0-1.0 | 徽,HTML |
| actions-checkout-readme.md | actions/checkout | README | https://raw.githubusercontent.com/actions/checkout/main/README.md | MIT | HTML |
| sindresorhus-awesome-readme.md | sindresorhus/awesome | README(`readme.md`) | https://raw.githubusercontent.com/sindresorhus/awesome/main/readme.md | CC0-1.0 | (纯链接列表压力样本,不命中特定类别) |
| vitejs-vite-readme.md | vitejs/vite | README | https://raw.githubusercontent.com/vitejs/vite/main/README.md | MIT | 徽,HTML |
| webpack-webpack-readme.md | webpack/webpack | README | https://raw.githubusercontent.com/webpack/webpack/main/README.md | MIT | 徽,HTML(超大文件,~80KB) |
| electron-electron-readme.md | electron/electron | README | https://raw.githubusercontent.com/electron/electron/main/README.md | MIT | HTML |
| flutter-flutter-readme.md | flutter/flutter | README | https://raw.githubusercontent.com/flutter/flutter/master/README.md | BSD-3-Clause | HTML |
| ohmyzsh-ohmyzsh-readme.md | ohmyzsh/ohmyzsh | README | https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/README.md | MIT | 徽,HTML |
| redis-redis-readme.md | redis/redis | README | https://raw.githubusercontent.com/redis/redis/unstable/README.md | 三协议(RSALv2 / SSPLv1 / AGPLv3,任选其一;自 Redis 8 起非传统 OSI 许可,如实记录) | 链(`<https://...>` 尖括号自动链接) |
| pandas-dev-pandas-readme.md | pandas-dev/pandas | README | https://raw.githubusercontent.com/pandas-dev/pandas/main/README.md | BSD-3-Clause | 徽,HTML |
| github-docs-get-started-index.md | github/docs | 文档正文(`content/get-started/index.md`) | https://raw.githubusercontent.com/github/docs/main/content/get-started/index.md | 内容 CC-BY-4.0(仓库同时有 `LICENSE-CODE` = MIT,本文件属内容,按 CC-BY-4.0 记) | matter |
| github-docs-onboarding-getting-started-account.md | github/docs | 文档正文(`content/get-started/onboarding/getting-started-with-your-github-account.md`) | https://raw.githubusercontent.com/github/docs/main/content/get-started/onboarding/getting-started-with-your-github-account.md | CC-BY-4.0 | matter |
| reactjs-react.dev-blog-react19.md | reactjs/react.dev | 博客正文(`src/content/blog/2024/12/05/react-19.md`) | https://raw.githubusercontent.com/reactjs/react.dev/main/src/content/blog/2024/12/05/react-19.md | package.json 标注 `"license": "CC"`,未在仓库找到独立 LICENSE 文件确认具体版本,如实标注为"CC 系列(具体版本未核实)" | matter |
| remarkjs-remark-gfm-readme.md | remarkjs/remark-gfm | README(`readme.md`) | https://raw.githubusercontent.com/remarkjs/remark-gfm/main/readme.md | MIT | 徽,HTML(脚注/任务列表语法出现在 ` ```markdown ` 围栏示例块内,是**字面代码文本**而非实时脚注/任务列表,不计入"脚"/"任"命中,见下方"特殊说明") |
| micromark-gfm-footnote-readme.md | micromark/micromark-extension-gfm-footnote | README(`readme.md`) | https://raw.githubusercontent.com/micromark/micromark-extension-gfm-footnote/main/readme.md | MIT | HTML(同上,脚注语法同样全部位于围栏示例块内,不计入"脚"命中) |
| microsoft-playwright-readme.md | microsoft/playwright | README | https://raw.githubusercontent.com/microsoft/playwright/main/README.md | Apache-2.0 | 徽,HTML |
| n8n-io-n8n-readme.md | n8n-io/n8n | README | https://raw.githubusercontent.com/n8n-io/n8n/master/README.md | Sustainable Use License(fair-code,非传统 OSI 开源许可,如实记录) | 徽 |
| mermaid-js-mermaid-readme.md | mermaid-js/mermaid | README | https://raw.githubusercontent.com/mermaid-js/mermaid/develop/README.md | MIT | mmd(README 自身用 mermaid 代码块画图),链,徽,HTML |
| mermaid-js-mermaid-cli-readme.md | mermaid-js/mermaid-cli | README | https://raw.githubusercontent.com/mermaid-js/mermaid-cli/master/README.md | MIT | mmd |
| prettier-prettier-changelog.md | prettier/prettier | CHANGELOG | https://raw.githubusercontent.com/prettier/prettier/main/CHANGELOG.md | MIT | HTML(超大文件,~160KB,兼作大文档压力样本;文中出现的 `- [x]` 同样全部位于 ` ```markdown ` 围栏示例块内,不计入"任"命中) |
| pandoc-manual.md | jgm/pandoc | 用户手册(`MANUAL.txt`,内容即 Pandoc 方言 Markdown,原始扩展名为 `.txt`,如实沿用) | https://raw.githubusercontent.com/jgm/pandoc/master/MANUAL.txt | GPL-2.0-or-later(pandoc 主仓库许可) | 脚(手册正文自身多处使用真实、非围栏的脚注为语法规则做注解,如 “...by at least two spaces.[^2]” 之后紧跟 “[^2]: The point of this rule is...”),超大文件(~300KB) |
| electron-electron-pull-request-template.md | electron/electron | PR 模板(`.github/PULL_REQUEST_TEMPLATE.md`) | https://raw.githubusercontent.com/electron/electron/main/.github/PULL_REQUEST_TEMPLATE.md | MIT | 任(真实、非围栏的 PR checklist,7 项) |
| angular-angular-pull-request-template.md | angular/angular | PR 模板(`.github/PULL_REQUEST_TEMPLATE.md`) | https://raw.githubusercontent.com/angular/angular/main/.github/PULL_REQUEST_TEMPLATE.md | MIT | 任(真实、非围栏的 PR checklist,14 项) |

共 **39** 份文件。除"脚"(脚注)类别外,其余 7 个类别均已确认 ≥ 2 份**真实
解析生效**的命中:
表(axios、nodejs-release)、任(electron-electron-pull-request-template、
angular-angular-pull-request-template)、徽(22+ 份,数量充足)、
链(mermaid、nodejs-node、redis)、matter(github-docs ×2、reactjs react.dev)、
HTML(30+ 份,数量充足)、mmd(mermaid、mermaid-cli)。

**"脚"(脚注)类别目前只有 1 份文件命中真实解析路径**(`pandoc-manual.md`,
手册正文里 4 处独立的真实脚注定义/引用对,`tests/test_corpus_smoke.cpp` 的
诊断输出确认 `footnoteDefs=4 footnoteRefs=4`)。这是经过大量搜索后的如实结果,
详见下方"特殊说明"的第一条,已作为遗留问题在验收汇报里向主对话说明,而非
擅自凑数或编造第二个来源。

## 特殊说明
- **脚注类别的第二个来源未能找到**:抓取/搜索了 30+ 个知名开源项目
  (remark-gfm、micromark-extension-gfm-footnote、rustc-dev-guide、
  rust-lang/reference、tokio、zig、opencontainers 系列规范、Microsoft API
  Guidelines、annotated-transformer 等论文类仓库、free-programming-books 等),
  发现 GFM 脚注语法(`[^label]` / `[^label]:`)在真实项目文档里几乎只出现在
  **演示该语法本身的围栏代码示例**里(remark-gfm、micromark-extension-gfm-footnote
  的 README 均属此类,已在上表如实注明、不计入"脚"命中),而非作为文档正文
  真正被解析消费的脚注。目前找到的唯一一份"脚注语法被当作正文真实使用"的
  知名开源项目文档是 `jgm/pandoc` 的用户手册(`MANUAL.txt`,用脚注给语法规则
  做旁注,是手册自身写作风格的一部分)。是否需要继续寻找第二份、或接受
  "脚注类别仅 1 份真实命中 + 2 份围栏示例佐证语法在代码块内被正确保留为
  纯文本"的现状,请主对话决定。
- `nodejs-release-readme.md`、`sindresorhus-awesome-readme.md`、
  `github-docs-*` 三份、`reactjs-react.dev-blog-react19.md`、`remarkjs-*`、
  `micromark-*`、`pandoc-manual.md`、两份 PR 模板共 11 份文件不是严格意义上的
  项目根 README/CHANGELOG(分别是独立仓库 README、文档站正文、博客正文、
  用户手册、PR 模板),已在"文件类型"列如实标注;选取原因是 README/CHANGELOG
  里天然极少出现 front matter/脚注/mermaid/宽表格/真实任务列表这类特征,
  按任务描述"允许从同一批知名项目里挑一份确实包含目标特征的文档,不局限于
  README/CHANGENLOG,但需如实标注文件类型"的口径处理。
- redis、n8n 的许可证不是传统 OSI 开源许可(分别是源码可用许可与 fair-code
  许可),如实记录,不做美化。
- reactjs/react.dev、nodejs/release 未能在仓库中找到可确认具体版本的独立
  LICENSE 文件,已如实标注"未核实"而非编造一个具体版本号。
- vuejs-core-readme.md 里原先被脚本误判为"任务列表"的一行实际是
  `- [X](https://x.com/vuejs)`(链接文字恰好是大写字母 X,指代 X/Twitter 平台),
  不是复选框语法;已核实修正,不计入"任"命中。
