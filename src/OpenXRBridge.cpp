#include "OpenXRBridge.h"
#include "Log.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <vector>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <d3d11_4.h>
#include "FlipBlit.h"
#include "GpuTimings.h"
#include "UIBlit.h"
#include "ShaderHunter.h"
#include "Foveation.h"
#include "QuadViewBlit.h"
#include "BufferUploads.h"
using Microsoft::WRL::ComPtr;
namespace xr {
static XrInstance instance{};static XrSystemId system{};static XrSession session{};static XrSpace space{};
static XrSpace menuSpace{};static XrSwapchain menuChain{};static std::vector<XrSwapchainImageD3D11KHR> menuImages;
static XrSwapchain uiChain{};static std::vector<XrSwapchainImageD3D11KHR> uiImages;
static ID3D11Texture2D* uiSource{};static std::atomic<uint64_t> uiPublished{};static uint32_t uiWidth{},uiHeight{};
static FlipBlit uiFlipBlit;static DXGI_FORMAT uiFormat{};static std::atomic<bool> uiFlipY{true};
static ID3D11Texture2D* uiFlipBoundSource{};
static UIBlit uiBlit;static DXGI_FORMAT menuFormat{};
static XrSwapchain aimChain{};static std::vector<XrSwapchainImageD3D11KHR> aimImages;
static ID3D11Texture2D* aimFull{};static std::atomic<uint64_t> aimPublished{};
static float aimPixelX{},aimPixelY{},aimSize{},aimBounds[4]{};static XrPosef aimPose{};
static constexpr uint32_t aimPixels=512;
static bool anchorMenus{true},menuUIActive{},mainMenuAnchorValid{};static XrPosef menuUIPose{},mainMenuPose{};
static std::atomic<uint64_t> menuPlacementFrame{},anchorGeneration{};static uint64_t uiAnchorSeen{},mainAnchorSeen{};
static FlipBlit flipBlit;static DXGI_FORMAT sceneFormat{};
static uint32_t menuWidth{},menuHeight{};static std::atomic<bool> menuVisible{true},flipY{false};static bool compositorFlip{},stopPending{};
static std::atomic<uint32_t> completedRenderFrame{};
static XrSwapchain chains[2]{};static std::vector<XrSwapchainImageD3D11KHR> images[2];
static XrSessionState state{XR_SESSION_STATE_UNKNOWN};
static std::atomic<bool> ready{},running{},frameOpen{},capture{};
static std::atomic<uint32_t> width{},height{},renderedMask{};
static std::atomic<uint64_t> published{};
static ID3D11Texture2D* source[4]{};
static std::atomic<uint32_t> sourceCount{2};
static QuadViewBlit quadBlit;
static std::atomic<bool> quadEnabled{},quadDebug{};
static quadview::Size cameraSizes[6]{};
static int focusHorizontal=50,focusVertical=50;
static int peripheralResolution=40;static uint64_t tuneChangedAt{};
static quadview::Settings queuedSettings{};static bool tuningQueued{};
static float focusFeather=.12f;
static Frame pending{};
static HANDLE frameDone=CreateEventW(nullptr,TRUE,TRUE,nullptr);
static ComPtr<ID3D11Device> device;static ComPtr<ID3D11DeviceContext> context;
static HRESULT(STDMETHODCALLTYPE *present)(IDXGISwapChain*,UINT,UINT);
static uint64_t pairs{},lastReport{};
static GpuTimings gpuTimings;
static PFN_xrGetVisibilityMaskKHR getVisibility{};
static bool visibilityExtension{},visibilityDirty=true;
static void Visibility(){
    if(!visibilityDirty||!getVisibility||!session)return;visibilityDirty=false;vrsmask::Mesh mesh[2];
    for(unsigned eye=0;eye<2;++eye){bool loaded=false;for(auto type:{XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR,XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR}){
        XrVisibilityMaskKHR data{XR_TYPE_VISIBILITY_MASK_KHR};if(XR_FAILED(getVisibility(session,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,eye,type,&data))||data.vertexCountOutput<3||data.vertexCountOutput>16384||!data.indexCountOutput||data.indexCountOutput>65535||data.indexCountOutput%3)continue;
        std::vector<XrVector2f> vertices(data.vertexCountOutput);std::vector<uint32_t> indices(data.indexCountOutput);data.vertexCapacityInput=(uint32_t)vertices.size();data.indexCapacityInput=(uint32_t)indices.size();data.vertices=vertices.data();data.indices=indices.data();
        if(XR_FAILED(getVisibility(session,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,eye,type,&data))||data.vertexCountOutput>vertices.size()||data.indexCountOutput>indices.size())continue;
        mesh[eye].visible=type==XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR;mesh[eye].vertices.clear();for(unsigned i=0;i<data.vertexCountOutput;++i)mesh[eye].vertices.push_back({vertices[i].x,vertices[i].y});indices.resize(data.indexCountOutput);mesh[eye].indices=std::move(indices);loaded=true;break;
    }Log("OpenXR visibility eye=%u available=%d vertices=%zu indices=%zu",eye,loaded,mesh[eye].vertices.size(),mesh[eye].indices.size());}
    foveation::SetVisibility(mesh);
}
static std::atomic<bool> batchTransfers{true};
static void (STDMETHODCALLTYPE *setTargets)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*);
static void (STDMETHODCALLTYPE *setTargetsUAV)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*);
static void (STDMETHODCALLTYPE *copyResource)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);
static void (STDMETHODCALLTYPE *copyRegion)(ID3D11DeviceContext*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);
static void MarkResource(ID3D11Resource* resource){
    for(uint32_t i=0;i<sourceCount.load(std::memory_order_relaxed);++i)if(resource==source[i])renderedMask.fetch_or(1u<<i,std::memory_order_relaxed);
}
static void STDMETHODCALLTYPE SetTargets(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* views,ID3D11DepthStencilView* depth){
    if(source[0]&&renderedMask!=((1u<<sourceCount.load())-1)&&views)for(UINT i=0;i<count;++i)if(views[i]){ID3D11Resource* resource{};views[i]->GetResource(&resource);MarkResource(resource);if(resource)resource->Release();}
    setTargets(ctx,count,views,depth);
}
static void STDMETHODCALLTYPE SetTargetsUAV(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* views,ID3D11DepthStencilView* depth,UINT start,UINT uavCount,ID3D11UnorderedAccessView* const* uavs,const UINT* initial){
    if(source[0]&&renderedMask!=((1u<<sourceCount.load())-1)&&views&&count<=D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)for(UINT i=0;i<count;++i)if(views[i]){ID3D11Resource* resource{};views[i]->GetResource(&resource);MarkResource(resource);if(resource)resource->Release();}
    setTargetsUAV(ctx,count,views,depth,start,uavCount,uavs,initial);
}
static void STDMETHODCALLTYPE CopyResource(ID3D11DeviceContext* ctx,ID3D11Resource* dst,ID3D11Resource* src){MarkResource(dst);copyResource(ctx,dst,src);}
static void STDMETHODCALLTYPE CopyRegion(ID3D11DeviceContext* ctx,ID3D11Resource* dst,UINT sub,UINT x,UINT y,UINT z,ID3D11Resource* src,UINT srcSub,const D3D11_BOX* box){MarkResource(dst);copyRegion(ctx,dst,sub,x,y,z,src,srcSub,box);}
static bool InstallWriteHooks(){
    void** table=*(void***)context.Get();bool ok=true;
    auto hook=[&](int slot,void* replacement,void** original){auto a=MH_CreateHook(table[slot],replacement,original);auto b=MH_EnableHook(table[slot]);Log("D3D11 eye-write hook slot=%d create=%d enable=%d",slot,a,b);ok&=a==0&&b==0;};
    hook(33,&SetTargets,(void**)&setTargets);hook(34,&SetTargetsUAV,(void**)&setTargetsUAV);hook(47,&CopyResource,(void**)&copyResource);hook(46,&CopyRegion,(void**)&copyRegion);return ok;
}
static bool Check(XrResult r,const char* name){if(XR_FAILED(r)){Log("OpenXR %s failed %d",name,r);return false;}return true;}
static void Poll(){
    if(!instance)return;
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while(xrPollEvent(instance,&event)==XR_SUCCESS){
        if(event.type==XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR)visibilityDirty=true;
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
            state=((XrEventDataSessionStateChanged*)&event)->state;Log("OpenXR session state=%d",state);
            if(state==XR_SESSION_STATE_READY){visibilityDirty=true;anchorGeneration.fetch_add(1);XrSessionBeginInfo info{XR_TYPE_SESSION_BEGIN_INFO};info.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;running=Check(xrBeginSession(session,&info),"xrBeginSession");}
            if(state==XR_SESSION_STATE_STOPPING){running=false;if(frameOpen)stopPending=true;else xrEndSession(session);}
            if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING){running=false;ready=false;}
        }
        event={XR_TYPE_EVENT_DATA_BUFFER};
    }
}
static bool Initialize(IDXGISwapChain* swap){
    if(FAILED(swap->GetDevice(IID_PPV_ARGS(&device)))){Log("D3D11 device not found; use -force-d3d11");return false;}
    device->GetImmediateContext(&context);
    ComPtr<ID3D11Multithread> multithread;
    if(SUCCEEDED(context.As(&multithread))){BOOL old=multithread->SetMultithreadProtected(TRUE);Log("D3D11 multithread protection enabled previous=%d creationFlags=%x",old,device->GetCreationFlags());}
    bufferuploads::Install(device.Get(),context.Get());
    bool hunterReady=hunter::Install(device.Get(),context.Get(),modulePath.parent_path()/L"shader-hunter");Log("Shader hunter hook coverage ready=%d (starts disabled)",hunterReady);
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};strcpy_s(info.applicationInfo.applicationName,"AvalonVR experimental");
    // All core calls used by this bridge are OpenXR 1.0. Request the minimum
    // API rather than the SDK header version, which can reject 1.0 runtimes.
    info.applicationInfo.applicationVersion=4;info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
    Log("OpenXR requested API=%u.%u.%u (SDK headers=%u.%u.%u)",
        XR_VERSION_MAJOR(info.applicationInfo.apiVersion),XR_VERSION_MINOR(info.applicationInfo.apiVersion),XR_VERSION_PATCH(info.applicationInfo.apiVersion),
        XR_VERSION_MAJOR(XR_CURRENT_API_VERSION),XR_VERSION_MINOR(XR_CURRENT_API_VERSION),XR_VERSION_PATCH(XR_CURRENT_API_VERSION));
    uint32_t extensionCount{};xrEnumerateInstanceExtensionProperties(nullptr,0,&extensionCount,nullptr);std::vector<XrExtensionProperties> available(extensionCount,{XR_TYPE_EXTENSION_PROPERTIES});xrEnumerateInstanceExtensionProperties(nullptr,extensionCount,&extensionCount,available.data());
    std::vector<const char*> extensions{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    for(auto& extension:available){if(strcmp(extension.extensionName,XR_FB_COMPOSITION_LAYER_IMAGE_LAYOUT_EXTENSION_NAME)==0){compositorFlip=true;extensions.push_back(XR_FB_COMPOSITION_LAYER_IMAGE_LAYOUT_EXTENSION_NAME);}if(strcmp(extension.extensionName,XR_KHR_VISIBILITY_MASK_EXTENSION_NAME)==0){visibilityExtension=true;extensions.push_back(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);}}
    info.enabledExtensionCount=(uint32_t)extensions.size();info.enabledExtensionNames=extensions.data();
    if(!Check(xrCreateInstance(&info,&instance),"xrCreateInstance"))return false;
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};xrGetInstanceProperties(instance,&props);Log("OpenXR runtime=%s version=%u.%u.%u",props.runtimeName,XR_VERSION_MAJOR(props.runtimeVersion),XR_VERSION_MINOR(props.runtimeVersion),XR_VERSION_PATCH(props.runtimeVersion));
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if(!Check(xrGetSystem(instance,&systemInfo,&system),"xrGetSystem"))return false;
    PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements{};xrGetInstanceProcAddr(instance,"xrGetD3D11GraphicsRequirementsKHR",(PFN_xrVoidFunction*)&getRequirements);
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if(!getRequirements||!Check(getRequirements(instance,system,&requirements),"xrGetD3D11GraphicsRequirementsKHR"))return false;
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC adapterDesc{};
    device.As(&dxgi);dxgi->GetAdapter(&adapter);adapter->GetDesc(&adapterDesc);
    if(memcmp(&adapterDesc.AdapterLuid,&requirements.adapterLuid,sizeof(LUID))!=0){Log("OpenXR adapter LUID differs from game adapter");return false;}
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};binding.device=device.Get();
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};sessionInfo.next=&binding;sessionInfo.systemId=system;
    if(!Check(xrCreateSession(instance,&sessionInfo,&session),"xrCreateSession"))return false;
    if(visibilityExtension)xrGetInstanceProcAddr(instance,"xrGetVisibilityMaskKHR",(PFN_xrVoidFunction*)&getVisibility);
    uint32_t count{};xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr);
    if(count!=2){Log("Expected two runtime views, got %u",count);return false;}
    XrViewConfigurationView view[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,view);
    auto ini=modulePath.parent_path()/L"AvalonVR.ini";
    foveation::Initialize(device.Get(),context.Get(),ini);
    batchTransfers=GetPrivateProfileIntW(L"Performance",L"BatchEyeTransfers",1,ini.c_str())!=0;Log("Batched eye transfers initial=%d (Ctrl+F11 compares legacy setup)",(int)batchTransfers.load());
    flipY=GetPrivateProfileIntW(L"Render",L"FlipY",0,ini.c_str())!=0;Log("Native eye vertical flip requested=%d compositorExtension=%d",(int)flipY.load(),compositorFlip);
    uiFlipY=GetPrivateProfileIntW(L"UI",L"FlipY",1,ini.c_str())!=0;Log("UI vertical flip requested=%d",(int)uiFlipY.load());
    anchorMenus=GetPrivateProfileIntW(L"UI",L"AnchorMenus",1,ini.c_str())!=0;Log("Menu world anchoring requested=%d",anchorMenus);
    float scale=std::clamp(GetPrivateProfileIntW(L"Render",L"ResolutionPercent",50,ini.c_str())/100.f,.25f,1.f);
    width=((uint32_t)(view[0].recommendedImageRectWidth*scale)+1)&~1u;
    height=((uint32_t)(view[0].recommendedImageRectHeight*scale)+1)&~1u;
    quadEnabled=GetPrivateProfileIntW(L"QuadViews",L"Enabled",0,ini.c_str())!=0;
    focusHorizontal=std::clamp((int)GetPrivateProfileIntW(L"QuadViews",L"FocusHorizontalPercent",50,ini.c_str()),10,95);
    focusVertical=std::clamp((int)GetPrivateProfileIntW(L"QuadViews",L"FocusVerticalPercent",50,ini.c_str()),10,95);
    int peripheral=std::clamp((int)GetPrivateProfileIntW(L"QuadViews",L"PeripheralResolutionPercent",40,ini.c_str()),20,100);peripheralResolution=peripheral;
    focusFeather=std::clamp((int)GetPrivateProfileIntW(L"QuadViews",L"FeatherPercent",12,ini.c_str()),1,40)*.01f;
    quadDebug=GetPrivateProfileIntW(L"QuadViews",L"DebugBorder",0,ini.c_str())!=0;
    cameraSizes[0]=cameraSizes[1]={width.load(),height.load()};
    cameraSizes[2]=cameraSizes[3]=quadview::Peripheral(cameraSizes[0],peripheral);
    cameraSizes[4]=cameraSizes[5]=quadview::Inset(cameraSizes[0],focusHorizontal,focusVertical);
    Log("Fixed quad views initial=%d outer=%ux%u focus=%ux%u section=%dx%d%% feather=%.2f; four native views composed locally to stereo; Ctrl+F12 compare, Ctrl+Shift+F12 border",(int)quadEnabled.load(),cameraSizes[2].width,cameraSizes[2].height,cameraSizes[4].width,cameraSizes[4].height,focusHorizontal,focusVertical,focusFeather);
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;spaceInfo.poseInReferenceSpace.orientation.w=1;
    if(!Check(xrCreateReferenceSpace(session,&spaceInfo,&space),"xrCreateReferenceSpace"))return false;
    spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
    if(!Check(xrCreateReferenceSpace(session,&spaceInfo,&menuSpace),"xrCreateReferenceSpace(view)"))return false;
    Log("OpenXR native pair target=%ux%u per eye recommended=%ux%u scale=%.2f",width.load(),height.load(),view[0].recommendedImageRectWidth,view[0].recommendedImageRectHeight,scale);
    ready=true;return true;
}
static bool CreateChains(){
    if(chains[0])return true;
    D3D11_TEXTURE2D_DESC desc{};source[0]->GetDesc(&desc);
    auto expected=cameraSizes[pending.quad?2:0];
    if(desc.Width!=expected.width||desc.Height!=expected.height||desc.SampleDesc.Count!=1){Log("Unexpected eye source desc %ux%u format=%u samples=%u",desc.Width,desc.Height,desc.Format,desc.SampleDesc.Count);return false;}
    int64_t desired{};
    switch(desc.Format){
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:desired=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:desired=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;break;
        default:Log("Unsupported eye source format=%u",desc.Format);return false;
    }
    uint32_t count{};xrEnumerateSwapchainFormats(session,0,&count,nullptr);std::vector<int64_t> formats(count);xrEnumerateSwapchainFormats(session,count,&count,formats.data());
    if(std::find(formats.begin(),formats.end(),desired)==formats.end()){Log("Runtime lacks matching sRGB format %lld",desired);return false;}
    sceneFormat=(DXGI_FORMAT)desired;
    if(!compositorFlip&&!flipBlit.Initialize(device.Get(),context.Get(),source[0],source[1],sceneFormat)){Log("Vertical flip GPU initialization failed");if(flipY)return false;}
    for(int eye=0;eye<2;++eye){
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format=desired;ci.sampleCount=1;ci.width=width;ci.height=height;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
        if(!Check(xrCreateSwapchain(session,&ci,&chains[eye]),"xrCreateSwapchain"))return false;
        xrEnumerateSwapchainImages(chains[eye],0,&count,nullptr);images[eye].resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        if(!Check(xrEnumerateSwapchainImages(chains[eye],count,&count,(XrSwapchainImageBaseHeader*)images[eye].data()),"xrEnumerateSwapchainImages"))return false;
    }
    Log("Native eye source format=%u -> OpenXR format=%lld (GPU copy)",desc.Format,desired);return true;
}
static void SaveBMP(ID3D11Texture2D* texture,int eye,uint64_t id,bool preserveAlpha=false){
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.BindFlags=0;d.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&d,nullptr,&staging)))return;
    context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return;
    std::vector<uint8_t> pixels(size_t(d.Width)*d.Height*4);
    bool rgba=d.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||d.Format==DXGI_FORMAT_R8G8B8A8_UNORM||d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    uint64_t transparent{},opaque{},partial{};
    for(uint32_t y=0;y<d.Height;++y)for(uint32_t x=0;x<d.Width;++x){auto in=(uint8_t*)mapped.pData+y*mapped.RowPitch+x*4;auto out=pixels.data()+(size_t(y)*d.Width+x)*4;out[0]=in[rgba?2:0];out[1]=in[1];out[2]=in[rgba?0:2];out[3]=preserveAlpha?in[3]:255;if(preserveAlpha){if(in[3]==0)++transparent;else if(in[3]==255)++opaque;else ++partial;}}
    context->Unmap(staging.Get(),0);
    BITMAPFILEHEADER fh{};fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(BITMAPINFOHEADER);fh.bfSize=fh.bfOffBits+(DWORD)pixels.size();
    BITMAPINFOHEADER bh{};bh.biSize=sizeof(bh);bh.biWidth=d.Width;bh.biHeight=-(LONG)d.Height;bh.biPlanes=1;bh.biBitCount=32;bh.biSizeImage=(DWORD)pixels.size();
    auto file=modulePath.parent_path()/(L"pair-"+std::to_wstring(id)+L"-eye"+std::to_wstring(eye)+L".bmp");
    std::ofstream f(file,std::ios::binary);f.write((char*)&fh,sizeof(fh));f.write((char*)&bh,sizeof(bh));f.write((char*)pixels.data(),pixels.size());
    Log("Captured bounded pair frame=%llu eye=%d",id,eye);
    if(preserveAlpha)Log("UI capture alpha transparent=%llu opaque=%llu partial=%llu",transparent,opaque,partial);
}
static IDXGISwapChain* currentSwap{};
static bool CopyUI(bool captureFrame){
    D3D11_TEXTURE2D_DESC desc{};uiSource->GetDesc(&desc);
    if(!uiChain){
        int64_t format{};
        if(desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if(desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if(!format||desc.SampleDesc.Count!=1){Log("UI source unsupported format=%u samples=%u",desc.Format,desc.SampleDesc.Count);return false;}
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;ci.format=format;ci.sampleCount=1;ci.width=desc.Width;ci.height=desc.Height;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
        if(!Check(xrCreateSwapchain(session,&ci,&uiChain),"xrCreateSwapchain(UI)"))return false;
        uint32_t count{};xrEnumerateSwapchainImages(uiChain,0,&count,nullptr);uiImages.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        if(!Check(xrEnumerateSwapchainImages(uiChain,count,&count,(XrSwapchainImageBaseHeader*)uiImages.data()),"xrEnumerateSwapchainImages(UI)"))return false;
        uiWidth=desc.Width;uiHeight=desc.Height;Log("UI alpha layer source=%ux%u format=%u",uiWidth,uiHeight,desc.Format);
        uiFormat=(DXGI_FORMAT)format;
    }
    if(!compositorFlip&&uiSource!=uiFlipBoundSource){
        bool rebound=uiFlipBoundSource?uiFlipBlit.BindSources(uiSource,uiSource,uiFormat):uiFlipBlit.Initialize(device.Get(),context.Get(),uiSource,uiSource,uiFormat);
        if(!rebound){Log("UI vertical GPU source binding failed");return false;}
        Log("UI vertical GPU source refreshed old=%p new=%p",uiFlipBoundSource,uiSource);uiFlipBoundSource=uiSource;
    }
    uint32_t index{};XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(!Check(xrAcquireSwapchainImage(uiChain,&acquire,&index),"xrAcquireSwapchainImage(UI)"))return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=100000000;
    if(!Check(xrWaitSwapchainImage(uiChain,&wait),"xrWaitSwapchainImage(UI)"))return false;
    bool copied=true;
    if(aimPublished.load(std::memory_order_acquire)==pending.id&&aimSize>0&&uiBlit.Initialize(device.Get(),context.Get()))copied=uiBlit.NativeUI(uiSource,uiImages[index].texture,uiFormat,aimPixelX,aimPixelY,aimBounds,uiFlipY,uiFlipY&&!compositorFlip,false);
    else if(uiFlipY&&!compositorFlip)copied=uiFlipBlit.Copy(0,uiImages[index].texture,uiFormat,uiWidth,uiHeight);
    else context->CopyResource(uiImages[index].texture,uiSource);
    if(copied&&uiBlit.Initialize(device.Get(),context.Get()))uiBlit.Cursor(currentSwap,uiImages[index].texture,uiFormat,compositorFlip&&uiFlipY);
    if(copied&&captureFrame){SaveBMP(uiImages[index].texture,2,pending.id,true);Log("UI capture effective vertical flip=%d",(int)uiFlipY.load());}
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};return Check(xrReleaseSwapchainImage(uiChain,&release),"xrReleaseSwapchainImage(UI)")&&copied;
}
static bool CopyAim(bool captureFrame){
    if(!aimFull||!uiBlit.Initialize(device.Get(),context.Get()))return false;
    if(!aimChain){
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;ci.format=uiFormat;ci.sampleCount=1;ci.width=ci.height=aimPixels;ci.faceCount=ci.arraySize=ci.mipCount=1;
        if(!Check(xrCreateSwapchain(session,&ci,&aimChain),"xrCreateSwapchain(native aim)"))return false;
        uint32_t count{};xrEnumerateSwapchainImages(aimChain,0,&count,nullptr);aimImages.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        if(!Check(xrEnumerateSwapchainImages(aimChain,count,&count,(XrSwapchainImageBaseHeader*)aimImages.data()),"xrEnumerateSwapchainImages(native aim)"))return false;
        Log("Native aim crosshair alpha layer prepared %ux%u",aimPixels,aimPixels);
    }
    uint32_t index{};XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};if(!Check(xrAcquireSwapchainImage(aimChain,&acquire,&index),"xrAcquireSwapchainImage(native aim)"))return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=100000000;if(!Check(xrWaitSwapchainImage(aimChain,&wait),"xrWaitSwapchainImage(native aim)"))return false;
    bool copied=uiBlit.NativeUI(aimFull,aimImages[index].texture,uiFormat,aimPixelX,aimPixelY,aimBounds,uiFlipY,uiFlipY,true);
    if(copied&&captureFrame)SaveBMP(aimImages[index].texture,3,pending.id,true);
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};return Check(xrReleaseSwapchainImage(aimChain,&release),"xrReleaseSwapchainImage(native aim)")&&copied;
}
static bool CopyMenu(IDXGISwapChain* swap){
    ComPtr<ID3D11Texture2D> back;if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&back))))return false;
    D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
    if(menuChain&&(desc.Width!=menuWidth||desc.Height!=menuHeight)){xrDestroySwapchain(menuChain);menuChain=XR_NULL_HANDLE;menuImages.clear();}
    if(!menuChain){
        int64_t format{};
        if(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if(desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if(!format){static bool logged{};if(!logged){Log("Unsupported menu backbuffer format=%u",desc.Format);logged=true;}return false;}
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;ci.format=format;ci.sampleCount=1;ci.width=desc.Width;ci.height=desc.Height;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
        if(!Check(xrCreateSwapchain(session,&ci,&menuChain),"xrCreateSwapchain(menu)"))return false;
        uint32_t count{};xrEnumerateSwapchainImages(menuChain,0,&count,nullptr);menuImages.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});xrEnumerateSwapchainImages(menuChain,count,&count,(XrSwapchainImageBaseHeader*)menuImages.data());menuWidth=desc.Width;menuHeight=desc.Height;
        menuFormat=(DXGI_FORMAT)format;Log("Menu layer native backbuffer=%ux%u format=%u",desc.Width,desc.Height,desc.Format);
    }
    uint32_t index{};XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};if(!Check(xrAcquireSwapchainImage(menuChain,&acquire,&index),"xrAcquireSwapchainImage(menu)"))return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=100000000;
    if(!Check(xrWaitSwapchainImage(menuChain,&wait),"xrWaitSwapchainImage(menu)"))return false;
    context->CopyResource(menuImages[index].texture,back.Get());
    if(uiBlit.Initialize(device.Get(),context.Get()))uiBlit.Cursor(swap,menuImages[index].texture,menuFormat);
    if(capture.exchange(false))SaveBMP(back.Get(),-1,pending.id);
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};return Check(xrReleaseSwapchainImage(menuChain,&release),"xrReleaseSwapchainImage(menu)");
}
static HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* swap,UINT sync,UINT flags){
    hunter::Bypass hunterBypass;
    if(flags&DXGI_PRESENT_TEST)return present(swap,sync,flags);
    currentSwap=swap;static bool attempted{};if(!attempted){attempted=true;Initialize(swap);}
    if(instance)Poll();
    Visibility();
    hunter::Poll(context.Get());
    gpuTimings.Poll(context.Get());
    if(!frameOpen&&capture&&published&&source[0]&&source[1]&&renderedMask==3&&device){
        capture=false;SaveBMP(source[0],0,published);SaveBMP(source[1],1,published);
    }
    if(frameOpen.load(std::memory_order_acquire)){
        bool captureUISnapshot=false;
        bool showMenu=menuVisible.load();
        // The completion marker follows the entire native camera list. Write
        // hooks are diagnostic only: optimized D3D forwarding can bypass them.
        bool pair=!showMenu&&pending.render&&published.load(std::memory_order_acquire)==pending.id&&completedRenderFrame.load(std::memory_order_acquire)==(uint32_t)pending.id&&source[0]&&source[1]&&
            (!pending.quad||(source[2]&&source[3]));
        XrCompositionLayerProjectionView views[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        bool copied=pair&&CreateChains();bool batchFallback=false;
        if(!showMenu&&pending.render&&!pair&&source[0]&&source[1]&&capture.exchange(false)){
            Log("Capturing unsubmitted native eye targets writeMask=%u",renderedMask.load());SaveBMP(source[0],0,pending.id);SaveBMP(source[1],1,pending.id);
        }
        if(copied){
            uint32_t index[2]{};bool acquired[2]{};bool waited[2]{};
            for(int eye=0;eye<2;++eye){
                XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};acquired[eye]=Check(xrAcquireSwapchainImage(chains[eye],&acquire,&index[eye]),"xrAcquireSwapchainImage");
                if(acquired[eye]){XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=100000000;waited[eye]=Check(xrWaitSwapchainImage(chains[eye],&wait),"xrWaitSwapchainImage");}
                copied&=waited[eye];
            }
            if(copied){
                gpuTimings.TransferBegin(context.Get(),(uint32_t)pending.id);
                bool batched=!pending.quad&&flipY&&!compositorFlip&&(pending.performanceMode&32);
                if(pending.quad){
                    copied=quadBlit.Initialize(device.Get(),context.Get())&&quadBlit.BindSources(source,sceneFormat)&&
                        quadBlit.CopyPair(images[0][index[0]].texture,images[1][index[1]].texture,sceneFormat,width,height,pending.focus,flipY,focusFeather,quadDebug);
                    if(!copied){quadEnabled=false;Log("Quad GPU composition failed; requesting fresh stereo rendering next frame");}
                }else if(flipY&&!compositorFlip){
                    // Mode switches select different native textures. Refresh both
                    // SRVs transactionally, including the first stereo frame.
                    static ID3D11Texture2D* bound[2]{};
                    if(bound[0]!=source[0]||bound[1]!=source[1]){copied=flipBlit.BindSources(source[0],source[1],sceneFormat);if(copied){bound[0]=source[0];bound[1]=source[1];}}
                }
                if(copied&&batched&&!flipBlit.CopyPair(images[0][index[0]].texture,images[1][index[1]].texture,sceneFormat,width,height)){
                    batchTransfers=false;batched=false;batchFallback=true;Log("Batched transfer unavailable; using legacy setup");
                }
                for(int eye=0;eye<2;++eye){
                    if(copied&&!pending.quad&&!batched){
                        if(flipY&&!compositorFlip)copied&=flipBlit.Copy(eye,images[eye][index[eye]].texture,sceneFormat,width,height);
                        else context->CopyResource(images[eye][index[eye]].texture,source[eye]);
                    }
                    views[eye].pose=pending.views[eye].pose;views[eye].fov=pending.views[eye].fov;views[eye].subImage.swapchain=chains[eye];views[eye].subImage.imageRect.extent={(int32_t)width.load(),(int32_t)height.load()};
                }
                if(copied&&capture.exchange(false)){SaveBMP(images[0][index[0]].texture,0,pending.id);SaveBMP(images[1][index[1]].texture,1,pending.id);captureUISnapshot=true;
                    if(pending.quad){
                        std::ofstream details(modulePath.parent_path()/(L"pair-"+std::to_wstring(pending.id)+L"-quad.txt"));
                        details<<"frame="<<pending.id<<" predictedTime="<<pending.time<<" mode="<<pending.performanceMode<<" mipMatch="<<((pending.performanceMode>>9)&1)<<" sharedExposure="<<((pending.performanceMode>>10)&1)<<" fogMatch="<<((pending.performanceMode>>11)&1)<<" writeMask="<<renderedMask.load()<<" output="<<width<<"x"<<height<<" sourceFlip="<<flipY<<" feather="<<focusFeather<<"\n";
                        for(int i=0;i<4;++i){SaveBMP(source[i],i+10,pending.id);D3D11_TEXTURE2D_DESC d{};source[i]->GetDesc(&d);auto& v=pending.views[i];
                            details<<"view="<<i<<" size="<<d.Width<<"x"<<d.Height<<" pose="<<v.pose.position.x<<","<<v.pose.position.y<<","<<v.pose.position.z<<","<<v.pose.orientation.x<<","<<v.pose.orientation.y<<","<<v.pose.orientation.z<<","<<v.pose.orientation.w<<" fov="<<v.fov.angleLeft<<","<<v.fov.angleRight<<","<<v.fov.angleUp<<","<<v.fov.angleDown<<"\n";}
                        for(auto& r:pending.focus)details<<"focusRect="<<r.left<<","<<r.top<<","<<r.right<<","<<r.bottom<<"\n";
                    }}
                if(copied)++pairs;
            }
            for(int eye=0;eye<2;++eye)if(waited[eye]){XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};copied&=Check(xrReleaseSwapchainImage(chains[eye],&release),"xrReleaseSwapchainImage");}
        }
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=space;layer.viewCount=2;layer.views=views;
        XrCompositionLayerImageLayoutFB imageLayout{XR_TYPE_COMPOSITION_LAYER_IMAGE_LAYOUT_FB};imageLayout.flags=XR_COMPOSITION_LAYER_IMAGE_LAYOUT_VERTICAL_FLIP_BIT_FB;
        // Quad composition already writes upright images; UI retains its own flip.
        if(!pending.quad&&compositorFlip&&flipY)layer.next=&imageLayout;
        bool menuCopied=showMenu&&pending.render&&CopyMenu(swap);
        bool uiCopied=copied&&uiPublished.load(std::memory_order_acquire)==pending.id&&uiSource&&CopyUI(captureUISnapshot);
        bool aimCopied=uiCopied&&aimPublished.load(std::memory_order_acquire)==pending.id&&aimSize>0&&CopyAim(captureUISnapshot);
        gpuTimings.Finish(context.Get(),(uint32_t)pending.id,copied&&!captureUISnapshot&&!batchFallback);
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};quad.space=menuSpace;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.pose.orientation.w=1;quad.pose.position.z=-1.5f;quad.size={1.8f,menuWidth?1.8f*menuHeight/menuWidth:1.f};quad.subImage.swapchain=menuChain;quad.subImage.imageRect.extent={(int32_t)menuWidth,(int32_t)menuHeight};
        if(!showMenu)mainMenuAnchorValid=false;
        if(menuCopied&&anchorMenus){
            auto generation=anchorGeneration.load();
            if(!mainMenuAnchorValid||generation!=mainAnchorSeen){
                auto a=pending.views[0].pose.position,b=pending.views[1].pose.position;auto q=pending.views[0].pose.orientation;Quat rot=Yaw({q.x,q.y,q.z,q.w});auto offset=Rotate(rot,{0,0,-1.5f});
                mainMenuPose.position={(a.x+b.x)*.5f+offset.x,(a.y+b.y)*.5f+offset.y,(a.z+b.z)*.5f+offset.z};mainMenuPose.orientation={rot.x,rot.y,rot.z,rot.w};mainMenuAnchorValid=true;mainAnchorSeen=generation;
                Log("Main menu anchored in local space at (%.3f %.3f %.3f)",mainMenuPose.position.x,mainMenuPose.position.y,mainMenuPose.position.z);
            }
            quad.space=space;quad.pose=mainMenuPose;
        }
        XrCompositionLayerQuad uiQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};uiQuad.space=menuSpace;uiQuad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;uiQuad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;uiQuad.pose.orientation.w=1;uiQuad.pose.position.z=-1.5f;uiQuad.size={1.8f,uiWidth?1.8f*uiHeight/uiWidth:1.f};uiQuad.subImage.swapchain=uiChain;uiQuad.subImage.imageRect.extent={(int32_t)uiWidth,(int32_t)uiHeight};
        bool fixedMenuUI=anchorMenus&&menuUIActive&&menuPlacementFrame.load(std::memory_order_acquire)==pending.id;
        if(fixedMenuUI){uiQuad.space=space;uiQuad.pose=menuUIPose;}
        if(compositorFlip&&uiFlipY)uiQuad.next=&imageLayout;
        XrCompositionLayerQuad aimQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};aimQuad.space=space;aimQuad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;aimQuad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;aimQuad.pose=aimPose;aimQuad.size={aimSize,aimSize};aimQuad.subImage.swapchain=aimChain;aimQuad.subImage.imageRect.extent={(int32_t)aimPixels,(int32_t)aimPixels};
        const XrCompositionLayerBaseHeader* layers[]={menuCopied?(const XrCompositionLayerBaseHeader*)&quad:(const XrCompositionLayerBaseHeader*)&layer,(const XrCompositionLayerBaseHeader*)&uiQuad,(const XrCompositionLayerBaseHeader*)&aimQuad};
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=pending.time;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=(copied||menuCopied)?(aimCopied?3:uiCopied?2:1):0;end.layers=end.layerCount?layers:nullptr;
        Check(xrEndFrame(session,&end),"xrEndFrame");
        if(stopPending){xrEndSession(session);stopPending=false;}
        uint64_t now=GetTickCount64();if(now-lastReport>5000){Log("Frame=%llu freshPairs=%llu renderedMask=%u shouldRender=%d submitted=%d menu=%d ui=%d state=%d aim=%d menuAnchored=%d",pending.id,pairs,renderedMask.load(),pending.render,copied,menuCopied,uiCopied,state,aimCopied,anchorMenus&&(menuCopied||fixedMenuUI));gpuTimings.Report();foveation::Report();bufferuploads::Report();lastReport=now;}
        frameOpen.store(false,std::memory_order_release);SetEvent(frameDone);
    }
    return present(swap,ready?0:sync,flags);
}
bool InstallPresentHook(){
    std::ifstream f(modulePath.parent_path()/L"AvalonVR.present");uintptr_t rva{};uint32_t timestamp{},size{};f>>std::hex>>rva>>timestamp>>size;
    HMODULE dxgi=GetModuleHandleW(L"dxgi.dll");if(!f||!dxgi)return false;
    auto dos=(IMAGE_DOS_HEADER*)dxgi;auto nt=(IMAGE_NT_HEADERS*)((char*)dxgi+dos->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=timestamp||nt->OptionalHeader.SizeOfImage!=size||rva>=size){Log("DXGI image differs from launcher probe");return false;}
    void* target=(char*)dxgi+rva;auto a=MH_CreateHook(target,&Present,(void**)&present);auto b=MH_EnableHook(target);
    Log("External D3D11 Present hook rva=%llx create=%d enable=%d",rva,a,b);return a==0&&b==0;
}
bool Begin(Frame& frame){
    if(!ready||!running)return false;
    if(WaitForSingleObject(frameDone,1000)!=WAIT_OBJECT_0)return false;
    // Apply only after Present consumed the previous plan and sources. Settings
    // requested by input must never resize that frame's compositor underneath it.
    if(tuningQueued){focusHorizontal=queuedSettings.horizontal;focusVertical=queuedSettings.vertical;peripheralResolution=queuedSettings.peripheral;tuningQueued=false;
        cameraSizes[2]=cameraSizes[3]=quadview::Peripheral(cameraSizes[0],peripheralResolution);
        cameraSizes[4]=cameraSizes[5]=quadview::Inset(cameraSizes[0],focusHorizontal,focusVertical);
        quadEnabled=true;tuneChangedAt=GetTickCount64();gpuTimings.ResetSamples();
        Log("Quad tuning applied focus=%dx%d%% strength=%d%% peripheral=%d%% outer=%ux%u focusTarget=%ux%u",focusHorizontal,focusVertical,100-peripheralResolution,peripheralResolution,cameraSizes[2].width,cameraSizes[2].height,cameraSizes[4].width,cameraSizes[4].height);
    }
    ResetEvent(frameDone);Frame next{};next.id=frame.id;next.performanceMode=frame.performanceMode;next.quad=quadEnabled;next.viewCount=next.quad?4:2;
    next.performanceMode=(next.performanceMode&255)|(next.quad?(256|(next.performanceMode&3584)):0);
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState fs{XR_TYPE_FRAME_STATE};
    if(!Check(xrWaitFrame(session,&wait,&fs),"xrWaitFrame")){SetEvent(frameDone);return false;}
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};if(!Check(xrBeginFrame(session,&begin),"xrBeginFrame")){SetEvent(frameDone);return false;}
    next.time=fs.predictedDisplayTime;next.render=fs.shouldRender;
    if(next.render){
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=next.time;locate.space=space;
        XrViewState vs{XR_TYPE_VIEW_STATE};uint32_t count{};
        bool ok=Check(xrLocateViews(session,&locate,&vs,2,&count,next.views),"xrLocateViews");
        next.render=ok&&count==2&&(vs.viewStateFlags&XR_VIEW_STATE_POSITION_VALID_BIT)&&(vs.viewStateFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT);
        if(next.render&&next.quad)for(int i=0;i<2;++i){auto f=next.views[i].fov;
            auto inset=quadview::MakeFocus({f.angleLeft,f.angleRight,f.angleUp,f.angleDown},focusHorizontal*.01f,focusVertical*.01f);
            if(!inset.valid){next.render=false;quadEnabled=false;Log("Quad focus rejected invalid runtime FOV eye=%d; stereo requested for next frame",i);break;}
            next.focus[i]=inset.rect;next.views[i+2]=next.views[i];next.views[i+2].fov={inset.fov.left,inset.fov.right,inset.fov.up,inset.fov.down};
        }
    }
    pending=next;frame=next;renderedMask=0;completedRenderFrame=0;published=0;frameOpen.store(true,std::memory_order_release);return true;
}
void PrepareSources(ID3D11Texture2D* const* textures,uint32_t count){sourceCount=std::clamp(count,2u,4u);for(uint32_t i=0;i<4;++i)source[i]=i<sourceCount?textures[i]:nullptr;renderedMask=0;}
void Publish(ID3D11Texture2D* l,ID3D11Texture2D* r,uint64_t frameId,uint32_t mask){renderedMask.fetch_or(mask);published.store(frameId,std::memory_order_release);}
void PublishUI(ID3D11Texture2D* texture,uint64_t frameId){uiSource=texture;uiPublished.store(frameId,std::memory_order_release);}
void ReanchorMenus(){anchorGeneration.fetch_add(1);}
void SetMenuUIPlacement(bool menu,uint64_t frameId,Vec3 position,Quat orientation){
    auto generation=anchorGeneration.load();
    if(menu&&(!menuUIActive||generation!=uiAnchorSeen)){menuUIPose.position={position.x,position.y,position.z};menuUIPose.orientation={orientation.x,orientation.y,orientation.z,orientation.w};uiAnchorSeen=generation;Log("Gameplay menu anchored in local space at (%.3f %.3f %.3f)",position.x,position.y,position.z);}
    if(menu!=menuUIActive)Log("Gameplay menu UI active=%d anchored=%d",menu,menu&&anchorMenus);menuUIActive=menu;menuPlacementFrame.store(frameId,std::memory_order_release);
}
void PublishAimUI(ID3D11Texture2D* full,uint64_t frameId,float pixelX,float pixelY,Vec3 target,Quat orientation,int logicalWidth,float minX,float minY,float maxX,float maxY){
    aimFull=full;aimPixelX=pixelX;aimPixelY=pixelY;aimBounds[0]=minX;aimBounds[1]=minY;aimBounds[2]=maxX;aimBounds[3]=maxY;aimPose.position={target.x,target.y,target.z};aimPose.orientation={orientation.x,orientation.y,orientation.z,orientation.w};
    auto a=pending.views[0].pose.position,b=pending.views[1].pose.position;float x=target.x-(a.x+b.x)*.5f,y=target.y-(a.y+b.y)*.5f,z=target.z-(a.z+b.z)*.5f;float distance=std::sqrt(x*x+y*y+z*z);
    aimSize=std::isfinite(distance)&&distance>.15f&&distance<1000&&logicalWidth>0?distance*(1.8f/1.5f)*aimPixels/logicalWidth:0;aimPublished.store(frameId,std::memory_order_release);
}
void RequestCapture(){capture=true;}
uint32_t Width(){return width;}uint32_t Height(){return height;}bool IsReady(){return ready;}
uint32_t RenderWidth(unsigned camera){return camera<6?cameraSizes[camera].width:0;}
uint32_t RenderHeight(unsigned camera){return camera<6?cameraSizes[camera].height:0;}
bool QuadViews(){return quadEnabled.load();}
bool ToggleQuadViews(){bool next=!QuadViews();quadEnabled=next;Log("Fixed quad views requested=%d (Ctrl+F12)",next);return next;}
bool ToggleQuadDebug(){bool next=!quadDebug.load();quadDebug=next;Log("Quad focus debug border=%d",next);return next;}
quadview::Settings QuadSettings(){return tuningQueued?queuedSettings:quadview::Settings{focusHorizontal,focusVertical,peripheralResolution};}
bool TuneQuad(quadview::Tune action){
    if(!ready)return false;auto old=QuadSettings(),next=quadview::Adjust(old,action);if(next==old)return false;
    queuedSettings=next;tuningQueued=true;return true;
}
void SaveQuadSettings(){
    if(!tuneChangedAt||GetTickCount64()-tuneChangedAt<750)return;
    auto ini=modulePath.parent_path()/L"AvalonVR.ini";bool ok=true;
    auto save=[&](const wchar_t* key,int value){ok&=WritePrivateProfileStringW(L"QuadViews",key,std::to_wstring(value).c_str(),ini.c_str())!=0;};
    save(L"FocusHorizontalPercent",focusHorizontal);save(L"FocusVerticalPercent",focusVertical);save(L"PeripheralResolutionPercent",peripheralResolution);
    Log("Quad tuning saved=%d focus=%dx%d%% peripheral=%d%%",ok,focusHorizontal,focusVertical,peripheralResolution);tuneChangedAt=0;
}
void QuadTargetsFailed(){quadEnabled=false;tuneChangedAt=0;Log("Quad target resize failed; ordinary stereo requested, tuning not saved");}
bool BatchTransfers(){return batchTransfers.load(std::memory_order_relaxed);}
bool ToggleBatchTransfers(){bool next=!BatchTransfers();batchTransfers.store(next,std::memory_order_relaxed);Log("Batched eye transfers requested=%d (Ctrl+F11)",next);return next;}
void SetMenuVisible(bool visible){menuVisible=visible;}
void MarkRenderStart(uint32_t frameId){if(frameId==(uint32_t)pending.id)gpuTimings.Begin(device.Get(),context.Get(),frameId,pending.performanceMode,state==XR_SESSION_STATE_FOCUSED);}
void MarkSceneEnd(uint32_t frameId){gpuTimings.SceneEnd(context.Get(),frameId);}
void MarkRendered(uint32_t frameId){gpuTimings.End(context.Get(),frameId,!capture.load());if(!pending.quad)renderedMask=3;completedRenderFrame.store(frameId,std::memory_order_release);static bool reported{};if(!reported){reported=true;Log("Unity render-queue completion marker reached frame=%u",frameId);}}
}
