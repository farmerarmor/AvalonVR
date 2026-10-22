#include "ShadowResolution.h"
#include "Il2Cpp.h"
#include <algorithm>

namespace shadowres {namespace {
uintptr_t held{},heldSun{};int oldOverride{},applied{};bool oldUse{};
void* Getter(void* object,const char* name){return object?il::Find(((il::Object*)object)->klass,name,0):nullptr;}
bool Values(void* hd,int& value,bool& use){
 auto method=Getter(hd,"get_shadowResolution");auto setting=method?il::Invoke(method,hd):nullptr;
 auto overrideGetter=Getter(setting,"get_override"),useGetter=Getter(setting,"get_useOverride");
 if(!overrideGetter||!useGetter)return false;value=il::Value<int>(il::Invoke(overrideGetter,setting));use=il::Value<bool>(il::Invoke(useGetter,setting));return true;
}
void Set(void* hd,int value,bool use){void* a[]={&value};void* b[]={&use};il::Call(hd,"SetShadowResolution",1,a);il::Call(hd,"SetShadowResolutionOverride",1,b);}
void Restore(){if(!held)return;auto hd=il::gchandle_get_target(held);int current{};bool use{};
 if(hd&&*(void**)((char*)hd+sizeof(il::Object))&&Values(hd,current,use)&&use&&current==applied){Set(hd,oldOverride,oldUse);Log("Radial shadow resolution restored override=%d use=%d",oldOverride,oldUse);}
 il::gchandle_free(held);if(heldSun)il::gchandle_free(heldSun);held=heldSun=0;applied=0;
}
void* GetSunData(void* sun){
 if(!sun)return nullptr;
 auto cls=il::Class("UnityEngine.Rendering.HighDefinition","HDAdditionalLightData");if(!cls)return nullptr;auto type=il::type_get_object(il::class_get_type(cls));
 for(auto c=((il::Object*)sun)->klass;c;c=il::class_get_parent(c)){void* it{};while(auto m=il::class_get_methods(c,&it))if(!strcmp(il::method_get_name(m),"GetComponent")&&il::method_get_param_count(m)==1&&!strcmp(il::type_get_name(il::method_get_param(m,0)),"System.Type")){void* args[]={type};return il::Invoke(m,sun,args);}}
 return nullptr;
}
}
bool Update(bool enabled,int observedResolution,int minimum,Vec3 observedLightZ){
 if(!enabled){Restore();return false;}
 if(observedResolution<=0||minimum<64||minimum>8192)return false;
 static auto sunGetter=il::Find("UnityEngine","RenderSettings","get_sun",0);auto sun=sunGetter?il::Invoke(sunGetter):nullptr;
 auto hd=heldSun&&il::gchandle_get_target(heldSun)==sun?il::gchandle_get_target(held):GetSunData(sun);if(!hd){Restore();return false;}
 // Select the actual observed directional light, not another loaded sun/moon.
 auto transform=il::Call(hd,"get_transform");auto forward=il::Value<Vec3>(il::Call(transform,"get_forward"));
 if(forward.x*observedLightZ.x+forward.y*observedLightZ.y+forward.z*observedLightZ.z>-.99f){Restore();return false;}
 if(held&&il::gchandle_get_target(held)!=hd)Restore();
 int value{};bool use{};if(!Values(hd,value,use))return false;
 if(held)return use&&value==applied;
 if(!il::Find(((il::Object*)hd)->klass,"SetShadowResolution",1)||!il::Find(((il::Object*)hd)->klass,"SetShadowResolutionOverride",1))return false;
 oldOverride=value;oldUse=use;applied=std::max({minimum,observedResolution,use?value:0});held=il::gchandle_new(hd,false);heldSun=il::gchandle_new(sun,false);Set(hd,applied,true);
 int effective{};bool effectiveUse{};bool valid=Values(hd,effective,effectiveUse)&&effectiveUse&&effective==applied;
 Log("Radial shadow resolution requested=%d previousOverride=%d previousUse=%d settingConfirmed=%d",applied,oldOverride,oldUse,valid);
 if(!valid)Restore();return valid;
}
}
