# M2-ASSOC：文件关联注册表零残留验收记录（T61）

> 依据 `07-m2-tasks.md` T61：04 的 M2 验收标准原文——"从干净虚拟机安装 → 双击
> md 打开全流程走通，卸载后注册表无残留（用 Process Monitor 对比前后）"。
> 落地为脚本层 + Process Monitor 层两条，本文件如实记录本次实测结果，
> **未做过的项如实注明未做，不为了报告好看而编造**。

## 0. 测量环境

- 机器：本机（Windows 10 Pro 10.0.19045）。
- 构建：`cmake --build build --config Release --target mdvn`，clean 后重新构建
  一次，确保 `build/src/Release/mdvn.exe` 是当前代码（含 T58/T59）的产物。
- exe 路径：`build/src/Release/mdvn.exe`。
- 验证前先手动确认 `HKCU\Software\Classes\mdvn.md` 及五个扩展名的
  `OpenWithProgids\mdvn.md` 均不存在（基线干净，不是从残留状态开始测）。

## 1. 脚本层验收（`ci/verify_assoc.ps1`）

脚本流程：注册前拍快照 → `--register` → 断言 1 个 ProgID 子树 + 5 条
`OpenWithProgids` 全部写入 → `--unregister` → 逐键断言全部消失 → 与注册前
快照做全量 diff。

实测运行两次（验证脚本可重复运行、不因残留误判），结果：

| 运行 | `--register` 退出码 | `--unregister` 退出码 | 脚本整体 exit code |
|---|---|---|---|
| 第 1 次 | 0 | 0 | **0** |
| 第 2 次（验证可重复运行） | 0 | 0 | **0** |

两次运行中，步骤 3（注册后断言）与步骤 5（卸载后断言 + 全量 diff）均全部
`[ OK ]`，无 `[FAIL]`。

**过程中发现并修复的脚本 bug（记录留痕）**：初版脚本用
`$item.GetValue($ProgId, $null) -ne $null` 判断 `OpenWithProgids` 值是否存在，
但该值是 `REG_NONE` 空字节数组，PowerShell 的 `-ne` 在左操作数为数组时会做
逐元素比较，空数组比较结果也是空数组，在 `if()` 里被当假，导致误判"值不存在"。
修正为把 `$null` 放在左边（`$null -ne $value`）强制走标量比较。另外发现用
`& $ExePath --register` 调用后，PowerShell 有时会在 `mdvn.exe`（WIN32 子系统
程序）真正完成注册表写入并退出之前就拿回控制权，导致紧跟着的 `Test-Path`
读到旧状态；改用 `Start-Process -Wait -PassThru` 后未再复现。

## 2. 最终注册表清理状态确认（人工复核，脚本断言之外的独立确认）

脚本两次运行结束后，另外手动执行以下检查，确认最终状态干净：

```powershell
Test-Path "HKCU:\Software\Classes\mdvn.md"                     # False
# 五个扩展名的 OpenWithProgids\mdvn.md 值均不存在（逐一 GetValue 检查为 $null）
```

结果：`mdvn.md` 子树不存在；`.md` / `.markdown` / `.mdown` / `.mkd` / `.mdtext`
五个扩展名的 `OpenWithProgids` 下均没有 `mdvn.md` 这条值。**HKCU 下与本程序
相关的注册表状态已完全还原为验证前的干净状态。**

## 3. Process Monitor 层验收（人工、权威）—— 未执行

`07-m2-tasks.md` T61 要求用 Process Monitor 过滤 `mdvn.exe` 的
`RegSetValue`/`RegCreateKey`/`RegDeleteKey` 事件，确认"注册时写的键集合 ==
卸载时删的键集合"，且"平时打开文档全程零注册表写入"。

**如实说明：ProcMon 本机未安装**（见 `bench/TOOLS.md` 的工具清单，Process
Monitor 一栏当前状态为"未安装"），本次任务**未执行**这一层验收，也没有
生成任何 ProcMon 导出的 CSV。是否安装该工具由用户决定，安装后可补做这一层
（过滤条件与验证方法已在 T61 原文中写清楚，安装后可直接照做）。

## 4. "干净虚拟机"安装验收 —— 未执行

04 的 M2 验收标准要求"从干净虚拟机安装 → 双击 md 打开全流程走通"。**如实
说明：本次任务在开发机（非干净虚拟机）上执行验证，未在干净系统上验证。**
若后续有可用的干净虚拟机环境，应补做一次真实的安装 + 双击打开 + 卸载全流程。

## 5. 小结

- 脚本层（可自动化部分）：**已完成**，`ci/verify_assoc.ps1` 实测两次均
  exit=0，最终注册表状态确认干净。
- Process Monitor 层：**未执行**，本机未装该工具。
- 干净虚拟机层：**未执行**，未在干净系统上验证。

后两项均依赖额外的工具/环境准备，不在本次任务的自动化范围内，如实记录为
待办，不冒充已完成。
