#include "file_map.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace mdvn {

// 所有字段清零,处于"未打开"状态。
FileMap::FileMap() : file_(nullptr), mapping_(nullptr), view_(nullptr), size_(0) {}

FileMapError FileMap::Open(const wchar_t* path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return FileMapError::NotFound;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize)) {
        CloseHandle(file);
        return FileMapError::NotFound;
    }

    // 0 字节文件:CreateFileMappingW/MapViewOfFile 在 Win32 上必然失败,
    // 这是正常行为而非错误——直接视为成功,内容为空切片。
    if (fileSize.QuadPart == 0) {
        file_ = file;
        mapping_ = nullptr;
        view_ = nullptr;
        size_ = 0;
        return FileMapError::None;
    }

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

    file_ = file;
    mapping_ = mapping;
    view_ = view;
    size_ = static_cast<u64>(fileSize.QuadPart);
    return FileMapError::None;
}

void FileMap::Close() {
    if (view_) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_) {
        CloseHandle(reinterpret_cast<HANDLE>(mapping_));
        mapping_ = nullptr;
    }
    if (file_) {
        CloseHandle(reinterpret_cast<HANDLE>(file_));
        file_ = nullptr;
    }
    size_ = 0;
}

StrSlice FileMap::Data() const {
    return StrSlice{static_cast<const char*>(view_), static_cast<u32>(size_)};
}

} // namespace mdvn
