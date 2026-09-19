// markair 的"打开目标"外壳逻辑:链接点击行为(T36)+ 图片点击查看原图(T36b)。
//
// T36 的三类链接目标各自的行为:
//   ① `http(s)://`(以及 `mailto:` / `file:`)→ `ShellExecuteW` 交给系统默认程序,
//      **不内嵌任何浏览**;
//   ② 相对/绝对 `.md` 路径 → `PathCchCombineEx` + `PathCchCanonicalize` 规范化后
//      **在当前窗口内替换文档**(裁决 #6:不新开进程,任意时刻只有一份文档状态常驻);
//   ③ `#锚点` → 在当前文档的标题块里按 GitHub 式 slug 规则匹配,滚动到该块顶部。
// **安全边界**:除 `http` / `https` / `mailto` / `file` 之外的 scheme(如 `javascript:`)
// 一律拒绝执行,判定发生在任何 `ShellExecuteW` 之前。
//
// 临时文件生命周期(T36b 裁决):不追踪外部查看器进程是否退出(对单实例转发型
// 看图工具会误判"已关闭"导致过早删除、白图),改为进程内维护一份"本次会话创建过的
// 临时文件"清单,`wWinMain` 正常退出前统一 DeleteFileW;异常终止的残留交给
// %TEMP% 的系统级清理兜底,不做额外保证。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../doc/model.h"
#include "../util/arena.h"
#include "../util/span.h"
#include "../util/str.h"
#include "../util/types.h"

namespace markair {

/** 点击一个链接时应当采取的行为(T36,纯判定,不含任何 Win32 调用)。 */
enum class LinkAction : u8 {
    OpenExternal,   // 交给系统默认程序打开(http / https / mailto / file)
    OpenMarkdown,   // 在当前窗口内替换文档(相对/绝对 .md 路径)
    ScrollToAnchor, // 滚动到当前文档内的 GitHub 式锚点
    Reject,         // 不支持或不安全的目标(javascript: 等),什么都不做
};

/**
 * 判定一个 href 的 scheme 是否在允许交给系统打开的白名单内。
 * 白名单固定为 `http` / `https` / `mailto` / `file`(大小写不敏感);没有 scheme
 * 的相对路径、以及单字母 scheme(Windows 盘符 `C:\...`)都不算外部 scheme。
 *
 * @param href 链接地址(UTF-8)。
 * @return 属于白名单 scheme 返回 true。
 * @example bool ok = markair::IsAllowedExternalScheme(markair::StrSlice{"https://a.com", 13}); // true
 */
bool IsAllowedExternalScheme(StrSlice href);

/**
 * 判定点击一个链接应当采取什么行为。纯函数,可脱离 Win32 单测。
 *
 * @param href 链接地址(UTF-8),来自 `LinkTarget::href`。
 * @return 应采取的行为;空 href、非白名单 scheme、非 `.md` 的本地路径一律
 *         返回 `LinkAction::Reject`。
 * @example
 *   auto a = markair::DecideLinkAction(markair::StrSlice{"javascript:alert(1)", 19});
 *   // a == markair::LinkAction::Reject
 */
LinkAction DecideLinkAction(StrSlice href);

/**
 * 按 GitHub 式规则把一段标题文本转成锚点 slug:转小写、空格转 `-`、
 * 丢弃 ASCII 标点(`-` 与 `_` 保留)、非 ASCII 字节原样保留(中文标题因此可用)。
 *
 * @param text 标题的可见文本(UTF-8)。
 * @param out 输出缓冲,函数保证以 '\0' 结尾;非空。
 * @param cap out 的容量(字节,含结尾符),须大于 0。
 * @return 写入的字节数(不含结尾 '\0')。
 * @example
 *   char slug[64];
 *   u32 n = markair::MakeHeadingSlug(markair::StrSlice{"Hello World!", 12}, slug, 64);
 *   // slug == "hello-world", n == 11
 */
u32 MakeHeadingSlug(StrSlice text, char* out, u32 cap);

/**
 * 在文档的标题块里查找与锚点匹配的块。
 *
 * 锚点允许带前导 `#`,并会先做一次百分号解码(GitHub 对中文标题的链接是
 * 百分号编码的),再按 `MakeHeadingSlug` 的规则与各标题块比对。
 *
 * @param doc 当前文档模型。
 * @param anchor 锚点文本(UTF-8),形如 `#section-title` 或 `section-title`。
 * @return 匹配到的标题块下标;未命中返回 `kInvalidIndex`。
 * @example u32 b = markair::FindAnchorBlock(doc, markair::StrSlice{"#安装", 8});
 */
u32 FindAnchorBlock(const Document& doc, StrSlice anchor);

/**
 * 把一个相对/绝对 `.md` 链接规范化成可直接打开的绝对路径。
 *
 * 内部用 `PathCchCombineEx` + `PathCchCanonicalize` 完成拼接与规范化,因此
 * `../../` 之类的路径穿越会被真正折叠掉,且不会越过盘符根目录;链接里的
 * 百分号编码(如 `%20`)先解码,URL 风格的 `/` 统一转成 `\`,`#`/`?` 之后的
 * 片段被丢弃。
 *
 * @param href 链接地址(UTF-8)。
 * @param docDirectory 当前文档所在目录(以 '\0' 结尾),相对路径据此解析;
 *        可为 nullptr(此时相对路径按当前工作目录解析)。
 * @param out 输出缓冲,成功时以 '\0' 结尾;非空。
 * @param cap out 的容量(宽字符个数),建议不小于 MAX_PATH。
 * @return 规范化成功返回 true;href 为空、超长或 API 失败返回 false。
 * @example
 *   wchar_t full[MAX_PATH];
 *   markair::ResolveMarkdownPath(markair::StrSlice{"../readme.md", 12}, L"C:\\docs\\sub", full, MAX_PATH);
 *   // full == L"C:\\docs\\readme.md"
 */
bool ResolveMarkdownPath(StrSlice href, const wchar_t* docDirectory, wchar_t* out, u32 cap);

/**
 * 把一个外链交给系统默认程序打开(T36 ①)。调用前会再判一次 scheme 白名单,
 * 非白名单 scheme 直接返回 false,**不会发生任何 `ShellExecuteW` 调用**。
 *
 * @param href 链接地址(UTF-8)。
 * @return 已交给系统打开返回 true;scheme 被拒绝或调用失败返回 false
 *         (失败不崩溃、不弹 MessageBox)。
 * @example markair::OpenExternalTarget(markair::StrSlice{"https://example.com", 19});
 */
bool OpenExternalTarget(StrSlice href);

/**
 * 判断一个路径当前是否指向一个存在的文件(T36 ② 的"路径不存在"分支用)。
 * @param path 完整路径(以 '\0' 结尾),可为 nullptr。
 * @return 存在且不是目录返回 true。
 * @example if (!markair::MarkdownFileExists(full)) { / * 窗口内提示,不弹框 * / }
 */
bool MarkdownFileExists(const wchar_t* path);

/**
 * 在系统文件管理器(explorer.exe)里打开某个文件所在的文件夹,并选中该文件
 * (历史记录侧栏"打开所在文件夹"按钮 / 底部栏路径区用)。调用方须先自行确认
 * 文件存在(见 `MarkdownFileExists`)——本函数不做存在性检查,不存在的路径
 * 交给 explorer.exe 处理,行为不保证(可能打开空文件夹)。
 *
 * @param path 完整文件路径(以 '\0' 结尾),非空。
 * @return `ShellExecuteW` 调用成功返回 true;路径为空或调用失败返回 false。
 * @example markair::OpenContainingFolderAndSelect(L"C:\\docs\\readme.md");
 */
bool OpenContainingFolderAndSelect(const wchar_t* path);

/** 点击一张图片时应当采取的打开方式(纯判定,不含任何 Win32 调用)。 */
enum class ImageOpenAction : u8 {
    OpenLocalPath,    // 本地相对/绝对路径:直接 ShellExecuteW 打开原文件
    WriteTempThenOpen, // data: URI / 网络图片:把原始字节写临时文件后打开
    Reject,            // 没有可打开的原始数据(如网络图片尚未下载、锚点、空 href)
};

/** 打开一张图片原始数据的执行结果。 */
enum class ImageOpenResult : u8 {
    Opened,       // 已交给系统默认程序打开
    NoData,       // 没有可用的原始数据(对应 ImageOpenAction::Reject)
    WriteFailed,  // 临时文件写入失败
    ShellFailed,  // ShellExecuteW 调用失败(无关联程序等)
};

/**
 * 判定点击一张图片应当怎么打开它的原始数据。纯函数,可脱离 Win32 单测。
 *
 * @param kind 图片目标种类(来自 LinkTarget::kind)。
 * @param hasRawBytes 是否已经持有该图的原始压缩字节(data: URI 解码结果,
 *        或网络图片下载到的响应体)。
 * @return 应采取的打开方式。
 * @example
 *   auto a = markair::DecideImageOpenAction(markair::LinkTargetKind::DataUri, true);
 *   // a == markair::ImageOpenAction::WriteTempThenOpen
 */
ImageOpenAction DecideImageOpenAction(LinkTargetKind kind, bool hasRawBytes);

/**
 * 本次会话创建过的临时文件清单:登记 + 退出时统一清理。
 * 用 Arena 上的 Vec 存放路径,不使用任何 STL 容器。
 *
 * @example
 *   markair::TempFileRegistry temps(&arena);
 *   const wchar_t* p = temps.WriteTempFile(bytes, len, L".png");
 *   // ... 程序退出前:
 *   temps.CleanupAll();
 */
class TempFileRegistry {
public:
    /**
     * 绑定存放路径字符串与清单数组的 Arena。
     * @param arena 生命周期须覆盖本对象(通常是文档 Arena 或进程级 Arena)。
     * @example markair::TempFileRegistry temps(&docArena);
     */
    explicit TempFileRegistry(Arena* arena);

    TempFileRegistry(const TempFileRegistry&) = delete;
    TempFileRegistry& operator=(const TempFileRegistry&) = delete;

    /**
     * 把一段原始字节写进 `%TEMP%\markair\` 下一个会话内自增编号的临时文件,并登记。
     *
     * @param bytes 原始字节,非空。
     * @param len 字节数,须大于 0。
     * @param extension 文件扩展名(含点号,如 L".png");为空时用 L".bin"。
     * @return 写入成功返回以 '\0' 结尾的完整路径(所有权归本对象,活到 Arena 释放);
     *         失败返回 nullptr。
     * @example const wchar_t* path = temps.WriteTempFile(p.bytes, p.len, L".png");
     */
    const wchar_t* WriteTempFile(const void* bytes, u32 len, const wchar_t* extension);

    /** 本次会话已登记的临时文件个数。 */
    u32 Count() const { return paths_.Size(); }

    /** 取第 index 条已登记的临时文件路径;调用方保证 index < Count()。 */
    const wchar_t* At(u32 index) const { return paths_[index]; }

    /**
     * 删除清单里的全部临时文件(逐个 DeleteFileW,失败静默忽略),并清空计数。
     * 应在 `wWinMain` 正常退出前调用一次。
     * @return 成功删除的文件个数。
     * @example temps.CleanupAll();
     */
    u32 CleanupAll();

private:
    Arena* arena_;              // 路径字符串与清单数组所在 Arena,不拥有
    Vec<const wchar_t*> paths_; // 本次会话创建过的临时文件路径
    u32 nextSequence_;          // 会话内自增编号,保证同一次运行中文件名不冲突
};

/**
 * 打开一张图片的原始数据(T36b 的执行入口)。
 *
 * 行为:
 *   - 本地路径图片:按 `docDirectory` 解析成绝对路径后 `ShellExecuteW` 打开**原文件**;
 *   - `data:` URI / 网络图片:把传入的原始字节写入临时文件后 `ShellExecuteW` 打开。
 * 两种分支打开的都是**未经降采样的原始数据**,因此 T30 的降采样只影响内存占用,
 * 不影响用户能看到原图。
 *
 * @param href 图片地址(UTF-8),来自 LinkTarget::href。
 * @param kind 图片目标种类。
 * @param docDirectory 当前文档所在目录(宽字符,以 '\0' 结尾);相对路径据此解析,
 *        可为 nullptr(此时相对路径按当前工作目录解析)。
 * @param rawBytes data: URI 解码结果或网络下载到的原始字节;无则传 nullptr。
 * @param rawLen rawBytes 的字节数;无则传 0。
 * @param extension 写临时文件时使用的扩展名(含点号),可为 nullptr(用 L".bin")。
 * @param temps 临时文件清单,非空;写出的文件会登记进去以便退出时清理。
 * @return 执行结果,见 ImageOpenResult;任何失败都不崩溃、不弹 MessageBox。
 * @example
 *   markair::OpenImageOriginal(box.href, box.kind, docDir, p.bytes, p.len, L".png", &temps);
 */
ImageOpenResult OpenImageOriginal(StrSlice href, LinkTargetKind kind,
                                   const wchar_t* docDirectory,
                                   const void* rawBytes, u32 rawLen,
                                   const wchar_t* extension,
                                   TempFileRegistry* temps);

}  // namespace markair
