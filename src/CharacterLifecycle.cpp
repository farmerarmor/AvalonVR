#include "CharacterLifecycle.h"
#include "CharacterLifecyclePolicy.h"
#include "Il2Cpp.h"
#include <MinHook.h>
#include <atomic>
namespace characterlife {namespace {
using Destroy=void(*)(void*,void*);using Unregister=void(*)(void*,void*,void*);
Destroy original{};Unregister unregister{};void* unregisterMethod{};
void* instanceField{};void* renderersField{};void* registeredField{};void* queuedField{};void* idField{};void* destroyedField{};bool layoutReady{};
std::atomic<uint64_t> observed{},submitted{};
void OnDestroy(void* self,void* method){
 ++observed;
 if(self){void* manager{};il::field_static_get_value(instanceField,&manager);
  if(manager){uint32_t id{};il::field_get_value(self,idField,&id);il::Array* slots{};il::field_get_value(manager,renderersField,&slots);auto slot=id&SlotMask;
   if(slots&&slots->length<=20000&&slot<slots->length&&slots->items[slot]==self){
    Mask registered{},queued{};il::field_get_value(manager,registeredField,&registered);il::field_get_value(manager,queuedField,&queued);
    if(ShouldQueue(id,slots->length,true,Has(registered,slot),Has(queued,slot))){
     // Queue through the game's existing path while RendererData is intact.
     // Its EarlyUpdate owns final release and pending-job synchronization.
     unregister(manager,self,unregisterMethod);++submitted;
    }
   }
  }
 }
 original(self,method);
}
bool Native(void* p){MEMORY_BASIC_INFORMATION r{};return p&&VirtualQuery(p,&r,sizeof(r))&&r.AllocationBase==GetModuleHandleW(L"GameAssembly.dll");}
}
bool Install(){
 auto ini=modulePath.parent_path()/L"AvalonVR.ini";if(!GetPrivateProfileIntW(L"Characters",L"FixDestroyedRegistrations",0,ini.c_str())){Log("Character lifecycle correction disabled");return false;}
 auto renderer=il::Class("Awaken.Kandra","KandraRenderer"),manager=il::Class("Awaken.Kandra","KandraRendererManager"),mask=il::Class("Awaken.Utility.LowLevel.Collections","UnsafeBitmask");
 auto size=(int(*)(void*,uint32_t*))GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_class_value_size");uint32_t alignment{};
 if(!renderer||!manager||!mask||!size||size(mask,&alignment)!=sizeof(Mask)){Log("Character lifecycle unsupported mask layout; native retained");return false;}
 idField=il::class_get_field_from_name(renderer,"<RenderingId>k__BackingField");destroyedField=il::class_get_field_from_name(renderer,"<Destroyed>k__BackingField");instanceField=il::class_get_field_from_name(manager,"<Instance>k__BackingField");renderersField=il::class_get_field_from_name(manager,"_renderers");registeredField=il::class_get_field_from_name(manager,"_fullyRegisteredSlots");queuedField=il::class_get_field_from_name(manager,"_toUnregister");
 if(!idField||!destroyedField||!instanceField||!renderersField||!registeredField||!queuedField||il::field_get_offset(idField)!=224||il::field_get_offset(destroyedField)!=228||il::field_get_offset(renderersField)!=120||il::field_get_offset(registeredField)!=96||il::field_get_offset(queuedField)!=48){Log("Character lifecycle unsupported field layout; native retained");return false;}
 auto destroy=il::Find(renderer,"OnDestroy",0);unregisterMethod=il::Find(manager,"Unregister",1);
 if(!destroy||!unregisterMethod||strcmp(il::type_get_name(il::method_get_param(unregisterMethod,0)),"Awaken.Kandra.KandraRenderer")||!Native(((il::Method*)destroy)->pointer)||!Native(((il::Method*)unregisterMethod)->pointer)){Log("Character lifecycle unsupported native methods; native retained");return false;}
 layoutReady=true;unregister=(Unregister)((il::Method*)unregisterMethod)->pointer;auto target=((il::Method*)destroy)->pointer;
 auto create=MH_CreateHook(target,&OnDestroy,(void**)&original),enable=create==MH_OK?MH_EnableHook(target):MH_ERROR_NOT_CREATED;
 Log("Character lifecycle hook=%d/%d; queues owned fully-registered OnDestroy entries only",create,enable);return create==MH_OK&&enable==MH_OK;
}
uint64_t Observed(){return observed.load();}uint64_t Queued(){return submitted.load();}
State Inspect(void* manager){
 State result{};if(!layoutReady||!manager)return result;
 il::Array* slots{};Mask registered{},queued{};il::field_get_value(manager,renderersField,&slots);il::field_get_value(manager,registeredField,&registered);il::field_get_value(manager,queuedField,&queued);
 if(!slots||slots->length>20000||!registered.data||registered.length<slots->length||registered.length>100000||!queued.data||queued.length<slots->length||queued.length>100000)return result;
 result.available=true;
 // Called only by the opt-in five-second main-thread diagnostic. No Unity
 // object/scene queries, GPU readback, or ownership changes are needed.
 for(uint32_t slot=0;slot<slots->length;++slot){
  result.pendingUnregister+=Has(queued,slot);
  if(!Has(registered,slot))continue;
  ++result.registered;if(auto renderer=slots->items[slot]){bool destroyed{};il::field_get_value(renderer,destroyedField,&destroyed);result.destroyedRegistered+=destroyed;}
 }
 return result;
}
}
