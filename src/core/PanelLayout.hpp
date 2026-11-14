#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
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
struct Input {
 Rect original,area; Insets borders; Dock dock=Dock::Bottom;
 bool vertical=false,autoFit=true,fullscreen=false;
 int sideWidth=240;
};
struct Plan { Rect game,panel; };
inline Plan plan(const Input &i) {
 const int bx=i.borders.left+i.borders.right,by=i.borders.top+i.borders.bottom;
 const int baseW=std::max(4,i.original.w-bx),baseH=std::max(3,i.original.h-by);
 const bool side=i.dock==Dock::Left||i.dock==Dock::Right;
 const int sideW=std::clamp(i.sideWidth,80,std::max(80,i.area.w/2));
 const auto panelSize=[&](int gameW,int gameH) {
  int w=side?sideW:gameW;
  int h=side?gameH:(w*290+500)/1000;
  if(!side&&h>i.area.h) {w=std::max(1,w*i.area.h/h);h=i.area.h;}
  return Rect{0,0,w,h};
 };
 Plan out{i.original,{}};
 const bool fit=i.autoFit;
 if(fit) {
  int lo=1,hi=std::max(1,std::min(baseW/4,baseH/3)),best=1;
  while(lo<=hi) {
   int u=lo+(hi-lo)/2,w=u*4+bx,h=u*3+by;const auto panel=panelSize(w,h);
   const bool fits=side?(w+panel.w<=i.area.w&&h<=i.area.h):
                       (w<=i.area.w&&h+panel.h<=i.area.h);
   if(fits){best=u;lo=u+1;}else hi=u-1;
  }
  out.game.w=best*4+bx;out.game.h=best*3+by;
 }
 out.panel=panelSize(out.game.w,out.game.h);
 if(fit) {
  Rect allowed=i.area;
  if(i.dock==Dock::Bottom)allowed.h=std::max(1,allowed.h-out.panel.h);
  if(i.dock==Dock::Left){allowed.x+=out.panel.w;allowed.w-=out.panel.w;}
  if(i.dock==Dock::Right)allowed.w-=out.panel.w;
  out.game=clampPosition(out.game,allowed);
  if(i.fullscreen&&i.dock==Dock::Bottom) {
   out.game.x=i.area.x+(i.area.w-out.game.w)/2;
   out.game.y=i.area.y+(i.area.h-out.game.h-out.panel.h)/2;
  }
 }
 out.panel.x=out.game.x;out.panel.y=out.game.y+out.game.h;
 if(side) {
  out.panel.x=i.dock==Dock::Left?out.game.x-out.panel.w:out.game.x+out.game.w;
  out.panel.y=out.game.y;
 }

 if(!fit) {
  // Follow a fixed game window. Use the remaining margin, or overlay the
  // selected edge if there is no usable margin. Never move/resize the game.
  if(side) {
   const int margin=i.dock==Dock::Left?out.game.x-i.area.x:
       i.area.x+i.area.w-out.game.x-out.game.w;
   if(margin>=100)out.panel.w=std::min(out.panel.w,margin);
   out.panel.x=i.dock==Dock::Left?out.game.x-out.panel.w:out.game.x+out.game.w;
   if(margin<100)out.panel.x=i.dock==Dock::Left?out.game.x:out.game.x+out.game.w-out.panel.w;
  } else {
   const int margin=i.area.y+i.area.h-out.game.y-out.game.h;
   if(margin>=60)out.panel.h=std::min(out.panel.h,margin);
   else out.panel.y=out.game.y+out.game.h-out.panel.h;
  }
 }
 if(!side)out.panel=clampPosition(out.panel,i.area);
 return out;
}
inline bool inside(int x,int y,Rect r){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
inline constexpr std::array<Rect,4> portraitButtons{{{314,10,28,28},{350,10,28,28},{314,46,28,28},{350,46,28,28}}};
inline int portraitHeight(int width,int height){return std::max(840,height*400/std::max(1,width));}
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
  if(area.y+area.h-game.y-game.h>=required-4)return current;
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
