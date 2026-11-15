#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include "NativeDamage.hpp"
namespace whm::native {
using Event=whm::NativeEvent;
inline whm::NativeActors actors;
inline std::array<Event,4096> events{};
inline unsigned count=0,dropped=0,frame=0;
inline const void *players[2]{};
inline bool active=false,installed=false;
inline DWORD gameThread=0;
inline thread_local void *creator=nullptr;
template<typename T> inline T read(const void *p,unsigned offset) noexcept {
 T v{};if(p)std::memcpy(&v,static_cast<const char*>(p)+offset,sizeof(v));return v;
}
inline bool isPlayer(const void *p){return p&&(p==players[0]||p==players[1]);}
inline bool capturing(){return active&&GetCurrentThreadId()==gameThread;}
inline void append(Event e) noexcept {if(count<events.size())events[count++]=e;else ++dropped;}
inline void begin(unsigned f,const void *left,const void *right) noexcept {
 if(players[0]!=left || players[1]!=right)actors.reset();
 count=0;dropped=0;frame=f;players[0]=left;players[1]=right;
 gameThread=GetCurrentThreadId();active=installed;
}
inline void end() noexcept {active=false;}
inline unsigned side(const void *p) {return p==players[0]?0:1;}
inline void resetActors() {actors.reset();players[0]=players[1]=nullptr;}
inline void captureAction(void *self,int action,int sequence=-1) {
 if(!capturing()||!isPlayer(self))return;
 const auto cast=actors.set(side(self),action,sequence);
 Event e;e.kind=Event::Action;e.frame=frame;e.owner=reinterpret_cast<std::uintptr_t>(self);
 e.action=action;e.sequence=sequence;e.cast=cast.id;e.castAction=cast.action;append(e);
}
__declspec(noinline) inline void __cdecl captureHit(void *attack,void *target,int amount,int path=0) noexcept {
 if(!capturing()||!attack||!isPlayer(target)||amount<=0)return;
 void *owner=read<void*>(attack,0x168);if(!isPlayer(owner))return;
 Event e;e.kind=Event::Hit;e.frame=frame;e.path=path;
 const auto cast=actors.get(side(owner));e.cast=cast.id;e.castAction=cast.action;
 e.object=reinterpret_cast<std::uintptr_t>(attack);e.victim=reinterpret_cast<std::uintptr_t>(target);
 e.owner=reinterpret_cast<std::uintptr_t>(owner);e.ally=read<std::uintptr_t>(attack,0x16c);
 e.action=read<unsigned short>(attack,0x13c);e.sequence=read<unsigned short>(attack,0x13e);
 e.boxAction=read<short>(attack,0x1c8);e.ownerAction=read<unsigned short>(owner,0x13c);
 e.victimAction=read<unsigned short>(target,0x13c);
 e.damage=amount;e.hp=read<short>(target,0x184);e.ownerHP=read<short>(owner,0x184);
 e.comboHits=read<short>(reinterpret_cast<void*>(e.ally),0x4b4);
 e.comboDamageBefore=read<short>(reinterpret_cast<void*>(e.ally),0x4b6);
 e.protectedHP=read<unsigned char>(target,0x575);
 if(attack!=owner){e.generation=read<unsigned>(attack,0x350);e.parent=read<std::uintptr_t>(attack,0x368);}
 append(e);
}
// At 1.10a +47B05C: EDI=attack object, ESI=victim, BX=one hit's
// final damage, EAX=combo owner. Preserve integer flags, all registers and
// the live x87/SSE state, then execute the exact displaced ADD instruction.
__declspec(naked) inline void settlementStub() {
 __asm {
  pushfd
  pushad
  mov ebp,esp
  sub esp,528
  and esp,0fffffff0h
  fxsave [esp]
  fninit
  movsx eax,bx
  push 0
  push eax
  push esi
  push edi
  call captureHit
  add esp,16
  fxrstor [esp]
  mov esp,ebp
  popad
  popfd
  add word ptr [eax+04b6h],bx
  ret
 }
}
// Scripted damage uses ESI=attack, EDI=victim at its combo increment.
__declspec(naked) inline void scriptedSettlementStub() {
 __asm {
  pushfd
  pushad
  mov ebp,esp
  sub esp,528
  and esp,0fffffff0h
  fxsave [esp]
  fninit
  movsx eax,bx
  push 1
  push eax
  push edi
  push esi
  call captureHit
  add esp,16
  fxrstor [esp]
  mov esp,ebp
  popad
  popfd
  add word ptr [eax+04b6h],bx
  ret
 }
}
using SetAction = void (__thiscall *)(void*,short);
using SetActionSequence = void (__thiscall *)(void*,short,short);
inline SetAction originalAction=nullptr;
inline SetActionSequence originalActionSequence=nullptr;
inline void __fastcall setAction(void *self,void*,short action) {
 captureAction(self,action);originalAction(self,action);
}
inline void __fastcall setActionSequence(void *self,void*,short action,short sequence) {
 captureAction(self,action,sequence);originalActionSequence(self,action,sequence);
}
using Create = void *(__thiscall *)(void*,short,float,float,int,int,void*,unsigned);
using Factory = void *(__thiscall *)(void*,void*,void*,short,float,float,int,int,void*,unsigned);
inline Create originalRoot=nullptr,originalObject=nullptr,originalChild=nullptr;
inline Factory originalFactory=nullptr;
struct CreatorScope {
 void *prior;
 explicit CreatorScope(void *p):prior(creator){creator=p;}
 ~CreatorScope(){creator=prior;}
};
inline void *__fastcall rootCreate(void *self,void*,short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 CreatorScope scope(self);return originalRoot(self,action,x,y,dir,layer,data,size);
}
inline void *__fastcall objectCreate(void *self,void*,short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 CreatorScope scope(self);return originalObject(self,action,x,y,dir,layer,data,size);
}
inline void *__fastcall childCreate(void *self,void*,short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 CreatorScope scope(self);return originalChild(self,action,x,y,dir,layer,data,size);
}
inline void *__fastcall factoryCreate(void *self,void*,void *parent,void *ally,short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 if(!capturing())return originalFactory(self,parent,ally,action,x,y,dir,layer,data,size);
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 void *producer=parent?parent:creator;
 const auto producerGeneration=producer&&!isPlayer(producer)?read<unsigned>(producer,0x350):0;
 const auto root=isPlayer(producer)?actors.get(side(producer)):whm::NativeCast{};
 void *result=originalFactory(self,parent,ally,action,x,y,dir,layer,data,size);
 if(capturing()&&result) {
  void *owner=read<void*>(result,0x168);
  if(isPlayer(owner)) {
   Event e;e.kind=Event::Birth;e.frame=frame;e.object=reinterpret_cast<std::uintptr_t>(result);
   e.generation=read<unsigned>(result,0x350);e.owner=reinterpret_cast<std::uintptr_t>(owner);
   e.ally=read<std::uintptr_t>(result,0x16c);e.parent=reinterpret_cast<std::uintptr_t>(parent);
   e.creator=reinterpret_cast<std::uintptr_t>(producer);e.caller=caller;
   e.cast=root.id;e.castAction=root.action;e.parentGeneration=parent?producerGeneration:0;
   e.creatorGeneration=producerGeneration;
   e.action=action;e.sequence=read<unsigned short>(result,0x13e);
   e.ownerAction=read<unsigned short>(owner,0x13c);append(e);
  }
 }
 return result;
}
struct Patch {void *site=nullptr,*bridge=nullptr;unsigned length=0;std::array<unsigned char,8> original{};};
inline std::array<Patch,8> patches{};
inline void jump(void *destination,void *target) {
 auto p=static_cast<unsigned char*>(destination);p[0]=0xe9;
 const auto delta=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(p)-5);
 std::memcpy(p+1,&delta,4);
}
inline bool writePatch(Patch &p,void *hook,bool call) {
 DWORD old=0;if(!VirtualProtect(p.site,p.length,PAGE_EXECUTE_READWRITE,&old))return false;
 jump(p.site,hook);if(call)*static_cast<unsigned char*>(p.site)=0xe8;
 for(unsigned i=5;i<p.length;++i)static_cast<unsigned char*>(p.site)[i]=0x90;
 DWORD ignored;VirtualProtect(p.site,p.length,old,&ignored);
 FlushInstructionCache(GetCurrentProcess(),p.site,p.length);return true;
}
inline void restore(Patch &p) {
 DWORD old=0;if(!VirtualProtect(p.site,p.length,PAGE_EXECUTE_READWRITE,&old))return;
 std::memcpy(p.site,p.original.data(),p.length);DWORD ignored;
 VirtualProtect(p.site,p.length,old,&ignored);FlushInstructionCache(GetCurrentProcess(),p.site,p.length);
}
inline bool install(std::uintptr_t imageBase=0x400000) {
 if(installed)return true;
 const unsigned char creatorBytes[]={0xd9,0x44,0x24,0x0c,0x56};
 const unsigned char factoryBytes[]={0x56,0x57,0x8b,0xf9,0x8d,0x4f,0x04};
 const unsigned char hitBytes[]={0x66,0x01,0x98,0xb6,0x04,0x00,0x00};
 const unsigned char actionBytes[]={0x66,0x8b,0x44,0x24,0x04};
 const std::uintptr_t addresses[]={0x46eb30,0x59b6c0,0x5b8ed0,0x4d2610,0x47b05c,0x47b28f,0x438ca0,0x438cc0};
 void *hooks[]={reinterpret_cast<void*>(&rootCreate),reinterpret_cast<void*>(&objectCreate),
     reinterpret_cast<void*>(&childCreate),reinterpret_cast<void*>(&factoryCreate),reinterpret_cast<void*>(&settlementStub),
     reinterpret_cast<void*>(&scriptedSettlementStub),reinterpret_cast<void*>(&setAction),reinterpret_cast<void*>(&setActionSequence)};
 // Reject modified/unsupported code before writing ANY location.
 for(unsigned i=0;i<8;++i) {
  auto &p=patches[i];p.site=reinterpret_cast<void*>(imageBase+addresses[i]-0x400000);p.length=(i<3||i>=6)?5:7;
  auto expected=i<3?creatorBytes:i==3?factoryBytes:i<6?hitBytes:actionBytes;
  if(std::memcmp(p.site,expected,p.length)!=0)return false;
  std::memcpy(p.original.data(),p.site,p.length);
 }
 for(unsigned i=0;i<8;++i) {
  if(i==4||i==5)continue;
  auto &p=patches[i];p.bridge=VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
  if(!p.bridge){for(unsigned j=0;j<i;++j)if(patches[j].bridge)VirtualFree(patches[j].bridge,0,MEM_RELEASE);return false;}
  std::memcpy(p.bridge,p.original.data(),p.length);
  jump(static_cast<unsigned char*>(p.bridge)+p.length,static_cast<unsigned char*>(p.site)+p.length);
 }
 originalRoot=reinterpret_cast<Create>(patches[0].bridge);originalObject=reinterpret_cast<Create>(patches[1].bridge);
 originalChild=reinterpret_cast<Create>(patches[2].bridge);originalFactory=reinterpret_cast<Factory>(patches[3].bridge);
 originalAction=reinterpret_cast<SetAction>(patches[6].bridge);
 originalActionSequence=reinterpret_cast<SetActionSequence>(patches[7].bridge);
 for(unsigned i=0;i<8;++i)if(!writePatch(patches[i],hooks[i],i==4||i==5)) {
  for(unsigned j=0;j<i;++j)restore(patches[j]);
  for(unsigned j=0;j<8;++j)if(patches[j].bridge)VirtualFree(patches[j].bridge,0,MEM_RELEASE);return false;
 }
 installed=true;return true;
}
} // namespace whm::native
