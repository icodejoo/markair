# 性能测量工具清单（T15）

本文档记录 M0 性能测量所依赖的外部工具及其版本号，保证测量结果可复现。
按 `05-m0-tasks.md` 要求，所有工具需在开工前装好并在此记录版本号。

## 检测结果（2026-09-16，本机）

在 PATH 与常见安装目录（`Program Files`、`Program Files (x86)`）中检测，
以下工具**均未安装**：

| 工具 | 当前状态 | 用途 |
|---|---|---|
| VMMap | 未安装 | 精确测量 Private Working Set / Private Bytes / Mapped File 分项，验证字体资源落在 Mapped File 而非 Private |
| RAMMap | 未安装 | `RAMMap64.exe -Et` 清空 Empty Standby List，用于模拟冷启动条件 |
| Process Monitor | 未安装 | 辅助排查文件/注册表 I/O，冷启动分析时定位实际磁盘读取路径 |
| PresentMon | 未安装 | 捕获 Present 帧时间序列，评估滚动时的 99 分位帧时间 |

`bench/run_bench.ps1` 已针对 RAMMap 缺失做了优雅降级：检测不到时打印警告并跳过
清 standby list 步骤，继续测量，不会报错退出（此时的"冷启动"数据不是真正冷启动）。

## 获取方式（待安装时参考，无需现在下载）

### VMMap / RAMMap / Process Monitor（Sysinternals 套件）

- 官方下载页：<https://learn.microsoft.com/sysinternals/downloads/>
- 可单独下载对应工具的 zip，或下载整套 `SysinternalsSuite.zip`。
- 也可通过 winget 安装（若本机已装 winget）：
  ```
  winget install Microsoft.Sysinternals.VMMap
  winget install Microsoft.Sysinternals.RAMMap
  winget install Microsoft.Sysinternals.ProcessMonitor
  ```
- 安装/解压后建议放到固定目录（例如 `C:\Tools\Sysinternals\`）并加入 PATH，
  方便 `run_bench.ps1` 通过 `Get-Command` 自动定位。

### PresentMon

- 官方仓库（Intel GameTechDev）：<https://github.com/GameTechDev/PresentMon>
- 从 Releases 页面下载对应版本的 `PresentMon.exe`（免安装，单文件）。

## 版本号记录（安装后填写）

| 工具 | 版本号 | 安装日期 | 记录人 |
|---|---|---|---|
| VMMap | _待填写_ | _待填写_ | _待填写_ |
| RAMMap | _待填写_ | _待填写_ | _待填写_ |
| Process Monitor | _待填写_ | _待填写_ | _待填写_ |
| PresentMon | _待填写_ | _待填写_ | _待填写_ |

> 填写方式：工具都是免安装的单文件可执行程序，右键属性 -> 详细信息 -> 产品版本，
> 或运行 `(Get-Item <exe路径>).VersionInfo.ProductVersion` 获取版本号后填入上表。
