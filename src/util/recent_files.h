#pragma once

#include "types.h"
#include "str.h"

namespace mdvn {

// Maximum number of recent file entries kept in history.
//
// 历史记录中保留的最大文件条目数量。
constexpr u32 kMaxRecentFiles = 100;

// Maximum length of a file path (including null terminator).
//
// 单条文件路径的最大字符长度（包含结尾 null 终止符）。
constexpr u32 kMaxRecentPathChars = 260;

/**
 * A single recent file entry containing full file path.
 *
 * 包含完整文件路径的单条最近打开文件记录。
 */
struct RecentFileEntry {
    /**
     * Absolute file path in UTF-16.
     *
     * UTF-16 格式的绝对文件路径。
     */
    wchar_t path[kMaxRecentPathChars];
};

/**
 * In-memory collection of recent file entries.
 *
 * 最近打开文件记录的内存数据集合。
 */
struct RecentFiles {
    /**
     * Array of recent file entries (ordered from newest to oldest).
     *
     * 最近文件条目数组（按从新到旧从前向后排序）。
     */
    RecentFileEntry entries[kMaxRecentFiles];

    /**
     * Current number of valid entries.
     *
     * 当前有效条目数量。
     */
    u32 count;
};

/**
 * Initialize a RecentFiles structure to empty. Pure function.
 *
 * 将 RecentFiles 结构体初始化为空状态。纯函数。
 *
 * @param out Pointer to RecentFiles struct to initialize.
 *
 *   指向待初始化的 RecentFiles 结构体指针。
 */
void InitRecentFiles(RecentFiles* out);

/**
 * Parse lines of UTF-8 text into RecentFiles struct. Pure function.
 *
 * 将 UTF-8 文本行解析为 RecentFiles 结构体。纯函数。
 *
 * @param text UTF-8 text slice containing newline-separated paths.
 *
 *   包含换行符分隔路径的 UTF-8 文本切片。
 *
 * @param out Output RecentFiles structure.
 *
 *   输出的 RecentFiles 结构体。
 *
 * @return Number of valid paths parsed.
 *
 *   实际解析出的有效路径数量。
 */
u32 ParseRecentFiles(StrSlice text, RecentFiles* out);

/**
 * Format RecentFiles into newline-separated UTF-8 buffer. Pure function.
 *
 * 将 RecentFiles 格式化为换行符分隔的 UTF-8 缓冲区。纯函数。
 *
 * @param list RecentFiles struct to serialize.
 *
 *   待序列化的 RecentFiles 结构体。
 *
 * @param out Destination char buffer.
 *
 *   目标字符缓冲区。
 *
 * @param cap Capacity of destination buffer in bytes.
 *
 *   目标缓冲区的字节容量。
 *
 * @return Number of bytes written.
 *
 *   实际写入的字节数。
 */
u32 FormatRecentFiles(const RecentFiles& list, char* out, u32 cap);

/**
 * Add a path to recent files: deduplicates by full path (case-insensitive) and moves to front.
 *
 * 向最近文件记录中添加路径：按完整路径（不区分大小写）去重并移动到最顶部。
 *
 * @param list Pointer to RecentFiles struct.
 *
 *   指向 RecentFiles 结构体的指针。
 *
 * @param path File path to add (UTF-16).
 *
 *   待添加的文件路径（UTF-16）。
 */
void AddRecentFile(RecentFiles* list, const wchar_t* path);

/**
 * Remove an entry at a given index and shift subsequent entries.
 *
 * 移除指定下标的条目并将后续条目向前平移。
 *
 * @param list Pointer to RecentFiles struct.
 *
 *   指向 RecentFiles 结构体的指针。
 *
 * @param index Zero-based index of entry to remove.
 *
 *   待移除条目的从零起始下标。
 *
 * @return True if removed, false if index out of bounds.
 *
 *   移除成功返回 true，下标越界返回 false。
 */
bool RemoveRecentFileAt(RecentFiles* list, u32 index);

/**
 * Load recent files from %LOCALAPPDATA%\mdvn\history.txt.
 *
 * 从 %LOCALAPPDATA%\mdvn\history.txt 加载最近打开文件记录。
 *
 * @param out Pointer to RecentFiles struct to populate.
 *
 *   待填充的 RecentFiles 结构体指针。
 *
 * @return True if loaded successfully, false on error or missing file.
 *
 *   加载成功返回 true，文件不存在或读取失败返回 false。
 */
bool LoadRecentFiles(RecentFiles* out);

/**
 * Save recent files to %LOCALAPPDATA%\mdvn\history.txt atomically.
 *
 * 将最近打开文件记录原子写入 %LOCALAPPDATA%\mdvn\history.txt。
 *
 * @param list RecentFiles struct to save.
 *
 *   待保存的 RecentFiles 结构体。
 *
 * @return True if saved successfully, false on error.
 *
 *   保存成功返回 true，失败返回 false。
 */
bool SaveRecentFiles(const RecentFiles& list);

}  // namespace mdvn
