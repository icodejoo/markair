#include "folder_scan.h"
#include "assoc.h"

#include <cwchar>

namespace markair {

namespace {

// 常见应当被跳过的构建产物与临时噪音目录名称。
constexpr const wchar_t* kIgnoredDirNames[] = {
    L".git", L".svn", L".hg", L".vscode", L".vs", L".idea",
    L"node_modules", L"build", L"dist", L"target", L"bin",
    L"obj", L"out", L"__pycache__", L".pytest_cache"
};
constexpr u32 kIgnoredDirCount = sizeof(kIgnoredDirNames) / sizeof(kIgnoredDirNames[0]);

// 宽字符 ASCII 小写折叠。
wchar_t LowerWideChar(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

// 比较两个宽字符串是否相等（大小写不敏感）。
bool EqualsCaseInsensitive(const wchar_t* a, const wchar_t* b) {
    while (*a != 0 && *b != 0) {
        if (LowerWideChar(*a) != LowerWideChar(*b)) return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

// 递归扫描具体实现函数。
void ScanDirectoryRecursive(const wchar_t* currentDir,
                            const wchar_t* rootPath,
                            size_t rootPathLen,
                            u32 currentDepth,
                            Arena* arena,
                            Vec<FolderEntry>* outEntries) {
    if (currentDepth > kMaxFolderScanDepth) return;
    if (outEntries->Size() >= kMaxFolderScanEntries) return;

    wchar_t searchPattern[MAX_PATH];
    swprintf_s(searchPattern, L"%s\\*", currentDir);

    WIN32_FIND_DATAW findData{};
    HANDLE hFind = FindFirstFileW(searchPattern, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        const wchar_t* name = findData.cFileName;
        if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) {
            continue;
        }

        wchar_t childPath[MAX_PATH];
        swprintf_s(childPath, L"%s\\%s", currentDir, name);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!IsIgnoredDirectoryName(name)) {
                ScanDirectoryRecursive(childPath, rootPath, rootPathLen, currentDepth + 1, arena, outEntries);
            }
        } else {
            if (IsMarkdownFileExtension(name)) {
                if (outEntries->Size() >= kMaxFolderScanEntries) {
                    break;
                }

                FolderEntry entry{};
                wcsncpy_s(entry.fullPath, MAX_PATH, childPath, _TRUNCATE);
                wcsncpy_s(entry.fileName, MAX_PATH, name, _TRUNCATE);

                // 计算相对路径：剥离 rootPath 前缀
                if (wcsncmp(childPath, rootPath, rootPathLen) == 0) {
                    const wchar_t* pRel = childPath + rootPathLen;
                    while (*pRel == L'\\' || *pRel == L'/') ++pRel;
                    wcsncpy_s(entry.relPath, MAX_PATH, pRel, _TRUNCATE);
                } else {
                    wcsncpy_s(entry.relPath, MAX_PATH, name, _TRUNCATE);
                }

                ULARGE_INTEGER fileSize;
                fileSize.LowPart = findData.nFileSizeLow;
                fileSize.HighPart = findData.nFileSizeHigh;
                entry.sizeBytes = fileSize.QuadPart;

                outEntries->Push(entry);
            }
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
}

// 比较两个条目的相对路径用于排序。
int CompareFolderEntries(const FolderEntry& a, const FolderEntry& b) {
    const wchar_t* pA = a.relPath;
    const wchar_t* pB = b.relPath;
    while (*pA != 0 && *pB != 0) {
        wchar_t ca = LowerWideChar(*pA);
        wchar_t cb = LowerWideChar(*pB);
        if (ca != cb) return (ca < cb) ? -1 : 1;
        ++pA;
        ++pB;
    }
    if (*pA == 0 && *pB != 0) return -1;
    if (*pA != 0 && *pB == 0) return 1;
    return 0;
}

// 简单对条目切片进行插入排序。
void SortFolderEntries(FolderEntry* entries, u32 count) {
    for (u32 i = 1; i < count; ++i) {
        FolderEntry key = entries[i];
        u32 j = i;
        while (j > 0 && CompareFolderEntries(entries[j - 1], key) > 0) {
            entries[j] = entries[j - 1];
            --j;
        }
        entries[j] = key;
    }
}

}  // namespace

bool IsMarkdownFileExtension(const wchar_t* fileName) {
    if (!fileName) return false;
    const wchar_t* dot = wcsrchr(fileName, L'.');
    if (!dot) return false;

    for (u32 i = 0; i < kAssociatedExtensionCount; ++i) {
        if (EqualsCaseInsensitive(dot, kAssociatedExtensions[i])) {
            return true;
        }
    }
    return false;
}

bool IsIgnoredDirectoryName(const wchar_t* dirName) {
    if (!dirName || dirName[0] == 0) return true;

    // 默认忽略所有以点号开头的隐藏文件夹（如 .git, .vscode, .obsidian 等）
    if (dirName[0] == L'.') return true;

    for (u32 i = 0; i < kIgnoredDirCount; ++i) {
        if (EqualsCaseInsensitive(dirName, kIgnoredDirNames[i])) {
            return true;
        }
    }
    return false;
}

u32 ScanMarkdownFolder(const wchar_t* rootPath, Arena* arena, Vec<FolderEntry>* outEntries) {
    if (!rootPath || rootPath[0] == 0 || !arena || !outEntries) return 0;

    wchar_t normRoot[MAX_PATH]{};
    DWORD fullLen = GetFullPathNameW(rootPath, MAX_PATH, normRoot, nullptr);
    if (fullLen == 0 || fullLen >= MAX_PATH) {
        wcsncpy_s(normRoot, MAX_PATH, rootPath, _TRUNCATE);
    }
    // 去除末尾反斜杠
    size_t len = wcslen(normRoot);
    while (len > 0 && (normRoot[len - 1] == L'\\' || normRoot[len - 1] == L'/')) {
        normRoot[--len] = 0;
    }

    outEntries->Init(arena, 64);
    ScanDirectoryRecursive(normRoot, normRoot, len, 0, arena, outEntries);

    if (outEntries->Size() > 1) {
        SortFolderEntries(outEntries->Data(), outEntries->Size());
    }

    return outEntries->Size();
}

}  // namespace markair
