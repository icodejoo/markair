# MarkAir 内存占用深度优化空间与技术方案（修订版）

> **修订时间**: 2026-09-21  
> **基线状态**: Post-M3 (498 tests pass, D2D 软件渲染 + Arena 线性分配 + 视口排版虚拟化 + 单例字体子系统)  
> **审查记录**: 本文档已通过 Windows 系统级架构复审（经 Claude Opus 逐行核对代码与实测数据，纠正了 3 处技术与事实硬伤，增补了 2 项核心低成本高收益优化）。

---

## 一、当前内存现状与实测基线

根据 `bench/M3-MEMORY.md` 权威实测、`bench/M3-BENCHD.md` 超大文档测量及多窗口基准，当前 MarkAir 内存呈现以下客观特征：

### 1. 实测基线数据
- **单窗口空载基线**: 约 **6.8 ~ 8.0 MB** Private Bytes (VMMap 实测 Private Working Set 约 3.9 MB)。
- **外部多开场景（Explorer 双击）**: 操作系统拉起独立全新进程，各进程独立加载 CRT 堆、WARP (`d3d10warp.dll`)、DXGI (`dxgi.dll`) 及安全模块，呈严格线性增长，每个新窗口 **+9.5 ~ 10 MB**。
- **进程内多开场景（应用内链接跳转/多顶层窗口）**: 共享 D2D Factory 与 `FontSubsystem`，呈平缓非线性增长，平均每增加一个窗口仅 **+2.5 ~ 3 MB**。
- **超大文档极端场景（BENCH-D, 33,299 块）**: Private Bytes 达到 **176.9 MB**（首屏耗时中位数 2108ms），远超日常文档，存在巨大的优化空间与未查明的提交源。

### 2. 底噪事实与构成分解
VMMap 数据显示，单窗口 ~7MB 的私有提交主要由以下部分构成：
1. **系统与运行期刚性底噪（约 4.0 ~ 5.0 MB）**：
   - DirectWrite 字体子系统映射缓存与字形缓存（由操作系统管理）。
   - Win32 消息循环、CRT 基础堆与第三方企业级注入模块（如安全软件、输入法残留钩子等）。
   - *注：`d3d10warp.dll` 映射为共享代码段，其实际私有页仅 4KB，不构成私有提交的主要负担。*
2. **文档模型与布局几何数据（日常文档约 0.5 ~ 1.5 MB；超大文档超 20 MB）**：
   - md4c 节点与 `StrSlice` 数组。
   - `BlockLayoutEngine::geometries_`：x64 下 `sizeof(BlockGeometry)` 约为 240 字节，33,299 块仅几何基础数据即达 7.6MB。
3. **Arena 预留与提交缓冲区**：
   - 单窗口目前包含 8~10 个 Arena（`docArena` 4MB 保留，`imageArena`/`imageScratch` 各 32MB 保留，`geometryArena_` 32MB 保留，以及多个 16MB 的 Scratch）。

---

## 二、优化方案与技术细节审查

### 优化 1：`Vec<T>` 增加 `Reserve` 消除 Arena 翻倍扩容废弃块 ——【P0 / 极高性价比】

#### 1. 现状痛点
[`src/util/span.h:52`](file:///E:/workspaces/mdvn/src/util/span.h#L52) 的 `Push` 扩容策略为 `capacity_ * 2`，且设计明确说明“旧块留给 Arena 复位时统一处理，这里不主动回收”。
在 [`src/layout/layout.cpp:220`](file:///E:/workspaces/mdvn/src/layout/layout.cpp#L220) 中，排版引擎已预先知晓总块数 `blockCount = doc.blocks.Size()`，但采用逐个 `Push(BlockGeometry{})`：
```cpp
u32 blockCount = doc.blocks.Size();
for (u32 i = 0; i < blockCount; ++i) {
    if (!geometries_.Push(BlockGeometry{})) ...
}
```
对于 BENCH-D 的 33,299 块，数组从容量 8 翻倍扩容至 65,536，历次废弃旧块（合计 65,528 块）全部滞留在 Arena 中并提交为物理页：
$$\text{实际提交} = (65536 + 65528) \times 240\text{B} \approx 23.6\text{ MB}$$
其中 **15.7 MB 是纯浪费**（废弃块 2 倍 + 翻倍过冲 1 倍）。

#### 2. 技术方案
为 `Vec<T>` 增加精确预留接口：
```cpp
bool Reserve(Arena& arena, u32 newCapacity) noexcept {
    if (newCapacity <= capacity_) return true;
    T* newItems = reinterpret_cast<T*>(arena.Alloc(sizeof(T) * newCapacity, alignof(T)));
    if (!newItems) return false;
    for (u32 i = 0; i < size_; ++i) newItems[i] = items_[i];
    items_ = newItems;
    capacity_ = newCapacity;
    return true;
}
```
在 `Relayout` 入口处直接 `geometries_.Reserve(arena, blockCount)`。同理对 `search` 的 `Vec<Match>` 等可预知长度的容器一并应用。

#### 3. 预期收益
改动约 10 行代码，超大文档 **一次性消除 15.7 MB 纯浪费提交**，零架构风险。

---

### 优化 2：Arena 弹性收缩（`ResetAndTrim`）——【P0 / 核心确定性收益】

#### 1. 现状痛点与机制纠错
在 [`src/util/arena.h`](file:///E:/workspaces/mdvn/src/util/arena.h) 中，当前 `Reset()` 仅执行 `used_ = 0`，从不退还已向系统提交的物理页。
- **机制纠错**：`VirtualFree(..., MEM_DECOMMIT)` 后的页面回到 `PAGE_NOACCESS` 保留态。**再次读写会直接触发 Access Violation 崩溃**，并非软缺页！
- **安全契约**：严禁在“窗口失焦”时执行 Trim（因为 `geometryArena_` 上存放着全量活跃几何数据，窗口随时可能重绘）。`Trim` **只能且必须在 `used_ == 0` 时调用**（与 `Reset` 语义严格绑定）。
- **保留阈值调整**：保留阈值定为 **64KB（16 页）**。若设为 1~2MB，单窗口 10 个 Arena 仅保留基线就高达 10~20MB，比初始基线还大。

#### 2. 技术方案
在 [`src/util/arena.h`](file:///E:/workspaces/mdvn/src/util/arena.h) 与 `arena.cpp` 引入 `ResetAndTrim`：
```cpp
void Arena::ResetAndTrim(size_t retainBytes) noexcept {
    if (!base_) return;
    // 页面对齐（4KB 向上取整）
    size_t alignedRetain = (retainBytes + 4095) & ~static_cast<size_t>(4095);
    if (committedSize_ > alignedRetain) {
        size_t decommitSize = committedSize_ - alignedRetain;
        VirtualFree(static_cast<unsigned char*>(base_) + alignedRetain, decommitSize, MEM_DECOMMIT);
        committedSize_ = alignedRetain;
    }
    used_ = 0;
}
```
#### 3. 预期收益
关闭或切换大文档后，提交的物理页立即退还操作系统，彻底杜绝单调不退的高水位锁定。

---

### 优化 3：单实例 IPC 汇聚（基于 `WM_COPYDATA`）——【P0 / 最大实用收益】

#### 1. 现状与选型
- **已有骨架**：[`src/app/main.cpp:192`](file:///E:/workspaces/mdvn/src/app/main.cpp#L192) 已有命名互斥体与 `EnumWindows` 前置同文件窗口机制；共享 `FontSubsystem` 与 `g_d2dFactory` 均已就绪。
- **选型决议**：选用 **`WM_COPYDATA`** 而非命名管道。传递目标文件绝对路径仅需几百字节，由 UI 线程同步派发；命名管道需额外启动工作线程与异步 I/O，线程栈的开销违背节约内存初衷。

#### 2. 必备鲁棒性设计（防踩坑）
1. **启动竞态防御**：`CreateMutexW` 返回 `ERROR_ALREADY_EXISTS` 时，进入带 3 秒超时的 `FindWindowW` 轮询；若超时主窗口仍未就绪，**优雅降级为自身作为新主窗口启动**，杜绝“双击无响应”。
2. **UIPI 权限隔离**：主窗口在创建时调用 `ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr)`，确保低权限进程（如普通 Explorer）能向可能提权运行的主实例发送消息。
3. **前台窗口激活**：次实例退出前先调用 `AllowSetForegroundWindow(mainPid)`，确保主实例 `SetForegroundWindow` 成功穿透前台锁定。
4. **防假死挂起**：向主窗口投递消息必须使用 `SendMessageTimeoutW(hwnd, WM_COPYDATA, ..., SMTO_ABORTIFHUNG, 2000, &res)`，若主实例阻塞挂起，次实例超时后降级自启。
5. **架构代价记录**：显式记录单点故障风险（只读查看器可接受单进程崩溃关闭所有窗口的代价）。

#### 3. 预期收益
外部双击多开彻底收敛至进程内窗口模型，10 个窗口全机内存从 **~95MB 降至 ~30.5MB**（节省超 60%）。

---

### 优化 4：`BlockGeometry` 冷热分离 ——【P1 / 替代分块几何虚拟化】

#### 1. 方案对比
原文档提议的“分块几何虚拟化（Chunked Geometry）”侵入性极高：由于 `totalHeight_ = geometries_[0].bottom`，全局滚动条、命中测试、文本查找高亮、大纲跳转均强依赖全局块坐标，动态预估高度会导致严重的滚动条漂移和跳转错位。

#### 2. 冷热分离落地
`BlockGeometry` 结构体（240 字节）中绝大多数为稀有类型字段：
- 表格专属：`tableColWidths`, `tableRowTops`, `tableHeadRowCount`, `cellWidth`（~48 字节）
- 列表专属：`listMarker*` 5 个字段
- 代码块专属：`codeCopyButton`, `textPad`
- 任务列表：`taskCheckbox`, `taskChecked`

**方案**：借鉴 `Block::detailIdx` 侧表成熟模式，将上述字段拆分到稀有侧表，热几何结构体压至 **~64 字节**。
- 33,299 块基准几何提交：7.6MB $\rightarrow$ **2.0MB**。
- 配合优化 1，总几何提交从 **23.6MB 骤降至 2.0MB**（净减 21.6MB），且不改变任何全局坐标系统与滚动映射逻辑。

---

### 优化 5：`docArena` 预留提升至 64MB ——【P1 / 正确性保障】

[`src/app/main.cpp:613`](file:///E:/workspaces/mdvn/src/app/main.cpp#L613) 目前写死 `docArena.Init(4 * 1024 * 1024)`。
对于 BENCH-D（33,299 个 Block + 十几万个 Inline + 扩容废弃块），4MB 虚拟预留已被突破或处于边缘。由于 `Arena::Alloc` 耗尽静默返回 `nullptr`，可能导致文档末尾被异常截断。
- **改动**：将虚拟地址空间预留提升至 `64 * 1024 * 1024`（仅 Reserve，无物理内存消耗），并在 `Alloc` 失败路径增加可观测的日志/断言。

---

### 优化 6：后台闲置处理（`MEMORY_PRIORITY_LOW`）——【P2】

#### 1. 机制纠偏
`SetProcessWorkingSetSize(-1, -1)` 仅修改 Working Set，完全不影响本项目的考核指标 **Private Bytes / Commit Charge**，且换入时由于脏页写盘会产生磁盘 I/O 级的 Hard Fault 卡顿，属于“指标粉饰”。
#### 2. 正确做法
在窗口最小化时调用：
```cpp
PROCESS_POWER_THROTTLING_STATE powerThrottling = {};
// 或 SetProcessInformation(..., ProcessMemoryPriority, MEMORY_PRIORITY_LOW)
```
告知 Windows 内存管理器在系统物理内存紧张时优先换出本进程页面，避免主动制造磁盘颠簸。

---

## 三、修正后的优先级执行矩阵

| 序号 | 优化任务 | 预期收益 | 复杂度 | 推荐优先级 | 说明 |
| :---: | :--- | :--- | :---: | :---: | :--- |
| **0** | **BENCH-D 176.9MB VMMap 归因** | 找出 150MB 未知内存源头 | 低 | **P0 前置** | 必须先确诊病因，避免盲目重构 |
| **1** | **`Vec::Reserve` 精确预留** | 超大文档消除 **15.7 MB** 废弃块 | 极低（~10行） | **P0** | 最高投入产出比，立即可做 |
| **2** | **Arena `ResetAndTrim` (64KB 保留)** | 彻底解决大文档关闭后高水位锁定 | 低 | **P0** | 严格绑定 Reset，补 4KB 页面对齐 |
| **3** | **单实例 IPC 汇聚 (`WM_COPYDATA`)** | 外部多开全机内存节省 **> 60%** | 低偏中 | **P0** | 补全 UIPI、超时降级与防假死 |
| **4** | **`BlockGeometry` 冷热分离** | 超大文档几何再省 **5.6 MB** | 中 | **P1** | 取代侵入性极高的分块几何虚拟化 |
| **5** | **`docArena` 预留提升至 64MB** | 消除超大文档静默截断风险 | 极低 | **P1** | 虚拟空间免费，保障解析完整性 |
| **6** | **跨窗口共享 Scratch 分配器** | 多窗口下消除重复提交 | 低 | **P2** | 收益与 Trim 重叠，待实测评估 |
| **7** | **闲置内存优先级调低** | 协助系统在物理内存紧张时调度 | 极低 | **P2** | 不影响 Private Bytes KPI |
| **—** | **分块几何虚拟化 (Chunked)** | 侵入性极大，容易导致滚动条跳跃 | 极高 | **暂缓** | 已被冷热分离方案更优覆盖 |

---

## 四、应避免的反模式与架构红线

1. **恪守 D2D 恒定软件渲染底线**：严禁打开硬件加速驱动，坚决杜绝 GPU 驱动 38MB 模块底噪。
2. **严禁使用 Working Set 手段粉饰 Private Bytes**：考核标准钉死在 Committed / Private Bytes。
3. **严禁在渲染关键路径引入常驻文本压缩**：不拿 CPU 滚动帧率换取几百 KB 内存。
4. **绝不绕过 DirectWrite 系统字体缓存**：信赖操作系统级字形管理契约。
