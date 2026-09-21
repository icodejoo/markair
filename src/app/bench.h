// markair 的内置性能埋点(T14)。
//
// 记录"进程入口 -> 解析完成 -> 布局完成 -> 窗口创建 -> 首次 Present"这条链路
// 上五个时间点(顺序对应 wWinMain 的实际执行顺序,而非文档里罗列的顺序 ——
// 当前实现是"先解析布局、再建窗口首绘",详见 main.cpp 的调用点注释),
// 外加 GetProcessMemoryInfo 的 PrivateUsage,以机器可读单行 KV 格式输出到
// stderr,供脚本采集(T15 的测量脚本消费方)。
//
// 不加 `--bench` 时,Enable() 不会被调用,所有 Mark 系列函数与 EmitReport 都只做一次
// 布尔判断就返回,没有 QueryPerformanceCounter 调用、没有任何输出。
#pragma once

#include <cstddef>
#include <cstdint>

namespace markair::bench {

/**
 * 命令行参数里识别出的埋点相关信息。
 */
struct ParsedArgs {
    bool benchEnabled;        // 是否出现 "--bench" 标志
    const wchar_t* filePath;  // 第一个非 "--bench"/"--register"/"--unregister" 的参数;没有则为 nullptr
    // T59:文件关联注册/卸载的 CLI 开关。两者互斥,是否同时出现留给调用方
    // (main.cpp)判断并报错——ParseArgs 只做纯粹的标志识别,不产生任何副作用。
    bool registerRequested;    // 是否出现 "--register" 标志
    bool unregisterRequested;  // 是否出现 "--unregister" 标志
};

/**
 * 从 argv 里识别 "--bench"/"--register"/"--unregister" 标志与目标文件路径的
 * 纯函数,不依赖任何全局状态,可脱离 Win32/QueryPerformanceCounter 单独做
 * 单元测试。
 * @param argc 参数个数(含 argv[0] 程序名)。
 * @param argv 参数数组,通常来自 CommandLineToArgvW。
 * @return benchEnabled 表示是否出现过 "--bench";registerRequested/
 *         unregisterRequested 分别表示是否出现过 "--register"/"--unregister";
 *         filePath 是第一个非这三个标志的参数(先出现的优先),没有则为 nullptr。
 * @example
 *   // argv = {L"markair.exe", L"--bench", L"C:\\a.md"}
 *   markair::bench::ParsedArgs a = markair::bench::ParseArgs(3, argv);
 *   // a.benchEnabled == true, a.filePath 指向 L"C:\\a.md"
 */
ParsedArgs ParseArgs(int argc, wchar_t* const* argv);

/**
 * 启用埋点。须在其余 Mark 系列函数与 EmitReport 调用之前调用(通常在
 * wWinMain 里解析出 "--bench" 标志后立即调用一次)。不调用时,全部 Mark
 * 系列函数与 EmitReport 都是空操作。
 * @example if (parsed.benchEnabled) markair::bench::Enable();
 */
void Enable();

// 记录"进程入口"时间点(尽量在 wWinMain 里尽早调用,解析完命令行后即可)。
void MarkProcessStart();
// 记录"文档解析完成"时间点(ParseMarkdown 跑完的那一刻)。
void MarkParseDone();
// 记录"块级布局完成"时间点(BlockLayoutEngine::Relayout 跑完的那一刻)。
void MarkLayoutDone();
// 记录"窗口创建完成"时间点。
void MarkWindowCreated();
// 记录"首次 Present/EndDraw 成功返回"时间点;重复调用只在第一次生效。
void MarkFirstPresent();

// ---------------------------------------------------------------------------
// T76:逐帧重绘耗时埋点(裁决 #2 的"方案 B",只在 --bench 下生效)。
//
// PresentMon 测到的是 DWM 合成上屏的节奏,混杂了"markair 自己的重绘"与"DWM
// 合成延迟 + 输入事件到达节奏"三部分;要判断"软件渲染够不够快",必须单独
// 把 markair 自己那一段测出来。下面三个 Mark 就是干这个的:
//   MarkFrameBegin      -> 一次 WM_PAINT 重绘开始
//   MarkFrameLayoutDone -> 虚拟化/滚动条同步做完、即将调 D2D 绘制
//   MarkFrameEnd        -> D2D EndDraw 返回
// 未 Enable() 时三者都只做一次布尔判断即返回,Release 默认路径零开销。
// ---------------------------------------------------------------------------

// 记录一帧重绘的开始时刻。
void MarkFrameBegin();
// 记录一帧里"虚拟化 + 滚动条同步"阶段结束、D2D 绘制即将开始的时刻。
void MarkFrameLayoutDone();
// 记录一帧 D2D EndDraw 返回的时刻,并把本帧耗时存入内部样本环。
void MarkFrameEnd();

/**
 * 把逐帧耗时样本的分布(样本数 / P50 / P95 / P99 / 最大值,总耗时与其中的
 * D2D 绘制耗时各一组,外加首帧的两个值)以单行 KV 格式输出到 stderr。
 * 仅在 Enable() 被调用过之后才产生任何效果/输出。
 * @example markair::bench::EmitFrameReport();
 */
void EmitFrameReport();

// ---------------------------------------------------------------------------
// 运行期"换文档"分段埋点(排查侧栏切换/文档内链接跳转普遍偏慢的问题)。
//
// 与上面启动期五段埋点是完全独立的一条时间线,覆盖 main.cpp 的
// OpenDocumentInPlace:
//   MarkSwitchBegin        -> 函数入口,旧状态还没释放
//   MarkSwitchReleaseDone  -> 位图/图片缓存/文件映射/文档 Arena 释放完成
//   MarkSwitchFileOpened   -> 新文件 FileMap::Open 完成(读盘)
//   MarkSwitchParseDone    -> LoadMarkdownFile 解析完成
//   MarkSwitchRelayoutDone -> BlockLayoutEngine::Relayout 完成
//   MarkSwitchFolderScanDone -> 仅目录变更触发 OpenAndScanFolder 时调用
// 未 Enable() 时全部只做一次布尔判断即返回。
// ---------------------------------------------------------------------------

// 记录一次换文档开始的时刻。
void MarkSwitchBegin();
// 记录旧状态(位图/图片 Arena/文件映射/文档 Arena)释放完成的时刻。
void MarkSwitchReleaseDone();
// 记录新文件读盘完成(FileMap::Open 返回)的时刻。
void MarkSwitchFileOpened();
// 记录 Markdown 解析完成(LoadMarkdownFile 返回)的时刻。
void MarkSwitchParseDone();
// 记录块级重排完成(Relayout 返回)的时刻。
void MarkSwitchRelayoutDone();
// 记录滚动位置复位(ResetScrollToTop 内部 UpdateVisibleRange,含可见区间
// IDWriteTextLayout 创建与图片解码)完成的时刻。
void MarkSwitchScrollResetDone();
// 记录目录扫描完成(OpenAndScanFolder 返回)的时刻;目录未变更时不调用。
void MarkSwitchFolderScanDone();

/**
 * 把本次换文档各阶段耗时(释放旧状态 / 读盘 / 解析 / 重排 / 可选的目录扫描 /
 * 总计)以单行 KV 格式输出到 stderr。仅在 Enable() 被调用过之后才产生任何
 * 效果/输出。
 * @param folderScanTriggered 本次是否触发了目录扫描(决定是否输出该字段)。
 * @example markair::bench::EmitSwitchReport(false);
 */
void EmitSwitchReport(bool folderScanTriggered);

/**
 * 纯函数:给定 QueryPerformanceCounter 的频率、五个计数值与 PrivateUsage
 * 字节数,格式化成一行机器可读的 KV 文本(以 '\n' 结尾)。不依赖任何全局
 * 状态,可脱离 Win32 单独做单元测试。
 * @param freq QueryPerformanceFrequency 的结果(每秒计数);非正数按 1 处理。
 * @param tProcessStart "进程入口"的计数值。
 * @param tParseDone "解析完成"的计数值。
 * @param tLayoutDone "布局完成"的计数值。
 * @param tWindowCreated "窗口创建完成"的计数值。
 * @param tFirstPresent "首次 Present 完成"的计数值。
 * @param privateBytes 进程私有内存字节数(PrivateUsage)。
 * @param out 输出缓冲区。
 * @param outCap 缓冲区容量(字节),建议 >= 256。
 * @return 写入的字符数(不含结尾 '\0')。
 * @example
 *   char line[256];
 *   markair::bench::FormatReportFromValues(1000, 0, 5, 8, 12, 20, 9'000'000ull,
 *                                       line, sizeof(line));
 */
size_t FormatReportFromValues(int64_t freq,
                               int64_t tProcessStart,
                               int64_t tParseDone,
                               int64_t tLayoutDone,
                               int64_t tWindowCreated,
                               int64_t tFirstPresent,
                               uint64_t privateBytes,
                               char* out, size_t outCap);

/**
 * 读取内部记录的真实时间点 + 当前进程的 PrivateUsage,格式化后输出到
 * stderr。仅在 Enable() 被调用过之后才产生任何效果/输出。
 * @example markair::bench::EmitReport();
 */
void EmitReport();

}  // namespace markair::bench
