#pragma once
#include <windows.h>
#include "PanelLayout.hpp"
#include <numeric>
namespace whm::window {
// Do not change process DPI awareness: the game and launcher retain theirs.
// Wine otherwise mixes virtualized monitor bounds with physical game bounds.
inline bool physicalThreadCoordinates() {
 using SetContext=HANDLE (WINAPI *)(HANDLE);
 auto set=reinterpret_cast<SetContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetThreadDpiAwarenessContext"));
 if(!set)return false;
 if(set(reinterpret_cast<HANDLE>(-4)))return true;
 return set(reinterpret_cast<HANDLE>(-3))!=nullptr;
}
inline unsigned windowDpi(HWND window) {
 using GetDpi=UINT (WINAPI *)(HWND);
 auto get=reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
 return get?get(window):96;
}
inline int physicalUnitStep(HWND game,HWND panel) {
 const auto source=windowDpi(game),target=windowDpi(panel);
 return source&&target?static_cast<int>(target/std::gcd(source,target)):1;
}
using layout::Rect;
inline Rect rectOf(RECT r){return {r.left,r.top,r.right-r.left,r.bottom-r.top};}
struct Snapshot {
 Rect outer,area,monitor;layout::Insets borders;LONG style=0;bool fullscreen=false,supported=true;
};
inline Snapshot snapshot(HWND game) {
 Snapshot s;RECT outer{},client{};GetWindowRect(game,&outer);GetClientRect(game,&client);
 POINT p{0,0};ClientToScreen(game,&p);s.outer=rectOf(outer);
 s.borders={p.x-outer.left,p.y-outer.top,outer.right-p.x-client.right,outer.bottom-p.y-client.bottom};
 MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(game,MONITOR_DEFAULTTONEAREST),&monitor);
 s.area=rectOf(monitor.rcWork);s.monitor=rectOf(monitor.rcMonitor);
 const bool wine=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version")!=nullptr;
 s.area=layout::physicalWorkArea(s.area,s.monitor,wine);
 const LONG raw=GetWindowLongW(game,GWL_STYLE);
 s.style=raw&(WS_CAPTION|WS_THICKFRAME|WS_POPUP);
 const bool frameless=!(raw&(WS_CAPTION|WS_THICKFRAME));
 const bool covers=outer.bottom-outer.top>=s.monitor.h-8 || outer.right-outer.left>=s.monitor.w-8;
 // WindowResizer renders a windowed 4:3 game and a separate bars window.
 // Never resize a possible exclusive D3D fullscreen window.
 const bool resizer=GetModuleHandleW(L"WindowResizer.dll")!=nullptr;
 s.fullscreen=frameless&&(covers||resizer);
 s.supported=!(frameless&&covers&&!resizer);
 return s;
}
// A bounded geometry request, rearmed only by explicit permitted layouts.
class InitialFit {
 bool used_=false;layout::PendingResize pending_{};
public:
 bool used()const{return used_;}
 // Explicit bottom fitting or fullscreen side restoration grants one request.
 void allowLayoutSelection(){if(!pending_.pending){used_=false;pending_={};}}
 bool pending()const{return pending_.pending;}
 bool blocked()const{return pending_.blocked;}
 void poll(Snapshot s,unsigned now){pending_.ready(s.outer,now);}
 bool apply(HWND game,Rect actual,Rect target,unsigned now) {
  if(used_)return false;
  used_=true;
  if(!pending_.request(actual,target,now))return false;
  if(!SetWindowPos(game,nullptr,target.x,target.y,target.w,target.h,
        SWP_NOACTIVATE|SWP_NOZORDER|SWP_ASYNCWINDOWPOS)) {
   pending_.pending=false;pending_.blocked=true;
  }
  return true;
 }
};
} // namespace whm::window
