#include "NativeAimUI.h"
#include "Il2Cpp.h"
#include <vector>
#include <string>
#include <algorithm>

namespace {
void* lastHero{};void* crosshair{};void* raycaster{};uintptr_t crosshairRoot{},raycasterRoot{};
uint64_t discoverAt{},lastReport{};std::vector<std::pair<void*,bool>> savedCull;
void* Exact(void* cls,const char* name,std::initializer_list<const char*> types){
    for(auto c=cls;c;c=il::class_get_parent(c)){void* it{};while(auto m=il::class_get_methods(c,&it)){
        if(strcmp(il::method_get_name(m),name)||il::method_get_param_count(m)!=types.size())continue;
        bool ok=true;uint32_t i{};for(auto type:types)if(strcmp(il::type_get_name(il::method_get_param(m,i++)),type))ok=false;if(ok)return m;
    }}return nullptr;
}
bool Invoke(void* method,void* object,void** args=nullptr){if(!method)return false;void* exception{};il::runtime_invoke(method,object,args,&exception);if(exception)Log("Native aim UI exception method=%s",il::method_get_name(method));return !exception;}
std::string Name(void* object){auto p=(char*)il::Call(object,"get_name");if(!p)return {};int n=*(int*)(p+sizeof(il::Object));if(n<0||n>256)return {};auto chars=(wchar_t*)(p+sizeof(il::Object)+4);std::string result;for(int i=0;i<n;++i)result+=chars[i]<128?(char)chars[i]:'?';return result;}
bool Alive(void* object){static auto exists=il::Find("UnityEngine","Object","op_Implicit",1);void* args[]={object};return object&&exists&&il::Value<bool>(il::Invoke(exists,nullptr,args));}
il::Array* FindAll(void* cls){static auto method=Exact(il::Class("UnityEngine","Resources"),"FindObjectsOfTypeAll",{"System.Type"});if(!cls)return nullptr;void* args[]={il::type_get_object(il::class_get_type(cls))};return (il::Array*)il::Invoke(method,nullptr,args);}
bool Active(void* component){return Alive(component)&&il::Value<bool>(il::Call(il::Call(component,"get_gameObject"),"get_activeInHierarchy"));}
void Discover(void* hero){
    if(hero!=lastHero){aimui::RestoreCrosshair();lastHero=hero;crosshair=raycaster=nullptr;if(crosshairRoot)il::gchandle_free(crosshairRoot);if(raycasterRoot)il::gchandle_free(raycasterRoot);crosshairRoot=raycasterRoot=0;discoverAt=0;}
    auto now=GetTickCount64();if(now<discoverAt)return;discoverAt=now+2000;
    if(!Alive(crosshair)){
        crosshair=nullptr;if(crosshairRoot)il::gchandle_free(crosshairRoot);crosshairRoot=0;
        auto all=FindAll(il::Class("UnityEngine","RectTransform"));
        if(all&&all->length<30000)for(uintptr_t i=0;i<all->length;++i){auto candidate=all->items[i];if(Name(candidate)!="Crosshair"||!Active(candidate))continue;
            bool heroHUD=false;auto parent=il::Call(candidate,"get_parent");for(int depth=0;parent&&depth<12;++depth){if(Name(parent)=="HeroHUD")heroHUD=true;parent=il::Call(parent,"get_parent");}
            if(heroHUD){crosshair=candidate;crosshairRoot=il::gchandle_new(candidate,false);Log("Native crosshair branch resolved=%p",crosshair);break;}
        }
    }
    if(!Alive(raycaster)){
        raycaster=nullptr;if(raycasterRoot)il::gchandle_free(raycasterRoot);raycasterRoot=0;
        auto all=FindAll(il::Class("Awaken.TG.Main.Heroes.Combat","VCHeroRaycaster"));
        if(all&&all->length<32)for(uintptr_t i=0;i<all->length;++i)if(Active(all->items[i])){raycaster=all->items[i];raycasterRoot=il::gchandle_new(raycaster,false);Log("Native aim raycaster resolved=%p",raycaster);break;}
    }
}
bool Raycast(Vec3 origin,Vec3 direction,Vec3& point){
    static auto cls=il::Class("UnityEngine","RaycastHit");static auto raycast=Exact(il::Class("UnityEngine","Physics"),"Raycast",{"UnityEngine.Vector3","UnityEngine.Vector3","UnityEngine.RaycastHit&","System.Single","System.Int32","UnityEngine.QueryTriggerInteraction"});
    static auto field=cls?il::class_get_field_from_name(cls,"m_Point"):nullptr;
    auto size=(int(*)(void*,uint32_t*))GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_class_value_size");
    uint32_t alignment{};if(!cls||!raycast||!field||!size||size(cls,&alignment)!=44||il::field_get_offset(field)!=sizeof(il::Object))return false;
    alignas(16) unsigned char hit[48]{};float distance=100;int mask=-5,ignoreTriggers=1;
    // Match the native raycaster's configured target/obstacle layers when available.
    auto markerField=il::class_get_field_from_name(((il::Object*)raycaster)->klass,"markerPlacementDetection");void* check{};if(markerField)il::field_get_value(raycaster,markerField,&check);
    if(check){int accept{},prevent{};auto c=((il::Object*)check)->klass;auto a=il::class_get_field_from_name(c,"accept"),p=il::class_get_field_from_name(c,"prevent");if(a&&p){il::field_get_value(check,a,&accept);il::field_get_value(check,p,&prevent);if(accept|prevent)mask=accept|prevent;}}
    void* args[]={&origin,&direction,hit,&distance,&mask,&ignoreTriggers};void* exception{};auto result=il::runtime_invoke(raycast,nullptr,args,&exception);
    if(exception||!il::Value<bool>(result))return false;memcpy(&point,hit,sizeof(point));return true;
}
}
NativeAimUIState aimui::Prepare(void* camera,void* hero){
    RestoreCrosshair();Discover(hero);NativeAimUIState state{};if(!Active(crosshair)||!Active(raycaster))return state;
    static auto visible=il::Find("UnityEngine","Cursor","get_visible",0),lock=il::Find("UnityEngine","Cursor","get_lockState",0);
    if(il::Value<bool>(il::Invoke(visible))&&il::Value<int>(il::Invoke(lock))!=1)return state;
    // Read the native artwork bounds; never modify CanvasRenderer culling.
    static auto children=Exact(il::Class("UnityEngine","GameObject"),"GetComponentsInChildren",{"System.Type","System.Boolean"});
    bool inactive=false;void* rendererArgs[]={il::type_get_object(il::class_get_type(il::Class("UnityEngine","CanvasRenderer"))),&inactive};
    auto renderers=(il::Array*)il::Invoke(children,il::Call(crosshair,"get_gameObject"),rendererArgs);if(!renderers||renderers->length>128)return state;
    static auto arrayNew=(void*(*)(void*,uintptr_t))GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_array_new");
    static void* corners{};static uintptr_t cornerRoot{};if(!corners&&arrayNew){corners=arrayNew(il::Class("UnityEngine","Vector3"),4);if(corners)cornerRoot=il::gchandle_new(corners,false);}if(!corners)return state;
    state.minX=state.minY=1e9f;state.maxX=state.maxY=-1e9f;
    for(uintptr_t i=0;i<renderers->length;++i){auto renderer=renderers->items[i];if(il::Value<bool>(il::Call(renderer,"get_cull"))||il::Value<float>(il::Call(renderer,"GetInheritedAlpha"))<=.001f||il::Value<float>(il::Call(renderer,"GetAlpha"))<=.001f)continue;
        // View containers can have an empty CanvasRenderer stretched across
        // the entire screen (VMeleeCrosshairPart). Only real Graphic artwork
        // contributes bounds; keep its descendant images/text eligible.
        static auto component=Exact(il::Class("UnityEngine","Component"),"GetComponent",{"System.Type"});
        static auto graphicType=il::type_get_object(il::class_get_type(il::Class("UnityEngine.UI","Graphic")));void* graphicArgs[]={graphicType};
        auto graphic=il::Invoke(component,renderer,graphicArgs);if(!graphic||!il::Value<bool>(il::Call(graphic,"get_isActiveAndEnabled")))continue;
        auto rect=il::Call(renderer,"get_transform");void* a[]={corners};il::Call(rect,"GetWorldCorners",1,a);auto values=(Vec3*)((char*)corners+sizeof(il::Array)-sizeof(void*));
        for(int j=0;j<4;++j){state.minX=std::min(state.minX,values[j].x);state.minY=std::min(state.minY,values[j].y);state.maxX=std::max(state.maxX,values[j].x);state.maxY=std::max(state.maxY,values[j].y);}
    }
    if(state.maxX<=state.minX||state.maxY<=state.minY||state.maxX-state.minX>500||state.maxY-state.minY>500)return state;
    state.minX-=2;state.minY-=2;state.maxX+=2;state.maxY+=2;
    static auto getRay=il::Find("Awaken.TG.Main.Heroes.Combat","VCHeroRaycaster","GetViewRay",2);
    Vec3 origin{},direction{};void* args[]={&origin,&direction};if(!Invoke(getRay,raycaster,args))return state;
    float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);if(!std::isfinite(length)||length<.1f)return state;
    direction={direction.x/length,direction.y/length,direction.z/length};Vec3 target=origin+Vec3{direction.x*100,direction.y*100,direction.z*100};bool hit=Raycast(origin,direction,target);
    auto p=il::Value<Vec3>(il::Call(crosshair,"get_position"));state.pixelX=p.x;state.pixelY=p.y;state.worldPoint=target;state.visible=true;
    if(GetTickCount64()-lastReport>5000){Log("Native aim UI ray origin=(%.3f %.3f %.3f) direction=(%.3f %.3f %.3f) target=(%.3f %.3f %.3f) hit=%d pixel=(%.1f %.1f) artwork=(%.1f %.1f %.1f %.1f)",origin.x,origin.y,origin.z,direction.x,direction.y,direction.z,target.x,target.y,target.z,hit,p.x,p.y,state.minX,state.minY,state.maxX,state.maxY);lastReport=GetTickCount64();}
    return state;
}
bool aimui::HideCrosshair(){
    RestoreCrosshair();if(!Active(crosshair))return false;
    auto go=il::Call(crosshair,"get_gameObject");static auto method=Exact(il::Class("UnityEngine","GameObject"),"GetComponentsInChildren",{"System.Type","System.Boolean"});
    bool no=false,yes=true;void* args[]={il::type_get_object(il::class_get_type(il::Class("UnityEngine","CanvasRenderer"))),&no};
    auto renderers=(il::Array*)il::Invoke(method,go,args);if(!renderers||renderers->length>128)return false;
    bool nonzero=false;
    for(uintptr_t i=0;i<renderers->length;++i){auto renderer=renderers->items[i];bool culled=il::Value<bool>(il::Call(renderer,"get_cull"));float alpha=il::Value<float>(il::Call(renderer,"GetInheritedAlpha"));
        if(!culled&&alpha>.001f)nonzero=true;savedCull.emplace_back(renderer,culled);void* value[]={&yes};il::Call(renderer,"set_cull",1,value);
    }
    return nonzero;
}
void aimui::RestoreCrosshair(){for(auto [renderer,culled]:savedCull){void* args[]={&culled};il::Call(renderer,"set_cull",1,args);}savedCull.clear();}
