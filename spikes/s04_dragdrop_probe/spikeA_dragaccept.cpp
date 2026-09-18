// spike A: 路径 A —— DragAcceptFiles(shell32.dll 导出符号)。
// 直接隐式链接 shell32.lib(不做 delay-load),就是要测"不做任何优化、
// 直接调用会怎样"这个最朴素的场景。
// 一次性实验代码,不进正式 CMake 构建,不影响主线。
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <cstdio>

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
  if (m == WM_DROPFILES) {
    // 只做最小桩:不读取任何文件路径,直接释放句柄。
    DragFinish((HDROP)w);
    return 0;
  }
  return DefWindowProc(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hi;
  wc.lpszClassName = L"s04_dragaccept";
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"s04 DragAcceptFiles",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              800, 600, nullptr, nullptr, hi, nullptr);

  // 关键调用:路径 A 的触发点。
  DragAcceptFiles(hwnd, TRUE);

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
  return 0;
}
