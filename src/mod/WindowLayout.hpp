#pragma once
#include <windows.h>
#include "PanelLayout.hpp"
namespace whm::window {
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
 const LONG raw=GetWindowLongW(game,GWL_STYLE);
 s.style=raw&(WS_CAPTION|WS_THICKFRAME|WS_POPUP);
 const bool frameless=!(raw&(WS_CAPTION|WS_THICKFRAME));
 const bool covers=outer.bottom-outer.top>=s.monitor.h-8 || outer.right-outer.left>=s.monitor.w-8;
 // WindowResizer renders a windowed 4:3 game and a separate bars window.
 // Never resize a possible exclusive D3D fullscreen window.
 const bool resizer=GetModuleHandleW(L"WindowResizer.dll")!=nullptr;
 s.fullscreen=frameless&&covers;
 s.supported=!(frameless&&covers&&!resizer);
 return s;
}
class Geometry {
 bool active_=false,changed_=false,book_=false,restoreAttempted_=false;Snapshot original_{};
 Rect applied_{},windowedBase_{},windowedApplied_{};
 layout::PendingResize pending_{};
public:
 bool active()const{return active_;}
 Snapshot original()const{return original_;}
 bool pending()const{return pending_.pending;}
 bool blocked()const{return pending_.blocked;}
 void clear(){active_=false;changed_=false;restoreAttempted_=false;pending_={};}
 void synchronize(Snapshot s,unsigned now) {
  if(active_&&s.style!=original_.style){
   if(!original_.fullscreen){book_=true;windowedBase_=original_.outer;windowedApplied_=applied_;}
   clear();
  }
  if(!active_) {
   original_=s;
   if(book_&&!s.fullscreen&&layout::closeRect(s.outer,windowedApplied_))original_.outer=windowedBase_;
   applied_=s.outer;active_=true;
  }
  const bool had=pending_.pending;
  if(!pending_.ready(s.outer,now))return;
  if(had&&!pending_.blocked){applied_=s.outer;changed_=!layout::closeRect(s.outer,original_.outer);}
  // A manual resize/move becomes the new baseline. Never fight it or restore
  // stale coordinates onto a new monitor. Our own pending request is excluded.
  if(!had&&!pending_.blocked&&!layout::closeRect(s.outer,applied_,2)) {
   if(std::abs(s.outer.w-applied_.w)<=1&&std::abs(s.outer.h-applied_.h)<=1) {
    original_.outer.x+=s.outer.x-applied_.x;
    original_.outer.y+=s.outer.y-applied_.y;
    original_.area=s.area;original_.monitor=s.monitor;
   } else {original_=s;changed_=false;}
   applied_=s.outer;
  }
 }
 bool apply(HWND game,Rect actual,Rect target,unsigned now) {
  if(!pending_.request(actual,target,now))return false;
  if(!SetWindowPos(game,nullptr,target.x,target.y,target.w,target.h,
        SWP_NOACTIVATE|SWP_NOZORDER|SWP_ASYNCWINDOWPOS)) {
   pending_.pending=false;pending_.blocked=true;
  }
  return true;
 }
 void restore(HWND game,Snapshot s,unsigned now) {
  if(!active_)return;
  if(s.style!=original_.style){clear();return;}
  if(!pending_.ready(s.outer,now))return;
  const auto area=original_.fullscreen?s.monitor:s.area;
  Rect target=original_.outer;
  if(target.w>area.w||target.h>area.h||(original_.fullscreen&&original_.monitor!=s.monitor)) {
   const int bx=s.borders.left+s.borders.right,by=s.borders.top+s.borders.bottom;
   const int unit=std::max(1,std::min((area.w-bx)/4,(area.h-by)/3));
   target.w=unit*4+bx;target.h=unit*3+by;
   if(original_.fullscreen){target.x=area.x+(area.w-target.w)/2;target.y=area.y+(area.h-target.h)/2;}
  }
  target=layout::clampPosition(target,area);
  if(layout::closeRect(s.outer,target)){clear();return;}
  if(pending_.blocked) { // one restoration attempt, never an endless resize loop
   pending_.blocked=false;
   if(!restoreAttempted_){restoreAttempted_=true;apply(game,s.outer,target,now);}else clear();
   return;
  }
  apply(game,s.outer,target,now);
 }
};
} // namespace whm::window
