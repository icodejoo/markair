#include "clipboard.h"

#include "../util/str.h"

namespace markair {

u32 CodeBlockPlainTextUtf8(const Document& doc, u32 blockIndex, char* out, u32 cap) {
    if (blockIndex >= doc.blocks.Size()) return 0;
    const Block& b = doc.blocks[blockIndex];
    if (b.type != BlockType::CodeBlock) return 0;

    u32 total = 0;
    for (u32 i = 0; i < b.inlineCount; ++i) {
        const Inline& in = doc.inlines[b.firstInlineIdx + i];
        // 必须走 InlineTextBytes:合成换行/合成缩进的 run 不指向 source,
        // 直接读 source + textOffset 会拿到错的字节(见 model.h 的标记说明)。
        const char* bytes = InlineTextBytes(in, doc);
        for (u32 k = 0; k < in.textLen; ++k) {
            if (out && total < cap) out[total] = bytes[k];
            ++total;
        }
    }
    return total;
}

bool SetClipboardUnicodeText(HWND owner, const wchar_t* text, u32 len) {
    if (!text) return false;

    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, (static_cast<SIZE_T>(len) + 1) * sizeof(wchar_t));
    if (!handle) return false;

    wchar_t* dst = static_cast<wchar_t*>(GlobalLock(handle));
    if (!dst) {
        GlobalFree(handle);
        return false;
    }
    for (u32 i = 0; i < len; ++i) dst[i] = text[i];
    dst[len] = L'\0';
    GlobalUnlock(handle);

    if (!OpenClipboard(owner)) {
        GlobalFree(handle);
        return false;
    }
    EmptyClipboard();
    // SetClipboardData 成功后 handle 归系统所有,不能再 GlobalFree;失败才由我们收尾。
    bool ok = SetClipboardData(CF_UNICODETEXT, handle) != nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(handle);
    return ok;
}

bool CopyCodeBlockToClipboard(HWND owner, const Document& doc, u32 blockIndex, Arena* scratch) {
    if (!scratch) return false;

    u32 need = CodeBlockPlainTextUtf8(doc, blockIndex, nullptr, 0);
    if (need == 0) return false;

    scratch->Reset();
    char* utf8 = static_cast<char*>(scratch->Alloc(need, 1));
    if (!utf8) return false;
    CodeBlockPlainTextUtf8(doc, blockIndex, utf8, need);

    // 剪贴板的 CF_UNICODETEXT 就是 UTF-16,复用已有的 UTF-8 -> UTF-16 转换工具,
    // 不自己再写一份解码(容错策略也因此与全工程一致)。
    Utf16Slice wide = Utf8ToUtf16(StrSlice{utf8, need}, scratch);
    if (!wide.data) return false;
    return SetClipboardUnicodeText(owner, wide.data, wide.len);
}

}  // namespace markair
