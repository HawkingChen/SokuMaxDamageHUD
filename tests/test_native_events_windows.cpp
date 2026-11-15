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
template<class T>void put(void *p,unsigned o,T x){std::memcpy(static_cast<char*>(p)+o,&x,sizeof(x));}
void * __fastcall fakeFactory(void*,void*,void*,void*,short action,float x,float y,int dir,int layer,void*,unsigned size) {
 assert(action==1010&&x==123.5f&&y==-17.25f&&dir==-1&&layer==2&&size==3);return bullet.data();
}
void * __fastcall fakeRoot(void *self,void*,short action,float x,float y,int dir,int layer,void *data,unsigned size) {
 return factoryCreate(nullptr,nullptr,nullptr,self,action,x,y,dir,layer,data,size);
}
void __fastcall fakeAction(void *self,void*,short action) {
 put(self,0x13c,static_cast<unsigned short>(action));
}
void __fastcall fakeActionSequence(void *self,void*,short action,short seq) {
 fakeAction(self,nullptr,action);put(self,0x13e,static_cast<unsigned short>(seq));
}
int main(int argc,char **argv){
 put(p1.data(),0x13c,static_cast<unsigned short>(614));put(p1.data(),0x184,static_cast<short>(10000));
 put(p1.data(),0x4b4,static_cast<short>(4));put(p2.data(),0x184,static_cast<short>(9000));
 put(bullet.data(),0x168,static_cast<void*>(p1.data()));put(bullet.data(),0x16c,static_cast<void*>(p1.data()));
 put(bullet.data(),0x13c,static_cast<unsigned short>(1010));put(bullet.data(),0x350,33u);
 put(parentObject.data(),0x350,22u);
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
 setAction(p1.data(),nullptr,614);
 const auto spellCast=actors.get(0);
 assert(spellCast.valid()&&read<unsigned short>(p1.data(),0x13c)==614);
 setAction(p1.data(),nullptr,664);
 assert(actors.get(0).id==spellCast.id);
 setActionSequence(p1.data(),nullptr,614,0);
 assert(actors.get(0).id!=spellCast.id&&read<unsigned short>(p1.data(),0x13c)==614);
 // Native creator identity is carried through the original factory call,
 // even when the child's persistent parentObject is null.
 originalFactory=reinterpret_cast<Factory>(&fakeFactory);
 originalRoot=reinterpret_cast<Create>(&fakeRoot);originalObject=originalRoot;originalChild=originalRoot;
 rootCreate(p1.data(),nullptr,1010,123.5f,-17.25f,-1,2,nullptr,3);
 assert(events[count-1].kind==Event::Birth&&events[count-1].creator==reinterpret_cast<std::uintptr_t>(p1.data()));
 assert(events[count-1].cast==actors.get(0).id&&events[count-1].castAction==614);
 objectCreate(parentObject.data(),nullptr,1010,123.5f,-17.25f,-1,2,nullptr,3);
 assert(events[count-1].creator==reinterpret_cast<std::uintptr_t>(parentObject.data()));
 assert(events[count-1].creatorGeneration==22&&events[count-1].parent==0&&creator==nullptr);
 end();auto oldCount=count;captureHit(bullet.data(),p2.data(),500);assert(count==oldCount);
 if(argc>1) {
  // Optional local integration check: map the real PE without executing its
  // entry point, install all signature-checked patches, then restore them.
  HMODULE game=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
  assert(game);
  installed=false;assert(install(reinterpret_cast<std::uintptr_t>(game)));
  for(unsigned i=0;i<patches.size();++i) {
   auto &p=patches[i];
   assert(*static_cast<unsigned char*>(p.site)==(i==4||i==5?0xe8:0xe9));
   restore(p);assert(std::memcmp(p.site,p.original.data(),p.length)==0);
   if(p.bridge)VirtualFree(p.bridge,0,MEM_RELEASE);
  }
  FreeLibrary(game);installed=false;
  std::cout<<"All eight actual 1.10a PE signatures, patch installation and restoration passed\n";
 }
 std::cout<<"Native hit amount/victim, null-parent creator chain, registers, flags, x87/SSE and displaced instruction passed\n";
}
