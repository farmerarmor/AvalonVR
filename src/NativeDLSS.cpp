#include "NativeDLSS.h"
#include "Il2Cpp.h"

namespace {
void* priorities[3]{};uintptr_t priorityRoots[3]{};bool attempted[3]{};
void* ObjectField(void* object,const char* name){
    if(object)for(auto cls=((il::Object*)object)->klass;cls;cls=il::class_get_parent(cls))if(auto field=il::class_get_field_from_name(cls,name)){void* result{};il::field_get_value(object,field,&result);return result;}return nullptr;
}
bool Initialize(void* original,int upscaler){
    int index=upscaler<0?0:upscaler==0?1:2;
    if(attempted[index])return priorities[index]!=nullptr;attempted[index]=true;
    auto cls=il::Class("UnityEngine.Rendering","AdvancedUpscalers");auto field=cls?il::class_get_field_from_name(cls,upscaler==2?"STP":"DLSS"):nullptr;
    if(!field||!original)return false;int selected{};il::field_static_get_value(field,&selected);if(upscaler>=0&&selected!=upscaler)return false;
    auto list=il::object_new(((il::Object*)original)->klass);if(!list)return false;
    auto root=il::gchandle_new(list,false);if(!root)return false;il::Call(list,".ctor");
    if(upscaler>=0){void* args[]={&selected};il::Call(list,"Add",1,args);}
    int count=il::Value<int>(il::Call(list,"get_Count"));int itemIndex=0;void* itemArgs[]={&itemIndex};
    if(count!=(upscaler>=0?1:0)||(upscaler>=0&&il::Value<int>(il::Call(list,"get_Item",1,itemArgs))!=selected)){il::gchandle_free(root);return false;}
    priorities[index]=list;priorityRoots[index]=root;Log("Eye upscaler priority prepared upscaler=%d count=%d",upscaler,count);return true;
}
template<class T>T* ValueSlot(char* data,void* cls,const char* name){
    auto field=il::class_get_field_from_name(cls,name);if(!field)return nullptr;
    auto offset=il::field_get_offset(field);return offset>=sizeof(il::Object)?(T*)(data+offset-sizeof(il::Object)):nullptr;
}
bool AfterPostValue(uint32_t& value){
    static bool attempted{},valid{};static uint32_t cached{};
    if(!attempted){attempted=true;
        auto module=GetModuleHandleW(L"GameAssembly.dll");
        auto nested=(void*(*)(void*,void**))GetProcAddress(module,"il2cpp_class_get_nested_types");
        auto size=(int(*)(void*,uint32_t*))GetProcAddress(module,"il2cpp_class_value_size");
        auto handler=il::Class("UnityEngine.Rendering","DynamicResolutionHandler");void* iter{};
        while(nested&&size&&handler){auto cls=nested(handler,&iter);if(!cls)break;if(strcmp(il::class_get_name(cls),"UpsamplerScheduleType"))continue;
            uint32_t alignment{};auto field=il::class_get_field_from_name(cls,"AfterPost");
            if(field&&size(cls,&alignment)==sizeof(uint32_t)){il::field_static_get_value(field,&cached);valid=true;Log("Native DLSS AfterPost enum verified=%u",cached);}break;
        }
    }
    value=cached;return valid;
}
}
NativeDLSSScope::NativeDLSSScope(void* pipeline,bool requested,int upscaler,float fixedPercentage,bool afterPost){
    if(!requested||!pipeline||(upscaler!=-1&&upscaler!=0&&upscaler!=2))return;
    if(fixedPercentage!=0&&(fixedPercentage<10||fixedPercentage>100))return;
    auto currentAsset=ObjectField(pipeline,"m_Asset");auto pass=ObjectField(pipeline,"m_DLSSPass");if(!currentAsset||(upscaler==0&&!pass))return;
    auto settings=il::class_get_field_from_name(((il::Object*)currentAsset)->klass,"m_RenderPipelineSettings");
    auto settingsClass=il::Class("UnityEngine.Rendering.HighDefinition","RenderPipelineSettings");
    auto drsField=settingsClass?il::class_get_field_from_name(settingsClass,"dynamicResolutionSettings"):nullptr;
    auto drsClass=il::Class("UnityEngine.Rendering","GlobalDynamicResolutionSettings");
    auto listField=drsClass?il::class_get_field_from_name(drsClass,"advancedUpscalersByPriority"):nullptr;
    if(!settings||!drsField||!listField)return;
    auto a=il::field_get_offset(settings),b=il::field_get_offset(drsField),c=il::field_get_offset(listField);
    if(a<sizeof(il::Object)||b<sizeof(il::Object)||c<sizeof(il::Object))return;
    auto slot=(void**)((char*)currentAsset+a+b+c-2*sizeof(il::Object));auto saved=*slot;
    auto module=GetModuleHandleW(L"GameAssembly.dll");
    barrier=(decltype(barrier))GetProcAddress(module,"il2cpp_gc_wbarrier_set_field");
    freeRoot=(decltype(freeRoot))GetProcAddress(module,"il2cpp_gchandle_free");
    if(!barrier||!freeRoot||!Initialize(saved,upscaler))return;
    bool* forced{};float* percentage{};uint8_t* filter{};uint8_t catmullRom{};
    auto drs=(char*)currentAsset+a+b-sizeof(il::Object);
    uint32_t* injection{};uint32_t afterPostEnum{};
    if(afterPost&&upscaler==0){injection=ValueSlot<uint32_t>(drs,drsClass,"DLSSInjectionPoint");if(!injection||!AfterPostValue(afterPostEnum)){Log("Native DLSS AfterPost override unavailable");return;}}
    if(fixedPercentage>0){
        forced=ValueSlot<bool>(drs,drsClass,"forceResolution");percentage=ValueSlot<float>(drs,drsClass,"forcedPercentage");filter=ValueSlot<uint8_t>(drs,drsClass,"upsampleFilter");
        auto filterClass=il::Class("UnityEngine.Rendering","DynamicResUpscaleFilter");
        auto filterValue=filterClass?il::class_get_field_from_name(filterClass,"CatmullRom"):nullptr;
        // Exact-build metadata places byte upsampleFilter immediately before bool forceResolution.
        if(!forced||!percentage||!filter||!filterValue||(char*)forced!=(char*)filter+1)return;
        il::field_static_get_value(filterValue,&catmullRom);
    }
    auto root=saved?il::gchandle_new(saved,false):0;if(saved&&!root)return;
    asset=currentAsset;prioritySlot=slot;original=saved;originalRoot=root;
    if(injection){injectionSlot=injection;originalInjection=*injection;*injection=afterPostEnum;}
    if(forced){forcedSlot=forced;percentageSlot=percentage;filterSlot=filter;originalForced=*forced;originalPercentage=*percentage;originalFilter=*filter;*forced=true;*percentage=fixedPercentage;*filter=catmullRom;}
    int index=upscaler<0?0:upscaler==0?1:2;barrier(asset,prioritySlot,priorities[index]);
    static bool reported[3]{};if(!reported[index]){reported[index]=true;Log("Eye upscaler scoped override active upscaler=%d fixedPercent=%.1f",upscaler,fixedPercentage);}
}
NativeDLSSScope::~NativeDLSSScope(){
    if(!prioritySlot)return;barrier(asset,prioritySlot,original);
    if(forcedSlot){*forcedSlot=originalForced;*percentageSlot=originalPercentage;*filterSlot=originalFilter;}
    if(injectionSlot)*injectionSlot=originalInjection;
    if(originalRoot)freeRoot(originalRoot);
    static bool reported{};if(!reported){reported=true;Log("Native eye DLSS original priority restored=%d",*prioritySlot==original);}
}
