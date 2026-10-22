#include "VRStatus.h"
#include "Il2Cpp.h"
#include <cstdio>

namespace {
void* canvasGO{};void* text{};uintptr_t roots[12]{};int rootCount{};
bool attempted{},available{},visible{};uint64_t expires{};char previousMessage[256]{};
void* Root(void* object){if(object&&rootCount<12)roots[rootCount++]=il::gchandle_new(object,false);return object;}
bool Call(void* object,const char* name,void* value){
    auto method=object?il::Find(((il::Object*)object)->klass,name,1):nullptr;if(!method)return false;
    void* args[]={value};void* exception{};il::runtime_invoke(method,object,args,&exception);
    if(exception)Log("VR status managed exception method=%s exception=%p",name,exception);return !exception;
}
void* Add(void* go,void* cls){
    if(!cls)return nullptr;auto type=il::type_get_object(il::class_get_type(cls));void* args[]={type};
    void* iter{};auto goClass=((il::Object*)go)->klass;
    while(auto method=il::class_get_methods(goClass,&iter))if(!strcmp(il::method_get_name(method),"AddComponent")&&il::method_get_param_count(method)==1&&
        !strcmp(il::type_get_name(il::method_get_param(method,0)),"System.Type"))return il::Invoke(method,go,args);
    return nullptr;
}
bool Initialize(){
    auto goClass=il::Class("UnityEngine","GameObject"),rectClass=il::Class("UnityEngine","RectTransform"),canvasClass=il::Class("UnityEngine","Canvas");
    auto textClass=il::Class("TMPro","TextMeshProUGUI"),typeClass=il::Class("System","Type");
    auto module=GetModuleHandleW(L"GameAssembly.dll");
    auto arrayNew=(void*(*)(void*,uintptr_t))GetProcAddress(module,"il2cpp_array_new");
    auto barrier=(void(*)(void*,void**,void*))GetProcAddress(module,"il2cpp_gc_wbarrier_set_field");
    auto size=(int(*)(void*,uint32_t*))GetProcAddress(module,"il2cpp_class_value_size");
    auto vecClass=il::Class("UnityEngine","Vector2");uint32_t alignment{};
    if(!goClass||!rectClass||!canvasClass||!textClass||!typeClass||!arrayNew||!barrier||!size||!vecClass||size(vecClass,&alignment)!=8)return false;
    auto types=(il::Array*)Root(arrayNew(typeClass,1));if(!types)return false;
    auto rectType=il::type_get_object(il::class_get_type(rectClass));barrier(types,&types->items[0],rectType);
    auto make=[&](const char* name)->void*{
        auto go=Root(il::object_new(goClass));auto label=il::string_new(name);void* args[]={label,types};il::Call(go,".ctor",2,args);return go;
    };
    canvasGO=make("AvalonVR_PresetStatus");if(!canvasGO)return false;
    int hide=61;bool no=false;Call(canvasGO,"set_hideFlags",&hide);
    auto dontDestroy=il::Find("UnityEngine","Object","DontDestroyOnLoad",1);void* rootArgs[]={canvasGO};il::Invoke(dontDestroy,nullptr,rootArgs);
    auto canvas=Root(Add(canvasGO,canvasClass));int overlay=0,order=32760;
    if(!canvas||!Call(canvas,"set_renderMode",&overlay)||!Call(canvas,"set_sortingOrder",&order))return false;
    auto child=make("AvalonVR_PresetLabel");auto rect=il::Call(child,"get_transform"),parent=il::Call(canvasGO,"get_transform");
    if(!rect||!parent)return false;void* parentArgs[]={parent,&no};il::Call(rect,"SetParent",2,parentArgs);
    struct V2{float x,y;};V2 anchor{0,1},position{36,-28},extent{1600,165};
    if(!Call(rect,"set_anchorMin",&anchor)||!Call(rect,"set_anchorMax",&anchor)||!Call(rect,"set_pivot",&anchor)||!Call(rect,"set_anchoredPosition",&position)||!Call(rect,"set_sizeDelta",&extent))return false;
    text=Root(Add(child,textClass));if(!text)return false;
    float fontSize=32;struct Color{float r,g,b,a;} white{1,1,1,1},black{0,0,0,1};
    auto settings=il::Class("TMPro","TMP_Settings");auto getFont=settings?il::Find(settings,"get_defaultFontAsset",0):nullptr;auto font=getFont?il::Invoke(getFont):nullptr;
    if(!font||!Call(text,"set_font",font))return false;
    if(!Call(text,"set_fontSize",&fontSize)||!Call(text,"set_color",&white)||!Call(text,"set_raycastTarget",&no)||!il::Find(((il::Object*)text)->klass,"set_text",1))return false;
    float outline=.2f;Call(text,"set_outlineWidth",&outline);Call(text,"set_outlineColor",&black);
    auto alignClass=il::Class("TMPro","TextAlignmentOptions");auto alignField=alignClass?il::class_get_field_from_name(alignClass,"TopLeft"):nullptr;
    if(alignField){int value{};il::field_static_get_value(alignField,&value);Call(text,"set_alignment",&value);}
    Call(canvasGO,"SetActive",&no);Log("VR preset status label prepared; input raycasts disabled");return true;
}
}
void vrstatus::Show(const char* title,int inW,int inH,int outW,int outH,bool afterPost,bool restartLifetime){
    if(!restartLifetime&&!visible)return;
    if(!attempted){attempted=true;available=Initialize();if(!available){bool no=false;Call(canvasGO,"SetActive",&no);Log("VR preset status label unavailable");}}
    if(!available)return;
    char message[256];snprintf(message,sizeof(message),"%s\n%dx%d -> %dx%d | %s",title,inW,inH,outW,outH,afterPost?"After post":"Original post order");
    if(strcmp(message,previousMessage)){
        auto value=il::string_new(message);if(!Call(text,"set_text",value))return;
        strcpy_s(previousMessage,message);Log("VR preset label mode=%s input=%dx%d output=%dx%d afterPost=%d",title,inW,inH,outW,outH,afterPost);
    }
    if(restartLifetime){bool yes=true;if(Call(canvasGO,"SetActive",&yes)){visible=true;expires=GetTickCount64()+6000;}}
}
void vrstatus::Tick(){if(visible&&GetTickCount64()>=expires){bool no=false;Call(canvasGO,"SetActive",&no);visible=false;}}
void vrstatus::Message(const char* message){
    if(!attempted){attempted=true;available=Initialize();}if(!available||!message)return;
    if(strcmp(message,previousMessage)){auto value=il::string_new(message);if(!Call(text,"set_text",value))return;strncpy_s(previousMessage,message,_TRUNCATE);}
    bool yes=true;if(Call(canvasGO,"SetActive",&yes)){visible=true;expires=GetTickCount64()+6000;}
}
