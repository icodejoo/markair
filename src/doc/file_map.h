// mdvn 的只读文件内存映射:零拷贝读取磁盘文件内容,不引入异常/RTTI。
#pragma once

#include "../util/str.h"
#include "../util/types.h"

namespace mdvn {

/**
 * 文件映射操作的错误码。
 */
enum class FileMapError {
    None,      // 无错误,映射成功(含空文件的情况)
    NotFound,  // 文件不存在、路径非法或无权限打开
    MapFailed, // 文件存在且非空,但创建映射/映射视图失败
};

/**
 * 只读内存映射一个文件的句柄封装。生命周期由调用者显式管理:
 * `Open` 成功后必须调用 `Close` 释放系统资源,不做 RAII 自动释放
 * (遵守"无副作用全局构造"约束的同源精神,保持资源释放时机显式可控)。
 *
 * @example
 *   mdvn::FileMap fm;
 *   if (fm.Open(L"C:\\a.md") == mdvn::FileMapError::None) {
 *       mdvn::StrSlice content = fm.Data();
 *       // 使用 content.data / content.len...
 *       fm.Close();
 *   }
 */
class FileMap {
public:
    // 构造一个未打开的 FileMap,所有字段清零。
    FileMap();

    /**
     * 只读映射指定路径的文件。
     * @param path 文件的宽字符路径,以 '\0' 结尾。
     * @return `FileMapError::None` 表示成功(包含 0 字节空文件);
     *         `FileMapError::NotFound` 表示文件不存在/无权限打开;
     *         `FileMapError::MapFailed` 表示非空文件但映射系统调用失败。
     * @example FileMapError err = fm.Open(L"C:\\a.md");
     */
    FileMapError Open(const wchar_t* path);

    /**
     * 释放映射视图与文件句柄。可对已关闭/未打开的实例重复调用,是安全的空操作。
     * @example fm.Close();
     */
    void Close();

    /**
     * 获取映射内容的只读视图,仅在 Open 返回 None 之后有效。
     * @return 内容切片;空文件时 data 可能为 nullptr,len 为 0。
     * @example StrSlice s = fm.Data();
     */
    StrSlice Data() const;

private:
    void* file_;    // 底层 HANDLE(文件),避免头文件引入 windows.h
    void* mapping_; // 底层 HANDLE(文件映射对象)
    void* view_;    // MapViewOfFile 返回的视图起始地址
    u64 size_;      // 文件字节数(映射视图长度)
};

} // namespace mdvn
