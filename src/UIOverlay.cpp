#include "UIOverlay.h"
#include "Il2Cpp.h"
#include "OpenXRBridge.h"
#include <string>
#include <vector>

namespace ui {
namespace {
void* texture{};ID3D11Texture2D* nativeTexture{};
void* prepare{};void* restore{};int logicalWidth{},logicalHeight{};
void* setupCamera{};void* execute{};void* draw{};void* submit{};
uintptr_t roots[12]{};int rootCount{};
bool attempted{},available{},rebuild{};
void* Root(void* object){if(object&&rootCount<12)roots[rootCount++]=il::gchandle_new(object,false);return object;}
void* Exact(void* cls,const char* name,const std::vector<std::string>& types){
    if(!cls)return nullptr;void* it{};
    while(auto m=il::class_get_methods(cls,&it)){
        if(strcmp(il::method_get_name(m),name)||il::method_get_param_count(m)!=types.size())continue;
        bool match=true;for(uint32_t i=0;i<types.size();++i)if(types[i]!=il::type_get_name(il::method_get_param(m,i)))match=false;
        if(match)return m;
    }return nullptr;
}
bool Invoke(void* method,void* object,void** args=nullptr){
    if(!method)return false;void* exception{};il::runtime_invoke(method,object,args,&exception);
    if(exception)Log("UI capture managed exception method=%s exception=%p",il::method_get_name(method),exception);
    return !exception;
}
void ReleaseCapture(){
    Log("UI capture rebuilding after load/lost target oldNative=%p roots=%d",nativeTexture,rootCount);
    if(prepare)il::Call(prepare,"Release");if(restore)il::Call(restore,"Release");
    if(texture&&*(void**)((char*)texture+sizeof(il::Object))){
        auto destroy=il::Find("UnityEngine","Object","Destroy",1);void* args[]={texture};if(destroy)il::Invoke(destroy,nullptr,args);
    }
    for(int i=0;i<rootCount;++i){if(roots[i])il::gchandle_free(roots[i]);roots[i]=0;}
    rootCount=0;texture=prepare=restore=nullptr;nativeTexture=nullptr;logicalWidth=logicalHeight=0;attempted=available=false;
}
void AuditCanvases(){
    auto cls=il::Class("UnityEngine","Canvas");if(!cls)return;
    auto method=Exact(il::Class("UnityEngine","Resources"),"FindObjectsOfTypeAll",{"System.Type"});
    auto type=il::type_get_object(il::class_get_type(cls));void* args[]={type};
    auto canvases=(il::Array*)il::Invoke(method,nullptr,args);
    Log("UI root canvas audit count=%llu",canvases?canvases->length:0);
    if(!canvases||canvases->length>512)return;
    for(uintptr_t i=0;i<canvases->length;++i){
        auto canvas=canvases->items[i];
        if(!il::Value<bool>(il::Call(canvas,"get_isRootCanvas")))continue;
        auto text=(char*)il::Call(canvas,"get_name");std::string name;
        if(text){int n=*(int*)(text+sizeof(il::Object));auto chars=(wchar_t*)(text+sizeof(il::Object)+4);if(n>0&&n<256)for(int j=0;j<n;++j)name+=chars[j]<128?(char)chars[j]:'?';}
        Log("UI root name=%s active=%d mode=%d display=%d order=%d camera=%p",name.c_str(),il::Value<bool>(il::Call(canvas,"get_isActiveAndEnabled")),il::Value<int>(il::Call(canvas,"get_renderMode")),il::Value<int>(il::Call(canvas,"get_targetDisplay")),il::Value<int>(il::Call(canvas,"get_sortingOrder")),il::Call(canvas,"get_worldCamera"));
    }
}
bool Initialize(void* camera){
    auto ctx=il::Class("UnityEngine.Rendering","ScriptableRenderContext");
    auto cb=il::Class("UnityEngine.Rendering","CommandBuffer");
    auto rt=il::Class("UnityEngine","RenderTexture");
    auto identifier=il::Class("UnityEngine.Rendering","RenderTargetIdentifier");
    setupCamera=Exact(ctx,"SetupCameraProperties",{"UnityEngine.Camera","System.Boolean"});
    execute=Exact(ctx,"ExecuteCommandBuffer",{"UnityEngine.Rendering.CommandBuffer"});
    draw=Exact(ctx,"DrawUIOverlay",{"UnityEngine.Camera"});submit=Exact(ctx,"Submit",{});
    auto ctor=Exact(rt,".ctor",{"System.Int32","System.Int32","System.Int32","UnityEngine.RenderTextureFormat","UnityEngine.RenderTextureReadWrite"});
    auto cbCtor=Exact(cb,".ctor",{});auto setTarget=Exact(cb,"SetRenderTarget",{"UnityEngine.Rendering.RenderTargetIdentifier"});
    auto clear=Exact(cb,"ClearRenderTarget",{"System.Boolean","System.Boolean","UnityEngine.Color","System.Single"});
    auto viewport=Exact(cb,"SetViewport",{"UnityEngine.Rect"});
    auto textureIdCtor=Exact(identifier,".ctor",{"UnityEngine.Texture"});
    auto cameraIdCtor=Exact(identifier,".ctor",{"UnityEngine.Rendering.BuiltinRenderTextureType"});
    auto getNative=(void*(*)(void*))il::resolve_icall("UnityEngine.Texture::GetNativeTexturePtr_Injected");
    if(!ctx||!cb||!rt||!identifier||!setupCamera||!execute||!draw||!submit||!ctor||!cbCtor||!setTarget||!clear||!viewport||!textureIdCtor||!cameraIdCtor||!getNative){Log("UI capture APIs missing");return false;}
    auto size=(int(*)(void*,uint32_t*))GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_class_value_size");uint32_t align{};
    if(!size||size(ctx,&align)!=8||size(identifier,&align)!=40){Log("UI capture value-type layout mismatch");return false;}
    int w=il::Value<int>(il::Call(camera,"get_pixelWidth")),h=il::Value<int>(il::Call(camera,"get_pixelHeight"));
    if(w<320||h<200||w>7680||h>4320){Log("UI capture unexpected logical size %dx%d",w,h);return false;}
    int depth=24,format=0,rw=2;texture=Root(il::object_new(rt));void* textureArgs[]={&w,&h,&depth,&format,&rw};
    if(!Invoke(ctor,texture,textureArgs))return false;
    // Keep this native Unity object alive across UnloadUnusedAssets, as with
    // the accepted eye render textures. A managed GC handle alone is insufficient.
    int hide=61;void* hideArgs[]={&hide};il::Call(texture,"set_hideFlags",1,hideArgs);
    void* nameArgs[]={il::string_new("AvalonVR_UI")};il::Call(texture,"set_name",1,nameArgs);il::Call(texture,"Create");
    nativeTexture=(ID3D11Texture2D*)getNative(*(void**)((char*)texture+0x10));if(!nativeTexture)return false;
    prepare=Root(il::object_new(cb));restore=Root(il::object_new(cb));
    if(!Invoke(cbCtor,prepare)||!Invoke(cbCtor,restore))return false;
    auto textureId=Root(il::object_new(identifier));auto cameraId=Root(il::object_new(identifier));
    void* idArgs[]={texture};int cameraTarget=2;void* cameraArgs[]={&cameraTarget};
    if(!Invoke(textureIdCtor,il::object_unbox(textureId),idArgs)||!Invoke(cameraIdCtor,il::object_unbox(cameraId),cameraArgs))return false;
    void* bindArgs[]={il::object_unbox(textureId)};void* restoreArgs[]={il::object_unbox(cameraId)};
    struct Rect {float x,y,w,h;} rect{0,0,(float)w,(float)h};struct Color {float r,g,b,a;} transparent{};
    bool yes=true;float farDepth=1;void* clearArgs[]={&yes,&yes,&transparent,&farDepth};void* viewportArgs[]={&rect};
    if(!Invoke(setTarget,prepare,bindArgs)||!Invoke(clear,prepare,clearArgs)||!Invoke(viewport,prepare,viewportArgs)||!Invoke(setTarget,restore,restoreArgs)||!Invoke(viewport,restore,viewportArgs))return false;
    logicalWidth=w;logicalHeight=h;AuditCanvases();Log("UI transparent capture prepared %dx%d native=%p",w,h,nativeTexture);return true;
}
}
void Render(void* context,void* camera,uint64_t frameId,const NativeAimUIState& aim,Vec3 target,Quat orientation){
    if(!context||!camera)return;
    if(texture&&(!*(void**)((char*)texture+sizeof(il::Object))||!il::Value<bool>(il::Call(texture,"IsCreated"))))rebuild=true;
    if(rebuild){rebuild=false;ReleaseCapture();auto refresh=il::Find("UnityEngine","Canvas","ForceUpdateCanvases",0);if(refresh)il::Invoke(refresh);}
    if(!attempted){attempted=true;available=Initialize(camera);}
    if(!available)return;
    // ScriptableRenderContext is an eight-byte value type. The HDRP hook receives
    // its native context by value; runtime_invoke expects the unboxed receiver.
    void* ctx=context;bool stereo=false;void* setupArgs[]={camera,&stereo};void* cameraArgs[]={camera};void* prepareArgs[]={prepare};void* restoreArgs[]={restore};
    bool ok=Invoke(setupCamera,&ctx,setupArgs)&&Invoke(execute,&ctx,prepareArgs);
    if(ok)ok=Invoke(draw,&ctx,cameraArgs);
    bool restored=Invoke(execute,&ctx,restoreArgs);bool submitted=Invoke(submit,&ctx);
    if(!ok||!restored||!submitted){available=false;Log("UI transparent capture disabled after invocation failure");return;}
    xr::PublishUI(nativeTexture,frameId);
    if(aim.visible)xr::PublishAimUI(nativeTexture,frameId,aim.pixelX,aim.pixelY,target,orientation,logicalWidth,aim.minX,aim.minY,aim.maxX,aim.maxY);
    static bool reported{};if(!reported){reported=true;Log("UI transparent draw queued frame=%llu",frameId);}
}
void ResetAfterLoad(){rebuild=true;Log("UI capture invalidated for hero/load transition");}
}
