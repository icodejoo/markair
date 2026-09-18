# mdvn

Windows 专用、只读的极轻量 Markdown 查看器。**M0(骨架)/ M1(基础 GFM 支持)/ M2(文件关联/主题/体验)均已完成,M3(性能打磨与发布)进行中**:429 个自写单测全绿,已推送至 [github.com/icodejoo/mdvn](https://github.com/icodejoo/mdvn)。

## 一句话目标

双击 `.md` 秒开,常驻内存 15MB 级 —— 比目前实测最好的第三方方案(mdviewer,约 95MB)再低 6 倍以上。

## 文档索引

| 文档 | 内容 |
|---|---|
| [01-requirements.md](01-requirements.md) | 需求脑暴:用户画像、核心场景、MVP 范围与非目标、**量化性能验收标准**、GFM 支持范围建议 |
| [02-tech-stack.md](02-tech-stack.md) | 技术选型:语言、GUI/渲染层、Markdown 解析库(md4c vs 自研)、代码高亮,含排除理由与核实来源 |
| [03-architecture.md](03-architecture.md) | 架构:模块划分、数据流、**规避 fontdb 式全量字体扫描的具体方案**、内存/启动优化清单 |
| [04-delivery-plan.md](04-delivery-plan.md) | 落地计划:M-1 Spike → M0 → M1 → M2 → M3,每阶段验收标准、风险、性能测量工具与方法 |
| [05-m0-tasks.md](05-m0-tasks.md) | **M0 可执行任务清单**:文件级任务拆解、每项验收标准、性能测量命令(**已完成**) |
| [06-m1-tasks.md](06-m1-tasks.md) | **M1 可执行任务清单**:基础 GFM 支持(表格/脚注/任务列表/图片/查找等)的文件级任务拆解(**已完成**) |

## 核心技术结论

- 语言 **C++17(受限子集,禁异常/RTTI/iostream)**
- 渲染 **Win32 + Direct2D + DirectWrite**(不用任何 GUI 框架)
- 解析 **vendored md4c**(MIT,单 .c + 单 .h,零依赖;Qt 6 官方捆绑使用)—— 不自己手写解析器
- 高亮 MVP 不做,M2 自研极简词法高亮(未检索到合适的小型现成 C 高亮库;Lexilla 为备选)

## 范围裁决(2026-09-16)

原先的 13 项待确认决策已全部裁决完毕,依据用户授权的原则:

> "mdvn 会增加显著内存的放弃,如果增加一点点换来使用体验就保留。"

结论速览:**不做** mermaid / LaTeX / SVG / emoji 短码 / 网络图片自动加载 / 单实例 / 同目录文件列表 / MSI 安装包 / 自绘标题栏 / 测试框架;**做** 代码高亮(M2 自研,11 语言)/ 深色模式(跟随系统 + 手动)/ 大纲侧栏(M2,默认关)/ 历史前进后退;许可 **MIT**,打包 **绿色单 exe**。

## 系统支持说明

**暂不支持 Windows 高对比度模式**。当前 mdvn 针对标准浅色/深色主题优化，在系统高对比度模式下的显示效果和易用性无法保证。

逐项量化论证见 [01-requirements.md §8](01-requirements.md#8-决策裁决记录-2026-09-16-定稿),各阶段范围收敛见 [04-delivery-plan.md](04-delivery-plan.md)。

## 许可

- mdvn 自身代码：MIT License，见根目录 [LICENSE](LICENSE)。
- 第三方组件（md4c）的许可与版本信息：见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。

## 使用说明

### 安装

下载发布 zip → 解压到任意目录 → 双击 `mdvn.exe` 即用。**无需安装、无需管理员权限、不写注册表**（除非显式执行 `--register`，见下）。

**首次运行的 SmartScreen 提示**：mdvn 未购买代码签名证书（成本与体量不匹配），首次运行 Windows SmartScreen 会拦截并提示"Windows 已保护你的电脑"。这是未签名 exe 的正常表现，不是病毒告警：点击"更多信息" → "仍要运行"即可继续。

### 文件关联

`mdvn.exe --register` 与 `mdvn.exe --unregister` 一次性处理以下 **5 个扩展名**：`.md`、`.markdown`、`.mdown`、`.mkd`、`.mdtext`。

- 退出码：**0 成功，1 失败**（包括 `--register` 与 `--unregister` 同时给出这种歧义场景）。
- 两个开关都必须在没有其他 mdvn 窗口打开时执行（一次性动作，不弹窗，结果通过控制台输出）。
- **"注册"只是把 mdvn 加进该扩展名的"打开方式"候选列表，不等于让 mdvn 成为默认程序**。现代 Windows（8 及以上）的默认程序关联受 `UserChoice` 注册表键的哈希保护，第三方程序无法（也不应该）绕过它去抢占默认程序——任何声称能自动设为默认程序的工具都是在撒谎或使用非法手段。若要把 mdvn 设为某扩展名的默认程序，请手动执行：设置 → 应用 → 默认应用 → 按文件类型指定默认应用 → 找到对应扩展名 → 选择 mdvn。

### 卸载

先执行一次 `mdvn.exe --unregister`（清理文件关联的注册表项），再直接删除 mdvn 所在目录即可，注册表零残留。

### 快捷键

| 操作 | 快捷键 |
|---|---|
| 滚动一行 / 翻页 / 到开头结尾 | 方向键 / `Page Up` `Page Down` / `Home` `End`、鼠标滚轮 |
| 查找 | `Ctrl+F` 打开查找条，`Enter`/`F3` 下一个，`Shift+Enter`/`Shift+F3` 上一个，`Esc` 关闭查找条 |
| 缩放字号 | `Ctrl++` 放大、`Ctrl+-` 缩小、`Ctrl+0` 复位；`Ctrl+滚轮` 效果相同 |
| 切换主题 | `Ctrl+Shift+T`（系统跟随 / 浅色 / 深色三态循环） |
| 大纲侧栏 | `Ctrl+\` 开关（默认关闭） |
| 历史前进 / 后退 | `Alt+←` 后退，`Alt+→` 前进 |
| 重新加载当前文档 | `F5`（不带任何修饰键） |
| 关闭窗口 | `Ctrl+W` 或 `Esc`（查找条打开时 `Esc` 先关查找条） |

### 已知限制

- **暂不支持 Windows 高对比度模式**（见上文「系统支持说明」）。
- 默认不加载网络图片。
- 只读，不可编辑。
- SVG 显示为占位块，不渲染实际内容。

### 系统要求

Windows 10 / 11，x64。

## 目录约定

- `spikes/` —— 仅用于验证性 spike 代码,**不属于产品代码**,不进主构建、不受编码规范约束。当前含 `s02_font_probe.cpp`(验证 DirectWrite 是否会因字体回退配置触发全量字体枚举)。
