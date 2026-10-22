#include "BufferUploads.h"
#include "StagingUpload.h"
#include "UploadHandoff.h"
#include "UploadPool.h"
#include "Log.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>

namespace bufferuploads {
namespace {
std::atomic<bool> enabled{};
std::atomic<uint64_t> observed{},completed{},bytesUploaded{},fallback{},handoffFull{};
DWORD renderThread{};
ID3D11DeviceContext* context{}; // Same process-lifetime context held by the bridge.
upload::Handoff handoff;
upload::Pool pool;
void* (*beginOriginal)(void*,uint64_t);
void (*endOriginal)(void*,uint64_t);
template<class T>T& Field(void* owner,unsigned offset){return *(T*)((uint8_t*)owner+offset);}

void* Begin(void* owner,uint64_t offset){
 if(!enabled||!owner)return beginOriginal(owner,offset);
 auto staging=Field<ID3D11Buffer*>(owner,0x58),gpu=Field<ID3D11Buffer*>(owner,0x40);auto base=Field<void*>(owner,0x60);
 bool eligible=false;D3D11_BUFFER_DESC gd{},sd{};
 if(staging&&gpu&&base){staging->GetDesc(&sd);if(sd.ByteWidth==upload::Capacity){gpu->GetDesc(&gd);eligible=upload::Eligible(gd,sd,offset,0);handoff.Take(owner);if(eligible)++observed;}}
 auto result=beginOriginal(owner,offset);
 // Native BeginWriteThreadSafe consumes mappedBase and publishes byte offset.
 // The writer may be a job/main thread; EndWrite runs on the graphics worker.
 if(eligible&&result==(uint8_t*)base+offset&&Field<void*>(owner,0x60)==nullptr&&Field<uint64_t>(owner,0x68)==offset)
  if(!handoff.Store({owner,gpu,staging,base,offset,gd.ByteWidth-offset,true}))++handoffFull;
 return result;
}
void End(void* owner,uint64_t bytes){
 if(!enabled||!owner||GetCurrentThreadId()!=renderThread||!Field<ID3D11Buffer*>(owner,0x58)){endOriginal(owner,bytes);return;}
 auto p=handoff.Take(owner);
 bool valid=p.accepted&&bytes<=p.capacity&&Field<ID3D11Buffer*>(owner,0x40)==p.gpu&&Field<ID3D11Buffer*>(owner,0x58)==p.staging&&Field<void*>(owner,0x60)==nullptr&&Field<uint64_t>(owner,0x68)==p.offset;
 if(valid&&pool.Copy(context,p.gpu,p.base,UINT(p.offset),UINT(bytes))){
  // Preserve native state for the next lock. Only helper-owned staging is used
  // by GPU copies; native CPU staging remains mapped throughout this path.
  InterlockedExchange64((volatile LONG64*)((uint8_t*)owner+0x68),-1);
  InterlockedExchangePointer((void* volatile*)((uint8_t*)owner+0x60),p.base);
  ++completed;bytesUploaded+=bytes;
 }else{if(p.accepted)++fallback;endOriginal(owner,bytes);}
}
}
bool Install(ID3D11Device* device,ID3D11DeviceContext* supplied){
 auto ini=modulePath.parent_path()/L"AvalonVR.ini";bool requested=GetPrivateProfileIntW(L"Performance",L"AsyncBufferUploads",1,ini.c_str())!=0;
 Log("Async buffer uploads requested=%d",requested);if(!requested)return false;
 if(!device||!supplied||supplied->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
 auto unity=(uint8_t*)GetModuleHandleW(L"UnityPlayer.dll");if(!unity)return false;
 // The launcher requires the exact Unity6000.0.64f1 binary SHA256. These
 // independent native prefixes protect the two verified ABI/layout entries.
 const uint8_t bp[]={0x40,0x53,0x48,0x81,0xec,0xa0,0x02,0x00,0x00,0x33,0xdb,0x44,0x8b,0xc3,0x4c,0x87,0x41,0x60};
 const uint8_t ep[]={0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0x48,0x83,0x79,0x40,0x00};
 auto b=unity+0x8f2e70,e=unity+0x8f2d10;
 if(memcmp(b,bp,sizeof(bp))||memcmp(e,ep,sizeof(ep))){Log("Async buffer uploads refused native prefix mismatch");return false;}
 if(!pool.Init(device)){Log("Async buffer uploads pool unavailable; retaining native uploads");return false;}
 context=supplied;renderThread=GetCurrentThreadId();
 auto bc=MH_CreateHook(b,(void*)&Begin,(void**)&beginOriginal),ec=MH_CreateHook(e,(void*)&End,(void**)&endOriginal);
 auto be=bc==MH_OK?MH_EnableHook(b):MH_ERROR_NOT_CREATED,ee=ec==MH_OK?MH_EnableHook(e):MH_ERROR_NOT_CREATED;
 bool okay=bc==MH_OK&&ec==MH_OK&&be==MH_OK&&ee==MH_OK;
 if(!okay){if(bc==MH_OK){MH_DisableHook(b);MH_RemoveHook(b);}if(ec==MH_OK){MH_DisableHook(e);MH_RemoveHook(e);}}
 enabled=okay;Log("Async buffer uploads ready=%d hooks=%d/%d,%d/%d renderThread=%lu slots=8 capacity=%u",okay,bc,be,ec,ee,renderThread,upload::Capacity);return okay;
}
void Report(){
 if(!enabled)return;
 Log("Async uploads observed=%llu completed=%llu bytes=%llu fallback=%llu handoffFull=%llu poolBusy=%llu mapDeclines=%llu",observed.load(),completed.load(),bytesUploaded.load(),fallback.load(),handoffFull.load(),pool.busy.load(),pool.mapDeclines.load());
}
}
