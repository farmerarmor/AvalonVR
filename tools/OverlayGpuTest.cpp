#include "UIBlit.h"
#include "FlipBlit.h"
#include <array>
#include <cstdio>
using Microsoft::WRL::ComPtr;
static int Run(DXGI_FORMAT viewFormat){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return 1;
    UIBlit blit;if(!blit.Initialize(device.Get(),context.Get())){puts("UI shader initialization failed");return 2;}
    std::array<uint32_t,256> full{},hud{};for(int y=3;y<9;++y)for(int x=4;x<12;++x){hud[y*16+x]=0x40000040;full[y*16+x]=0xA0008060;}
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=16;d.MipLevels=d.ArraySize=1;d.Format=viewFormat==DXGI_FORMAT_R8G8B8A8_UNORM?viewFormat:DXGI_FORMAT_R8G8B8A8_TYPELESS;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA a{full.data(),64,0},b{hud.data(),64,0};ComPtr<ID3D11Texture2D> all,background,out,read;
    if(FAILED(device->CreateTexture2D(&d,&a,&all))||FAILED(device->CreateTexture2D(&d,&b,&background))||FAILED(device->CreateTexture2D(&d,nullptr,&out)))return 3;
    d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;if(FAILED(device->CreateTexture2D(&d,nullptr,&read)))return 5;
    for(bool flip:{false,true}){
    if(!blit.ExtractAim(all.Get(),background.Get(),out.Get(),viewFormat,8,8,flip))return 4;
    context->CopyResource(read.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)))return 6;
    int count{},bad{};for(int y=0;y<16;++y)for(int x=0;x<16;++x){auto p=(uint8_t*)mapped.pData+y*mapped.RowPitch+x*4;if(p[3])++count;
        int sourceY=flip?15-y:y;
        if(sourceY>=3&&sourceY<9&&x>=4&&x<12){if(abs((int)p[3]-128)>2||(viewFormat==DXGI_FORMAT_R8G8B8A8_UNORM&&(abs((int)p[0]-64)>2||abs((int)p[1]-128)>2)))++bad;}else if(p[3])++bad;
    }context->Unmap(read.Get(),0);printf("UI GPU extraction flip=%d nonzero=%d expected=48 bad=%d\n",flip,count,bad);if(count!=48||bad)return 7;
    }
    // The single-capture path must keep native alpha, isolate only the reticle
    // rectangle, and leave unrelated HUD pixels intact outside that rectangle.
    full[0]=0xff00ff00;context->UpdateSubresource(all.Get(),0,nullptr,full.data(),64,0);float bounds[]={4,7,12,13};D3D11_MAPPED_SUBRESOURCE mapped{};
    for(bool aim:{false,true})for(bool flip:{false,true}){
        if(!blit.NativeUI(all.Get(),out.Get(),viewFormat,8,8,bounds,false,flip,aim))return 8;
        context->CopyResource(read.Get(),out.Get());if(FAILED(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)))return 9;
        int nonzero{},bad{};for(int y=0;y<16;++y)for(int x=0;x<16;++x){auto p=(uint8_t*)mapped.pData+y*mapped.RowPitch+x*4;int sy=flip?15-y:y;bool reticle=x>=4&&x<12&&sy>=3&&sy<9;int expected=aim?(reticle?160:0):(x==0&&sy==0?255:0);if(p[3])++nonzero;if(abs((int)p[3]-expected)>1)++bad;}
        context->Unmap(read.Get(),0);printf("Native UI GPU aim=%d flip=%d nonzero=%d bad=%d\n",aim,flip,nonzero,bad);if(bad||nonzero!=(aim?48:1))return 10;
    }
    // A reload replaces the Unity UI texture. Every flip must read the new
    // resource, including when no native-aim layer is active in a menu.
    FlipBlit flipBlit;if(!flipBlit.Initialize(device.Get(),context.Get(),all.Get(),all.Get(),viewFormat))return 11;
    for(int generation=0;generation<3;++generation){
        if(generation&&!flipBlit.BindSources(generation==1?background.Get():all.Get(),generation==1?background.Get():all.Get(),viewFormat))return 12;
        for(int eye=0;eye<2;++eye){if(!flipBlit.Copy(eye,out.Get(),viewFormat,16,16))return 13;
            context->CopyResource(read.Get(),out.Get());if(FAILED(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)))return 14;
            int bad{};for(int y=0;y<16;++y)for(int x=0;x<16;++x){auto p=(uint8_t*)mapped.pData+y*mapped.RowPitch+x*4;uint32_t expected=(generation==1?hud:full)[(15-y)*16+x];for(int c=0;c<4;++c)if(abs((int)p[c]-(int)((expected>>(8*c))&255))>1)++bad;}
            context->Unmap(read.Get(),0);printf("UI flip source generation=%d eye=%d bad=%d\n",generation,eye,bad);if(bad)return 15;
        }
    }
    // Compare the real two-eye transfer with distinct source/target resources,
    // repeated source replacements and more target generations than its cache.
    // Native bindings must survive both successful and refused pair transfers.
    ComPtr<ID3D11Texture2D> other;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&other)))return 16;
    ComPtr<ID3D11RenderTargetView> nativeTarget;D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=viewFormat;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    if(FAILED(device->CreateRenderTargetView(background.Get(),&rd,&nativeTarget)))return 17;
    auto rtv=nativeTarget.Get();context->OMSetRenderTargets(1,&rtv,nullptr);D3D11_VIEWPORT expectedViewport{2,3,4,5,0,1};context->RSSetViewports(1,&expectedViewport);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    auto restored=[&](){ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);D3D11_VIEWPORT vp{};UINT n=1;context->RSGetViewports(&n,&vp);D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);return target.Get()==nativeTarget.Get()&&n==1&&!memcmp(&vp,&expectedViewport,sizeof(vp))&&topology==D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;};
    for(int generation=0;generation<22;++generation){
        bool swap=generation&1;
        if(!flipBlit.BindSources(swap?background.Get():all.Get(),swap?all.Get():background.Get(),viewFormat))return 18;
        if(generation>1){other.Reset();if(FAILED(device->CreateTexture2D(&d,nullptr,&other)))return 19;}
        if(!flipBlit.CopyPair(out.Get(),other.Get(),viewFormat,16,16)||!restored())return 20;
        int bad{};for(int eye=0;eye<2;++eye){context->CopyResource(read.Get(),eye?other.Get():out.Get());if(FAILED(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)))return 21;
            const auto& expected=(bool(eye)^swap)?hud:full;
            for(int y=0;y<16;++y)for(int x=0;x<16;++x){auto p=(uint8_t*)mapped.pData+y*mapped.RowPitch+x*4;auto value=expected[(15-y)*16+x];for(int c=0;c<4;++c)if(abs((int)p[c]-(int)((value>>(8*c))&255))>1)++bad;}
            context->Unmap(read.Get(),0);
        }
        printf("Batched eye transfer generation=%d sourceSwap=%d bad=%d stateRestored=%d\n",generation,swap,bad,restored());if(bad)return 22;
    }
    if(flipBlit.CopyPair(out.Get(),nullptr,viewFormat,16,16)||!restored())return 23;
    return 0;
}
int main(){for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}){printf("UI format=%u\n",format);auto result=Run(format);if(result)return result;}return 0;}
