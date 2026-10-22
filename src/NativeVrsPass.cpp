#include "NativeVrsPass.h"
#include "Foveation.h"
#include "Il2Cpp.h"
#include <MinHook.h>
#include <atomic>
namespace nativevrs {namespace {
void(*original)(void*,void*,void*,void*);void* getCommand{};void* issue{};
std::atomic<uint64_t> queued{},failed{};
void __stdcall Begin(int){foveation::BeginNativePass();}
void __stdcall End(int){foveation::EndNativePass();}
bool Event(void* cmd,void* callback){int id=0;void* args[]={&callback,&id};void* exception{};il::runtime_invoke(issue,cmd,args,&exception);return !exception;}
void Render(void* self,void* data,void* context,void* method){
    if(!foveation::Requested()||!foveation::NativePassMode()){original(self,data,context,method);return;}
    // RenderGraphContext is an eight-byte value, not a managed reference.
    // Its wrapped context is passed by value in r8 in this exact callback.
    auto cmd=il::Invoke(getCommand,&context);bool start=cmd&&Event(cmd,(void*)&Begin);
    original(self,data,context,method);
    if(start){if(Event(cmd,(void*)&End)){auto n=++queued;if(n<=2)Log("Native VRS GBuffer ordered event pair queued=%llu",n);}else{++failed;Log("Native VRS GBuffer end event failed; disabling candidate");foveation::Disable();}}
}
}
bool Install(){
    auto module=GetModuleHandleW(L"GameAssembly.dll");auto count=(size_t(*)(void*))GetProcAddress(module,"il2cpp_image_get_class_count");auto at=(void*(*)(void*,size_t))GetProcAddress(module,"il2cpp_image_get_class");auto size=(int(*)(void*,uint32_t*))GetProcAddress(module,"il2cpp_class_value_size");auto valueType=(bool(*)(void*))GetProcAddress(module,"il2cpp_class_is_valuetype");
    auto cls=il::Class("UnityEngine.Rendering.RenderGraphModule","RenderGraphContext");uint32_t alignment{};auto wrapped=cls?il::class_get_field_from_name(cls,"wrappedContext"):nullptr;
    if(!cls||!size||!valueType||!valueType(cls)||size(cls,&alignment)!=8||!wrapped||il::field_get_offset(wrapped)!=16)return false;
    getCommand=il::Find(cls,"get_cmd",0);auto command=il::Class("UnityEngine.Rendering","CommandBuffer");issue=command?il::Find(command,"IssuePluginEvent",2):nullptr;
    if(!getCommand||!issue||strcmp(il::type_get_name(il::method_get_param(issue,0)),"System.IntPtr")||strcmp(il::type_get_name(il::method_get_param(issue,1)),"System.Int32")||!count||!at)return false;
    void* callback{};size_t assemblies{};auto images=il::domain_get_assemblies(il::domain_get(),&assemblies);
    for(size_t a=0;a<assemblies&&!callback;++a){auto image=il::assembly_get_image(images[a]);if(strcmp(il::image_get_name(image),"Unity.RenderPipelines.HighDefinition.Runtime.dll"))continue;for(size_t i=0,n=count(image);i<n&&!callback;++i){auto c=at(image,i);if(strcmp(il::class_get_name(c),"<>c"))continue;void* it{};while(auto m=il::class_get_methods(c,&it))if(!strcmp(il::method_get_name(m),"<RenderGBuffer>b__1024_0")&&il::method_get_param_count(m)==2&&!strcmp(il::type_get_name(il::method_get_param(m,0)),"UnityEngine.Rendering.HighDefinition.HDRenderPipeline.GBufferPassData")&&!strcmp(il::type_get_name(il::method_get_param(m,1)),"UnityEngine.Rendering.RenderGraphModule.RenderGraphContext")){callback=m;break;}}}
    if(!callback)return false;auto pointer=((il::Method*)callback)->pointer;const unsigned char expected[]={0x4c,0x89,0x44,0x24,0x18,0x53,0x48,0x83,0xec,0x60};
    if((uintptr_t)pointer-(uintptr_t)module!=0x8151c60||memcmp(pointer,expected,sizeof(expected)))return false;
    auto a=MH_CreateHook(pointer,&Render,(void**)&original),b=a==MH_OK?MH_EnableHook(pointer):MH_ERROR_NOT_CREATED;Log("Native VRS GBuffer hook=%d/%d verified RenderGraphContext8 and ordered CommandBuffer events",a,b);return a==MH_OK&&b==MH_OK;
}
}
