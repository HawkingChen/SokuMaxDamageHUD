#define NOMINMAX
#include "WindowLayout.hpp"
#include <atomic>
#include <cassert>
#include <iostream>
std::atomic<HWND> gameWindow{};
std::atomic<unsigned> observedSizes{0},badRatios{0};
std::atomic<bool> inspectSizes{false};
LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM l) {
 if(m==WM_SIZE&&inspectSizes) {
  RECT c{};GetClientRect(h,&c);++observedSizes;
  if(c.right*3!=c.bottom*4)++badRatios;
 }
 if(m==WM_CLOSE){DestroyWindow(h);return 0;}
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
DWORD WINAPI ownerThread(void*) {
 WNDCLASSW c{};c.lpfnWndProc=windowProc;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"WHMWindowGeometryTest";
 RegisterClassW(&c);RECT r{0,0,640,480};AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,0);
 HWND h=CreateWindowExW(0,c.lpszClassName,L"Layout regression fixture",WS_OVERLAPPEDWINDOW,
  20,20,r.right-r.left,r.bottom-r.top,nullptr,nullptr,c.hInstance,nullptr);
 ShowWindow(h,SW_SHOWNOACTIVATE);gameWindow.store(h);
 MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
}
int main() {
 using namespace whm;HANDLE thread=CreateThread(nullptr,0,ownerThread,nullptr,0,nullptr);assert(thread);
 unsigned start=GetTickCount();while(!gameWindow&&GetTickCount()-start<2000)Sleep(1);assert(gameWindow);
 HWND h=gameWindow.load();Sleep(40);const auto original=window::snapshot(h);
 assert(original.outer.w>640&&original.outer.h>480);
 inspectSizes=true;
 for(int cycle=0;cycle<16;++cycle) {
  window::Geometry geometry;auto s=window::snapshot(h);geometry.synchronize(s,GetTickCount());
  layout::Input input;input.original=s.outer;input.borders=s.borders;
  input.area={0,0,800,520};input.dock=cycle%2?layout::Dock::Right:layout::Dock::Bottom;input.vertical=cycle%2;
  const auto planned=layout::plan(input);assert(planned.game.w<s.outer.w||planned.game.h<s.outer.h);
  assert(geometry.apply(h,s.outer,planned.game,GetTickCount()));
  start=GetTickCount();
  do {Sleep(1);s=window::snapshot(h);geometry.synchronize(s,GetTickCount());}
  while(geometry.pending()&&GetTickCount()-start<2000);
  assert(!geometry.pending()&&!geometry.blocked());
  assert(layout::closeRect(s.outer,planned.game));
  for(int repeat=0;repeat<5;++repeat)assert(!geometry.apply(h,s.outer,planned.game,GetTickCount()));
  start=GetTickCount();
  do {geometry.restore(h,window::snapshot(h),GetTickCount());Sleep(1);}
  while(geometry.active()&&GetTickCount()-start<2000);
  assert(!geometry.active());
  auto restored=window::snapshot(h).outer;
  if(!layout::closeRect(restored,original.outer))std::cerr<<"cycle "<<cycle<<" original "<<original.outer.x<<","<<original.outer.y<<","<<original.outer.w<<","<<original.outer.h<<" restored "<<restored.x<<","<<restored.y<<","<<restored.w<<","<<restored.h<<" area "<<s.area.x<<","<<s.area.y<<","<<s.area.w<<","<<s.area.h<<"\n";
  assert(layout::closeRect(restored,original.outer));
 }
 assert(observedSizes>=16&&badRatios==0);
 inspectSizes=false;PostMessageW(h,WM_CLOSE,0,0);assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);CloseHandle(thread);
 std::cout<<"Cross-thread async resize, 4:3 clients and 16 restore cycles passed\n";
}
