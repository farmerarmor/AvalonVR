#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <cstdio>
#include <string>
#include "BuildGuard.h"
#include "PresentProbe.h"
static bool InjectEarlyHelper(HANDLE process,const std::filesystem::path& helper){
 auto name=helper.wstring();size_t size=(name.size()+1)*sizeof(wchar_t);
 void* remote=VirtualAllocEx(process,nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!remote)return false;
 if(!WriteProcessMemory(process,remote,name.c_str(),size,nullptr)){VirtualFreeEx(process,remote,0,MEM_RELEASE);return false;}
 HANDLE thread=CreateRemoteThread(process,nullptr,0,(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"),remote,0,nullptr);
 if(!thread){VirtualFreeEx(process,remote,0,MEM_RELEASE);return false;}
 DWORD wait=WaitForSingleObject(thread,10000),code{};
 if(wait==WAIT_OBJECT_0){GetExitCodeThread(thread,&code);VirtualFreeEx(process,remote,0,MEM_RELEASE);}CloseHandle(thread);
 return wait==WAIT_OBJECT_0&&code;
}
int wmain(int argc,wchar_t** argv){
    if(argc<3){fwprintf(stderr,L"Usage: AvalonVRLauncher <game exe> <mod dll> [arguments]\n");return 2;}
    std::filesystem::path game=std::filesystem::absolute(argv[1]), dll=std::filesystem::absolute(argv[2]);
    if(!std::filesystem::is_regular_file(game)||!std::filesystem::is_regular_file(dll))return 3;
    if(!SupportedBuild(game.parent_path()))return 11;
    if(!ProbePresent(dll.parent_path()/L"AvalonVR.present")){fprintf(stderr,"D3D11 Present probe failed\n");return 13;}
    std::wstring cmd=L"\""+game.wstring()+L"\"";
    for(int i=3;i<argc;++i)cmd+=L" \""+std::wstring(argv[i])+L"\"";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    bool attach=argc>=5&&wcscmp(argv[3],L"--attach")==0;
    // Use this process's Steam app context, avoiding a new URL-argument dialog
    // for each diagnostic log path. This does not change global environment.
    SetEnvironmentVariableW(L"SteamAppId",L"1466060");SetEnvironmentVariableW(L"SteamGameId",L"1466060");
    if(attach){pi.dwProcessId=wcstoul(argv[4],nullptr,10);pi.hProcess=OpenProcess(PROCESS_ALL_ACCESS,FALSE,pi.dwProcessId);if(!pi.hProcess)return 9;}
    if(!attach&&!CreateProcessW(game.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,nullptr,game.parent_path().c_str(),&si,&pi)){
        fwprintf(stderr,L"CreateProcess failed %lu\n",GetLastError());return 4;
    }
    printf("Game PID %lu\n",pi.dwProcessId);fflush(stdout);
    bool ready=false;
    for(int tries=0;tries<1200&&!ready;++tries){
        if(WaitForSingleObject(pi.hProcess,0)==WAIT_OBJECT_0){
            if(attach){DWORD exitCode{};GetExitCodeProcess(pi.hProcess,&exitCode);fprintf(stderr,"Attach target exited code=%lu\n",exitCode);break;}
            HANDLE processes=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
            PROCESSENTRY32W pe{sizeof(pe)};bool found=false;
            if(processes!=INVALID_HANDLE_VALUE&&Process32FirstW(processes,&pe))do{
                if(_wcsicmp(pe.szExeFile,game.filename().c_str())!=0||pe.th32ProcessID==pi.dwProcessId)continue;
                HANDLE candidate=OpenProcess(PROCESS_ALL_ACCESS,FALSE,pe.th32ProcessID);
                wchar_t image[32768];DWORD len=32768;
                if(candidate&&QueryFullProcessImageNameW(candidate,0,image,&len)&&_wcsicmp(image,game.c_str())==0){
                    CloseHandle(pi.hProcess);pi.hProcess=candidate;pi.dwProcessId=pe.th32ProcessID;found=true;
                    printf("Following Steam relaunch PID %lu\n",pi.dwProcessId);fflush(stdout);break;
                }
                if(candidate)CloseHandle(candidate);
            }while(Process32NextW(processes,&pe));
            if(processes!=INVALID_HANDLE_VALUE)CloseHandle(processes);
            if(!found){Sleep(100);continue;}
        }
        HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pi.dwProcessId);
        if(snap!=INVALID_HANDLE_VALUE){MODULEENTRY32W me{sizeof(me)};if(Module32FirstW(snap,&me))do{
            if(_wcsicmp(me.szModule,L"GameAssembly.dll")==0){ready=true;break;}
        }while(Module32NextW(snap,&me));CloseHandle(snap);}
        if(!ready)Sleep(100);
    }
    if(!ready){fprintf(stderr,"GameAssembly not loaded\n");return 5;}
    wchar_t verifiedPath[32768];DWORD verifiedLength=32768;
    if(!QueryFullProcessImageNameW(pi.hProcess,0,verifiedPath,&verifiedLength)||_wcsicmp(verifiedPath,game.c_str())!=0){fprintf(stderr,"Process path mismatch\n");return 12;}
    if(!attach&&GetPrivateProfileIntW(L"Characters",L"PoolCapacityScale",1,(dll.parent_path()/L"AvalonVR.ini").c_str())>1){
        auto helper=dll.parent_path()/L"AvalonVRCharacterPools.dll";
        bool loaded=std::filesystem::is_regular_file(helper)&&InjectEarlyHelper(pi.hProcess,helper);
        printf("Early character-pool helper loaded=%d (graphics mod retains normal startup timing)\n",loaded);fflush(stdout);
    }
    // Inject only after the engine has completed its native startup. Loading a
    // graphics-linked DLL while Unity creates its device can disturb startup.
    WaitForInputIdle(pi.hProcess,30000);
    Sleep(5000);
    if(WaitForSingleObject(pi.hProcess,0)==WAIT_OBJECT_0){DWORD exitCode{};GetExitCodeProcess(pi.hProcess,&exitCode);printf("Game exited before injection: %lu\n",exitCode);return 10;}
    size_t size=(dll.wstring().size()+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(pi.hProcess,nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!remote||!WriteProcessMemory(pi.hProcess,remote,dll.c_str(),size,nullptr))return 6;
    HANDLE t=CreateRemoteThread(pi.hProcess,nullptr,0,(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"),remote,0,nullptr);
    if(!t)return 7;
    DWORD wait=WaitForSingleObject(t,30000),code{};if(wait==WAIT_OBJECT_0){GetExitCodeThread(t,&code);VirtualFreeEx(pi.hProcess,remote,0,MEM_RELEASE);}
    printf("Injection wait=%lu module-low32=%08lx\n",wait,code);
    CloseHandle(t);if(pi.hThread)CloseHandle(pi.hThread);
    if(code){printf("Launcher is keeping the game session alive. Close the game normally to finish.\n");fflush(stdout);WaitForSingleObject(pi.hProcess,INFINITE);DWORD exitCode{};GetExitCodeProcess(pi.hProcess,&exitCode);printf("Game exited: %lu\n",exitCode);}
    CloseHandle(pi.hProcess);
    return code?0:8;
}
