#include "QuadViewBlit.h"
#include <array>
#include <cstdio>
#include <cstdint>
using Microsoft::WRL::ComPtr;
struct Input {int w{},h{};std::vector<uint32_t> pixels;ComPtr<ID3D11Texture2D> texture;};
static float Decode(float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
static float Encode(float x){return x<=.0031308f?x*12.92f:1.055f*std::pow(x,1/2.4f)-.055f;}
static std::array<float,4> Sample(const Input& in,float u,float v,bool srgb){
    float x=u*in.w-.5f,y=v*in.h-.5f;int ix=(int)std::floor(x),iy=(int)std::floor(y);float dx=x-ix,dy=y-iy;
    std::array<float,4> color{};
    for(int j=0;j<2;++j)for(int i=0;i<2;++i){auto p=in.pixels[std::clamp(iy+j,0,in.h-1)*in.w+std::clamp(ix+i,0,in.w-1)];float weight=(i?dx:1-dx)*(j?dy:1-dy);
        for(int c=0;c<4;++c){float value=((p>>(c*8))&255)/255.f;if(srgb&&c<3)value=Decode(value);color[c]+=value*weight;}}
    return color;
}
int main(){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return 1;
    unsigned checks{};
    for(bool srgb:{false,true}){
        auto format=srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
        auto resourceFormat=srgb?DXGI_FORMAT_R8G8B8A8_TYPELESS:format;
        QuadViewBlit blit;if(!blit.Initialize(device.Get(),context.Get()))return 2;
        D3D11_TEXTURE2D_DESC d{};d.Width=80;d.Height=96;d.MipLevels=d.ArraySize=1;d.Format=resourceFormat;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> targets[2],staging;
        for(auto& t:targets)if(FAILED(device->CreateTexture2D(&d,nullptr,&t)))return 3;
        d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&staging)))return 4;
        ComPtr<ID3D11RenderTargetView> savedTarget;D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=format;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        if(FAILED(device->CreateRenderTargetView(targets[0].Get(),&rd,&savedTarget)))return 5;
        auto saved=savedTarget.Get();context->OMSetRenderTargets(1,&saved,nullptr);
        D3D11_VIEWPORT savedViewport{2,3,4,5,0,1};context->RSSetViewports(1,&savedViewport);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
        auto restored=[&](){ComPtr<ID3D11RenderTargetView> t;context->OMGetRenderTargets(1,&t,nullptr);D3D11_VIEWPORT vp{};UINT n=1;context->RSGetViewports(&n,&vp);D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
            return t.Get()==savedTarget.Get()&&n==1&&!memcmp(&vp,&savedViewport,sizeof(vp))&&topology==D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;};
        quadview::Rect rect[2]={{.22f,.18f,.72f,.68f},{.28f,.32f,.78f,.82f}};
        for(int generation=0;generation<18;++generation){
            Input inputs[4];ID3D11Texture2D* raw[4]{};
            for(int i=0;i<4;++i){auto& in=inputs[i];in.w=i<2?32+(generation%4)*4:20+(generation%3)*6;in.h=i<2?40+(generation%3)*4:24+(generation%4)*4;in.pixels.resize(in.w*in.h);
                // Smooth, distinct two-axis gradients avoid sRGB filtering
                // quantization at an artificial 219->0 modulo discontinuity.
                for(int y=0;y<in.h;++y)for(int x=0;x<in.w;++x)in.pixels[y*in.w+x]=0xff000000u|uint32_t(20+x*3+i*27+generation)|uint32_t(10+y*2+i*21+generation)<<8|uint32_t(25+x+y+i*13+generation)<<16;
                d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.Width=in.w;d.Height=in.h;
                D3D11_SUBRESOURCE_DATA data{in.pixels.data(),(UINT)in.w*4,0};if(FAILED(device->CreateTexture2D(&d,&data,&in.texture)))return 6;raw[i]=in.texture.Get();
            }
            if(!blit.BindSources(raw,format))return 7;
            // Failed rebinding must retain the complete previous generation.
            auto temp=raw[3];raw[3]=nullptr;if(blit.BindSources(raw,format)||!restored())return 8;raw[3]=temp;
            if(generation){for(auto& t:targets){t.Reset();d.Width=80;d.Height=96;d.BindFlags=D3D11_BIND_RENDER_TARGET;if(FAILED(device->CreateTexture2D(&d,nullptr,&t)))return 9;}}
            for(bool flip:{false,true})for(bool debug:{false,true}){
                if(!blit.CopyPair(targets[0].Get(),targets[1].Get(),format,80,96,rect,flip,.12f,debug)||!restored())return 10;
                unsigned bad{};
                for(int eye=0;eye<2;++eye){context->CopyResource(staging.Get(),targets[eye].Get());D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return 11;
                    for(int y=0;y<96;++y)for(int x=0;x<80;++x){float u=(x+.5f)/80,v=(y+.5f)/96;auto r=rect[eye];float fx=(u-r.left)/(r.right-r.left),fy=(v-r.top)/(r.bottom-r.top);
                        auto a=Sample(inputs[eye],u,flip?1-v:v,srgb);float w=quadview::Weight(r,u,v,.12f);
                        if(w>0){auto b=Sample(inputs[eye+2],fx,flip?1-fy:fy,srgb);for(int c=0;c<4;++c)a[c]=a[c]*(1-w)+b[c]*w;}
                        float edge=std::min({fx,1-fx,fy,1-fy});if(debug&&edge>0&&edge<.008f)a={1,0,1,1};
                        auto actual=(uint8_t*)map.pData+y*map.RowPitch+x*4;
                        for(int c=0;c<4;++c){float value=srgb&&c<3?Encode(a[c]):a[c];int expected=(int)std::lround(std::clamp(value,0.f,1.f)*255);if(abs((int)actual[c]-expected)>2){if(bad<6)printf("pixel eye=%d xy=%d,%d channel=%d actual=%u expected=%d weight=%.6f\n",eye,x,y,c,actual[c],expected,w);++bad;}}
                    }context->Unmap(staging.Get(),0);
                }
                if(bad){printf("Quad GPU mismatch format=%u generation=%d flip=%d debug=%d bad=%u\n",format,generation,flip,debug,bad);return 12;}++checks;
            }
            if(blit.CopyPair(targets[0].Get(),nullptr,format,80,96,rect,false,.12f)||!restored())return 13;
        }
    }
    printf("Quad GPU: %u full-image checks passed: four distinct views, crop/feather, both orientations, sRGB/UNORM, border, source and target replacement, refusal and native state restoration\n",checks);
}
