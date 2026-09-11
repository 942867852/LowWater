#include <windows.h>
#include <stdio.h>
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
  if(m==WM_PAINT){ PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); POINT p[3]={{100,20},{20,150},{180,150}};
    Polyline(dc,p,3); LineTo(dc,100,20); EndPaint(h,&ps); }
  if(m==WM_DESTROY){PostQuitMessage(0);} return DefWindowProc(h,m,w,l);
}
int main(void){
  WNDCLASS wc={0}; wc.lpfnWndProc=WndProc; wc.hInstance=GetModuleHandle(0); wc.lpszClassName="LWS";
  if(!RegisterClass(&wc)){ printf("RegisterClass failed\n"); return 1; }
  HWND h=CreateWindow("LWS","LowWater spike",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,wc.hInstance,0);
  if(!h){ printf("CreateWindow FAILED (err=%lu)\n",GetLastError()); return 2; }
  printf("WINDOW_OK hwnd=%p\n",(void*)h);
  ShowWindow(h,SW_SHOW); UpdateWindow(h);
  MSG msg; int n=0; while(PeekMessage(&msg,0,0,0,PM_REMOVE) && n<50){ TranslateMessage(&msg); DispatchMessage(&msg); ++n; }
  printf("PUMPED %d msgs; GDI triangle drawn\n",n); DestroyWindow(h); return 0;
}
