#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <utility>
namespace whm::layout {
struct Rect { int x=0,y=0,w=0,h=0; bool operator==(const Rect&)const=default; };
struct Insets { int left=0,top=0,right=0,bottom=0; };
enum class Dock { Bottom=0, Left=1, Right=2 };
constexpr int verticalWidth=400,verticalHeight=1000;
inline bool closeRect(Rect a,Rect b,int tolerance=1) {
 return std::abs(a.x-b.x)<=tolerance&&std::abs(a.y-b.y)<=tolerance&&
        std::abs(a.w-b.w)<=tolerance&&std::abs(a.h-b.h)<=tolerance;
}
inline Rect clampPosition(Rect r,Rect area) {
 r.x=std::clamp(r.x,area.x,area.x+std::max(0,area.w-r.w));
 r.y=std::clamp(r.y,area.y,area.y+std::max(0,area.h-r.h));return r;
}
// Some Wine/XWayland sessions publish _NET_WORKAREA in compositor logical
// units while monitor/window coordinates are physical pixels. Correct only
// the characteristic uniformly scaled, monitor-wide work area on Wine.
inline Rect physicalWorkArea(Rect work,Rect monitor,bool wine) {
 if(!wine||work.w<=0||work.h<=0||monitor.w<=0||monitor.h<=0)return work;
 const double scale=static_cast<double>(monitor.w)/work.w;
 if(scale<1.2||scale>4.)return work;
 Rect corrected{static_cast<int>(std::lround(work.x*scale)),static_cast<int>(std::lround(work.y*scale)),
     monitor.w,static_cast<int>(std::lround(work.h*scale))};
 if(corrected.h<monitor.h*3/4||corrected.h>monitor.h)return work;
 if(corrected.x!=monitor.x||corrected.y<monitor.y||corrected.y+corrected.h>monitor.y+monitor.h)return work;
 return corrected;
}
struct Input {
 Rect original,area; Insets borders; Dock dock=Dock::Bottom;
 bool vertical=false,autoFit=true,fullscreen=false;
 int sideWidth=240,unitStep=1;
};
struct Plan { Rect game,panel; };
inline int bottomTolerance(int panelHeight){return std::clamp(panelHeight/6,24,48);}
inline Plan plan(const Input &i) {
 const int bx=i.borders.left+i.borders.right,by=i.borders.top+i.borders.bottom;
 const bool side=i.dock!=Dock::Bottom;
 auto panelSize=[&](Rect game) {
  const int w=side?std::min(i.sideWidth,std::max(100,game.h*400/720)):game.w;
  return Rect{0,0,w,side?game.h:(w*290+500)/1000};
 };
 Plan out{i.original,panelSize(i.original)};
 // Fit only when substantially off the bottom, and solve against the space
 // BELOW THE FIXED ANCHOR rather than the whole desktop's height.
 if(i.autoFit&&!side) {
  const int overflow=out.game.y+out.game.h+out.panel.h-i.area.y-i.area.h;
  if(overflow>bottomTolerance(out.panel.h)) {
   const int availableH=i.area.y+i.area.h-i.original.y-8;
   const int availableW=i.area.x+i.area.w-i.original.x;
   const int maxUnit=std::max(1,std::min((i.original.w-bx)/4,(i.original.h-by)/3));
   const int step=std::max(1,i.unitStep);
   int lo=std::min((80+step-1)/step,maxUnit/step),hi=maxUnit/step,best=0;
   while(lo<=hi) {
    const int index=lo+(hi-lo)/2,u=index*step;Rect candidate{i.original.x,i.original.y,u*4+bx,u*3+by};
    const auto panel=panelSize(candidate);
    if(candidate.w<=availableW&&candidate.h+panel.h<=availableH){best=u;lo=index+1;}else hi=index-1;
   }
   // No room even for a small usable window: never collapse the game to a
   // few pixels. Keep its size; the user can move it or choose a side.
   if(best){out.game.w=best*4+bx;out.game.h=best*3+by;out.panel=panelSize(out.game);}
  }
 }
 out.panel.x=out.game.x;out.panel.y=out.game.y+out.game.h;
 if(side) {
  const int margin=i.dock==Dock::Left?out.game.x-i.area.x:i.area.x+i.area.w-out.game.x-out.game.w;
  if(margin>=100)out.panel.w=std::min(out.panel.w,margin);
  out.panel.x=i.dock==Dock::Left?out.game.x-out.panel.w:out.game.x+out.game.w;
  out.panel.y=out.game.y;
 }
 // Preserve attachment coordinates even when a few pixels are offscreen.
 // Automatic docking, not overlaying or stretching, handles desktop edges.
 return out;
}
inline bool inside(int x,int y,Rect r){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
inline constexpr std::array<Rect,4> portraitButtons{{{314,10,28,28},{350,10,28,28},{314,46,28,28},{350,46,28,28}}};
inline int portraitHeight(int width,int height){return std::max(1,height*400/std::max(1,width));}
inline std::pair<int,int> canonicalPoint(bool vertical,int x,int y) {
 if(!vertical)return {x,y};
 for(int n=0;n<4;++n)if(inside(x,y,portraitButtons[n]))
  return {910+(n%2)*34+x-portraitButtons[n].x,204+(n/2)*34+y-portraitButtons[n].y};
 return {-1,-1};
}
inline std::array<Dock,2> alternateDocks(Dock dock) {
 if(dock==Dock::Left)return {Dock::Bottom,Dock::Right};
 if(dock==Dock::Right)return {Dock::Bottom,Dock::Left};
 return {Dock::Left,Dock::Right};
}
inline Dock nearestDock(Rect game,int x,int y) {
 const long long dy=y-std::clamp(y,game.y,game.y+game.h);
 const long long dx=x-std::clamp(x,game.x,game.x+game.w);
 const auto square=[](long long n){return n*n;};
 const auto left=square(x-game.x)+square(dy),right=square(x-game.x-game.w)+square(dy);
 const auto bottom=square(y-game.y-game.h)+square(dx);
 if(bottom<=left&&bottom<=right)return Dock::Bottom;
 return left<right?Dock::Left:Dock::Right;
}
inline Dock automaticDock(Rect game,Rect area,Dock current,int width) {
 const int left=game.x-area.x,right=area.x+area.w-game.x-game.w;
 if(current==Dock::Bottom) {
  const int required=(game.w*290+500)/1000;
  if(area.y+area.h-game.y-game.h>=required-bottomTolerance(required))return current;
  return left>right?Dock::Left:Dock::Right;
 }
 const int here=current==Dock::Left?left:right,other=current==Dock::Left?right:left;
 // Switch only to a materially better side. Both sides cramped stays stable.
 if(here<width-8&&other>=here+32)return current==Dock::Left?Dock::Right:Dock::Left;
 return current;
}
inline bool nearDock(Rect game,int x,int y,Dock dock,int threshold=24) {
 if(dock==Dock::Bottom)return std::abs(y-game.y-game.h)<=threshold&&x>=game.x-threshold&&x<=game.x+game.w+threshold;
 return std::abs(x-(dock==Dock::Left?game.x:game.x+game.w))<=threshold&&y>=game.y-threshold&&y<=game.y+game.h+threshold;
}
inline Rect foldedButton(Rect game,Insets border) {
 const int size=std::clamp((game.w-border.left-border.right)/24,24,38);
 return {game.x+game.w-border.right-size-8,game.y+game.h-border.bottom-size-8,size,size};
}
// One outstanding async request, with bounded waiting. Caller keeps the
// original snapshot; it never scales an already-scaled window again.
struct PendingResize {
 bool pending=false,blocked=false; Rect sent{}; unsigned sentAt=0;
 bool ready(Rect actual,unsigned now) {
  if(!pending)return true;
  if(closeRect(actual,sent)){pending=false;return true;}
  if(now-sentAt>=1500){pending=false;blocked=true;return true;}
  return false;
 }
 bool request(Rect actual,Rect target,unsigned now) {
  if(!ready(actual,now)||blocked||closeRect(actual,target))return false;
  sent=target;sentAt=now;pending=true;return true;
 }
};
} // namespace whm::layout
