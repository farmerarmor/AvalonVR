// Lightweight startup helper: no D3D, Unity or IL2CPP API calls before VM init.
#include "Log.h"
#include "CharacterPoolPolicy.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cstdint>
namespace {
using Getter=float(*)(void*,float,void*);using Constructor=void(*)(void*,void*);
Getter originalGetter{};Constructor originalConstructor{};
std::atomic<unsigned> constructors{},overrides{};std::atomic<bool> ready{},constructed{};
std::atomic<int> scale{1};
float Get(void* key,float fallback,void* method){
 float native=originalGetter(key,fallback,method);if(!key)return native;
 // Exact-build managed System.String layout, checked by the native signatures
 // and launcher build guard. Do not invoke the VM from this early helper.
 int length=*(int*)((char*)key+16);if(length<0||length>256)return native;
 float effective=characterpool::Effective((wchar_t*)((char*)key+20),length,native,scale.load(),constructed.load(std::memory_order_acquire));
 if(effective!=native)++overrides;
 return effective;
}
void Construct(void* self,void* method){
 // Arm only when the game constructs a fresh manager. Late injection therefore
 // cannot reinterpret already allocated pools with a different capacity.
 constructed.store(true,std::memory_order_release);++constructors;
 originalConstructor(self,method);
 Log("Character pool native manager constructed scaleFloor=%d getterOverrides=%u",scale.load(),overrides.load());
}
DWORD WINAPI Worker(void*){
 scale=characterpool::Scale(GetPrivateProfileIntW(L"Characters",L"PoolCapacityScale",1,(modulePath.parent_path()/L"AvalonVR.ini").c_str()));
 if(scale==1){Log("Character pool bootstrap dormant scale=1");return 0;}
 auto mod=GetModuleHandleW(L"GameAssembly.dll");if(!mod){Log("Character pool bootstrap: GameAssembly absent; native retained");return 1;}
 auto getter=(uint8_t*)mod+0x11707e0,ctor=(uint8_t*)mod+0x112f270;
 constexpr uint8_t getPrefix[]={0x40,0x53,0x48,0x83,0xec,0x50,0x80,0x3d,0x04,0xe2,0xb8,0x0a,0x00};
 constexpr uint8_t ctorPrefix[]={0x48,0x89,0x5c,0x24,0x08,0x56,0x48,0x83,0xec,0x50,0x80,0x3d,0x15,0xf6,0xbc,0x0a,0x00};
 if(memcmp(getter,getPrefix,sizeof(getPrefix))||memcmp(ctor,ctorPrefix,sizeof(ctorPrefix))){Log("Character pool signatures mismatch; native retained");return 2;}
 auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return 3;
 auto g=MH_CreateHook(getter,&Get,(void**)&originalGetter),c=MH_CreateHook(ctor,&Construct,(void**)&originalConstructor);
 if(g!=MH_OK||c!=MH_OK){Log("Character pool hook creation failed getter=%d constructor=%d",g,c);return 4;}
 // Both hooks become executable together. A constructor never arms before the
 // configuration reader is ready to give every native allocator the same floor.
 auto a=MH_QueueEnableHook(getter),b=MH_QueueEnableHook(ctor),applied=(a==MH_OK&&b==MH_OK)?MH_ApplyQueued():MH_ERROR_NOT_CREATED;
 ready=applied==MH_OK;Log("Character pool early hooks ready=%d scaleFloor=%d; waits for fresh native construction",ready.load(),scale.load());
 return ready?0:5;
}
}
extern "C" __declspec(dllexport) uint64_t AvalonCharacterPoolStatus(){return (uint64_t(constructors.load())<<32)|(uint64_t(overrides.load()&0xffffff)<<8)|(uint64_t(scale.load())<<1)|unsigned(ready.load());}
BOOL WINAPI DllMain(HINSTANCE h,DWORD why){if(why==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);wchar_t path[32768];GetModuleFileNameW(h,path,32768);modulePath=path;CloseHandle(CreateThread(nullptr,0,Worker,nullptr,0,nullptr));}return TRUE;}
