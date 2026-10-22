#include "QuadExposure.h"
#include "Il2Cpp.h"
#include <MinHook.h>
#include <array>

namespace quadexposure {namespace {
bool installed{},requested{};DWORD owner{};bool active{};void* cameras[4]{};void* reference{};
void(*setup)(void*,void*);void(*setParent)(void*,void*,bool,float,void*);void* parentMethod{};
unsigned roots{},linked[4]{},declined{};
struct Exposure {bool own{};uint8_t pad[7]{};void* parent{};void* current{};void* previous{};bool fetched{};uint8_t pad2[3]{};float value{};};
static_assert(sizeof(Exposure)==40&&offsetof(Exposure,parent)==8&&offsetof(Exposure,current)==16&&offsetof(Exposure,previous)==24&&offsetof(Exposure,fetched)==32&&offsetof(Exposure,value)==36);
Exposure last[4]{};
void Setup(void* self,void* method){
    int view=-1;if(owner==GetCurrentThreadId()&&self){auto camera=*(void**)((uint8_t*)self+56);for(unsigned i=0;i<4;++i)if(cameras[i]&&cameras[i]==camera){view=int(i);break;}}
    // SetParentCamera clears the exposure descriptors. Do this BEFORE the
    // engine allocates/swaps current/previous textures in SetupExposureTextures.
    // Each child keeps its own textures; UpdateParentExposure copies on GPU.
    bool link=active&&view>0&&reference;
    if(view>=0){setParent(self,link?reference:nullptr,false,1.f,parentMethod);if(active&&view>0&&!reference)++declined;}
    setup(self,method);
    if(view>=0){memcpy(&last[view],(uint8_t*)self+1936,sizeof(Exposure));
        if(active&&view==0){reference=self;++roots;}
        else if(link&&last[view].parent&&last[view].current&&!last[view].own)++linked[view];
    }
}
bool Field(void* cls,const char* name,size_t offset,const char* type){auto f=il::class_get_field_from_name(cls,name);auto fieldType=(void*(*)(void*))GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_field_get_type");return f&&fieldType&&il::field_get_offset(f)==offset&&!strcmp(il::type_get_name(fieldType(f)),type);}
bool Method(void* m,uintptr_t rva,const uint8_t* prefix,size_t length){if(!m)return false;auto mod=GetModuleHandleW(L"GameAssembly.dll");auto rt=(void*(*)(void*))GetProcAddress(mod,"il2cpp_method_get_return_type");auto p=((il::Method*)m)->pointer;return rt&&!strcmp(il::type_get_name(rt(m)),"System.Void")&&(uintptr_t)p-(uintptr_t)mod==rva&&!memcmp(p,prefix,length);}
}
bool Install(){
    requested=GetPrivateProfileIntW(L"QuadViews",L"SharedExposure",0,(modulePath.parent_path()/L"AvalonVR.ini").c_str())!=0;
    auto hd=il::Class("UnityEngine.Rendering.HighDefinition","HDCamera");auto mod=GetModuleHandleW(L"GameAssembly.dll");
    auto nested=(void*(*)(void*,void**))GetProcAddress(mod,"il2cpp_class_get_nested_types");auto size=(int(*)(void*,uint32_t*))GetProcAddress(mod,"il2cpp_class_value_size");void* exposure{};void* it{};
    while(nested&&hd){auto c=nested(hd,&it);if(!c)break;if(!strcmp(il::class_get_name(c),"ExposureTextures")){exposure=c;break;}}
    uint32_t align{};bool valid=hd&&exposure&&size&&size(exposure,&align)==40&&Field(hd,"camera",56,"UnityEngine.Camera")&&Field(hd,"m_ExposureTextures",1936,"UnityEngine.Rendering.HighDefinition.HDCamera.ExposureTextures")&&Field(exposure,"useCurrentCamera",16,"System.Boolean")&&Field(exposure,"parent",24,"UnityEngine.Rendering.RTHandle")&&Field(exposure,"current",32,"UnityEngine.Rendering.RTHandle")&&Field(exposure,"previous",40,"UnityEngine.Rendering.RTHandle")&&Field(exposure,"useFetchedExposure",48,"System.Boolean")&&Field(exposure,"fetchedGpuExposure",52,"System.Single");
    auto m=hd?il::Find(hd,"SetupExposureTextures",0):nullptr;parentMethod=hd?il::Find(hd,"SetParentCamera",3):nullptr;
    const uint8_t a[]={0x40,0x57,0x48,0x83,0xec,0x30},b[]={0x48,0x89,0x5c,0x24,0x08};
    valid&=Method(m,0x7f05a50,a,sizeof(a))&&Method(parentMethod,0x7f04610,b,sizeof(b));
    if(parentMethod)valid&=!strcmp(il::type_get_name(il::method_get_param(parentMethod,0)),"UnityEngine.Rendering.HighDefinition.HDCamera")&&!strcmp(il::type_get_name(il::method_get_param(parentMethod,1)),"System.Boolean")&&!strcmp(il::type_get_name(il::method_get_param(parentMethod,2)),"System.Single");
    if(valid){setParent=(decltype(setParent))((il::Method*)parentMethod)->pointer;auto p=((il::Method*)m)->pointer;auto c=MH_CreateHook(p,&Setup,(void**)&setup),e=c==MH_OK?MH_EnableHook(p):MH_ERROR_NOT_CREATED;installed=c==MH_OK&&e==MH_OK;Log("Quad shared exposure hook=%d/%d requested=%d; native parent GPU copy, distinct per-view textures",c,e,requested);}
    else Log("Quad shared exposure exact-build validation failed; native exposure retained");
    return installed;
}
bool Enabled(){return installed&&requested;}
bool Toggle(){requested=!requested;Log("Quad shared exposure requested=%d available=%d (Ctrl+Alt+F11)",requested,installed);return Enabled();}
void Begin(void* const* views,unsigned count,bool quad){owner=GetCurrentThreadId();reference=nullptr;active=Enabled()&&quad&&count==4;for(unsigned i=0;i<4;++i)cameras[i]=views&&i<count?views[i]:nullptr;}
void End(){active=false;reference=nullptr;for(auto& c:cameras)c=nullptr;owner=0;}
void Report(){Log("Quad shared exposure requested=%d available=%d referenceSetups=%u links=%u/%u/%u declinedOrder=%u",requested,installed,roots,linked[1],linked[2],linked[3],declined);for(unsigned i=0;i<4;++i)if(roots||linked[i])Log("Quad exposure view=%u own=%d parent=%p current=%p previous=%p fetched=%d",i,last[i].own,last[i].parent,last[i].current,last[i].previous,last[i].fetched);roots=declined=0;for(auto& n:linked)n=0;}
}
