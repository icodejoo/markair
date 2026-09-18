# M3-RELEASE：发布形态验收报告（T80）

> 对应 `08-m3-tasks.md` T80。任务性质是**验证**（静态 CRT 已在
> 根 `CMakeLists.txt:17` 就位），不是实现。

## 0. 环境与验收目标

- 验收目标：`build\src\Release\mdvn.exe`（Release，clean 未强制重建，本次实测直接复用已有构建产物）。
- 本机为真实 Windows 10 Pro 桌面环境（双屏，`SystemInformation.VirtualScreen` 实测
  `5120x1440`，`query session` 确认 console 会话 ID=1 处于 Active 状态），不是"环境不具备"。
- `dumpbin.exe` 定位：本机未装独立 VS IDE，只有 BuildTools，`dumpbin` 不在系统 PATH，
  实际路径：
  `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe`

## 1. dumpbin /dependents —— 导入 DLL 名单

```
Image has the following dependencies:
    d2d1.dll
    DWrite.dll
    IMM32.dll
    ole32.dll
    api-ms-win-core-path-l1-1-0.dll
    KERNEL32.dll
    USER32.dll
    GDI32.dll
    ADVAPI32.dll

Image has the following delay load dependencies:
    SHELL32.dll
    WINHTTP.dll
    dwmapi.dll
    UxTheme.dll
```

**结论：达标。** 导入表 + 延迟加载表里全部是系统 DLL（`d2d1`/`dwrite`/`imm32`/`ole32`/
`kernel32`/`user32`/`gdi32`/`advapi32`/`shell32`/`winhttp`/`dwmapi`/`uxtheme`，以及一个
Windows API Set 转发桩 `api-ms-win-core-path-l1-1-0.dll`，属操作系统本身的一部分，不是
CRT），**没有出现任何 `vcruntime*.dll`/`msvcp*.dll`/`api-ms-win-crt-*.dll`**，证明静态 CRT
链接确实生效，没有隐藏的动态 CRT 依赖漏网。

## 2. dumpbin /exports 与 /headers —— 子系统、体积、时间戳

```
2 subsystem (Windows GUI)
6.00 subsystem version
8664 machine (x64)
6AACDD93 time date stamp Fri Sep 18 14:43:31 2026
```

- 子系统：**WINDOWS**（GUI，数值 2）。达标（不是 CONSOLE，不会弹出多余的控制台窗口）。
- 体积：**360448 字节（约 0.344 MB）**。
- 时间戳：`2026-09-18 14:43:31`（本次实测构建产物的链接时间）。
- `/exports`：无导出符号（exe 本就不对外导出符号，这是预期结果，仅记录留证）。
- 段名：`.data`/`.fptable`/`.pdata`/`.rdata`/`.reloc`/`.rsrc`/`.text`，是标准 MSVC 链接器
  产物的段名（**没有** `UPX0`/`UPX1`/`.aspack`/`.petite`/`.mpress` 等加壳/压缩工具的
  典型段名特征）。

> 口径说明：体积只记录基线，不在本报告里重复定义硬性门禁——那是
> `ci\check_budget.ps1` 的 `HighlightExeSizeThresholdBytes` 参数负责的事，本报告不代替
> 也不冲突。

## 3. 子进程数恒为 0 验证（01 §4 硬指标）

### 3.1 方法说明（先讲清楚测法，避免误判）

用 `Get-CimInstance Win32_Process -Filter "ParentProcessId=<mdvn PID>"` 在整个观察窗口内
持续轮询（间隔 150ms），断言**任何时刻该查询结果都为空**。

**关键澄清**（任务要求必须写清楚）：`ShellExecuteW`（`src\shell\navigate.cpp:309` 打开外链、
`:463` 打开本地图片文件）会让系统默认浏览器/看图软件启动，但这个新进程的父进程是
Windows Shell 代为启动的宿主（explorer.exe 或对应的 Shell 组件），**不是** mdvn.exe 的直接
子进程——`Get-CimInstance Filter "ParentProcessId=<mdvn PID>"` 查不到它，属于设计上的正确
行为，不能因为"点了外链之后系统里出现了新进程"就误判 mdvn 违反了"子进程恒为 0"。

### 3.2 实测结果

| 场景 | 方法 | 结果 |
|---|---|---|
| 打开 BENCH-A.md | 启动进程，观察 4 秒 | 子进程数恒为 0 |
| 打开 BENCH-B.md（图片密集） | 启动进程，观察 4 秒 | 子进程数恒为 0 |
| 打开 BENCH-D.md（10MB 超大文档） | 启动进程，观察 5 秒（放宽 settle 时间） | 子进程数恒为 0 |
| 点一次外链 + 点一次图片 + 按一次 F5 | 见下方 3.3 说明 | 子进程数恒为 0（48+390 次合成点击 + 1 次 F5 全程监控） |
| `--register` | 启动进程，观察至进程自然退出（<6秒） | 子进程数恒为 0；随后执行 `--unregister` 清理残留 |

### 3.3 点击外链/图片的方法局限（如实注明，不夸大结论）

本环境下真实截图两条路径均实测失败，不能用来精确定位链接/图片的像素坐标：

- `Graphics.CopyFromScreen`：报 `The handle is invalid`（即便切到
  `-dangerouslyDisableSandbox` 也一样），推测当前 PowerShell 工具执行所在的
  窗口站/桌面与交互式桌面不完全等价，尽管 `query session` 显示同一个 Active 会话。
- `PrintWindow`：调用返回成功（`ok=True`），但截图内容是**纯白空白**，与本项目
  既有踩坑记录一致（memory: "UI截图验收要用真实截图，不要用 PrintWindow，否则验收
  结果不可信"）——mdvn 走 D2D 软件渲染，`PrintWindow` 对这类窗口截不到真实内容，
  这里只是如实复现了该已知结论，没有拿它的空白截图冒充验收证据。

因此，本次改用**网格扫点点击**（在文档左上角"标题/外链/图片"所在区域按 10~20px
步进撒点，48 点与 390 点两轮，覆盖坐标范围 `x∈[20,300], y∈[20,400]`）+ 全程子进程
轮询的方式验证：无论是否精确命中链接/图片的可点击热区，**mdvn.exe 在任何一次点击后
都没有产生过子进程**。同时对系统级新进程做了前后差异比对，两轮扫点后新出现的进程
都是与本次操作无关的系统噪声（`svchost.exe`/`backgroundTaskHost.exe`/`audiodg.exe`，
父进程分别是系统服务宿主），**没有观测到浏览器/图片查看器被拉起**，说明本次网格扫点
大概率没有精确命中链接/图片的可点击热区（也可能是本沙箱环境对合成鼠标事件的实际
送达存在限制，未能进一步排查到底是哪一种原因）。

**如实结论**：子进程恒为 0 这条硬指标在"点击外链/点击图片"场景下**得到了验证**（大量
合成点击 + F5 全程零子进程），但"点击确实精准激活了外链/图片打开"这一前提本身**没有
被独立证实**（缺少可信截图无法确认点中）。结合代码层证据——`navigate.cpp` 的白名单
判定与 `ShellExecuteW` 调用路径已在既有单测（`test_navigate*` 等）里覆盖验证过——两者
合起来构成"实测 + 代码路径审查"的双重证据，但不等同于"人眼可见的端到端截图验收"。
如需更强的证据，需要在非沙箱化的交互式会话里人工复核一次点击效果（不在本次自动化
范围内）。

## 4. 不加壳、不 UPX 声明

**声明：本次交付流程未对 `mdvn.exe` 执行任何可执行文件压缩或加壳操作。**
构建产物直接来自 CMake + MSVC 链接器（`cmake --build build --config Release`）的输出，
未引入 UPX 或任何其它 exe 压缩/加壳工具，`ci\verify_release.ps1` 的段名特征检查
（第 4 步）也未检测到相关特征。

## 5. 干净系统验证 —— 按裁决 #6，未做

按 `08-m3-tasks.md` 「M3 裁决记录」#6：不建虚拟机、不建新用户账户，
**本报告如实注明：mdvn.exe 未在干净系统（无 Visual C++ Redistributable、无本项目
开发环境残留的独立系统/账户）上验证过。** 已验证的是当前开发机上的行为（含
dumpbin 依赖检查、子进程数观察），不能替代干净系统验证。

## 6. `ci\verify_release.ps1` 自动化 + 双向验证结果

新增 `ci\verify_release.ps1`，自动化上面①②③（dumpbin /dependents 名单检查、
/headers 子系统检查、/exports + 体积/时间戳记录），外加一个不加壳的段名特征检查。

| 运行方式 | 预期 | 实测退出码 |
|---|---|---|
| 正常运行（不加任何注入开关） | 全部通过 | `0` |
| `-InjectForbiddenDllFault`（把 `kernel32.dll` 强行加入禁止名单） | 必然失败 | `1` |
| `-InjectSubsystemFault`（把期望子系统改成必然不匹配的 `CONSOLE`） | 必然失败 | `1` |

三次实测均与预期一致：脚本对着完全正常的 exe 在**未注入故障时通过**，在**故意注入
两类不同的错误期望值时都能正确触发失败**，证明检测逻辑确实在起作用，不是走过场的
空壳判断。

## 7. 总体结论

| 验收项 | 结果 |
|---|---|
| dumpbin /dependents 只含系统 DLL，无 CRT 动态依赖 | 达标 |
| dumpbin /headers 子系统为 WINDOWS | 达标 |
| exe 体积/时间戳记录 | 已记录（360448 字节 / 2026-09-18 14:43:31），不在本报告设新门槛 |
| 子进程数恒为 0（打开 BENCH-A/B/D、F5、`--register`） | 达标，实测通过 |
| 子进程数恒为 0（点外链/点图片场景） | 达标（合成点击 + F5 全程零子进程），但点击是否精准命中热区未获得独立证实，如实注明 |
| 不加壳、不 UPX | 达标（声明 + 段名特征检查双重确认） |
| 干净系统验证 | 按裁决 #6，未做，如实注明 |
| `ci\verify_release.ps1` 双向验证 | 达标，两个方向的故障注入均能正确触发 exit=1 |
