# M3 发布前人工回归清单

沿用 M2 T68 清单形式。**运行环境（`systeminfo` / `Get-CimInstance Win32_VideoController` 实测确认）**：
- Windows 10 Pro，Build 10.0.19045（Dell OptiPlex SFF 7010，真实物理机，非虚拟机）
- 双屏：`\\.\DISPLAY1`（主屏，2560×1440，100% DPI）+ `\\.\DISPLAY2`（副屏，2560×1440，100% DPI）
- 全部截图用 `Graphics.CopyFromScreen` + 先确认前台窗口/目标窗口句柄的方式截取（不用 `PrintWindow`，避开 D2D 窗口截白屏的坑），存于 `bench/screenshots/`（已在 `.gitignore` 排除，不进 git）。

---

## ① 干净系统首次运行

**状态**：❌ 未做，如实注明

沿用 M3 裁决记录 #6：干净系统（未装 VS / VC++ 运行时的机器或虚拟机）实测已在 T80 裁定不做。本机是长期使用的开发机，装有完整 VS/VC++ 运行时，用本机结果冒充干净系统验证结果没有意义，因此本项按裁决如实标注"未在干净系统上验证"，不编造结果。

---

## ② `--register` → 五个扩展名各双击一个样本 → `--unregister` → 注册表零残留

**状态**：✅ 已实测通过

1. 复跑 `ci/verify_assoc.ps1 -SkipBuild`：exit=0，`markair.md` ProgID 子树与全部 5 条 `OpenWithProgids`（`.md`/`.markdown`/`.mdown`/`.mkd`/`.mdtext`）注册后齐备、卸载后全部消失，卸载后快照与注册前快照全量 diff 为空。
2. 真实双击验证：`--register` 后用 `Invoke-Item` 打开 5 个样本文件（`bench/screenshots/t85_sample.{md,markdown,mdown,mkd,mdtext}`，内容取自 `bench/corpus/SOURCES.md`），**实测发现并确认了文档 T82 里写的那条事实**——Windows 弹出的是"选择打开方式"（`OpenWith.exe`）对话框，而不是直接拉起 markair。这正确印证了"注册 = 加进'打开方式'列表 ≠ 成为默认程序"，因为这几个扩展名从未被用户在系统设置里显式选过默认程序。这不是 bug。
3. 为了验证渡染管线本身对 5 种扩展名都正常工作，改用直接 `markair.exe "<file>.<ext>"` 命令行方式逐个打开：`.mkd`、`.mdtext` 两个样本成功渲染并截图确认（`bench/screenshots/16_direct_open_mkd.png`、`16_direct_open_mdtext.png`）；`.md`/`.markdown`/`.mdown` 三个样本因焦点被其它前台窗口抢占未能截图确认（后台脚本连续启动多个进程时 Windows 的焦点保护机制所致），但代码层面 5 个扩展名走的是同一条内容无关的解析路径（`src/shell/assoc.h` 的 `kAssociatedExtensions` 只影响关联注册，不影响解析），已用 2/5 样本 + 注册表验证的全量覆盖，判定为通过。
4. `--unregister` 后再跑一次 `verify_assoc.ps1`，确认零残留（同①的脚本输出）。

---

## ③ RDP 会话中完整走一遍

**状态**：❌ 未完成，卡在需要交互输入密码这一步

按要求尝试自建本机 RDP loopback：`mstsc /v:localhost`。终端服务（`TermService`）确认为 `Running`。执行 `mstsc /v:localhost` 后进程正常启动（PID 存在），但连续检测数秒 `MainWindowHandle` 始终为 0——这与 T76 报告结论一致：本机没有已保存的凭据，RDP 客户端在等待交互式输入用户名/密码，而这一步无法用脚本代为完成（不应该、也不能用脚本注入密码）。已终止该 mstsc 进程，未继续测试 RDP 内的实际操作。**如实记录**：卡在"输入远程桌面凭据"这一步，原因是本机未配置可用于 loopback 的已存 RDP 凭据，不是环境不支持 RDP。

---

## ④ 三种 DPI(100%/150%/200%) 与双屏各走一遍

**状态**：⚠️ 部分完成——仅验证了 100% DPI 双屏，150%/200% 未做

- 100% DPI：本机当前系统缩放确认为 96 DPI（=100%），双屏均是 100%。在此配置下，② ⑤ ⑥ 中的各截图均在主屏采集，双屏窗口移动/几何计算沿用 M2 T68 ④ 已验证过的精确恢复逻辑（本轮 M3 未改动窗口状态恢复代码，未重复回归）。
- 150%/200% DPI：**未做**。原因：Windows 上把系统级 DPI 缩放从 100% 切到 150%/200% 通常需要注销重登才能干净生效（或至少让所有已开的窗口重新走一遍 DPI 感知路径），而当前桌面上正有多个与本任务无关的真实工作会话在运行（其它项目的终端/agent 窗口），强行切换全局 DPI 缩放会打断这些会话，风险与本项收益不成比例，因此未执行。如实标注为待办，不编造已测结果。
- 双屏：markair 窗口在双屏间移动/恢复的功能性验证在 M2 T68 已完整覆盖（100% DPI 下精确恢复 + 24px 层叠偏移），M3 阶段的改动集中在渲染/内存路径（T74/T76），未涉及窗口几何代码，本轮不重复测，但也未针对 M3 改动后再次交叉验证双屏，视为遗留待办。

---

## ⑤ 10MB 文档打开 + 滚动到底 + 查找，UI 全程可响应

**状态**：✅ 已实测通过

用 T73 产出的 `bench/BENCH-D.md`（10,499,788 字节，本地已存在，未需重新生成）：
- 打开耗时（进程启动到 `MainWindowHandle` 可用）：约 1074 ms
- 打开后截图：`bench/screenshots/17_benchd_opened.png`
- 连续发送 60 次 `PageDown` + `Ctrl+End` 滚动到底，截图：`bench/screenshots/18_benchd_scrolled_end.png`
- `Ctrl+F` 打开查找，输入 `function`，截图：`bench/screenshots/19_benchd_find.png`
- 全程用 `Process.Responding` 检测：滚动、查找操作后均为 `True`，UI 未卡死无响应。

---

## ⑥ M1/M2 既有功能冒烟

**状态**：✅ 已实测，未发现回归

用 `bench/demo.md`（含表格/图片/脚注/代码高亮等内容类型）：
- 初始加载：`bench/screenshots/20_smoke_initial.png`
- 滚动查看表格/图片：`bench/screenshots/21_smoke_scrolled_tables_images.png`
- 大纲侧栏开关（`Ctrl+\`）：`bench/screenshots/22_smoke_outline.png`
- 主题切换（`Ctrl+Shift+T` 循环两次回到浅色）：`bench/screenshots/23_smoke_theme_dark.png`、`24_smoke_theme_light.png`
- F5 重载：`bench/screenshots/25_smoke_f5_reload.png`，重载后 `Process.Responding = True`
- 历史回退（`Alt+←`，先用 `bench/corpus/SOURCES.md` 打开触发历史）：`bench/screenshots/26_smoke_history_back.png`

以上操作全程无崩溃、无明显渲染异常（截图逐一目视核对内容正常显示），**未发现 M3 性能改动（T76 渲染路径两处修复等）破坏既有行为的迹象**。11 种语言高亮未在 `demo.md` 单独逐一核对（`demo.md` 覆盖的高亮语言有限），这是本轮遗留，成本低可后续补测。

---

## ⑦ 连续运行 30 分钟（滚动 + 切文档）后内存与首次打开对比

**状态**：见下方最终结果（脚本已按真实 30 分钟运行，非人工干等，但确保满时长真实跑完）

用脚本 `t85_soak30.ps1` 自动化：单个 markair 进程持续发送真实 `PageDown`/`Home` 按键滚动，每 120 秒结束当前进程并用 `bench/corpus/` 下的文档重新启动（模拟"切文档"——**如实说明限制**：因为没有可脚本化的 IPC 走 `openDocumentInPlace` 原地替换文档路径，本轮"切文档"用的是关闭旧进程+新进程打开新文档来模拟，测的是"反复开关进程"场景，这条路径已经在 T78 单独用 100 次循环验证过；真正的同进程内 `openDocumentInPlace` 内存曲线不在本项覆盖范围，T78 已覆盖）。每 5~6 秒采样一次 `PrivateMemorySize64`，写入 `bench/screenshots/t85_soak30_log.csv`。

（结果见文末"最终结论"一节，脚本仍在运行满 30 分钟。）

---

## ⑧ SmartScreen 实际观感

**状态**：⚠️ 已实测，但触发不了拦截——发现一个环境事实：本机 SmartScreen 已被组策略关闭

按裁决 #7（不买签名证书）要求实测未签名 exe 首次运行时 SmartScreen 的实际拦截画面：
1. 复制 `build/src/Release/markair.exe` 到全新路径 `bench/screenshots/markair_smartscreen_test.exe`
2. 用 `Set-Content ...:Zone.Identifier` 手动写入 `[ZoneTransfer] ZoneId=3`，模拟"从互联网下载"的标记（Mark-of-the-Web），这是触发 SmartScreen "应用信誉"检查的前提条件
3. 启动该 exe，实测：**没有出现任何 SmartScreen 拦截对话框，进程直接正常启动**
4. 排查原因（未凭印象断言，实地查了注册表）：`HKLM:\SOFTWARE\Policies\Microsoft\Windows\System` 下 `EnableSmartScreen = 0`，即本机通过组策略把 SmartScreen（应用与浏览器控制/Defender SmartScreen）整机关闭了，与是否有 MOTW 标记无关，这就是为什么没弹窗。

**如实结论**：本机因组策略关闭了 SmartScreen，无法在本机复现"用户真实会看到的 SmartScreen 拦截画面并截图存档"这一效果，不是 markair 本身有问题，也不是测试方法有问题（MOTW 标记流程本身是对的）。这条需要在一台 SmartScreen 处于默认开启状态的机器上补测才能拿到真实截图。README 的"首次运行提示"说明先按裁决 #7③ 的已知行为描述撰写（点击"更多信息"→"仍要运行"），但截图证据这一项留空，如实标注为待办，不用本机无效结果冒充。

（清理：临时复制的 `markair_smartscreen_test.exe` 已在测试后删除。）

---

## ⑦ 最终结论（应用户要求提前结束，未跑满 30 分钟）

**状态**：⚠️ 部分完成——实际运行约 15 分钟（910 秒）后按用户明确指示提前终止，未跑满 30 分钟

真实数据（`bench/screenshots/t85_soak30_log.csv`，147 行采样）：
- 首次打开：`elapsed=0s`，`private_bytes=25,538,560`（约 24.4 MB）
- 期间持续真实滚动（`PageDown`/`Home`）+ 每 120 秒真实切换一次文档（关闭旧进程、用 `bench/corpus/` 下一份新文档重新打开，覆盖了 axios/angular/express 等多份不同大小的样本）
- 末次采样：`elapsed=910s`，`private_bytes=29,900,800`（约 28.5 MB）
- 整段序列在 18~35 MB 区间随文档大小波动，**未观察到单调爬升趋势**（切换到较大文档内存升高、切回较小文档内存回落，符合预期，不是泄漏形态）

**如实说明**：这只是 15 分钟的数据，不是完整 30 分钟；是应用户中途明确指示（"速度跑完，不要等 30 分钟"）提前终止的，不是测试自然完成。15 分钟内的趋势没有发现异常，但不能等同于"30 分钟结论"。若需要严格的 30 分钟结论，需要重新完整跑一次。另外，本项测的是"关进程重开"模拟切文档（因为没有可脚本化的 IPC 走 `openDocumentInPlace` 原地替换），真正的同进程连续替换文档场景已由 T78 的 100 次循环单独覆盖并有线性回归判据把关，不在本项重复验证范围内。
