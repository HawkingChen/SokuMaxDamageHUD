#define NOMINMAX
#include "NativeEvents.hpp"
#include <cassert>
#include <iostream>
using namespace whm::native;
struct alignas(16) Result {unsigned regs[8],flags,padding[3];unsigned char fx[512];};
__declspec(naked) void __cdecl runInstruction(void *code,void *attack,void *victim,void *ally,int amount,Result *out) {
 __asm {
  push ebp
  mov ebp,esp
  push ebx
  push esi
  push edi
  mov eax,[ebp+28]
  mov [eax+36],ebp
  lea edx,[esp-4]
  mov [eax+40],edx
  fninit
  sub esp,4
  fnstcw [esp]
  or word ptr [esp],0800h
  fldcw [esp]
  add esp,4
  fld1
  fldpi
  mov eax,[ebp+20]
  mov ebx,[ebp+24]
  mov esi,[ebp+16]
  mov edi,[ebp+12]
  mov ecx,012345678h
  mov edx,076543210h
  call dword ptr [ebp+8]
  pushfd
  pushad
  mov edi,[ebp+28]
  mov esi,esp
  mov ecx,9
  rep movsd
  mov edi,[ebp+28]
  add edi,48
  fxsave [edi]
  popad
  popfd
  fninit
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
std::array<unsigned char,4096> p1{},p2{},bullet{},parentObject{};
void *factoryResult=bullet.data();
template<class T>void put(void *p,unsigned o,T x){std::memcpy(static_cast<char*>(p)+o,&x,sizeof(x));}
void * __fastcall fakeFactory(void*,void*,void*,void*,short action,float x,float y,int dir,int layer,void*,unsigned size) {
 assert(action==1010&&x==123.5f&&y==-17.25f&&dir==-1&&layer==2&&size==3);return factoryResult;
}
unsigned lastFactory=999;
template<unsigned I> void *__fastcall fakeIndexedFactory(void *self,void *edx,void *parent,void *ally,
 short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 lastFactory=I;return fakeFactory(self,edx,parent,ally,action,x,y,dir,layer,data,size);
}
Factory fakeFactories[]={
reinterpret_cast<Factory>(&fakeIndexedFactory<0>),reinterpret_cast<Factory>(&fakeIndexedFactory<1>),reinterpret_cast<Factory>(&fakeIndexedFactory<2>),reinterpret_cast<Factory>(&fakeIndexedFactory<3>),reinterpret_cast<Factory>(&fakeIndexedFactory<4>),reinterpret_cast<Factory>(&fakeIndexedFactory<5>),reinterpret_cast<Factory>(&fakeIndexedFactory<6>),reinterpret_cast<Factory>(&fakeIndexedFactory<7>),reinterpret_cast<Factory>(&fakeIndexedFactory<8>),reinterpret_cast<Factory>(&fakeIndexedFactory<9>),reinterpret_cast<Factory>(&fakeIndexedFactory<10>),reinterpret_cast<Factory>(&fakeIndexedFactory<11>),reinterpret_cast<Factory>(&fakeIndexedFactory<12>),reinterpret_cast<Factory>(&fakeIndexedFactory<13>),reinterpret_cast<Factory>(&fakeIndexedFactory<14>),reinterpret_cast<Factory>(&fakeIndexedFactory<15>),reinterpret_cast<Factory>(&fakeIndexedFactory<16>),reinterpret_cast<Factory>(&fakeIndexedFactory<17>),reinterpret_cast<Factory>(&fakeIndexedFactory<18>),reinterpret_cast<Factory>(&fakeIndexedFactory<19>)
};
void * __fastcall fakeRoot(void *self,void*,short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 return factoryCreate<17>(nullptr,nullptr,nullptr,self,action,x,y,dir,layer,data,size);
}
bool __fastcall fakeAction(void *self,void*,short action) {
 put(self,0x13c,static_cast<unsigned short>(action));return action>=300;
}
void __fastcall fakeActionSequence(void *self,void*,short action,short seq) {
 fakeAction(self,nullptr,action);put(self,0x13e,static_cast<unsigned short>(seq));
}
void __fastcall fakeDelayedUpdate(void *self,void*) {
 assert(updating==self);
 // Reusable Bomb's emitter calls PLAYER.createObject while the player is idle.
 rootCreate(p1.data(),nullptr,1010,123.5f,-17.25f,-1,2,nullptr,3);
}
int main(int argc,char **argv){
 put(p1.data(),0x13c,static_cast<unsigned short>(614));put(p1.data(),0x184,static_cast<short>(10000));
 put(p1.data(),0x4b4,static_cast<short>(4));put(p2.data(),0x184,static_cast<short>(9000));
 put(bullet.data(),0x168,static_cast<void*>(p1.data()));put(bullet.data(),0x16c,static_cast<void*>(p1.data()));
 put(bullet.data(),0x13c,static_cast<unsigned short>(1010));put(bullet.data(),0x350,33u);
 put(parentObject.data(),0x350,22u);
 put(parentObject.data(),0x168,static_cast<void*>(p1.data()));
 put(parentObject.data(),0x16c,static_cast<void*>(p1.data()));
 installed=true;begin(100,p1.data(),p2.data());
 auto code=static_cast<unsigned char*>(VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(code);
 const unsigned char original[]={0x66,0x01,0x98,0xb6,0x04,0x00,0x00,0xc3};std::memcpy(code,original,sizeof(original));
 Result baseline{},hooked{};put(p1.data(),0x4b6,static_cast<short>(700));
 runInstruction(code,bullet.data(),p2.data(),p1.data(),1234,&baseline);
 assert(read<short>(p1.data(),0x4b6)==1934);
 Patch patch;patch.site=code;patch.length=7;std::memcpy(patch.original.data(),original,7);
 assert(writePatch(patch,reinterpret_cast<void*>(&settlementStub),true));
 put(p1.data(),0x4b6,static_cast<short>(700));runInstruction(code,bullet.data(),p2.data(),p1.data(),1234,&hooked);
 assert(read<short>(p1.data(),0x4b6)==1934&&count==1);
 // The compiler may reserve different outgoing-argument areas at the two
 // call sites. EBP/ESP must match each invocation's own entry, not each other.
 assert(baseline.regs[2]==baseline.padding[0]&&hooked.regs[2]==hooked.padding[0]);
 assert(baseline.regs[3]==baseline.padding[1]&&hooked.regs[3]==hooked.padding[1]);
 for(unsigned i:{0u,1u,4u,5u,6u,7u})assert(baseline.regs[i]==hooked.regs[i]);
 assert((baseline.flags&0x8d5)==(hooked.flags&0x8d5));
 assert(std::memcmp(baseline.fx,hooked.fx,8)==0); // x87 control, status and tag
 assert(std::memcmp(baseline.fx+24,hooked.fx+24,8)==0); // MXCSR
 assert(std::memcmp(baseline.fx+32,hooked.fx+32,128)==0); // x87 values
 assert(std::memcmp(baseline.fx+160,hooked.fx+160,128)==0); // SSE values
 assert(events[0].damage==1234&&events[0].hp==9000&&events[0].comboDamageBefore==700);
 assert(events[0].object==reinterpret_cast<std::uintptr_t>(bullet.data())&&events[0].generation==33);
 assert(events[0].owner!=events[0].victim);
 runInstruction(code,bullet.data(),p1.data(),p1.data(),5333,&hooked);
 assert(count==2&&events[1].damage==5333&&events[1].owner==events[1].victim);
 restore(patch);assert(std::memcmp(code,original,sizeof(original))==0);
 // Scripted damage has the opposite ESI/EDI assignment at its ADD site.
 assert(writePatch(patch,reinterpret_cast<void*>(&scriptedSettlementStub),true));
 runInstruction(code,p2.data(),bullet.data(),p1.data(),4000,&hooked);
 assert(count==3&&events[2].path==1&&events[2].damage==4000);
 assert(events[2].victim==reinterpret_cast<std::uintptr_t>(p2.data()));
 assert(events[2].object==reinterpret_cast<std::uintptr_t>(bullet.data()));
 assert(std::memcmp(baseline.fx,hooked.fx,8)==0);
 assert(std::memcmp(baseline.fx+32,hooked.fx+32,128)==0);
 restore(patch);VirtualFree(code,0,MEM_RELEASE);
 originalAction=reinterpret_cast<SetAction>(&fakeAction);
 originalActionSequence=reinterpret_cast<SetActionSequence>(&fakeActionSequence);
 assert(setAction(p1.data(),nullptr,614));
 const auto spellCast=actors.get(0);
 assert(spellCast.valid()&&read<unsigned short>(p1.data(),0x13c)==614);
 setAction(p1.data(),nullptr,664);
 assert(actors.get(0).id==spellCast.id);
 setActionSequence(p1.data(),nullptr,614,0);
 assert(actors.get(0).id!=spellCast.id&&read<unsigned short>(p1.data(),0x13c)==614);
 // Native creator identity is carried through the original factory call,
 // even when the child's persistent parentObject is null.
 originalFactories[17]=reinterpret_cast<Factory>(&fakeFactory);
 originalRoot=reinterpret_cast<Create>(&fakeRoot);originalObject=originalRoot;originalChild=originalRoot;
 rootCreate(p1.data(),nullptr,1010,123.5f,-17.25f,-1,2,nullptr,3);
 assert(events[count-1].kind==Event::Birth&&events[count-1].creator==reinterpret_cast<std::uintptr_t>(p1.data()));
 assert(events[count-1].cast==actors.get(0).id&&events[count-1].castAction==614);
 objectCreate(parentObject.data(),nullptr,1010,123.5f,-17.25f,-1,2,nullptr,3);
 assert(events[count-1].creator==reinterpret_cast<std::uintptr_t>(parentObject.data()));
 assert(events[count-1].creatorGeneration==22&&events[count-1].parent==0&&creator==nullptr);
 assert(!setAction(p1.data(),nullptr,0));
 // Real 22B failure: actor 550 creates an emitter, returns to idle, then
 // the emitter asks the player to create its explosion. No damage guessing.
 assert(setAction(p1.data(),nullptr,550));factoryResult=parentObject.data();
 rootCreate(p1.data(),nullptr,1010,123.5f,-17.25f,-1,2,nullptr,3);
 const auto emitterBirth=events[count-1];
 assert(emitterBirth.castAction==550);
 assert(!setAction(p1.data(),nullptr,0));factoryResult=bullet.data();
 originalUpdates[1]=reinterpret_cast<Update>(&fakeDelayedUpdate);
 objectUpdate<1>(parentObject.data(),nullptr);
 const auto explosionBirth=events[count-1];
 assert(explosionBirth.creator==reinterpret_cast<std::uintptr_t>(parentObject.data()));
 assert(explosionBirth.creatorGeneration==22&&explosionBirth.ownerAction==0);
 assert(updating==nullptr&&creator==nullptr);
 captureHit(bullet.data(),p2.data(),1650);
 const std::array<whm::NativeEvent,3> delayed={emitterBirth,explosionBirth,events[count-1]};
 whm::NativeAttribution sources;
 const auto attributed=sources.consume(delayed,reinterpret_cast<std::uintptr_t>(p1.data()));
 assert(attributed.size()==1&&attributed[0].damage==1650&&attributed[0].action==550&&attributed[0].instance);
 // Every character has a distinct factory. Verify that each dispatch
 // forwards to ITS own original, rather than only testing Marisa's factory.
 assert(setAction(p1.data(),nullptr,561));
 using FactoryHook=void *(__fastcall *)(void*,void*,void*,void*,short,float,float,int,int,void*,unsigned);
 for(unsigned i=0;i<20;++i) {
  originalFactories[i]=fakeFactories[i];
  CreatorScope root(p1.data());
  auto fn=reinterpret_cast<FactoryHook>(factoryHooks[i]);
  fn(nullptr,nullptr,nullptr,p1.data(),1010,123.5f,-17.25f,-1,2,nullptr,3);
  assert(lastFactory==i&&events[count-1].castAction==561);
 }
 // Meiling Fang Po: five separately allocated projectiles, ONE cast.
 std::vector<whm::NativeEvent> waves;
 const int damage[]={500,366,336,310,285};
 for(unsigned i=0;i<5;++i) {
  put(bullet.data(),0x350,1000u+i);
  rootCreate(p1.data(),nullptr,1010,123.5f,-17.25f,-1,2,nullptr,3);
  waves.push_back(events[count-1]);captureHit(bullet.data(),p2.data(),damage[i]);
  waves.push_back(events[count-1]);
 }
 whm::NativeAttribution fangPo;
 const auto five=fangPo.consume(waves,reinterpret_cast<std::uintptr_t>(p1.data()));
 assert(five.size()==5);int total=0;
 for(const auto &h:five){assert(h.action==561&&h.instance==five[0].instance);total+=h.damage;}
 assert(total==1797);
 end();auto oldCount=count;captureHit(bullet.data(),p2.data(),500);assert(count==oldCount);
 if(argc>1) {
  // Optional local integration check: map the real PE without executing its
  // entry point, install all signature-checked patches, then restore them.
  HMODULE game=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
  assert(game);
  // Audit the assumption of shared projectile creator functions across
  // ALL characters, separately from the per-character factory addresses.
  for(const auto &row:updateSites) {
   auto table=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(game)+row[0]-0x400000);
   assert(read<std::uintptr_t>(table,17*4)==0x59b6c0);
   assert(read<std::uintptr_t>(table,18*4)==0x5b8ed0);
  }
  installed=false;assert(install(reinterpret_cast<std::uintptr_t>(game)));
  for(unsigned i=0;i<patches.size();++i) {
   auto &p=patches[i];
   assert(*static_cast<unsigned char*>(p.site)==(i==3||i==4?0xe8:0xe9));
   restore(p);assert(std::memcmp(p.site,p.original.data(),p.length)==0);
   if(p.bridge)VirtualFree(p.bridge,0,MEM_RELEASE);
  }
  for(unsigned i=0;i<updatePatches.size();++i) {
   auto &p=updatePatches[i];assert(read<void*>(p.site,0)==updateHooks[i]);
   restore(p);assert(std::memcmp(p.site,p.original.data(),p.length)==0);
  }
  for(unsigned i=0;i<factoryPatches.size();++i) {
   auto &p=factoryPatches[i];assert(read<void*>(p.site,0)==factoryHooks[i]);
   restore(p);assert(std::memcmp(p.site,p.original.data(),p.length)==0);
  }
  FreeLibrary(game);installed=false;
  std::cout<<"All seven code signatures, twenty update and twenty factory slots: installation/restoration passed\n";
 }
 std::cout<<"Native hit amount/victim, null-parent creator chain, registers, flags, x87/SSE and displaced instruction passed\n";
}
