#include "edit_box.h"

#include <uxtheme.h>  // SetWindowTheme，与 window.cpp 里其它 EDIT 关主题调用同一套 /DELAYLOAD

namespace markair {

HWND CreateSubclassedEditBox(HWND parent, HINSTANCE instance, int controlId,
                              WNDPROC subclassProc) {
    HWND hwnd = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(controlId)), instance, nullptr);
    if (!hwnd) return nullptr;

    LONG_PTR origProc = SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                           reinterpret_cast<LONG_PTR>(subclassProc));
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, origProc);
    SetWindowTheme(hwnd, L"", L"");
    return hwnd;
}

void RepositionEditBox(HWND editHwnd, float scale, float xDip, float yDip, float wDip,
                        float hDip, float fontSizeDip, float* fontScaleCache,
                        float marginLeftDip, float marginRightDip) {
    if (!editHwnd) return;

    int x = static_cast<int>(xDip * scale + 0.5f);
    int y = static_cast<int>(yDip * scale + 0.5f);
    int w = static_cast<int>(wDip * scale + 0.5f);
    int h = static_cast<int>(hDip * scale + 0.5f);
    SetWindowPos(editHwnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);

    // 文字内边距用 EM_SETMARGINS 实现，不再靠缩小窗口矩形留白——窗口矩形
    // 铺满整个容器，视觉边框才能跟实际能输入文字的区域严丝合缝。
    int marginLeft = static_cast<int>(marginLeftDip * scale + 0.5f);
    int marginRight = static_cast<int>(marginRightDip * scale + 0.5f);
    SendMessageW(editHwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(marginLeft, marginRight));

    if (!fontScaleCache || *fontScaleCache == scale) return;

    HFONT oldFont = reinterpret_cast<HFONT>(SendMessageW(editHwnd, WM_GETFONT, 0, 0));
    LOGFONTW lf{};
    lf.lfHeight = -static_cast<LONG>(fontSizeDip * scale + 0.5f);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    HFONT newFont = CreateFontIndirectW(&lf);
    if (newFont) {
        SendMessageW(editHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(newFont), TRUE);
        if (oldFont) DeleteObject(oldFont);
        *fontScaleCache = scale;
    }
}

}  // namespace markair
