# mdvn

Windows 专用、只读的极轻量 Markdown 查看器。**当前处于立项设计阶段,尚无任何产品代码。**

## 一句话目标

双击 `.md` 秒开,常驻内存 15MB 级 —— 比目前实测最好的第三方方案(mdviewer,约 95MB)再低 6 倍以上。

## 文档索引

| 文档 | 内容 |
|---|---|
| [01-requirements.md](01-requirements.md) | 需求脑暴:用户画像、核心场景、MVP 范围与非目标、**量化性能验收标准**、GFM 支持范围建议 |
| [02-tech-stack.md](02-tech-stack.md) | 技术选型:语言、GUI/渲染层、Markdown 解析库(md4c vs 自研)、代码高亮,含排除理由与核实来源 |
| [03-architecture.md](03-architecture.md) | 架构:模块划分、数据流、**规避 fontdb 式全量字体扫描的具体方案**、内存/启动优化清单 |
| [04-delivery-plan.md](04-delivery-plan.md) | 落地计划:M-1 Spike → M0 → M1 → M2 → M3,每阶段验收标准、风险、性能测量工具与方法 |

## 核心技术结论

- 语言 **C++17(受限子集,禁异常/RTTI/iostream)**
- 渲染 **Win32 + Direct2D + DirectWrite**(不用任何 GUI 框架)
- 解析 **vendored md4c**(MIT,单 .c + 单 .h,零依赖;Qt 6 官方捆绑使用)—— 不自己手写解析器
- 高亮 MVP 不做,M2 自研极简词法高亮(未检索到合适的小型现成 C 高亮库;Lexilla 为备选)

## 待用户确认

见 [04-delivery-plan.md 最后一节](04-delivery-plan.md#需要用户确认的决策点阻塞对应阶段),共 13 项偏好/范围类决策。

## 目录约定

- `spikes/` —— 仅用于验证性 spike 代码,**不属于产品代码**,不进主构建。目前为空。
