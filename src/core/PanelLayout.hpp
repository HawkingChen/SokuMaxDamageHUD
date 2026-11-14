#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>
namespace whm::layout {
struct Rect { int x=0,y=0,w=0,h=0; bool operator==(const Rect&)const=default; };
struct Insets { int left=0,top=0,right=0,bottom=0; };
enum class Dock { Bottom=0, Left=1, Right=2, Free=3 };
constexpr int verticalWidth=400,verticalHeight=776;
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
 int sideWidth=240,freeX=0,freeY=0;
};
struct Plan { Rect game,panel; };
inline Plan plan(const Input &i) {
 const int bx=i.borders.left+i.borders.right,by=i.borders.top+i.borders.bottom;
 const int baseW=std::max(4,i.original.w-bx),baseH=std::max(3,i.original.h-by);
 const bool side=i.dock==Dock::Left||i.dock==Dock::Right;
 const int sideW=std::clamp(i.sideWidth,80,std::max(80,i.area.w/2));
 const auto panelSize=[&](int gameW) {
  int w=i.vertical?sideW:gameW;
  int h=i.vertical?(w*verticalHeight+verticalWidth/2)/verticalWidth:(w*290+500)/1000;
  if(h>i.area.h) {w=std::max(1,w*i.area.h/h);h=i.area.h;}
  return Rect{0,0,w,h};
 };
 Plan out{i.original,{}};
 const bool fit=i.autoFit&&i.dock!=Dock::Free;
 if(fit) {
  int lo=1,hi=std::max(1,std::min(baseW/4,baseH/3)),best=1;
  while(lo<=hi) {
   int u=lo+(hi-lo)/2,w=u*4+bx,h=u*3+by;const auto panel=panelSize(w);
   const bool fits=side?(w+4+panel.w<=i.area.w&&h<=i.area.h):
                       (w<=i.area.w&&h+4+panel.h<=i.area.h);
   if(fits){best=u;lo=u+1;}else hi=u-1;
  }
  out.game.w=best*4+bx;out.game.h=best*3+by;
 }
 out.panel=panelSize(out.game.w);
 if(fit) {
  Rect allowed=i.area;
  if(i.dock==Dock::Bottom)allowed.h=std::max(1,allowed.h-out.panel.h-4);
  if(i.dock==Dock::Left){allowed.x+=out.panel.w+4;allowed.w-=out.panel.w+4;}
  if(i.dock==Dock::Right)allowed.w-=out.panel.w+4;
  out.game=clampPosition(out.game,allowed);
  if(i.fullscreen&&i.dock==Dock::Bottom) {
   out.game.x=i.area.x+(i.area.w-out.game.w)/2;
   out.game.y=i.area.y+(i.area.h-out.game.h-out.panel.h-4)/2;
  }
 }
 out.panel.x=out.game.x;out.panel.y=out.game.y+out.game.h+4;
 if(side) {
  out.panel.x=i.dock==Dock::Left?out.game.x-out.panel.w-4:out.game.x+out.game.w+4;
  out.panel.y=out.game.y+(out.game.h-out.panel.h)/2;
 }
 if(i.dock==Dock::Free) {out.panel.x=out.game.x+i.freeX;out.panel.y=out.game.y+i.freeY;}
 out.panel=clampPosition(out.panel,i.area);
 return out;
}
struct Tile { Rect source; int x,y; };
inline constexpr std::array<Tile,6> verticalTiles{{
 {{10,10,208,276},4,28},{{904,198,72,72},310,30},
 {{235,199,115,25},222,109},{{235,229,115,25},222,134},
 {{220,1,380,184},10,394},{{604,1,380,184},10,580}}};
inline Tile handTile(int n) {return {{358+n*100,204,84,73},220+(n%2)*88,164+(n/2)*74};}
inline bool inside(int x,int y,Rect r){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
inline std::pair<int,int> canonicalPoint(bool vertical,int x,int y) {
 if(!vertical)return {x,y};
 for(auto t:verticalTiles)if(inside(x,y,{t.x,t.y,t.source.w,t.source.h}))
  return {t.source.x+x-t.x,t.source.y+y-t.y};
 for(int n=0;n<5;++n){auto t=handTile(n);if(inside(x,y,{t.x,t.y,t.source.w,t.source.h}))return {t.source.x+x-t.x,t.source.y+y-t.y};}
 return {-1,-1};
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
