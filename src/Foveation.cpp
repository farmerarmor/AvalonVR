#include "Foveation.h"
#include "Log.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <nvapi.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace foveation {namespace {
ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> immediate;
std::atomic<bool> ready{},fault{};std::atomic<unsigned> requested{};
bool attempted{},bound{};float radius=.4f;unsigned strength=1,margin=32;
std::atomic<bool> nativePassMode{};bool nativePassActive{};std::atomic<uint64_t> nativeBegins{},nativeApplied{},nativeDeclined{},nativeCoarse{},nativeHidden{};
ComPtr<ID3D11NvShadingRateResourceView> activeView;
std::mutex visibilityMutex;vrsmask::Mesh visibility[2];uint64_t generation{};
thread_local bool scene{};thread_local Frame current{};
struct Map {Frame frame{};uint32_t width{},height{},viewWidth{},viewHeight{},kind{};uint64_t generation{};ComPtr<ID3D11NvShadingRateResourceView> view;unsigned full{},coarse{},hidden{};ComPtr<ID3D11Texture2D> texture;};
std::vector<Map> maps;
std::atomic<uint64_t> applied{},masked{},coarsened{},declined{},deferred{},unknown{},resets{};
const GUID shaderTag={0x1fd03068,0x98ea,0x4614,{0x91,0xee,0x92,0x2d,0xa4,0x64,0x7d,0x37}};
const GUID targetTag={0xfdb4a2b1,0x7446,0x4d02,{0x97,0xda,0xba,0x46,0xe7,0xc0,0xbe,0x0a}};
struct ShaderInfo{uint32_t known{},geometry{},colour{};};
struct TargetInfo{uint32_t width{},height{},samples{},valid{};};
bool Reset(){
    immediate->Flush();
    NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;
    auto a=NvAPI_D3D11_RSSetShadingRateResourceView(immediate.Get(),nullptr),b=NvAPI_D3D11_RSSetViewportsPixelShadingRates(immediate.Get(),&desc);++resets;
    bound=a!=NVAPI_OK||b!=NVAPI_OK;activeView.Reset();if(bound){fault=true;Log("VRS reset failed view=%d rates=%d; candidate disabled",a,b);}return !bound;
}
TargetInfo Target(ID3D11RenderTargetView* view){
    TargetInfo info{};UINT size=sizeof(info);if(SUCCEEDED(view->GetPrivateData(targetTag,&size,&info))&&size==sizeof(info))return info;
    D3D11_RENDER_TARGET_VIEW_DESC desc{};view->GetDesc(&desc);bool valid=false;
    if(desc.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D)valid=desc.Texture2D.MipSlice==0;
    else if(desc.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DARRAY)valid=desc.Texture2DArray.MipSlice==0&&desc.Texture2DArray.FirstArraySlice==0&&desc.Texture2DArray.ArraySize==1;
    switch(desc.Format){case DXGI_FORMAT_R32G32B32A32_FLOAT:case DXGI_FORMAT_R16G16B16A16_FLOAT:case DXGI_FORMAT_R16G16B16A16_UNORM:case DXGI_FORMAT_R11G11B10_FLOAT:case DXGI_FORMAT_R10G10B10A2_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:case DXGI_FORMAT_R32G32_FLOAT:case DXGI_FORMAT_R16G16_FLOAT:case DXGI_FORMAT_R16G16_UNORM:case DXGI_FORMAT_R8G8_UNORM:case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R16_FLOAT:case DXGI_FORMAT_R16_UNORM:case DXGI_FORMAT_R8_UNORM:break;default:valid=false;}
    ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;view->GetResource(&resource);if(SUCCEEDED(resource.As(&texture))){D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);info={td.Width,td.Height,td.SampleDesc.Count,uint32_t(valid&&td.ArraySize==1&&td.SampleDesc.Count==1)};}
    view->SetPrivateData(targetTag,sizeof(info),&info);return info;
}
bool SameOptics(const Frame& a,const Frame& b){return !memcmp(a.fovs,b.fovs,sizeof(a.fovs))&&a.inputWidth==b.inputWidth&&a.inputHeight==b.inputHeight&&a.outputWidth==b.outputWidth&&a.outputHeight==b.outputHeight;}
Map* GetMap(unsigned width,unsigned height,unsigned viewWidth,unsigned viewHeight,unsigned kind){
    std::lock_guard lock(visibilityMutex);
    for(auto& m:maps)if(m.width==width&&m.height==height&&m.viewWidth==viewWidth&&m.viewHeight==viewHeight&&m.kind==kind&&m.generation==generation&&SameOptics(m.frame,current))return &m;
    if(maps.size()>=12)maps.erase(maps.begin());
    vrsmask::Mask mask;if(!vrsmask::Build(width,height,viewWidth,viewHeight,current.fovs,visibility,radius,margin,mask))return nullptr;
    for(auto& p:mask.pixels){if(!(kind&1)&&p==1)p=0;if(!(kind&2)&&p==2)p=(kind&1)?1:0;}
    D3D11_TEXTURE2D_DESC td{};td.Width=mask.width;td.Height=mask.height;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8_UINT;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA data{mask.pixels.data(),mask.width,0};ComPtr<ID3D11Texture2D> texture;
    if(FAILED(device->CreateTexture2D(&td,&data,&texture)))return nullptr;
    NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC vd{};vd.version=NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;vd.Format=td.Format;vd.ViewDimension=NV_SRRV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11NvShadingRateResourceView> view;auto result=NvAPI_D3D11_CreateShadingRateResourceView(device.Get(),texture.Get(),&vd,&view);if(result!=NVAPI_OK){fault=true;Log("VRS map creation failed=%d",result);return nullptr;}
    maps.push_back({current,width,height,viewWidth,viewHeight,kind,generation,view,mask.full,mask.coarse,mask.hidden,texture});
    Log("VRS map target=%ux%u viewport=%ux%u kind=%u tiles=%ux%u full=%u coarse=%u commonHidden=%u margin=%u radius=%.2f",width,height,viewWidth,viewHeight,kind,mask.width,mask.height,mask.full,mask.coarse,mask.hidden,margin,radius);return &maps.back();
}
}
bool Initialize(ID3D11Device* d,ID3D11DeviceContext* c,const std::filesystem::path& ini){
    if(attempted)return ready;attempted=true;device=d;immediate=c;
    requested=(GetPrivateProfileIntW(L"Performance",L"FixedFoveation",0,ini.c_str())?1:0)|(GetPrivateProfileIntW(L"Performance",L"HiddenAreaMask",0,ini.c_str())?2:0);
    nativePassMode=GetPrivateProfileIntW(L"Performance",L"NativeVrsPasses",1,ini.c_str())!=0;
    radius=std::clamp(GetPrivateProfileIntW(L"Performance",L"FoveationRadiusPercent",40,ini.c_str())*.01f,.2f,.75f);strength=std::clamp(GetPrivateProfileIntW(L"Performance",L"FoveationStrength",1,ini.c_str()),1u,2u);margin=std::clamp(GetPrivateProfileIntW(L"Performance",L"HiddenAreaMarginPixels",32,ini.c_str()),16u,128u);
    auto result=NvAPI_Initialize();NV_D3D1x_GRAPHICS_CAPS caps{};auto status=result==NVAPI_OK?NvAPI_D3D1x_GetGraphicsCapabilities(d,NV_D3D1x_GRAPHICS_CAPS_VER,&caps):result;
    ready=status==NVAPI_OK&&caps.bVariablePixelRateShadingSupported;Log("VRS capability init=%d caps=%d supported=%d requested=%u; immediate raster only, separate FFR/HAM toggles",result,status,int(ready.load()),requested.load());return ready;
}
unsigned Requested(){return requested.load(std::memory_order_relaxed);}
unsigned Toggle(unsigned bit){auto result=requested.fetch_xor(bit,std::memory_order_relaxed)^bit;Log("VRS requested fixed=%u hidden=%u supported=%d fault=%d",result&1,(result>>1)&1,int(ready.load()),int(fault.load()));return result;}
void SetVisibility(const vrsmask::Mesh meshes[2]){std::lock_guard lock(visibilityMutex);for(int i=0;i<2;++i)visibility[i]=meshes[i];++generation;Log("VRS visibility generation=%llu mesh=%zu/%zu vertices type=%s/%s",generation,visibility[0].vertices.size(),visibility[1].vertices.size(),visibility[0].visible?"visible":"hidden",visibility[1].visible?"visible":"hidden");}
void BeginFrame(const Frame& frame){if(bound)Reset();nativePassActive=false;current=frame;scene=true;}
void EndFrame(){nativePassActive=false;scene=false;current.flags=0;if(bound)Reset();}
bool NativePassMode(){return nativePassMode.load(std::memory_order_relaxed);}
void Disable(){requested=0;fault=true;}
void BeginNativePass(){
    ++nativeBegins;nativePassActive=false;if(!ready||fault||!scene||!(current.flags&3))return;if(bound&&!Reset())return;
    D3D11_VIEWPORT vp[2]{};UINT count=2;immediate->RSGetViewports(&count,vp);if(count!=1||vp[0].TopLeftX!=0||vp[0].TopLeftY!=0){++nativeDeclined;return;}
    unsigned w=unsigned(vp[0].Width),h=unsigned(vp[0].Height);if((w!=current.inputWidth||h!=current.inputHeight)&&(w!=current.outputWidth||h!=current.outputHeight)){++nativeDeclined;return;}
    ComPtr<ID3D11RenderTargetView> targets[8];ID3D11RenderTargetView* raw[8]{};ComPtr<ID3D11DepthStencilView> depth;immediate->OMGetRenderTargets(8,raw,&depth);for(int i=0;i<8;++i)targets[i].Attach(raw[i]);unsigned width{},height{},targetCount{};
    for(auto& target:targets)if(target){auto info=Target(target.Get());if(!info.valid||info.width<w||info.height<h||(width&&(width!=info.width||height!=info.height))){++nativeDeclined;return;}width=info.width;height=info.height;++targetCount;}
    if(targetCount<3){++nativeDeclined;return;}unsigned kind=current.flags&3;
    // The entire native renderer list can change depth/stencil states. Allow
    // hidden-tile culling only when the VIEW itself forbids both kinds of writes.
    if(depth){D3D11_DEPTH_STENCIL_VIEW_DESC desc{};depth->GetDesc(&desc);bool depthReadOnly=(desc.Flags&D3D11_DSV_READ_ONLY_DEPTH)!=0;bool hasStencil=desc.Format==DXGI_FORMAT_D24_UNORM_S8_UINT||desc.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT;bool stencilReadOnly=!hasStencil||(desc.Flags&D3D11_DSV_READ_ONLY_STENCIL)!=0;if(!depthReadOnly||!stencilReadOnly)kind&=~2u;}
    if(!kind){++nativeDeclined;return;}auto map=GetMap(width,height,w,h,kind);if(!map||((kind==2)&&!map->hidden)){++nativeDeclined;return;}
    NV_D3D11_VIEWPORT_SHADING_RATE_DESC rate{};rate.enableVariablePixelShadingRate=true;for(auto& entry:rate.shadingRateTable)entry=NV_PIXEL_X1_PER_RASTER_PIXEL;rate.shadingRateTable[1]=strength==1?NV_PIXEL_X1_PER_2X1_RASTER_PIXELS:NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;rate.shadingRateTable[2]=NV_PIXEL_X0_CULL_RASTER_PIXELS;NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;desc.numViewports=1;desc.pViewports=&rate;
    immediate->Flush();bound=true;auto b=NvAPI_D3D11_RSSetViewportsPixelShadingRates(immediate.Get(),&desc),a=NvAPI_D3D11_RSSetShadingRateResourceView(immediate.Get(),map->view.Get());if(a!=NVAPI_OK||b!=NVAPI_OK){Reset();fault=true;Log("Native pass VRS apply failed view=%d rate=%d",a,b);return;}
    activeView=map->view;nativePassActive=true;++nativeApplied;if(kind&1)++nativeCoarse;if((kind&2)&&map->hidden)++nativeHidden;if(nativeApplied<=2)Log("Native VRS GPU pass applied kind=%u MRT=%u viewport=%ux%u target=%ux%u",kind,targetCount,w,h,width,height);
}
void EndNativePass(){nativePassActive=false;if(bound)Reset();}
void RegisterShader(ID3D11DeviceChild* shader,const void* code,size_t length,unsigned stage){
    if(stage!=0||!shader||!code||!length)return;ShaderInfo info{};info.known=1;ComPtr<ID3D11ShaderReflection> reflection;
    if(SUCCEEDED(D3DReflect(code,length,IID_PPV_ARGS(&reflection)))){D3D11_SHADER_DESC desc{};reflection->GetDesc(&desc);bool safe=true;for(unsigned i=0;i<desc.OutputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC out{};reflection->GetOutputParameterDesc(i,&out);if(out.SystemValueType!=D3D_NAME_TARGET)safe=false;}
        for(unsigned i=0;i<desc.InputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC in{};reflection->GetInputParameterDesc(i,&in);if(in.SystemValueType==D3D_NAME_SAMPLE_INDEX||in.SystemValueType==D3D_NAME_COVERAGE)safe=false;}
        for(unsigned i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC binding{};reflection->GetResourceBindingDesc(i,&binding);if(binding.Type==D3D_SIT_UAV_RWTYPED||binding.Type==D3D_SIT_UAV_RWSTRUCTURED||binding.Type==D3D_SIT_UAV_RWBYTEADDRESS||binding.Type==D3D_SIT_UAV_APPEND_STRUCTURED||binding.Type==D3D_SIT_UAV_CONSUME_STRUCTURED||binding.Type==D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER)safe=false;}
        info.colour=safe&&desc.OutputParameters>0;
        // GBuffer is a narrower raster-only first target; it excludes sky,
        // shadow, fullscreen depth and most post-processing shaders.
        info.geometry=info.colour&&desc.OutputParameters>=3;
    }shader->SetPrivateData(shaderTag,sizeof(info),&info);
}
Command::Command(ID3D11DeviceContext* context,bool inScene,bool geometry,bool outer){
    if(NativePassMode()||nativePassActive)return;
    if(!outer||!ready||!context)return;
    if(!bound&&(!inScene||!scene||!(current.flags&3)))return;
    if(context!=immediate.Get()||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE){++deferred;return;}
    auto choose=[&]()->Map*{
    if(fault||!inScene||!scene||!(current.flags&3))return nullptr;
    ComPtr<ID3D11PixelShader> pixel;context->PSGetShader(&pixel,nullptr,nullptr);ShaderInfo shader{};UINT bytes=sizeof(shader);if(!pixel||FAILED(pixel->GetPrivateData(shaderTag,&bytes,&shader))||bytes!=sizeof(shader)||!shader.colour){++unknown;return nullptr;}
    D3D11_VIEWPORT viewport[2]{};UINT count=2;context->RSGetViewports(&count,viewport);if(count!=1||viewport[0].TopLeftX!=0||viewport[0].TopLeftY!=0){++declined;return nullptr;}
    auto w=unsigned(viewport[0].Width),h=unsigned(viewport[0].Height);bool input=w==current.inputWidth&&h==current.inputHeight,output=w==current.outputWidth&&h==current.outputHeight;if((!input&&!output)||viewport[0].Width!=float(w)||viewport[0].Height!=float(h)){++declined;return nullptr;}
    ComPtr<ID3D11RenderTargetView> targets[8];ID3D11RenderTargetView* raw[8]{};ComPtr<ID3D11DepthStencilView> depth;context->OMGetRenderTargets(8,raw,&depth);for(int i=0;i<8;++i)targets[i].Attach(raw[i]);unsigned width{},height{},targetCount{};
    for(auto& view:targets)if(view){auto info=Target(view.Get());if(!info.valid||info.width<w||info.height<h||(width&&(width!=info.width||height!=info.height))){++declined;return nullptr;}width=info.width;height=info.height;++targetCount;}
    if(!targetCount){++declined;return nullptr;}
    unsigned kind=current.flags&3;
    // Coarse shading starts on multi-target geometry only. Color-only passes
    // may use the conservative hidden mask but retain full shading elsewhere.
    if(!geometry||!shader.geometry||targetCount<3)kind&=~1u;
    if(depth){D3D11_DEPTH_STENCIL_VIEW_DESC dv{};depth->GetDesc(&dv);ComPtr<ID3D11DepthStencilState> state;UINT ref{};context->OMGetDepthStencilState(&state,&ref);D3D11_DEPTH_STENCIL_DESC ds{};if(state)state->GetDesc(&ds);else{ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;}
        auto changes=[](const D3D11_DEPTH_STENCILOP_DESC& op){return op.StencilFailOp!=D3D11_STENCIL_OP_KEEP||op.StencilDepthFailOp!=D3D11_STENCIL_OP_KEEP||op.StencilPassOp!=D3D11_STENCIL_OP_KEEP;};
        bool writes=(!(dv.Flags&D3D11_DSV_READ_ONLY_DEPTH)&&ds.DepthEnable&&ds.DepthWriteMask!=D3D11_DEPTH_WRITE_MASK_ZERO)||(!(dv.Flags&D3D11_DSV_READ_ONLY_STENCIL)&&ds.StencilEnable&&ds.StencilWriteMask&&(changes(ds.FrontFace)||changes(ds.BackFace)));
        // Never cull native depth/stencil coverage used by temporal or lighting
        // passes. Coarse shading retains the fixed-function sample coverage.
        if(writes)kind&=~2u;
    }
    if(!kind){++declined;return nullptr;}auto map=GetMap(width,height,w,h,kind);if(!map||((kind==2)&&!map->hidden)){++declined;return nullptr;}return map;
    };
    auto map=choose();if(!map){if(bound)Reset();return;}unsigned kind=map->kind;
    if(bound&&activeView.Get()==map->view.Get()){applied=true;++foveation::applied;if(kind&1)++coarsened;if((kind&2)&&map->hidden)++masked;return;}
    NV_D3D11_VIEWPORT_SHADING_RATE_DESC rate{};rate.enableVariablePixelShadingRate=true;for(auto& entry:rate.shadingRateTable)entry=NV_PIXEL_X1_PER_RASTER_PIXEL;rate.shadingRateTable[1]=strength==1?NV_PIXEL_X1_PER_2X1_RASTER_PIXELS:NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;rate.shadingRateTable[2]=NV_PIXEL_X0_CULL_RASTER_PIXELS;
    NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;desc.numViewports=1;desc.pViewports=&rate;
    // The installed D3D11 runtime queues Draw before NVAPI's sideband state.
    // GPU validation proves unflushed scoped calls otherwise have no effect.
    // Submit previous native commands before the driver state change; Flush
    // submits without waiting for GPU completion. Keep this optional and measure
    // its CPU cost as well as the reduced raster work.
    if(bound){if(!Reset())return;}else context->Flush();
    bound=true;auto b=NvAPI_D3D11_RSSetViewportsPixelShadingRates(context,&desc),a=NvAPI_D3D11_RSSetShadingRateResourceView(context,map->view.Get());if(a!=NVAPI_OK||b!=NVAPI_OK){Reset();fault=true;Log("VRS apply failed view=%d rate=%d",a,b);return;}
    activeView=map->view;applied=true;++foveation::applied;if(kind&1)++coarsened;if((kind&2)&&map->hidden)++masked;
}
void Report(){Log("VRS supported=%d fault=%d requested=%u applied=%llu coarse=%llu masked=%llu declined=%llu deferred=%llu unknown=%llu resets=%llu nativeMode=%d nativeBegins=%llu nativeApplied=%llu nativeDeclined=%llu nativeCoarse=%llu nativeHidden=%llu",int(ready.load()),int(fault.load()),requested.load(),applied.load(),coarsened.load(),masked.load(),declined.load(),deferred.load(),unknown.load(),resets.load(),NativePassMode(),nativeBegins.load(),nativeApplied.load(),nativeDeclined.load(),nativeCoarse.load(),nativeHidden.load());}
}
