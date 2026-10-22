#include "StereoCamera.h"
#include "Il2Cpp.h"
#include "OpenXRBridge.h"
#include "UIOverlay.h"
#include "NativeDLSS.h"
#include "PerformanceModes.h"
#include "KeyEdges.h"
#include "VRStatus.h"
#include "ShaderHunter.h"
#include "StereoCulling.h"
#include "ShadowCulling.h"
#include "CharacterPoolDiagnostics.h"
#include "GameLoading.h"
#include "LoadingGatePolicy.h"
#include "Foveation.h"
#include "NativeVrsPass.h"
#include "QuadSampling.h"
#include "QuadExposure.h"
#include "QuadFog.h"
#include <MinHook.h>
#include <string>
#include <vector>
namespace {
struct Eye {void* camera{};void* transform{};void* hd{};void* texture{};ID3D11Texture2D* native{};uint32_t width{},height{};};
// Full stereo pair plus separate peripheral/focus pairs. The comparison never
// resizes a live HDRP target or aliases its temporal history with another view.
Eye eyes[6];void* eyeList{};void* cullCamera{};void* cullTransform{};uintptr_t roots[32]{};int rootCount{};
unsigned activeSlots[4]{0,1,0,0},activeViews=2;
bool eyeObjectsReady{};
bool IsEye(void* c){for(auto& eye:eyes)if(eye.camera&&c==eye.camera)return true;return false;}
void(*render)(void*,void*,void*,void*);
void(*endCamera)(void*,void*,void*);
void(*beginCamera)(void*,void*,void*);
uint32_t mask{};uint64_t activeFrame{},serial{};
bool centered{},enabled=true;Vec3 origin{};Quat originYaw{};
bool skipDesktop{},eyeDLSS{},dlssAfterPost{};bool resetEyeHistory{};
bool matchPeripheralMips{};void* materialMipBiasField{};
bool matchEyeClipPlanes=true,clipDiagnostics{};uint64_t clipLabelUntil{},clipCaptureAt{};
bool sharedCulling=true,shadowCompare{};uint64_t shadowLabelUntil{};
bool radialShadows{},radialCompare{};int radialResolution=1024;uint64_t radialLabelUntil{};
uint32_t upscalerPreset=5;
KeyEdges keyEdges;bool statusDirty=true;uint64_t statusRefreshUntil{},statusRefreshAt{};
quadview::TuningKeys tuningKeys;
bool Pressed(int key){return keyEdges.Pressed(key,(uint16_t)GetAsyncKeyState(key));}
void* getMain{};void* frameCount{};void* hdType{};
void* getHero{};void* lastHero{};uint64_t worldReadyAt{};bool manualMenu{};
void(*issueRenderEvent)(void*,int);
void __stdcall PairRendered(int frameId){xr::MarkRendered((uint32_t)frameId);}
void __stdcall PairRenderStart(int frameId){hunter::BeginFrame((uint32_t)frameId);xr::MarkRenderStart((uint32_t)frameId);}
void __stdcall PairSceneEnd(int frameId){hunter::EndScene();xr::MarkSceneEnd((uint32_t)frameId);}
void __stdcall HunterEye(int eye){hunter::Eye(eye);}
void* AddRoot(void* o){if(o&&rootCount<32)roots[rootCount++]=il::gchandle_new(o,false);return o;}
void* Setter(void* o,const char* name,void* value){void* a[]={value};return il::Call(o,name,1,a);}
void* Exact(void* cls,const char* name,const std::vector<std::string>& types){
    for(void* c=cls;c;c=il::class_get_parent(c)){void* it{};while(auto m=il::class_get_methods(c,&it)){
        if(strcmp(il::method_get_name(m),name)||il::method_get_param_count(m)!=types.size())continue;
        bool match=true;for(uint32_t i=0;i<types.size();++i){const char* t=il::type_get_name(il::method_get_param(m,i));if(!t||types[i]!=t)match=false;}
        if(match)return m;
    }}return nullptr;
}
void* Component(void* go,void* cls){
    void* type=il::type_get_object(il::class_get_type(cls));void* args[]={type};
    return il::Invoke(Exact(((il::Object*)go)->klass,"AddComponent",{"System.Type"}),go,args);
}
void* GetHD(void* camera){
    auto c=((il::Object*)camera)->klass;auto m=Exact(c,"GetComponent",{"System.Type"});void* a[]={hdType};return il::Invoke(m,camera,a);
}
template<class T>T ReadField(void* object,const char* name,T fallback={}){
    if(!object)return fallback;for(auto c=((il::Object*)object)->klass;c;c=il::class_get_parent(c))if(auto f=il::class_get_field_from_name(c,name)){T value{};il::field_get_value(object,f,&value);return value;}return fallback;
}
void* GetEyeHDCamera(int eye){
    static auto method=Exact(il::Class("UnityEngine.Rendering.HighDefinition","HDCamera"),"GetOrCreate",{"UnityEngine.Camera","System.Int32"});
    int multiPass=0;void* args[]={eyes[eye].camera,&multiPass};return method?il::Invoke(method,nullptr,args):nullptr;
}
void ReportEyeSettings(void* sourceHD){
    Log("Eye upscaler preset=%s enabled=%d afterPostRequested=%d",perf::Name(upscalerPreset),eyeDLSS,dlssAfterPost);
    static auto capability=il::Find("UnityEngine.Rendering.HighDefinition","HDDynamicResolutionPlatformCapabilities","get_DLSSDetected",0);
    Log("DLSS platform detected=%d source dynamicRes=%d allowDLSS=%d activeDLSS=%d quality=%u",capability?il::Value<bool>(il::Invoke(capability)):0,ReadField<bool>(sourceHD,"allowDynamicResolution"),ReadField<bool>(sourceHD,"allowDeepLearningSuperSampling"),ReadField<bool>(sourceHD,"cameraCanRenderDLSS"),ReadField<uint32_t>(sourceHD,"deepLearningSuperSamplingQuality"));
    for(unsigned v=0;v<activeViews;++v){auto i=activeSlots[v];auto hd=GetEyeHDCamera(i);
        Log("Eye %d clip planes consistent=%d near=%.6f far=%.3f",i,matchEyeClipPlanes,il::Value<float>(il::Call(eyes[i].camera,"get_nearClipPlane")),il::Value<float>(il::Call(eyes[i].camera,"get_farClipPlane")));
        Log("Eye %d effective dynamicRes=%d allowDLSS=%d activeDLSS=%d activeSTP=%d internal=%dx%d output=%ux%u",i,ReadField<bool>(eyes[i].hd,"allowDynamicResolution"),ReadField<bool>(eyes[i].hd,"allowDeepLearningSuperSampling"),ReadField<bool>(eyes[i].hd,"cameraCanRenderDLSS"),ReadField<bool>(eyes[i].hd,"cameraCanRenderSTP"),hd?il::Value<int>(il::Call(hd,"get_actualWidth")):-1,hd?il::Value<int>(il::Call(hd,"get_actualHeight")):-1,xr::RenderWidth(i),xr::RenderHeight(i));
        if(hd)Log("Eye %d upscaler scheduleValue=%u",i,(unsigned)il::Value<uint8_t>(il::Call(hd,"UpsampleSyncPoint")));
        Log("Eye %d texture mip material=%.6f global=%.6f matchingRequested=%d available=%d",i,ReadField<float>(eyes[i].hd,"materialMipBias"),ReadField<float>(hd,"<globalMipBias>k__BackingField"),matchPeripheralMips,materialMipBiasField!=nullptr);
    }
}
bool CreateEyes(void* sourceCamera,void* list){
    if(eyeObjectsReady)return true;
    auto goClass=il::Class("UnityEngine","GameObject"),camClass=il::Class("UnityEngine","Camera"),hdClass=il::Class("UnityEngine.Rendering.HighDefinition","HDAdditionalCameraData"),rtClass=il::Class("UnityEngine","RenderTexture");
    hdType=il::type_get_object(il::class_get_type(hdClass));
    auto textureCtor=Exact(rtClass,".ctor",{"System.Int32","System.Int32","System.Int32","UnityEngine.RenderTextureFormat","UnityEngine.RenderTextureReadWrite"});
    auto getNative=(void*(*)(void*))il::resolve_icall("UnityEngine.Texture::GetNativeTexturePtr_Injected");
    if(!goClass||!camClass||!hdClass||!rtClass||!textureCtor||!getNative){Log("Required camera construction API missing ctor=%p getNative=%p",textureCtor,getNative);return false;}
    int depth=24,format=0,rw=2,hide=61;
    const char* names[]={"AvalonVR_LeftEye","AvalonVR_RightEye","AvalonVR_PeripheralLeft","AvalonVR_PeripheralRight","AvalonVR_FocusLeft","AvalonVR_FocusRight"};
    for(int i=0;i<6;++i){
        int w=xr::RenderWidth(i),h=xr::RenderHeight(i);if(!w||!h)return false;
        void* go=AddRoot(il::object_new(goClass));void* name=il::string_new(names[i]);
        void* nameArg[]={name};il::Invoke(Exact(goClass,".ctor",{"System.String"}),go,nameArg);
        Setter(go,"set_hideFlags",&hide);
        auto ddol=il::Find("UnityEngine","Object","DontDestroyOnLoad",1);void* goArg[]={go};il::Invoke(ddol,nullptr,goArg);
        eyes[i].camera=AddRoot(Component(go,camClass));eyes[i].hd=AddRoot(Component(go,hdClass));
        eyes[i].transform=il::Call(eyes[i].camera,"get_transform");
        eyes[i].texture=AddRoot(il::object_new(rtClass));void* args[]={&w,&h,&depth,&format,&rw};il::Invoke(textureCtor,eyes[i].texture,args);
        Setter(eyes[i].texture,"set_hideFlags",&hide);il::Call(eyes[i].texture,"Create");
        // UnityEngine.Object.m_CachedPtr was verified at +0x10 in this build's metadata.
        eyes[i].native=(ID3D11Texture2D*)getNative(*(void**)((char*)eyes[i].texture+0x10));
        eyes[i].width=w;eyes[i].height=h;
        bool no=false;Setter(eyes[i].camera,"set_enabled",&no);
        Log("Eye %d camera=%p target=%p native=%p %dx%d",i,eyes[i].camera,eyes[i].texture,eyes[i].native,w,h);
    }
    eyeList=AddRoot(il::object_new(((il::Object*)list)->klass));il::Call(eyeList,".ctor");
    void* cullGO=AddRoot(il::object_new(goClass));void* cullName=il::string_new("AvalonVR_ShadowCulling");void* cullArgs[]={cullName};il::Invoke(Exact(goClass,".ctor",{"System.String"}),cullGO,cullArgs);
    Setter(cullGO,"set_hideFlags",&hide);void* ddolArgs[]={cullGO};il::Invoke(il::Find("UnityEngine","Object","DontDestroyOnLoad",1),nullptr,ddolArgs);
    cullCamera=AddRoot(Component(cullGO,camClass));cullTransform=il::Call(cullCamera,"get_transform");bool disabled=false;Setter(cullCamera,"set_enabled",&disabled);
    bool valid=eyeList!=nullptr;for(auto& eye:eyes)valid&=eye.native!=nullptr;eyeObjectsReady=valid;return valid;
}
bool ResizeActiveTargets(){
    static auto getNative=(void*(*)(void*))il::resolve_icall("UnityEngine.Texture::GetNativeTexturePtr_Injected");
    if(!getNative)return false;
    for(unsigned v=0;v<activeViews;++v){auto i=activeSlots[v];auto& e=eyes[i];int w=xr::RenderWidth(i),h=xr::RenderHeight(i);
        if(e.native&&e.width==w&&e.height==h)continue;D3D11_TEXTURE2D_DESC d{};
        auto cls=((il::Object*)e.texture)->klass;
        auto release=Exact(cls,"Release",{}),create=Exact(cls,"Create",{}),setWidth=Exact(cls,"set_width",{"System.Int32"}),setHeight=Exact(cls,"set_height",{"System.Int32"});
        if(!release||!create||!setWidth||!setHeight)return false;
        // Unity owns destruction/recreation of the native texture. COM views
        // retain the old generation until the compositor has rebound all four.
        bool okay=true;
        auto invoke=[&](void* m,void** args=nullptr)->void*{if(!okay)return nullptr;void* exception{};auto result=il::runtime_invoke(m,e.texture,args,&exception);if(exception){okay=false;Log("Quad resize managed exception eye=%u method=%s",i,il::method_get_name(m));}return exception?nullptr:result;};
        e.native=nullptr;invoke(release);void* a[]={&w};invoke(setWidth,a);void* b[]={&h};invoke(setHeight,b);
        if(!il::Value<bool>(invoke(create)))return false;
        e.native=(ID3D11Texture2D*)getNative(*(void**)((char*)e.texture+0x10));if(!e.native)return false;e.native->GetDesc(&d);
        if(d.Width!=w||d.Height!=h||d.SampleDesc.Count!=1)return false;
        e.width=w;e.height=h;
        resetEyeHistory=true;Log("Quad target recreated eye=%u size=%dx%d native=%p",i,w,h,e.native);
    }
    return true;
}
void EndCamera(void* ctx,void* camera,void* method){
    for(unsigned v=0;v<activeViews;++v)if(camera==eyes[activeSlots[v]].camera)mask|=1u<<v;
    endCamera(ctx,camera,method);
    if(IsEye(camera))issueRenderEvent((void*)&HunterEye,-1);
}
void BeginCamera(void* ctx,void* camera,void* method){
    for(unsigned v=0;v<activeViews;++v)if(camera==eyes[activeSlots[v]].camera)issueRenderEvent((void*)&HunterEye,v&1);
    beginCamera(ctx,camera,method);
}
void Render(void* pipeline,void* ctx,void* list,void* method){
    static bool reported{};if(!reported){Log("HDRP concrete Render callback reached");reported=true;}
    vrstatus::Tick();
    ReportCharacterPools();
    hunter::Tick(lastHero&&enabled&&xr::IsReady()&&!manualMenu);if(hunter::CaptureRequested())xr::RequestCapture();
    if(Pressed(VK_F1)){enabled=!enabled;Log("Stereo enabled=%d",enabled);}
    if(Pressed(VK_F2)){centered=false;xr::ReanchorMenus();Log("Recenter requested");}
    if(Pressed(VK_F4)){xr::RequestCapture();hunter::RequestShadowConsumers();}
    DWORD foregroundPID{};GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPID);
    auto tune=tuningKeys.Update(GetTickCount64(),(GetAsyncKeyState(VK_OEM_PLUS)&0x8000)||(GetAsyncKeyState(VK_ADD)&0x8000),
        (GetAsyncKeyState(VK_OEM_MINUS)&0x8000)||(GetAsyncKeyState(VK_SUBTRACT)&0x8000),(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,
        (GetAsyncKeyState(VK_SHIFT)&0x8000)!=0,foregroundPID==GetCurrentProcessId());
    if(xr::TuneQuad(tune)){resetEyeHistory=true;statusDirty=true;}
    if(Pressed(VK_F12)&&(GetAsyncKeyState(VK_CONTROL)&0x8000)){
        if(GetAsyncKeyState(VK_MENU)&0x8000){matchPeripheralMips=!matchPeripheralMips;resetEyeHistory=true;statusDirty=true;Log("Quad peripheral mip matching requested=%d available=%d (Ctrl+Alt+F12)",matchPeripheralMips,materialMipBiasField!=nullptr);vrstatus::Message(matchPeripheralMips&&materialMipBiasField?"Peripheral mip matching ON | Ctrl+Alt+F12 compare":"Peripheral mip matching OFF | Ctrl+Alt+F12 compare");}
        else if(GetAsyncKeyState(VK_SHIFT)&0x8000){bool border=xr::ToggleQuadDebug();vrstatus::Message(border?"Quad focus border ON":"Quad focus border OFF");}
        else {bool quad=xr::ToggleQuadViews();resetEyeHistory=true;statusDirty=true;vrstatus::Message(quad?"Quad views ON | Ctrl+F12 compare":"Stereo views | Ctrl+F12 quad views");}
    }
    if(Pressed(VK_F3)){manualMenu=!manualMenu;Log("Manual menu layer=%d",manualMenu);}
    if(Pressed(VK_F8)){bool controlDown=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;if(controlDown&&(GetAsyncKeyState(VK_SHIFT)&0x8000)){bool fog=quadfog::Toggle();resetEyeHistory=true;statusDirty=true;vrstatus::Message(fog?"Matched fog sampling ON | Ctrl+Shift+F8 compare":"Native fog sampling | Ctrl+Shift+F8 restore");}else if(controlDown&&radialCompare){radialShadows=!radialShadows;resetEyeHistory=true;radialLabelUntil=GetTickCount64()+6000;xr::RequestCapture();Log("Player-centered shadow cascades requested=%d (Ctrl+F8)",radialShadows);}else if(controlDown&&shadowCompare){sharedCulling=!sharedCulling;resetEyeHistory=true;shadowLabelUntil=GetTickCount64()+6000;xr::RequestCapture();Log("Shared shadow camera requested=%d (Ctrl+F8)",sharedCulling);}else if(controlDown&&clipDiagnostics){matchEyeClipPlanes=!matchEyeClipPlanes;resetEyeHistory=true;clipLabelUntil=GetTickCount64()+6000;clipCaptureAt=GetTickCount64()+2000;Log("Eye depth correction requested=%d (Ctrl+F8)",matchEyeClipPlanes);}else if(!controlDown){skipDesktop=!skipDesktop;statusDirty=true;Log("Performance desktop skip requested=%d",skipDesktop);}}
    if(Pressed(VK_F10)){eyeDLSS=!eyeDLSS;resetEyeHistory=true;statusDirty=true;Log("Performance eye DLSS requested=%d",eyeDLSS);}
    auto ini=modulePath.parent_path()/L"AvalonVR.ini";
    static bool force=GetPrivateProfileIntW(L"Debug",L"ForceStereoWithoutHeadset",0,ini.c_str())!=0;
    static bool initializedPerf{};if(!initializedPerf){initializedPerf=true;skipDesktop=GetPrivateProfileIntW(L"Performance",L"SkipDesktopRender",1,ini.c_str())!=0;eyeDLSS=GetPrivateProfileIntW(L"Performance",L"EyeDLSS",0,ini.c_str())!=0;
        bool legacyNative=GetPrivateProfileIntW(L"Performance",L"NativeDLSS",1,ini.c_str())!=0;uint32_t legacyQuality=GetPrivateProfileIntW(L"Performance",L"DLSSQuality",2,ini.c_str());if(legacyQuality>3)legacyQuality=2;
        upscalerPreset=GetPrivateProfileIntW(L"Performance",L"UpscalerPreset",perf::Preset(legacyNative,legacyQuality),ini.c_str());if(upscalerPreset<1||upscalerPreset>perf::PresetCount)upscalerPreset=5;
        dlssAfterPost=GetPrivateProfileIntW(L"Performance",L"DLSSAfterPost",0,ini.c_str())!=0;
        matchPeripheralMips=GetPrivateProfileIntW(L"QuadViews",L"MatchPeripheralMips",0,ini.c_str())!=0;
        Log("Quad peripheral mip matching initial=%d available=%d",matchPeripheralMips,materialMipBiasField!=nullptr);
        matchEyeClipPlanes=GetPrivateProfileIntW(L"Debug",L"MatchEyeClipPlanes",1,ini.c_str())!=0;clipDiagnostics=GetPrivateProfileIntW(L"Debug",L"ClipPlaneDiagnostics",0,ini.c_str())!=0;
        sharedCulling=GetPrivateProfileIntW(L"Render",L"SharedCulling",1,ini.c_str())!=0;shadowCompare=GetPrivateProfileIntW(L"Debug",L"ShadowCullingComparison",0,ini.c_str())!=0;
        int legacyShadow=GetPrivateProfileIntW(L"Render",L"RadialShadowCascades",0,ini.c_str());
        radialShadows=GetPrivateProfileIntW(L"Render",L"ShadowFix",legacyShadow,ini.c_str())!=0;radialCompare=GetPrivateProfileIntW(L"Debug",L"RadialShadowComparison",1,ini.c_str())!=0;radialResolution=GetPrivateProfileIntW(L"Render",L"RadialShadowResolutionFloor",1024,ini.c_str());
        Log("Optional shadow fix initial=%d comparison=%d resolutionFloor=%d (shorter visible range)",radialShadows,radialCompare,radialResolution);
        Log("Performance desktop skip initial=%d eye upscaling initial=%d preset=%s dlssAfterPost=%d clipPlaneCorrection=%d clipDiagnostics=%d",skipDesktop,eyeDLSS,perf::Name(upscalerPreset),dlssAfterPost,matchEyeClipPlanes,clipDiagnostics);}
    if(Pressed(VK_F11)){
        if((GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_MENU)&0x8000)){bool shared=quadexposure::Toggle();resetEyeHistory=true;statusDirty=true;vrstatus::Message(shared?"Quad shared exposure ON | Ctrl+Alt+F11 compare":"Independent view exposure | Ctrl+Alt+F11 restore");}
        else if(GetAsyncKeyState(VK_CONTROL)&0x8000){bool batched=xr::ToggleBatchTransfers();vrstatus::Message(batched?"Batched eye transfers ON | Ctrl+F11 compare":"Legacy eye transfers | Ctrl+F11 restore");}
        else {
        upscalerPreset=upscalerPreset>=1&&upscalerPreset<4?upscalerPreset+1:1;eyeDLSS=true;resetEyeHistory=true;statusDirty=true;
        Log("Performance upscaler preset=%s (F10 full-resolution comparison; F11 next)",perf::Name(upscalerPreset));
        }
    }
    if(Pressed(VK_INSERT)){dlssAfterPost=!dlssAfterPost;resetEyeHistory=true;statusDirty=true;Log("Native DLSS after-post requested=%d",dlssAfterPost);}
    if(!enabled||(!xr::IsReady()&&!force)||!list){shadowcull::StopRadial();render(pipeline,ctx,list,method);return;}
    void* hero=getHero?il::Invoke(getHero):nullptr;
    if(hero!=lastHero){lastHero=hero;worldReadyAt=hero?GetTickCount64()+8000:0;clipCaptureAt=hero&&clipDiagnostics?worldReadyAt+2000:0;Log("Hero transition=%p stereoNotBefore=%llu",hero,worldReadyAt);centered=false;ui::ResetAfterLoad();resetEyeHistory=true;statusDirty=true;}
    static bool nativeLoadingGuard=GetPrivateProfileIntW(L"Debug",L"NativeLoadingGuard",1,ini.c_str())!=0;
    static loading::Gate loadingGate;
    auto loadingState=nativeLoadingGuard?loading::Read():loading::State{true,false};
    auto loadDecision=loadingGate.Observe(loadingState.available,loadingState.active,GetTickCount64());
    if(loadDecision.entered||loadDecision.exited){
        ui::ResetAfterLoad();resetEyeHistory=true;statusDirty=true;centered=false;
        Log("Native loading guard entered=%d exited=%d available=%d loading=%d hero=%p; original camera list retained until ready",loadDecision.entered,loadDecision.exited,loadingState.available,loadingState.active,hero);
    }
    bool menu=manualMenu||!hero||GetTickCount64()<worldReadyAt||loadDecision.nativeOnly;xr::SetMenuVisible(menu);
    static bool lastMenu=true;if(menu!=lastMenu){Log("Menu layer visible=%d",menu);lastMenu=menu;}
    if(menu){shadowcull::StopRadial();xr::Frame frame{};frame.id=++serial;xr::Begin(frame);render(pipeline,ctx,list,method);return;}
    void* camera=il::Invoke(getMain);int count=il::Value<int>(il::Call(list,"get_Count"));
    bool found=false;for(int i=0;i<count;++i){void* a[]={&i};if(il::Call(list,"get_Item",1,a)==camera)found=true;}
    if(!found){camera=nullptr;for(int i=0;i<count;++i){void* a[]={&i};void* candidate=il::Call(list,"get_Item",1,a);if(candidate&&!IsEye(candidate)&&!il::Call(candidate,"get_targetTexture")){camera=candidate;break;}}}
    if(!camera){render(pipeline,ctx,list,method);return;}
    if(!CreateEyes(camera,list)){enabled=false;render(pipeline,ctx,list,method);return;}
    bool omitDesktop=skipDesktop&&found;
    bool lateDLSS=eyeDLSS&&perf::UsesDLSS(upscalerPreset)&&dlssAfterPost;
    void* sourceHD=GetHD(camera);
    xr::Frame frame{};frame.id=++serial;frame.performanceMode=(omitDesktop?1:0)|((eyeDLSS?upscalerPreset:0)<<1)|(lateDLSS?16:0)|(xr::BatchTransfers()?32:0)|((foveation::Requested()&3)<<6)|((matchPeripheralMips&&materialMipBiasField&&sourceHD)?512:0)|(quadexposure::Enabled()?1024:0)|(quadfog::Enabled()?2048:0);bool xrFrame=xr::Begin(frame);
    if(!xrFrame&&!force){shadowcull::StopRadial();render(pipeline,ctx,list,method);return;}
    if(xrFrame&&!frame.render){shadowcull::StopRadial();render(pipeline,ctx,list,method);return;}
    activeViews=frame.viewCount;for(unsigned v=0;v<activeViews;++v)activeSlots[v]=frame.quad?v+2:v;
    if(!ResizeActiveTargets()){xr::QuadTargetsFailed();shadowcull::StopRadial();render(pipeline,ctx,list,method);return;}
    xr::SaveQuadSettings();
    static bool previousQuad{};if(frame.quad!=previousQuad){previousQuad=frame.quad;resetEyeHistory=true;statusDirty=true;}
    void* transform=il::Call(camera,"get_transform");Vec3 basePos=il::Value<Vec3>(il::Call(transform,"get_position"));Quat baseRot=il::Value<Quat>(il::Call(transform,"get_rotation"));
    auto convertPos=[](XrVector3f v){return Vec3{v.x,v.y,-v.z};};auto convertRot=[](XrQuaternionf q){return Quat{-q.x,-q.y,q.z,q.w};};
    Vec3 viewPos[4];Quat viewRot[4];
    for(unsigned i=0;i<activeViews;++i){viewPos[i]=xrFrame?convertPos(frame.views[i].pose.position):Vec3{(i&1?.032f:-.032f),0,0};viewRot[i]=xrFrame?convertRot(frame.views[i].pose.orientation):Quat{};}
    if(!centered){origin={(viewPos[0].x+viewPos[1].x)*.5f,(viewPos[0].y+viewPos[1].y)*.5f,(viewPos[0].z+viewPos[1].z)*.5f};originYaw=Yaw(viewRot[0]);centered=true;Log("Centered head position=(%.3f %.3f %.3f)",origin.x,origin.y,origin.z);}
    Quat heading=Mul(Yaw(baseRot),Inv(originYaw));
    float nearZ=il::Value<float>(il::Call(camera,"get_nearClipPlane"));float farZ=il::Value<float>(il::Call(camera,"get_farClipPlane"));nearZ=std::max(.03f,nearZ);farZ=std::max(nearZ+1,farZ);
    bool no=false;int noAA=0;
    il::Call(eyeList,"Clear");
    // Skip only the verified source view. Menu/loading/F3 keep the original list;
    // camera scripts and native input still run normally outside this render list.
    for(int i=0;i<count;++i){void* a[]={&i};void* c=il::Call(list,"get_Item",1,a);if(!IsEye(c)&&!(omitDesktop&&c==camera)){void* b[]={c};il::Call(eyeList,"Add",1,b);}}
    hunter::FrameInfo hunterFrame{};hunterFrame.pair=frame.id;
    Vec3 gameEyePositions[2];Quat gameEyeRotations[2];
    for(unsigned v=0;v<activeViews;++v){auto i=activeSlots[v];
        il::Call(eyes[i].camera,"ResetCullingMatrix");
        Setter(eyes[i].camera,"CopyFrom",camera);Setter(eyes[i].camera,"set_targetTexture",eyes[i].texture);Setter(eyes[i].camera,"set_enabled",&no);
        // HDRP derives _ZBufferParams from these properties independently of
        // the custom projection. Both must use the same clamped clip range.
        if(matchEyeClipPlanes){Setter(eyes[i].camera,"set_nearClipPlane",&nearZ);Setter(eyes[i].camera,"set_farClipPlane",&farZ);}
        if(sourceHD){void* a[]={eyes[i].hd};il::Call(sourceHD,"CopyTo",1,a);}
        // Assign every owned view from the source each frame, including OFF,
        // focus and ordinary stereo. Never depend on CopyTo's field coverage
        // or accumulate the previous frame's compensation.
        if(sourceHD&&materialMipBiasField){
            float base=ReadField<float>(sourceHD,"materialMipBias");
            float delta=frame.quad&&(frame.performanceMode&512)&&v<2?quadsampling::PeripheralBias(xr::RenderWidth(i),xr::RenderHeight(i),xr::Width(),xr::Height()):0;
            if(std::isfinite(base)){float adjusted=base+delta;il::field_set_value(eyes[i].hd,materialMipBiasField,&adjusted);}
        }
        bool useDLSS=eyeDLSS&&perf::UsesDLSS(upscalerPreset);
        il::Set(eyes[i].hd,"xrRendering",&no);il::Set(eyes[i].hd,"allowDynamicResolution",&eyeDLSS);il::Set(eyes[i].hd,"allowDeepLearningSuperSampling",&useDLSS);il::Set(eyes[i].hd,"allowFidelityFX2SuperResolution",&no);
        if(useDLSS){bool yes=true,optimal=!perf::FixedScale(upscalerPreset);auto quality=perf::Quality(upscalerPreset);il::Set(eyes[i].hd,"deepLearningSuperSamplingUseCustomQualitySettings",&yes);il::Set(eyes[i].hd,"deepLearningSuperSamplingQuality",&quality);il::Set(eyes[i].hd,"deepLearningSuperSamplingUseCustomAttributes",&yes);il::Set(eyes[i].hd,"deepLearningSuperSamplingUseOptimalSettings",&optimal);}
        // Dedicated eyes retain independent histories. Fixed-input presets request
        // the same 40-percent scale for spatial, STP and native DLSS comparison.
        il::Set(eyes[i].hd,"antialiasing",&noAA);
        if(resetEyeHistory){bool yes=true;if(auto hd=GetEyeHDCamera(i))il::Set(hd,"resetPostProcessingHistory",&yes);}
        Vec3 p=basePos+Rotate(heading,viewPos[v]-origin);Quat q=Mul(heading,viewRot[v]);
        if(v<2){gameEyePositions[v]=p;gameEyeRotations[v]=q;}
        Setter(eyes[i].transform,"set_position",&p);Setter(eyes[i].transform,"set_rotation",&q);il::Call(eyes[i].camera,"ResetWorldToCameraMatrix");
        Matrix projection=xrFrame?Projection(frame.views[v].fov.angleLeft,frame.views[v].fov.angleRight,frame.views[v].fov.angleUp,frame.views[v].fov.angleDown,nearZ,farZ):il::Value<Matrix>(il::Call(camera,"get_projectionMatrix"));
        Setter(eyes[i].camera,"set_projectionMatrix",&projection);
        if(v<2){auto& pose=frame.views[v].pose;float poseData[]={pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w,pose.position.x,pose.position.y,pose.position.z};memcpy(hunterFrame.poses[v],poseData,sizeof(poseData));
        auto& fov=frame.views[v].fov;float angles[]={fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown};memcpy(hunterFrame.fovs[v],angles,sizeof(angles));memcpy(hunterFrame.gamePositions[v],&p,sizeof(p));memcpy(hunterFrame.projections[v],projection.m,sizeof(projection.m));}
        void* a[]={eyes[i].camera};il::Call(eyeList,"Add",1,a);
    }
    auto commonCull=stereocull::Build(gameEyePositions,gameEyeRotations,hunterFrame.fovs,nearZ,farZ);
    shadowcull::ConfigureRadial(stereocull::Scale(gameEyePositions[0]+gameEyePositions[1],.5f),radialShadows&&sharedCulling,radialResolution);
    bool cullingPrepared=false;
    if(sharedCulling&&commonCull.valid&&cullCamera){
        il::Call(cullCamera,"ResetCullingMatrix");Setter(cullCamera,"CopyFrom",camera);Setter(cullCamera,"set_targetTexture",eyes[0].texture);Setter(cullCamera,"set_enabled",&no);
        Setter(cullCamera,"set_nearClipPlane",&commonCull.nearClip);Setter(cullCamera,"set_farClipPlane",&commonCull.farClip);
        Setter(cullTransform,"set_position",&commonCull.position);Setter(cullTransform,"set_rotation",&commonCull.rotation);il::Call(cullCamera,"ResetWorldToCameraMatrix");Setter(cullCamera,"set_projectionMatrix",&commonCull.projection);
        void* cameras[4]{};for(unsigned v=0;v<activeViews;++v)cameras[v]=eyes[activeSlots[v]].camera;
        cullingPrepared=shadowcull::PrepareViews(cullCamera,cameras,activeViews,true);
    }else shadowcull::Prepare(nullptr,eyes[activeSlots[0]].camera,eyes[activeSlots[1]].camera,false);
    if(cullingPrepared)for(unsigned v=0;v<activeViews;++v)Setter(eyes[activeSlots[v]].camera,"set_cullingMatrix",&commonCull.matrix);
    static uint64_t cullingReportAt{};if(GetTickCount64()>=cullingReportAt){Log("Eye shadow culling shared=%d prepared=%d valid=%d backwardShift=%.5f tangents=(%.5f %.5f)",sharedCulling,cullingPrepared,commonCull.valid,commonCull.backwardShift,commonCull.horizontalTangent,commonCull.verticalTangent);cullingReportAt=GetTickCount64()+5000;}
    resetEyeHistory=false;
    hunterFrame.outputWidth=xr::Width();hunterFrame.outputHeight=xr::Height();hunterFrame.vrsFlags=(frame.performanceMode>>6)&3;
    if(hunterFrame.vrsFlags){auto hd=GetEyeHDCamera(activeSlots[0]);hunterFrame.inputWidth=hd?il::Value<int>(il::Call(hd,"get_actualWidth")):0;hunterFrame.inputHeight=hd?il::Value<int>(il::Call(hd,"get_actualHeight")):0;}
    hunter::QueueFrame(hunterFrame);
    activeFrame=frameCount?(uint64_t)il::Value<int>(il::Invoke(frameCount)):frame.id;mask=0;
    ID3D11Texture2D* textures[4]{};for(unsigned v=0;v<activeViews;++v)textures[v]=eyes[activeSlots[v]].native;
    xr::PrepareSources(textures,activeViews);
    issueRenderEvent((void*)&PairRenderStart,(int)frame.id);
    static uint64_t lastReport{};bool report=GetTickCount64()-lastReport>5000;
    {NativeDLSSScope dlss(pipeline,eyeDLSS,perf::Upscaler(upscalerPreset),perf::FixedScale(upscalerPreset)?40.0f:0.0f,lateDLSS);
        void* views[4]{};for(unsigned v=0;v<activeViews;++v)views[v]=eyes[activeSlots[v]].camera;
        quadexposure::Begin(views,activeViews,frame.quad);
        float fogDensity=std::max(float(xr::Width())/std::max(1u,xr::RenderWidth(2)),float(xr::Height())/std::max(1u,xr::RenderHeight(2)));
        quadfog::Begin(views,activeViews,frame.quad,fogDensity);
        render(pipeline,ctx,eyeList,method);
        quadfog::End();
        quadexposure::End();
        shadowcull::End();
        if(report){Log("Native DLSS after-post applied=%d",dlss.AppliedAfterPost());ReportEyeSettings(sourceHD);quadexposure::Report();quadfog::Report();}
        // HDRP can take a frame to apply a new scale. Refresh only while the
        // label is visible, without extending its lifetime or dirtying unchanged text.
        auto statusNow=GetTickCount64();bool restartStatus=statusDirty;
        if(statusDirty){statusDirty=false;statusRefreshUntil=statusNow+6000;statusRefreshAt=0;}
        if(statusNow<statusRefreshUntil&&statusNow>=statusRefreshAt){auto hd=GetEyeHDCamera(activeSlots[0]);
            int w=hd?il::Value<int>(il::Call(hd,"get_actualWidth")):0,h=hd?il::Value<int>(il::Call(hd,"get_actualHeight")):0;
            std::string label=frame.quad?"Quad views | ":"Stereo | ";label+=eyeDLSS?perf::DisplayName(upscalerPreset):"Full resolution";
            if(frame.quad)label+=(frame.performanceMode&512)?" | Mip match ON":" | Mip match OFF";
            if(frame.quad)label+=(frame.performanceMode&1024)?" | Shared exposure":" | Separate exposure";
            if(frame.quad)label+=(frame.performanceMode&2048)?" | Matched fog":" | Native fog";
            if(frame.quad){auto s=xr::QuadSettings();char tuning[128];snprintf(tuning,sizeof(tuning),"\nFocus %dx%d%% | Strength %d%% (outer %d%%)",s.horizontal,s.vertical,100-s.peripheral,s.peripheral);label+=tuning;}
            vrstatus::Show(label.c_str(),w,h,xr::Width(),xr::Height(),dlss.AppliedAfterPost(),restartStatus);statusRefreshAt=statusNow+250;
        }
    }
    issueRenderEvent((void*)&PairSceneEnd,(int)frame.id);
    auto labelNow=GetTickCount64();if(clipDiagnostics&&clipCaptureAt&&labelNow>=clipCaptureAt){auto output=modulePath.parent_path()/L"shader-hunter"/((matchEyeClipPlanes?L"depth-corrected-":L"depth-legacy-")+std::to_wstring(labelNow));hunter::CaptureNativeInputs(0xc46d40fa169e0c5eull,2,output,8);xr::RequestCapture();clipCaptureAt=0;Log("Bounded native inputs requested for user mark 6: %ls",output.c_str());}
    static uint64_t hunterLabelAt{};if(labelNow>=hunterLabelAt){auto label=hunter::Label();if(!label.empty())vrstatus::Message(label.c_str());else if(labelNow<radialLabelUntil)vrstatus::Message(radialShadows?(shadowcull::RadialActive()?"Shadow fix ON (shorter range) | Ctrl+F8 compare":"Shadow fix unavailable; native retained"):"Shadow fix OFF | Ctrl+F8 enable");else if(labelNow<shadowLabelUntil)vrstatus::Message(sharedCulling?"Shared shadow camera ON | Ctrl+F8 compare":"Separate shadow cameras (legacy) | Ctrl+F8 restore");else if(labelNow<clipLabelUntil)vrstatus::Message(matchEyeClipPlanes?"Depth correction ON | Ctrl+F8 compare":"Depth correction OFF (legacy) | Ctrl+F8 restore");hunterLabelAt=labelNow+200;}
    static auto cursorVisible=il::Find("UnityEngine","Cursor","get_visible",0),cursorLock=il::Find("UnityEngine","Cursor","get_lockState",0);
    bool menuUI=il::Value<bool>(il::Invoke(cursorVisible))&&il::Value<int>(il::Invoke(cursorLock))!=1;
    // Use the same recentered game heading as the native world. Menu camera
    // pitch can point straight up into the game's avatar-rendering scene;
    // preserve the VR horizon instead of inheriting that offscreen setup.
    auto menuCenter=origin+Rotate(originYaw,{0,0,1.5f});Vec3 menuPosition{menuCenter.x,menuCenter.y,-menuCenter.z};
    Quat menuOrientation{-originYaw.x,-originYaw.y,originYaw.z,originYaw.w};xr::SetMenuUIPlacement(menuUI,frame.id,menuPosition,menuOrientation);
    auto aim=aimui::Prepare(camera,hero);auto xrTarget=GamePointToXR(aim.worldPoint,basePos,heading,origin);
    auto oq=frame.views[0].pose.orientation;Quat xrRotation{oq.x,oq.y,oq.z,oq.w};
    ui::Render(ctx,camera,frame.id,aim,xrTarget,xrRotation);
    issueRenderEvent((void*)&PairRendered,(int)frame.id);
    xr::Publish(textures[0],textures[1],frame.id,frame.quad?0:mask);
    static int pointerChecks=0;
    if(++pointerChecks==60){
        auto native=(void*(*)(void*))il::resolve_icall("UnityEngine.Texture::GetNativeTexturePtr_Injected");
        for(unsigned v=0;v<activeViews;++v){auto i=activeSlots[v];Log("Eye %d native generation check cached=%p actual=%p",i,eyes[i].native,native(*(void**)((char*)eyes[i].texture+0x10)));}
    }
    static bool firstCapture=GetPrivateProfileIntW(L"Debug",L"CaptureFirstPair",0,ini.c_str())!=0;
    if(firstCapture){firstCapture=false;xr::RequestCapture();}
    if(report){Log("Native HDRP pair sourceFrame=%llu pair=%llu endEyeMask=%u desktopCameras=%d skipDesktop=%d eyeDLSSRequested=%d camera=%p quad=%d views=%u",activeFrame,frame.id,mask,count,omitDesktop,eyeDLSS,camera,frame.quad,activeViews);lastReport=GetTickCount64();}
}
}
bool InstallCameraHooks(){
    auto hdClass=il::Class("UnityEngine.Rendering.HighDefinition","HDAdditionalCameraData");
    auto fieldType=(void*(*)(void*))GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"),"il2cpp_field_get_type");
    auto mipField=hdClass?il::class_get_field_from_name(hdClass,"materialMipBias"):nullptr;
    if(mipField&&fieldType&&!strcmp(il::type_get_name(fieldType(mipField)),"System.Single"))materialMipBiasField=mipField;
    getMain=il::Find("UnityEngine","Camera","get_main",0);frameCount=il::Find("UnityEngine","Time","get_frameCount",0);
    getHero=il::Find("Awaken.TG.Main.Heroes","Hero","get_Current",0);Log("Hero-state method=%p",getHero);
    issueRenderEvent=(void(*)(void*,int))il::resolve_icall("UnityEngine.GL::GLIssuePluginEvent");Log("Unity render event icall=%p",issueRenderEvent);
    void* method{};auto cls=il::Class("UnityEngine.Rendering.HighDefinition","HDRenderPipeline");void* it{};
    while(auto m=il::class_get_methods(cls,&it))if(strcmp(il::method_get_name(m),"Render")==0&&il::method_get_param_count(m)==2){
        const char* param=il::type_get_name(il::method_get_param(m,1));Log("HDRP Render candidate %s ptr=%p",param,((il::Method*)m)->pointer);
        if(param&&strstr(param,"List")&&strstr(param,"Camera"))method=m;
    }
    auto end=il::Find("UnityEngine.Rendering","RenderPipelineManager","EndCameraRendering",2);
    auto begin=il::Find("UnityEngine.Rendering","RenderPipelineManager","BeginCameraRendering",2);
    if(!getMain||!method||!end||!issueRenderEvent){Log("Camera hook APIs missing");return false;}
    shadowcull::Install();
    quadexposure::Install();
    quadfog::Install();
    Log("Native VRS pass boundary available=%d",nativevrs::Install());
    auto a=MH_CreateHook(((il::Method*)method)->pointer,&Render,(void**)&render);auto b=MH_EnableHook(((il::Method*)method)->pointer);
    auto c=MH_CreateHook(((il::Method*)end)->pointer,&EndCamera,(void**)&endCamera);auto d=MH_EnableHook(((il::Method*)end)->pointer);
    if(begin){auto e=MH_CreateHook(((il::Method*)begin)->pointer,&BeginCamera,(void**)&beginCamera);auto f=MH_EnableHook(((il::Method*)begin)->pointer);Log("Hunter begin-camera marker hook=%d/%d",e,f);}
    Log("HDRP camera hooks render=%d/%d endCamera=%d/%d",a,b,c,d);return a==0&&b==0&&c==0&&d==0;
}
