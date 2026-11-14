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
 using namespace whm;window::physicalThreadCoordinates();HANDLE thread=CreateThread(nullptr,0,ownerThread,nullptr,0,nullptr);assert(thread);
 unsigned start=GetTickCount();while(!gameWindow&&GetTickCount()-start<2000)Sleep(1);assert(gameWindow);
 HWND h=gameWindow.load();Sleep(40);const auto original=window::snapshot(h);
 assert(original.outer.w>640&&original.outer.h>480);
 HWND probe=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"WHMWindowGeometryTest",L"Physical DPI probe",WS_POPUP,
     0,0,16,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(probe);
 const int dpiStep=window::physicalUnitStep(h,probe);
 std::cout<<"DPI game/panel "<<window::windowDpi(h)<<"/"<<window::windowDpi(probe)
     <<" work "<<original.area.w<<"x"<<original.area.h<<"\n";
 inspectSizes=true;
 for(int cycle=0;cycle<16;++cycle) {
  window::InitialFit geometry;auto s=window::snapshot(h);
  layout::Input input;input.original=s.outer;input.borders=s.borders;
  input.unitStep=dpiStep;input.area={0,0,800,520};input.dock=layout::Dock::Bottom;input.vertical=false;
  const auto planned=layout::plan(input);assert(planned.game.w<s.outer.w||planned.game.h<s.outer.h);
  assert(geometry.apply(h,s.outer,planned.game,GetTickCount()));
  start=GetTickCount();
  do {Sleep(1);s=window::snapshot(h);geometry.poll(s,GetTickCount());}
  while(geometry.pending()&&GetTickCount()-start<2000);
  assert(!geometry.pending()&&!geometry.blocked());
  assert(layout::closeRect(s.outer,planned.game));
  for(int repeat=0;repeat<5;++repeat)assert(!geometry.apply(h,s.outer,planned.game,GetTickCount()));
  assert(geometry.used());
  // Manual resize/fullscreen transitions are external actions. The HUD's
  // one-shot gate must not attempt to fit again after any such action.
  assert(SetWindowPos(h,nullptr,original.outer.x,original.outer.y,original.outer.w,original.outer.h,
      SWP_NOACTIVATE|SWP_NOZORDER|SWP_ASYNCWINDOWPOS));
  start=GetTickCount();
  while(!layout::closeRect(window::snapshot(h).outer,original.outer)&&GetTickCount()-start<2000)Sleep(1);
  assert(layout::closeRect(window::snapshot(h).outer,original.outer));
  for(auto dock:{layout::Dock::Bottom,layout::Dock::Left,layout::Dock::Right}) {
      input.original=original.outer;input.autoFit=false;input.dock=dock;input.vertical=dock!=layout::Dock::Bottom;
      assert(layout::plan(input).game==original.outer);
      assert(!geometry.apply(h,original.outer,planned.game,GetTickCount()));
  }
  geometry.poll(window::snapshot(h),GetTickCount());
  assert(!geometry.apply(h,original.outer,planned.game,GetTickCount()));
  // Only an explicit bottom action rearms the resize permission.
  geometry.allowLayoutSelection();
  assert(geometry.apply(h,original.outer,planned.game,GetTickCount()));
  start=GetTickCount();
  do {Sleep(1);geometry.poll(window::snapshot(h),GetTickCount());}
  while(geometry.pending()&&GetTickCount()-start<2000);
  assert(!geometry.pending()&&!geometry.blocked());
  assert(SetWindowPos(h,nullptr,original.outer.x,original.outer.y,original.outer.w,original.outer.h,
      SWP_NOACTIVATE|SWP_NOZORDER|SWP_ASYNCWINDOWPOS));
  start=GetTickCount();
  while(!layout::closeRect(window::snapshot(h).outer,original.outer)&&GetTickCount()-start<2000)Sleep(1);
  assert(layout::closeRect(window::snapshot(h).outer,original.outer));
 }
 // Model WindowResizer's frameless window without changing a D3D mode.
 inspectSizes=false;
 const auto monitor=window::snapshot(h).monitor;
 const int u=std::min(monitor.w/4,monitor.h/3);
 const layout::Rect fullscreen{monitor.x+(monitor.w-u*4)/2,monitor.y,u*4,u*3};
 SetWindowLongW(h,GWL_STYLE,WS_POPUP|WS_VISIBLE);
 assert(SetWindowPos(h,nullptr,fullscreen.x,fullscreen.y,fullscreen.w,fullscreen.h,
     SWP_NOACTIVATE|SWP_NOZORDER|SWP_ASYNCWINDOWPOS|SWP_FRAMECHANGED));
 start=GetTickCount();
 while(!layout::closeRect(window::snapshot(h).outer,fullscreen)&&GetTickCount()-start<2000)Sleep(1);
 assert(layout::closeRect(window::snapshot(h).outer,fullscreen));
 inspectSizes=true;
 window::InitialFit fullFit;layout::Input fullInput;fullInput.original=fullscreen;
 fullInput.area=monitor;fullInput.fullscreen=true;fullInput.unitStep=dpiStep;
 const auto smaller=layout::plan(fullInput).game;
 assert(smaller.w<fullscreen.w);
 assert(std::abs((smaller.x-monitor.x)*2+smaller.w-monitor.w)<=1);
 const auto fullPanel=layout::plan(fullInput).panel;
 assert(std::abs((smaller.y-monitor.y)*2+smaller.h+fullPanel.h-monitor.h)<=1);
 assert(fullFit.apply(h,fullscreen,smaller,GetTickCount()));
 start=GetTickCount();do {Sleep(1);fullFit.poll(window::snapshot(h),GetTickCount());}
 while(fullFit.pending()&&GetTickCount()-start<2000);
 assert(!fullFit.pending()&&!fullFit.blocked());
 fullFit.allowLayoutSelection();
 assert(fullFit.apply(h,window::snapshot(h).outer,fullscreen,GetTickCount()));
 start=GetTickCount();do {Sleep(1);fullFit.poll(window::snapshot(h),GetTickCount());}
 while(fullFit.pending()&&GetTickCount()-start<2000);
 assert(!fullFit.pending()&&!fullFit.blocked());
 assert(layout::closeRect(window::snapshot(h).outer,fullscreen));
 assert(!(GetWindowLongW(h,GWL_STYLE)&(WS_CAPTION|WS_THICKFRAME)));
 assert(observedSizes>=16&&badRatios==0);
 inspectSizes=false;PostMessageW(h,WM_CLOSE,0,0);assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);CloseHandle(thread);
 std::cout<<"Anchored initial/bottom fit, stable docks, 4:3 clients and frameless size restoration passed\n";
}
