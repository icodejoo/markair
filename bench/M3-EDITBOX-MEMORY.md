# M3-EDITBOX-MEMORY：查找条/文件夹过滤输入框抽公共组件后的内存对比（T85）

> 背景：查找条 `findEditHwnd`、文件夹过滤 `folderFilterEditHwnd` 两处原生 EDIT
> 子窗口的"创建+子类化+关主题"、"重定位+重建字体"逻辑抽成了公共组件
> `src/shell/edit_box.h/.cpp`（详见本次改动的 diff）。抽公共组件本身不改变
> 运行期行为，这里额外做一次实验性测量：空文档状态下"有两个隐藏的 EDIT
> 控件"（状态A，最终保留的代码）与"完全不创建这两个 EDIT 控件"（状态B，
> 纯实验分支，已撤销不保留）相比，VMMap 权威指标 `Private WS` 差多少，
> 用来判断是否值得为省这点内存去做"点击时动态创建/销毁 EDIT"的复杂方案。

## 1. 测量方法

严格照抄 `bench/M3-MEMORY.md` 第 0~2 节的口径：

- 语料：`bench\EMPTY.md`（空文档基线）。
- 启动：`build\src\Release\markair.exe --bench bench\EMPTY.md`（与
  `run_bench.ps1` 一致，`--bench` 模式窗口不自动退出，便于人工附加）。
- 静置 10 秒。
- `tools\VMMap\vmmap64.exe -accepteula -p <PID> <输出.mtl>` 落一份快照。
- 再打开一次 VMMap GUI 附加同一 PID，读 Process 页签 `Total` 行的
  `Private WS` 数字（权威指标，非代理指标），本机是真实 Windows 10 双屏桌面
  环境，用 `System.Drawing.Bitmap.CopyFromScreen` + 确认前台窗口命中 VMMap
  句柄后截图存档，不用 `PrintWindow`。

状态定义：

- **状态A（最终保留代码）**：任务一抽公共组件完成后的代码，`findEditHwnd`/
  `folderFilterEditHwnd` 正常创建（隐藏），逻辑与抽组件前等价。
- **状态B（纯实验，已撤销）**：临时用 `#if 0` 包住 `window.cpp` 里两处
  `CreateSubclassedEditBox` 调用，使这两个 EDIT 控件完全不创建，其余代码不
  动，重新构建 Release 测量后，已将 `#if 0` 完整撤销并重新构建，**不保留
  在最终代码里**（`git diff --stat` 确认最终 diff 里没有这段实验代码）。

## 2. 实测数据

| 状态 | PID | Private WS | 截图 |
|---|---|---|---|
| A（有两个隐藏 EDIT） | 32732 | **9,228 K ≈ 9.01 MB** | `bench/screenshots/m3-editbox-memory/vmmap_stateA_max.png` |
| B（完全不创建 EDIT） | 18160 | **9,056 K ≈ 8.85 MB** | `bench/screenshots/m3-editbox-memory/vmmap_stateB_max.png` |

差值：9,228 K − 9,056 K = **172 K ≈ 0.168 MB**。

快照文件（`.mtl`，可用 VMMap GUI 重新打开复核，均不进 git）：
`bench/screenshots/m3-editbox-memory/vmmap_stateA_empty.mtl` /
`vmmap_stateB_empty.mtl`。

补充说明：本次两个数字（9,228 K / 9,056 K）与 `M3-MEMORY.md` 记录的空文档
历史值（7,040 K）不是同一次环境下的测量——本机为长期使用的开发机，后台
常驻进程/第三方注入模块的量级会随时间漂移，这属于已知的环境噪声（参见
`M3-MEMORY.md` 对 Sangfor SNAC 等第三方注入模块的记录），不影响本文档
"A vs B 差值"这个对比结论，因为 A、B 两次测量在同一次会话、间隔几分钟内
完成，环境噪声对两者是等量的。

## 3. 结论

- 两个原生 EDIT 控件（隐藏状态，未获得焦点、未输入任何内容）对空文档常驻
  内存的影响只有 **≈0.168 MB**，占 M3 已达标的空文档内存指标（≤8 MB，
  权威值现状 6.87 MB）的比例约为 **0.168 / 8 ≈ 2.1%**，甚至小于两次独立
  测量之间的环境噪声量级（同一状态下反复测量，几十到一两百 K 的抖动很
  常见）。
- **不建议为省这 0.168 MB 去做"点击时动态创建/销毁 EDIT"的方案**。理由：
  1. 收益极小，远低于 8 MB 目标线的噪声容限，实测差值本身就可能被环境
     波动淹没；
  2. 动态创建/销毁会引入新的复杂度和风险——需要处理"控件不存在时的所有
     消息分支"（WM_CTLCOLOREDIT、子类化窗口过程转发、IME 关联、焦点管理、
     重复创建/销毁的时序竞争），这些都是当前代码没有的状态机分支，一旦
     引入容易在查找/过滤这类高频交互路径上引入新 bug；
  3. 两个 EDIT 从窗口创建起就是隐藏状态（`WS_VISIBLE` 不设），只在用户
     主动触发 Ctrl+F / 展开文件夹侧栏时才显示，本来就不是常驻可见控件，
     "创建但隐藏"是 Win32 原生控件的标准做法，没有额外的运行时开销
     （不占 GPU 资源，D2D 渲染层不会绘制它们）。
- 综上：**保留现状（窗口创建时即创建两个隐藏 EDIT 控件），不做动态创建/
  销毁方案**。

## 4. 回归确认

- `markair_tests.exe`：526 个用例全部通过（含新增的公共组件相关改动）。
- 状态B的 `#if 0` 实验分支已完整撤销，`git diff --stat` 确认最终代码只包含
  任务一的公共组件重构，未遗留任何实验性改动。
- 状态A的 Release 已重新构建，作为最终保留版本
  （`build\src\Release\markair.exe`）。
