#include "open_dialog.h"

#include <shobjidl.h>
#include <objbase.h>

#include "assoc.h"  // kAssociatedExtensions(唯一定义处)

namespace mdvn {

namespace {

// 把 kAssociatedExtensions 拼成 IFileOpenDialog 过滤器串"*.md;*.markdown;..."。
// 固定长度栈缓冲,五个扩展名远用不满,超长静默截断(防御性,不会真的发生)。
u32 BuildExtensionFilterPattern(wchar_t* out, u32 outCap) {
    if (!out || outCap == 0) return 0;
    u32 pos = 0;
    for (u32 i = 0; i < kAssociatedExtensionCount && pos + 3 < outCap; ++i) {
        if (i > 0 && pos + 1 < outCap) out[pos++] = L';';
        if (pos + 1 < outCap) out[pos++] = L'*';
        const wchar_t* ext = kAssociatedExtensions[i];
        for (u32 k = 0; ext[k] != L'\0' && pos + 1 < outCap; ++k) out[pos++] = ext[k];
    }
    out[pos] = L'\0';
    return pos;
}

}  // namespace

bool ShowOpenMarkdownDialog(HWND owner, wchar_t* outPath, u32 outCap) {
    if (!outPath || outCap == 0) return false;
    outPath[0] = L'\0';

    // 与 assets/image.cpp 的 WIC 惰性初始化同一手法:按需 CoInitializeEx,
    // 用完对称 CoUninitialize;S_OK/S_FALSE 都表示当前线程已进入 COM 公寓,
    // 都需要之后配一次 CoUninitialize(引用计数式,不会因为多次调用而出错)。
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needUninit = SUCCEEDED(coHr);

    bool ok = false;
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr) && dialog) {
        wchar_t pattern[128];
        BuildExtensionFilterPattern(pattern, 128);
        COMDLG_FILTERSPEC filter[] = {
            {L"Markdown 文档", pattern},
        };
        dialog->SetFileTypes(1, filter);
        dialog->SetFileTypeIndex(1);

        hr = dialog->Show(owner);
        if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            hr = dialog->GetResult(&item);
            if (SUCCEEDED(hr) && item) {
                PWSTR path = nullptr;
                hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(hr) && path) {
                    u32 i = 0;
                    for (; path[i] != L'\0' && i + 1 < outCap; ++i) outPath[i] = path[i];
                    outPath[i] = L'\0';
                    ok = (i > 0);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    if (needUninit) CoUninitialize();
    return ok;
}

bool BuildLaunchCommandLine(const wchar_t* exePath, const wchar_t* filePath,
                             wchar_t* outCmdLine, u32 outCap) {
    if (!exePath || !filePath || !outCmdLine || outCap == 0) return false;
    if (exePath[0] == L'\0' || filePath[0] == L'\0') return false;

    // 命令行:`"<exe>" "<filePath>"`,两段都加引号防止路径中的空格拆散参数。
    // CreateProcessW 的命令行缓冲要求可写,不能直接指向字面量/const 数据。
    u32 pos = 0;
    if (pos + 1 >= outCap) return false;
    outCmdLine[pos++] = L'"';
    for (u32 i = 0; exePath[i] != L'\0'; ++i) {
        if (pos + 1 >= outCap) return false;
        outCmdLine[pos++] = exePath[i];
    }
    if (pos + 3 >= outCap) return false;
    outCmdLine[pos++] = L'"';
    outCmdLine[pos++] = L' ';
    outCmdLine[pos++] = L'"';
    for (u32 i = 0; filePath[i] != L'\0'; ++i) {
        if (pos + 1 >= outCap) return false;
        outCmdLine[pos++] = filePath[i];
    }
    if (pos + 1 >= outCap) return false;
    outCmdLine[pos++] = L'"';
    outCmdLine[pos] = L'\0';
    return true;
}

bool LaunchNewInstance(const wchar_t* filePath) {
    if (!filePath || filePath[0] == L'\0') return false;

    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return false;

    wchar_t cmdLine[MAX_PATH * 2 + 8];
    if (!BuildLaunchCommandLine(exePath, filePath, cmdLine, MAX_PATH * 2 + 8)) return false;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL created = CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                                   &si, &pi);
    if (!created) return false;

    // 不等待新进程、不保留任何跨进程状态——新窗口是完全独立的实例,
    // 只是不需要的句柄要关掉,避免泄漏。
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}  // namespace mdvn
