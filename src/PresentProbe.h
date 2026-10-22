#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <filesystem>
#include <fstream>
inline bool ProbePresent(const std::filesystem::path& output){
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AvalonVRLauncherProbe";RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,wc.hInstance,nullptr);
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=32;desc.BufferDesc.Height=32;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* swap{};ID3D11Device* device{};ID3D11DeviceContext* context{};
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);
    bool ok=false;
    if(SUCCEEDED(hr)){
        auto target=(*(uintptr_t**)swap)[8];HMODULE owner{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)target,&owner);
        if(owner==GetModuleHandleW(L"dxgi.dll")){
            auto dos=(IMAGE_DOS_HEADER*)owner;auto nt=(IMAGE_NT_HEADERS*)((char*)owner+dos->e_lfanew);
            std::ofstream f(output);f<<std::hex<<(target-(uintptr_t)owner)<<" "<<nt->FileHeader.TimeDateStamp<<" "<<nt->OptionalHeader.SizeOfImage<<"\n";ok=f.good();
        }
        swap->Release();context->Release();device->Release();
    }
    DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return ok;
}
