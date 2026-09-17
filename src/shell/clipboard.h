// mdvn 的剪贴板模块(T45 代码块复制按钮):代码块纯文本拼接 + 系统剪贴板写入。
//
// 分成两层是刻意的:`CodeBlockPlainTextUtf8` 是不碰任何 Win32 API 的纯函数,
// 可以直接单测(见 tests/test_code_copy.cpp);真正的 OpenClipboard/
// SetClipboardData 调用集中在 `SetClipboardUnicodeText` 里,单测不覆盖
// (跨进程剪贴板在 CI 环境不可靠)。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../doc/model.h"
#include "../util/arena.h"

namespace mdvn {

/**
 * 拼出一个围栏/缩进代码块的纯文本(UTF-8),原样保留行首缩进与换行,
 * 不含围栏标记(```)与语言标注 —— 那两者本来就不在代码块的 inline run 里。
 *
 * 缓冲区不足时只写前 `cap` 个字节,但返回值仍是完整长度,调用方可据此
 * 先用 `out = nullptr, cap = 0` 问一次所需字节数,再按需分配后正式拼接。
 *
 * @param doc 已解析的文档模型。
 * @param blockIndex 目标块下标;越界或该块不是 `BlockType::CodeBlock` 时返回 0。
 * @param out 输出缓冲区,可为 nullptr(此时只统计长度,不写入)。
 * @param cap `out` 的容量(字节),`out` 为 nullptr 时应传 0。
 * @return 该代码块纯文本的完整字节数(不含结尾 '\0')。
 * @example
 *   u32 need = mdvn::CodeBlockPlainTextUtf8(doc, idx, nullptr, 0);
 *   char* buf = static_cast<char*>(arena.Alloc(need + 1, 1));
 *   mdvn::CodeBlockPlainTextUtf8(doc, idx, buf, need);
 */
u32 CodeBlockPlainTextUtf8(const Document& doc, u32 blockIndex, char* out, u32 cap);

/**
 * 把一段宽字符文本写进系统剪贴板(`CF_UNICODETEXT`)。
 *
 * 用标准 Win32 流程:GlobalAlloc(GMEM_MOVEABLE) 填数据 -> OpenClipboard ->
 * EmptyClipboard -> SetClipboardData -> CloseClipboard。SetClipboardData
 * 成功后 HGLOBAL 的所有权归系统,本函数不再释放它。
 *
 * @param owner 剪贴板归属窗口,可为 nullptr(按当前任务归属)。
 * @param text 待写入的文本,非空;函数自行补结尾 '\0'。
 * @param len 文本长度(UTF-16 code unit 个数,不含结尾 '\0')。
 * @return 写入成功返回 true;分配失败/剪贴板被其他进程占用时返回 false。
 * @example mdvn::SetClipboardUnicodeText(hwnd, L"hello", 5);
 */
bool SetClipboardUnicodeText(HWND owner, const wchar_t* text, u32 len);

/**
 * 把一个代码块的纯文本复制到系统剪贴板(上面两个函数的组合)。
 *
 * @param owner 剪贴板归属窗口,可为 nullptr。
 * @param doc 已解析的文档模型。
 * @param blockIndex 目标代码块下标。
 * @param scratch 拼接用的临时 Arena,非空;函数入口会整体 Reset,调用方不应
 *                在其上保存需要跨调用存活的数据。
 * @return 复制成功返回 true;块非法/空代码块/剪贴板不可用时返回 false。
 * @example if (mdvn::CopyCodeBlockToClipboard(hwnd, doc, blockIndex, &scratch)) { / * 进入已复制反馈态 * / }
 */
bool CopyCodeBlockToClipboard(HWND owner, const Document& doc, u32 blockIndex, Arena* scratch);

}  // namespace mdvn
