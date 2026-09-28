#include "file_map.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace markair {

// 所有字段清零,处于"未打开"状态。
FileMap::FileMap() : view_(nullptr), size_(0) {}

FileMapError FileMap::Open(const wchar_t* path) {
    // 只读打开: 允许其他进程同时读/写/重命名/删除这个文件——本类只是把它读进来
    // 查看,不需要独占。
    //
    // Read-only open: allow other processes to concurrently read/write/rename/
    // delete this file — this class only reads it in for viewing and needs no
    // exclusivity.
    HANDLE file = CreateFileW(path, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return FileMapError::NotFound;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize)) {
        CloseHandle(file);
        return FileMapError::NotFound;
    }

    // 0 字节文件:不需要映射/拷贝任何内容,直接视为成功,内容为空切片。
    if (fileSize.QuadPart == 0) {
        CloseHandle(file);
        view_ = nullptr;
        size_ = 0;
        return FileMapError::None;
    }

    // 之前这里用 CreateFileMappingW/MapViewOfFile 做零拷贝映射,并让这个映射
    // (section)存活到文档整个显示期间。这个设计有个隐藏代价:只要 section
    // 还映射着,外部程序对同一文件的删除/改名覆盖(大多数编辑器"写临时文件再
    // 替换原文件"的保存方式)在内核层面必然失败——跟 CreateFileW 的共享标志
    // 无关,是 section 本身的限制,实测 Notepad++ 保存会直接报"保存失败,请
    // 检查文件是否在别的程序中打开"。
    //
    // 现在改成:读完立即把字节拷贝到自有缓冲区,然后马上解除映射/关闭句柄,
    // 不再让 section 存活超过 Open() 这一次调用——放弃零拷贝,换取"markair
    // 打开着文件查看时,外部程序仍能正常保存/替换该文件"。
    //
    // This used to be a zero-copy CreateFileMappingW/MapViewOfFile mapping
    // kept alive for the document's entire display lifetime. That has a
    // hidden cost: as long as the section stays mapped, an external program's
    // delete/rename-over of the same file (how most editors save — write a
    // temp file, then replace the original) fails at the kernel level
    // regardless of CreateFileW's share flags — verified against Notepad++,
    // which reports a flat "save failed, check whether the file is open in
    // another program."
    //
    // Now: copy the bytes into an owned buffer right away, then unmap/close
    // immediately — the section never outlives this one Open() call. Trades
    // away zero-copy for "markair can still have the file open for viewing
    // while an external program saves/replaces it."
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) {
        CloseHandle(file);
        return FileMapError::MapFailed;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(mapping);
        CloseHandle(file);
        return FileMapError::MapFailed;
    }

    u64 size = static_cast<u64>(fileSize.QuadPart);
    void* owned = VirtualAlloc(nullptr, static_cast<SIZE_T>(size), MEM_RESERVE | MEM_COMMIT,
                                PAGE_READWRITE);
    FileMapError result = FileMapError::None;
    if (!owned) {
        result = FileMapError::MapFailed;
    } else {
        memcpy(owned, view, static_cast<size_t>(size));
    }

    UnmapViewOfFile(view);
    CloseHandle(mapping);
    CloseHandle(file);

    if (result != FileMapError::None) {
        return result;
    }

    view_ = owned;
    size_ = size;
    return FileMapError::None;
}

void FileMap::Close() {
    if (view_) {
        VirtualFree(view_, 0, MEM_RELEASE);
        view_ = nullptr;
    }
    size_ = 0;
}

StrSlice FileMap::Data() const {
    return StrSlice{static_cast<const char*>(view_), static_cast<u32>(size_)};
}

} // namespace markair
