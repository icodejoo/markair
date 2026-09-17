#include <windows.h>
#include <psapi.h>
#include <cstdio>
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l){
  if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
  return DefWindowProc(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int){
  WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName=L"bare";
  wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
  RegisterClassW(&wc);
  HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"bare",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,800,600,nullptr,nullptr,hi,nullptr);
  ShowWindow(hwnd,SW_SHOW);
  PROCESS_MEMORY_COUNTERS_EX pmc{}; pmc.cb=sizeof(pmc);
  GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&pmc,sizeof(pmc));
  fprintf(stderr,"private_bytes=%zu\n",(size_t)pmc.PrivateUsage);
  MSG msg{};
  while(GetMessage(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}
  return 0;
}
