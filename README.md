# markair

Windows 专用、只读的极轻量 Markdown 查看器。双击 `.md` 秒开，常驻内存 15MB 级 —— 比目前实测最好的第三方方案（mdviewer，约 95MB）再低 6 倍以上。

## 安装

下载发布 zip → 解压到任意目录 → 双击 `markair.exe` 即用。**无需安装、无需管理员权限、不写注册表**（除非显式执行 `--register`，见下）。

**首次运行的 SmartScreen 提示**：markair 未购买代码签名证书（成本与体量不匹配），首次运行 Windows SmartScreen 会拦截并提示"Windows 已保护你的电脑"。这是未签名 exe 的正常表现，不是病毒告警：点击"更多信息" → "仍要运行"即可继续。

## 文件关联

`markair.exe --register` 与 `markair.exe --unregister` 一次性处理以下 **5 个扩展名**：`.md`、`.markdown`、`.mdown`、`.mkd`、`.mdtext`。

- 退出码：**0 成功，1 失败**（包括 `--register` 与 `--unregister` 同时给出这种歧义场景）。
- 两个开关都必须在没有其他 markair 窗口打开时执行（一次性动作，不弹窗，结果通过控制台输出）。
- **"注册"只是把 markair 加进该扩展名的"打开方式"候选列表，不等于让 markair 成为默认程序**。现代 Windows（8 及以上）的默认程序关联受 `UserChoice` 注册表键的哈希保护，第三方程序无法（也不应该）绕过它去抢占默认程序——任何声称能自动设为默认程序的工具都是在撒谎或使用非法手段。若要把 markair 设为某扩展名的默认程序，请手动执行：设置 → 应用 → 默认应用 → 按文件类型指定默认应用 → 找到对应扩展名 → 选择 markair。

## 卸载

先执行一次 `markair.exe --unregister`（清理文件关联的注册表项），再直接删除 markair 所在目录即可，注册表零残留。

## 快捷键

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

## 已知限制

- **暂不支持 Windows 高对比度模式**：当前 markair 针对标准浅色/深色主题优化，在系统高对比度模式下的显示效果和易用性无法保证。
- 默认自动加载网络图片（`load_remote_images=1`）：文档打开后网络图片会在首屏之后自动发起下载。想恢复"零主动网络请求"，在 `state.ini` 里手动设 `load_remote_images=0`——关闭后网络图片显示占位块，点击占位块仍可单次加载该图。
- 只读，不可编辑。
- SVG 图片走 [lunasvg](https://github.com/sammycage/lunasvg) 离线栅格化（vendored，见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)），支持 shape / gradient / clip-path / mask；**不支持 filter**（`feGaussianBlur` 等特效，图标级场景通常用不到）。

## 系统要求

Windows 10 / 11，x64。

## 许可

- markair 自身代码：MIT License，见根目录 [LICENSE](LICENSE)。
- 第三方组件（md4c、lunasvg 等）的许可与版本信息：见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。

## 参与开发

设计文档索引、技术选型结论、范围裁决历史见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。
