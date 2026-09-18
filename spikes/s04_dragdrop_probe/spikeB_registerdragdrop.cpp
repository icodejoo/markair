// spike B: 路径 B —— OleInitialize + RegisterDragDrop(ole32.dll 导出)。
// pDropTarget 只是最小桩:QueryInterface/AddRef/Release/DragEnter 等方法
// 全部 no-op 或返回 E_NOTIMPL,不处理任何拖放数据。
// 一次性实验代码,不进正式 CMake 构建,不影响主线。
#include <windows.h>
#include <psapi.h>
#include <ole2.h>
#include <cstdio>

// 最小 IDropTarget 桩实现,仅用于让 RegisterDragDrop 能够注册成功。
class MinimalDropTarget : public IDropTarget {
 public:
  MinimalDropTarget() : refCount_(1) {}

  HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG __stdcall AddRef() override { return ++refCount_; }
  ULONG __stdcall Release() override {
    ULONG r = --refCount_;
    if (r == 0) delete this;
    return r;
  }
  HRESULT __stdcall DragEnter(IDataObject*, DWORD, POINTL, DWORD* effect) override {
    *effect = DROPEFFECT_NONE;
    return S_OK;
  }
  HRESULT __stdcall DragOver(DWORD, POINTL, DWORD* effect) override {
    *effect = DROPEFFECT_NONE;
    return S_OK;
  }
  HRESULT __stdcall DragLeave() override { return S_OK; }
  HRESULT __stdcall Drop(IDataObject*, DWORD, POINTL, DWORD* effect) override {
    *effect = DROPEFFECT_NONE;
    return S_OK;
  }

 private:
  ULONG refCount_;
};

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProc(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hi;
  wc.lpszClassName = L"s04_registerdragdrop";
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"s04 RegisterDragDrop",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              800, 600, nullptr, nullptr, hi, nullptr);

  // 关键调用:路径 B 的触发点——先 OleInitialize,再 RegisterDragDrop。
  OleInitialize(nullptr);
  MinimalDropTarget* dropTarget = new MinimalDropTarget();
  RegisterDragDrop(hwnd, dropTarget);

  ShowWindow(hwnd, SW_SHOW);

  PROCESS_MEMORY_COUNTERS_EX pmc{};
  pmc.cb = sizeof(pmc);
  GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
  fprintf(stderr, "private_bytes=%zu\n", (size_t)pmc.PrivateUsage);
  fflush(stderr);

  MSG msg{};
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  RevokeDragDrop(hwnd);
  dropTarget->Release();
  OleUninitialize();
  return 0;
}
