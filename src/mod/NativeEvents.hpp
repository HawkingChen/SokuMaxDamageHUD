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
inline whm::NativeCast ghostPending[2]{},ghostBound[2]{};
inline const void *ghostObjects[2]{};
inline unsigned ghostGenerations[2]{};
inline std::array<Event,4096> events{};
inline unsigned count=0,dropped=0,frame=0;
inline const void *players[2]{};
inline bool active=false,installed=false;
inline DWORD gameThread=0;
inline thread_local void *creator=nullptr;
inline thread_local void *updating=nullptr;
inline thread_local std::uintptr_t creatorCaller=0;
using Update=void (__thiscall *)(void*);
inline std::array<Update,20> originalUpdates{};
struct UpdateScope {
 void *prior;
 explicit UpdateScope(void *p):prior(updating){updating=p;}
 ~UpdateScope(){updating=prior;}
};
template<typename T> inline T read(const void *p,unsigned offset) noexcept {
 T v{};if(p)std::memcpy(&v,static_cast<const char*>(p)+offset,sizeof(v));return v;
}
inline bool isPlayer(const void *p){return p&&(p==players[0]||p==players[1]);}
inline bool capturing(){return active&&GetCurrentThreadId()==gameThread;}
inline void beginGhostCommand(void *self) noexcept;
inline void endGhostCommand(void *self) noexcept;
template<unsigned I> inline void __fastcall objectUpdate(void *self,void*) {
 if(!capturing()){originalUpdates[I](self);return;}
 UpdateScope scope(self);
 if constexpr(I==5)beginGhostCommand(self);
 originalUpdates[I](self);
 if constexpr(I==5)endGhostCommand(self);
}
inline void append(Event e) noexcept {if(count<events.size())events[count++]=e;else ++dropped;}
inline void clearGhostCommands() {
 for(unsigned side=0;side<2;++side){ghostPending[side]={};ghostBound[side]={};ghostObjects[side]=nullptr;ghostGenerations[side]=0;}
}
inline void begin(unsigned f,const void *left,const void *right) noexcept {
 if(players[0]!=left || players[1]!=right){actors.reset();clearGhostCommands();}
 count=0;dropped=0;frame=f;players[0]=left;players[1]=right;
 gameThread=GetCurrentThreadId();active=installed;
}
inline void end() noexcept {active=false;}
inline unsigned side(const void *p) {return p==players[0]?0:1;}
inline void resetActors() {actors.reset();clearGhostCommands();players[0]=players[1]=nullptr;}
// Youmu's persistent half-ghost (899) consumes the explicit +0x8D2
// command mailbox. It is not a fresh projectile born for each attack.
inline void beginGhostCommand(void *self) noexcept {
 if(read<unsigned short>(self,0x13c)!=899 || read<unsigned short>(self,0x13e)!=0)return;
 void *owner=read<void*>(self,0x168);if(!isPlayer(owner)||read<short>(owner,0x8d2)<=0)return;
 const unsigned index=side(owner),generation=read<unsigned>(self,0x350);
 const auto root=ghostPending[index].valid()?ghostPending[index]:actors.get(index);
 if(root.valid()&&ghostBound[index].id==root.id&&ghostObjects[index]==self&&ghostGenerations[index]==generation)return;
 Event e;e.kind=Event::Rebind;e.frame=frame;e.object=reinterpret_cast<std::uintptr_t>(self);
 e.generation=generation;e.owner=e.creator=e.ally=reinterpret_cast<std::uintptr_t>(owner);
 e.cast=root.id;e.castAction=root.action;e.action=899;
 append(e);ghostBound[index]=root;ghostObjects[index]=self;ghostGenerations[index]=generation;
}
inline void endGhostCommand(void *self) noexcept {
 void *owner=read<void*>(self,0x168);
 if(isPlayer(owner)&&read<short>(owner,0x8d2)==0)ghostPending[side(owner)]={};
}
inline void captureAction(void *self,int action,int sequence=-1) {
 if(!capturing()||!isPlayer(self))return;
 // Preserve the issued command's cast if the owner leaves the move before
 // a stopped half-ghost gets an update. Never borrow a later unrelated cast.
 if(read<int>(self,0x34c)==5) {
  auto &pending=ghostPending[side(self)];
  if(read<short>(self,0x8d2)<=0)pending={};
  else if(!pending.valid())pending=actors.get(side(self));
 }
 const auto cast=actors.set(side(self),action,sequence,read<int>(self,0x34c));
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
 e.victimAction=read<unsigned short>(target,0x13c);e.ownerCharacter=read<int>(owner,0x34c);
 e.damage=amount;e.hp=read<short>(target,0x184);e.ownerHP=read<short>(owner,0x184);
 e.comboHits=read<short>(reinterpret_cast<void*>(e.ally),0x4b4);
 e.comboDamageBefore=read<short>(reinterpret_cast<void*>(e.ally),0x4b6);
 e.protectedHP=read<unsigned char>(target,0x575);
 if(attack!=owner){e.generation=read<unsigned>(attack,0x350);e.parent=read<std::uintptr_t>(attack,0x368);}
 append(e);
}
// Called immediately BEFORE a verified damage-over-time HP store.
// Derive the amount from its exact old/new values, not from a frame delta.
__declspec(noinline) inline void __cdecl captureDirectHP(void *attack,void *target,int afterHP) noexcept {
 if(!capturing()||!isPlayer(target))return;
 const int amount=static_cast<int>(read<short>(target,0x184))-afterHP;
 if(amount>0)captureHit(attack,target,amount,2);
}
// 1.10a 0x63E650: ESI=gas object, ECX=victim, AX=new HP.
__declspec(naked) inline void directHPStub() {
 __asm {
  pushfd
  pushad
  mov ebp,esp
  sub esp,528
  and esp,0fffffff0h
  fxsave [esp]
  fninit
  movsx eax,ax
  push eax
  push ecx
  push esi
  call captureDirectHP
  add esp,12
  fxrstor [esp]
  mov esp,ebp
  popad
  popfd
  mov word ptr [ecx+0184h],ax
  ret
 }
}
// Yuyuko poison's low-HP branch stores BX=1 instead of subtracting four.
__declspec(naked) inline void directHPFloorStub() {
 __asm {
  pushfd
  pushad
  mov ebp,esp
  sub esp,528
  and esp,0fffffff0h
  fxsave [esp]
  fninit
  movsx eax,bx
  push eax
  push ecx
  push esi
  call captureDirectHP
  add esp,12
  fxrstor [esp]
  mov esp,ebp
  popad
  popfd
  mov word ptr [ecx+0184h],bx
  ret
 }
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
using SetAction = bool (__thiscall *)(void*,short);
using SetActionSequence = void (__thiscall *)(void*,short,short);
inline SetAction originalAction=nullptr;
inline SetActionSequence originalActionSequence=nullptr;
inline bool __fastcall setAction(void *self,void*,short action) {
 captureAction(self,action);return originalAction(self,action);
}
inline void __fastcall setActionSequence(void *self,void*,short action,short sequence) {
 captureAction(self,action,sequence);originalActionSequence(self,action,sequence);
}
using Create = void *(__thiscall *)(void*,short,float,float,int,int,void*,unsigned);
using Factory = void *(__thiscall *)(void*,void*,void*,short,float,float,int,int,void*,unsigned);
inline Create originalRoot=nullptr,originalObject=nullptr,originalChild=nullptr;
inline std::array<Factory,20> originalFactories{};
struct CreatorScope {
 void *prior;std::uintptr_t priorCaller;
 explicit CreatorScope(void *p,std::uintptr_t caller=0):prior(creator),priorCaller(creatorCaller){creator=p;creatorCaller=caller;}
 ~CreatorScope(){creator=prior;creatorCaller=priorCaller;}
};
inline void *__fastcall rootCreate(void *self,void*,short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 // The player can be called as a spawn service by a delayed projectile.
 // Preserve the currently executing projectile as the causal creator.
 void *producer=capturing()&&updating&&read<void*>(updating,0x168)==self?updating:self;
 CreatorScope scope(producer,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));return originalRoot(self,action,x,y,dir,layer,data,size);
}
inline void *__fastcall objectCreate(void *self,void*,short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 CreatorScope scope(self,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));return originalObject(self,action,x,y,dir,layer,data,size);
}
inline void *__fastcall childCreate(void *self,void*,short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 CreatorScope scope(self,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));return originalChild(self,action,x,y,dir,layer,data,size);
}
template<unsigned I> inline void *__fastcall factoryCreate(void *self,void*,void *parent,void *ally,short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 if(!capturing())return originalFactories[I](self,parent,ally,action,x,y,dir,layer,data,size);
 const auto caller=creatorCaller?creatorCaller:reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 void *producer=parent?parent:creator?creator:updating;
 const auto producerGeneration=producer&&!isPlayer(producer)?read<unsigned>(producer,0x350):0;
 const auto root=isPlayer(producer)?actors.get(side(producer)):whm::NativeCast{};
 void *result=originalFactories[I](self,parent,ally,action,x,y,dir,layer,data,size);
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
inline std::array<Patch,10> patches{};
inline std::array<Patch,20> updatePatches{};
inline std::array<Patch,20> factoryPatches{};
inline constexpr std::array<std::array<std::uintptr_t,2>,20> factorySites{{
 {0x85bf1c,0x4ae480},{0x85c34c,0x4d2610},{0x85c6d4,0x4ff380},{0x85c9c4,0x5246e0},
 {0x85ccfc,0x556250},{0x85cfac,0x57ad00},{0x85d254,0x59be20},{0x85d4f4,0x5b95f0},
 {0x85d78c,0x5dec30},{0x85da54,0x609bd0},{0x85dd2c,0x633d10},{0x85e20c,0x67d670},
 {0x85df7c,0x657f50},{0x85e474,0x69c380},{0x85e6ec,0x6c11c0},{0x85efbc,0x74c8f0},
 {0x85ea84,0x6fb6c0},{0x85ed0c,0x72e300},{0x85f5bc,0x7b7750},{0x85f28c,0x784020}
}};
inline void *factoryHooks[]={
reinterpret_cast<void*>(&factoryCreate<0>),reinterpret_cast<void*>(&factoryCreate<1>),reinterpret_cast<void*>(&factoryCreate<2>),reinterpret_cast<void*>(&factoryCreate<3>),reinterpret_cast<void*>(&factoryCreate<4>),reinterpret_cast<void*>(&factoryCreate<5>),reinterpret_cast<void*>(&factoryCreate<6>),reinterpret_cast<void*>(&factoryCreate<7>),reinterpret_cast<void*>(&factoryCreate<8>),reinterpret_cast<void*>(&factoryCreate<9>),reinterpret_cast<void*>(&factoryCreate<10>),reinterpret_cast<void*>(&factoryCreate<11>),reinterpret_cast<void*>(&factoryCreate<12>),reinterpret_cast<void*>(&factoryCreate<13>),reinterpret_cast<void*>(&factoryCreate<14>),reinterpret_cast<void*>(&factoryCreate<15>),reinterpret_cast<void*>(&factoryCreate<16>),reinterpret_cast<void*>(&factoryCreate<17>),reinterpret_cast<void*>(&factoryCreate<18>),reinterpret_cast<void*>(&factoryCreate<19>)
};
inline const std::array<std::array<std::uintptr_t,2>,20> updateSites{{
 {0x85bebc,0x4b1070},{0x85c2ec,0x4d5200},{0x85c674,0x501be0},{0x85c964,0x527280},
 {0x85cc9c,0x558df0},{0x85cf4c,0x57d0b0},{0x85d1f4,0x59dfb0},{0x85d494,0x5bbb00},
 {0x85d72c,0x5e1670},{0x85d9f4,0x60c200},{0x85dccc,0x636360},{0x85e1ac,0x6800f0},
 {0x85df1c,0x65a290},{0x85e414,0x69e9d0},{0x85e68c,0x6c3950},{0x85ef5c,0x74f190},
 {0x85ea24,0x6fdff0},{0x85ecac,0x7304d0},{0x85f55c,0x7ba1f0},{0x85f22c,0x7876d0}
}};
inline void *updateHooks[]={
reinterpret_cast<void*>(&objectUpdate<0>),reinterpret_cast<void*>(&objectUpdate<1>),reinterpret_cast<void*>(&objectUpdate<2>),reinterpret_cast<void*>(&objectUpdate<3>),reinterpret_cast<void*>(&objectUpdate<4>),reinterpret_cast<void*>(&objectUpdate<5>),reinterpret_cast<void*>(&objectUpdate<6>),reinterpret_cast<void*>(&objectUpdate<7>),reinterpret_cast<void*>(&objectUpdate<8>),reinterpret_cast<void*>(&objectUpdate<9>),reinterpret_cast<void*>(&objectUpdate<10>),reinterpret_cast<void*>(&objectUpdate<11>),reinterpret_cast<void*>(&objectUpdate<12>),reinterpret_cast<void*>(&objectUpdate<13>),reinterpret_cast<void*>(&objectUpdate<14>),reinterpret_cast<void*>(&objectUpdate<15>),reinterpret_cast<void*>(&objectUpdate<16>),reinterpret_cast<void*>(&objectUpdate<17>),reinterpret_cast<void*>(&objectUpdate<18>),reinterpret_cast<void*>(&objectUpdate<19>)
};
inline bool writePointer(Patch &p,void *value) {
 DWORD old=0;if(!VirtualProtect(p.site,p.length,PAGE_EXECUTE_READWRITE,&old))return false;
 std::memcpy(p.site,&value,sizeof(value));DWORD ignored;
 VirtualProtect(p.site,p.length,old,&ignored);return true;
}
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
 const unsigned char hitBytes[]={0x66,0x01,0x98,0xb6,0x04,0x00,0x00};
 const unsigned char actionBytes[]={0x66,0x8b,0x44,0x24,0x04};
 const unsigned char poisonBytes[]={0x66,0x89,0x81,0x84,0x01,0x00,0x00};
 const unsigned char floorBytes[]={0x66,0x89,0x99,0x84,0x01,0x00,0x00};
 const std::uintptr_t addresses[]={0x46eb30,0x59b6c0,0x5b8ed0,0x47b05c,0x47b28f,0x438ca0,0x438cc0,0x63e650,0x5c2102,0x5c2110};
 void *hooks[]={reinterpret_cast<void*>(&rootCreate),reinterpret_cast<void*>(&objectCreate),
     reinterpret_cast<void*>(&childCreate),reinterpret_cast<void*>(&settlementStub),
     reinterpret_cast<void*>(&scriptedSettlementStub),reinterpret_cast<void*>(&setAction),reinterpret_cast<void*>(&setActionSequence),reinterpret_cast<void*>(&directHPStub),reinterpret_cast<void*>(&directHPStub),reinterpret_cast<void*>(&directHPFloorStub)};
 // Reject modified/unsupported code before writing ANY location.
 for(unsigned i=0;i<patches.size();++i) {
  auto &p=patches[i];p.site=reinterpret_cast<void*>(imageBase+addresses[i]-0x400000);p.length=(i<3||i==5||i==6)?5:7;
  auto expected=i<3?creatorBytes:i<5?hitBytes:i<7?actionBytes:i<9?poisonBytes:floorBytes;
  if(std::memcmp(p.site,expected,p.length)!=0)return false;
  std::memcpy(p.original.data(),p.site,p.length);
 }
 // Update dispatch identifies the object whose script invoked a player
 // spawn service. Validate every slot before applying any code/table patch.
 for(unsigned i=0;i<updatePatches.size();++i) {
  auto &p=updatePatches[i];p.site=reinterpret_cast<void*>(imageBase+updateSites[i][0]-0x400000+10*4);p.length=4;
  // 1.10a is a fixed-base EXE; its vtable entries remain absolute even
  // when the optional test maps the PE at a different address.
  const auto expected=updateSites[i][1];
  if(read<std::uintptr_t>(p.site,0)!=expected)return false;
  std::memcpy(p.original.data(),p.site,p.length);
  originalUpdates[i]=reinterpret_cast<Update>(expected);
 }
 for(unsigned i=0;i<factoryPatches.size();++i) {
  auto &p=factoryPatches[i];p.site=reinterpret_cast<void*>(imageBase+factorySites[i][0]-0x400000+4);p.length=4;
  const auto expected=factorySites[i][1];
  if(read<std::uintptr_t>(p.site,0)!=expected)return false;
  std::memcpy(p.original.data(),p.site,p.length);
  originalFactories[i]=reinterpret_cast<Factory>(expected);
 }
 for(unsigned i=0;i<patches.size();++i) {
  if(i==3||i==4||i>=7)continue;
  auto &p=patches[i];p.bridge=VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
  if(!p.bridge){for(unsigned j=0;j<i;++j)if(patches[j].bridge)VirtualFree(patches[j].bridge,0,MEM_RELEASE);return false;}
  std::memcpy(p.bridge,p.original.data(),p.length);
  jump(static_cast<unsigned char*>(p.bridge)+p.length,static_cast<unsigned char*>(p.site)+p.length);
 }
 originalRoot=reinterpret_cast<Create>(patches[0].bridge);originalObject=reinterpret_cast<Create>(patches[1].bridge);
 originalChild=reinterpret_cast<Create>(patches[2].bridge);
 originalAction=reinterpret_cast<SetAction>(patches[5].bridge);
 originalActionSequence=reinterpret_cast<SetActionSequence>(patches[6].bridge);
 for(unsigned i=0;i<patches.size();++i)if(!writePatch(patches[i],hooks[i],i==3||i==4||i>=7)) {
  for(unsigned j=0;j<i;++j)restore(patches[j]);
  for(unsigned j=0;j<patches.size();++j)if(patches[j].bridge)VirtualFree(patches[j].bridge,0,MEM_RELEASE);return false;
 }
 for(unsigned i=0;i<updatePatches.size();++i)if(!writePointer(updatePatches[i],updateHooks[i])) {
  for(unsigned j=0;j<i;++j)restore(updatePatches[j]);
  for(auto &p:patches){restore(p);if(p.bridge)VirtualFree(p.bridge,0,MEM_RELEASE);}
  return false;
 }
 for(unsigned i=0;i<factoryPatches.size();++i)if(!writePointer(factoryPatches[i],factoryHooks[i])) {
  for(unsigned j=0;j<i;++j)restore(factoryPatches[j]);
  for(auto &p:updatePatches)restore(p);
  for(auto &p:patches){restore(p);if(p.bridge)VirtualFree(p.bridge,0,MEM_RELEASE);}
  return false;
 }
 installed=true;return true;
}
} // namespace whm::native
