#include "Il2Cpp.h"
#include <MinHook.h>
#include <atomic>
#include "StereoCamera.h"
#include "OpenXRBridge.h"
#include "DLSSDiagnostics.h"
#include "CharacterLifecycle.h"
#include "GameLoading.h"
static void(*beforeRender)(void*);
static std::atomic<int> callbacks{};
static void BeforeRender(void* info) {
    int n=++callbacks;
    if(n==1)Log("Main-thread before-render callback reached");
    beforeRender(info);
}
static DWORD WINAPI Worker(void*) {
    Log("AvalonVR Stage58 optional marked ground fog removal / Stage57 matched quad fog");
    // GameAssembly exports exist before its VM is initialized. A visible Unity
    // player window is a later native startup milestone; never call VM APIs at DLL load.
    bool visible=false;
    for(int i=0;i<1200&&!visible;++i){
        EnumWindows([](HWND w,LPARAM data)->BOOL{DWORD pid{};GetWindowThreadProcessId(w,&pid);wchar_t cls[128]{};GetClassNameW(w,cls,128);
            if(pid==GetCurrentProcessId()&&IsWindowVisible(w)&&wcscmp(cls,L"UnityWndClass")==0){*(bool*)data=true;return FALSE;}return TRUE;
        },(LPARAM)&visible);
        if(!visible)Sleep(100);
    }
    if(!visible){Log("Unity player window timeout");return 1;}
    Sleep(3000);
    Log("Unity window ready; resolving initialized VM");
    for(int i=0;i<1200&&!il::Init();++i)Sleep(100);
    Log("VM APIs resolved");
    if(!il::domain_get||!il::domain_get()){Log("IL2CPP timeout");return 1;}
    void* thread=il::thread_attach(il::domain_get());
    Log("Worker attached to VM");
    for(int i=0;i<1200&&!il::Class("UnityEngine","Camera");++i)Sleep(100);
    Log("Camera metadata ready");
    MH_Initialize();Log("MinHook initialized");
    characterlife::Install();
    loading::Initialize();
    if(GetPrivateProfileIntW(L"Debug",L"DLSSDiagnostics",0,(modulePath.parent_path()/L"AvalonVR.ini").c_str()))dlssdiag::Install();
    if(xr::InstallPresentHook())InstallCameraHooks();
    auto c=il::Class("UnityEngine","Application");
    auto m=c?il::class_get_method_from_name(c,"InvokeOnBeforeRender",0):nullptr;
    if(m && GetPrivateProfileIntW(L"Debug",L"Hook",0,(modulePath.parent_path()/L"AvalonVR.ini").c_str())){
        void* p=((il::Method*)m)->pointer;
        auto a=MH_Initialize();auto b=MH_CreateHook(p,&BeforeRender,(void**)&beforeRender);auto d=MH_EnableHook(p);
        Log("BeforeRender hook init=%d create=%d enable=%d target=%p",a,b,d,p);
    }
    il::thread_detach(thread);
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        DisableThreadLibraryCalls(h);wchar_t p[32768];GetModuleFileNameW(h,p,32768);modulePath=p;
        CloseHandle(CreateThread(nullptr,0,Worker,nullptr,0,nullptr));
    }
    return TRUE;
}
