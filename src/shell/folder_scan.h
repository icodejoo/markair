// markair 文件夹穿透扫描模块。
// 递归遍历指定文件夹，筛选所有 Markdown 目标文件并生成有序文件条目列表。
#pragma once

#include "../util/arena.h"
#include "../util/types.h"
#include "../util/span.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace markair {

// 穿透扫描允许收集的最大文件数量保护上限，防止扫描超大盘符耗尽内存。
constexpr u32 kMaxFolderScanEntries = 2048;

// 递归穿透的最大目录层级深度。
constexpr u32 kMaxFolderScanDepth = 16;

/**
 * 扫描到的单个 Markdown 文件条目。
 */
struct FolderEntry {
    wchar_t fullPath[MAX_PATH];   // 文件的完整物理路径
    wchar_t relPath[MAX_PATH];    // 相对选定根目录的路径（用于树形或分层清晰展示）
    wchar_t fileName[MAX_PATH];   // 文件名部分
    uint64_t sizeBytes;           // 文件字节大小
};

/**
 * 递归扫描根目录下的所有 Markdown 文件。
 * 纯逻辑函数，自动过滤 .git、node_modules 等噪音目录，匹配标准 Markdown 后缀。
 *
 * @param rootPath 待扫描的根文件夹绝对路径，非空。
 * @param arena 供结果数组及临时数据分配的 Arena，非空。
 * @param outEntries 输出收集到的条目列表，非空。
 * @return 收集到的条目总数。
 * @example
 *   Vec<FolderEntry> entries;
 *   u32 count = ScanMarkdownFolder(L"D:\\notes", &arena, &entries);
 */
u32 ScanMarkdownFolder(const wchar_t* rootPath, Arena* arena, Vec<FolderEntry>* outEntries);

/**
 * 判断指定文件名是否为支持的 Markdown 文件扩展名。纯函数。
 *
 * @param fileName 文件名或文件路径。
 * @return 是支持的 Markdown 后缀返回 true。
 */
bool IsMarkdownFileExtension(const wchar_t* fileName);

/**
 * 判断指定目录名是否应该被忽略（如 .git, node_modules, build 等）。纯函数。
 *
 * @param dirName 待检查的目录名称（不含父路径）。
 * @return 属于噪音目录应忽略返回 true。
 */
bool IsIgnoredDirectoryName(const wchar_t* dirName);

}  // namespace markair
