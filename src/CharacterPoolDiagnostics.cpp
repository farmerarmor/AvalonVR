#include "CharacterPoolDiagnostics.h"
#include "Il2Cpp.h"
#include "CharacterLifecycle.h"
namespace {
void* Field(void* object,const char* name){if(!object)return nullptr;auto f=il::class_get_field_from_name(((il::Object*)object)->klass,name);void* result{};if(f)il::field_get_value(object,f,&result);return result;}
int Count(void* manager,const char* field){auto buffer=Field(manager,field);return buffer?il::Value<int>(il::Call(buffer,"get_count")):-1;}
float Fill(void* manager){return manager?il::Value<float>(il::Call(manager,"get_FillPercentage")):-1.f;}
}
void ReportCharacterPools(){
 static uint64_t next{};auto now=GetTickCount64();if(now<next)return;next=now+5000;
 static auto config=modulePath.parent_path()/L"AvalonVR.ini";if(!GetPrivateProfileIntW(L"Debug",L"CharacterPoolDiagnostics",0,config.c_str()))return;
 auto bootstrap=GetModuleHandleW(L"AvalonVRCharacterPools.dll");auto status=bootstrap?(uint64_t(*)())GetProcAddress(bootstrap,"AvalonCharacterPoolStatus"):nullptr;uint64_t state=status?status():0;
 static auto get=il::Find("Awaken.Kandra","KandraRendererManager","get_Instance",0);auto manager=il::Invoke(get);
 auto renderers=(il::Array*)Field(manager,"_renderers");auto rig=Field(manager,"<RigManager>k__BackingField"),skin=Field(manager,"<SkinningManager>k__BackingField");
 Log("Character pools bootstrap=%d ready=%u scale=%u constructors=%u overrides=%u manager=%p rendererSlots=%llu boneBuffer=%d skinnedBuffer=%d",bootstrap!=nullptr,unsigned(state&1),unsigned((state>>1)&7),unsigned(state>>32),unsigned((state>>8)&0xffffff),manager,renderers?renderers->length:0,Count(rig,"_inputBonesBuffer"),Count(skin,"_outputVerticesBuffer"));
 auto lifecycle=characterlife::Inspect(manager);
 Log("Character lifecycle destroys=%llu queuedForNativeRelease=%llu inspect=%d registered=%u destroyedRegistered=%u pendingUnregister=%u boneFill=%.6f skinnedFill=%.6f",characterlife::Observed(),characterlife::Queued(),lifecycle.available,lifecycle.registered,lifecycle.destroyedRegistered,lifecycle.pendingUnregister,Fill(rig),Fill(skin));
}
