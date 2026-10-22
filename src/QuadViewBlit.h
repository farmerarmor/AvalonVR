#pragma once
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <cstring>
#include "QuadViewMath.h"

// Four real HDRP outputs -> two OpenXR swapchain images. No depth synthesis,
// readback, or inter-frame eye reuse. D3D11.1 restores the complete game state.
class QuadViewBlit {
    template<class T>using Com=Microsoft::WRL::ComPtr<T>;
    Com<ID3D11Device> device;Com<ID3D11DeviceContext1> context;
    Com<ID3DDeviceContextState> state;Com<ID3D11VertexShader> vertex;Com<ID3D11PixelShader> pixel;
    Com<ID3D11SamplerState> sampler;Com<ID3D11Buffer> constants;
    Com<ID3D11ShaderResourceView> sources[4];ID3D11Texture2D* bound[4]{};DXGI_FORMAT boundFormat{};
    struct Target {ID3D11Texture2D* texture{};DXGI_FORMAT format{};Com<ID3D11RenderTargetView> view;};
    std::vector<Target> targets;
    Com<ID3D11RenderTargetView> TargetView(ID3D11Texture2D* texture,DXGI_FORMAT format){
        for(auto& t:targets)if(t.texture==texture&&t.format==format)return t.view;
        D3D11_RENDER_TARGET_VIEW_DESC d{};d.Format=format;d.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        Com<ID3D11RenderTargetView> r;if(!texture||FAILED(device->CreateRenderTargetView(texture,&d,&r)))return {};
        if(targets.size()==16)targets.erase(targets.begin());targets.push_back({texture,format,r});return r;
    }
public:
    bool Initialize(ID3D11Device* d,ID3D11DeviceContext* c){
        if(state)return true;device=d;Com<ID3D11Device1> d1;
        if(FAILED(device.As(&d1))||FAILED(c->QueryInterface(IID_PPV_ARGS(&context))))return false;
        D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0},selected{};
        Com<ID3DDeviceContextState> nextState;
        if(FAILED(d1->CreateDeviceContextState(0,levels,2,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&selected,&nextState)))return false;
        const char* vs="struct V{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};V main(uint id:SV_VertexID){V o;float2 uv=float2((id<<1)&2,id&2);o.p=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.uv=uv;return o;}";
        const char* ps=R"(
Texture2D peripheral:register(t0);Texture2D focus:register(t1);SamplerState smp:register(s0);
cbuffer Settings:register(b0){float4 inset;float flip;float feather;float debug;float pad;};
float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{
 float2 outerUV=uv;outerUV.y=flip>0.5?1-outerUV.y:outerUV.y;
 float4 color=peripheral.SampleLevel(smp,outerUV,0);
 float2 f=(uv-inset.xy)/(inset.zw-inset.xy);
 float edge=min(min(f.x,1-f.x),min(f.y,1-f.y));
 if(edge>0){float a=smoothstep(0,feather,edge);float2 innerUV=float2(f.x,flip>0.5?1-f.y:f.y);
  color=lerp(color,focus.SampleLevel(smp,innerUV,0),a);
  if(debug>0.5&&edge<0.008)color=float4(1,0,1,1);
 }return color;
})";
        Com<ID3DBlob> v,p,error;
        if(FAILED(D3DCompile(vs,strlen(vs),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&v,&error))||
           FAILED(D3DCompile(ps,strlen(ps),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&p,&error)))return false;
        if(FAILED(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vertex))||
           FAILED(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&pixel)))return false;
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device->CreateSamplerState(&sd,&sampler))||FAILED(device->CreateBuffer(&bd,nullptr,&constants)))return false;
        state=std::move(nextState);return true;
    }
    bool BindSources(ID3D11Texture2D* const* textures,DXGI_FORMAT format){
        if(!device||!textures)return false;bool same=boundFormat==format;
        for(int i=0;i<4;++i)same&=textures[i]&&textures[i]==bound[i];if(same)return true;
        Com<ID3D11ShaderResourceView> next[4];
        for(int i=0;i<4;++i){if(!textures[i])return false;D3D11_SHADER_RESOURCE_VIEW_DESC d{};d.Format=format;d.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;
            if(FAILED(device->CreateShaderResourceView(textures[i],&d,&next[i])))return false;}
        for(int i=0;i<4;++i){sources[i]=std::move(next[i]);bound[i]=textures[i];}boundFormat=format;return true;
    }
    bool CopyPair(ID3D11Texture2D* left,ID3D11Texture2D* right,DXGI_FORMAT format,uint32_t width,uint32_t height,const quadview::Rect* rect,bool flip,float feather,bool debug=false){
        if(!state||!sources[3]||!rect)return false;
        auto l=TargetView(left,format),r=TargetView(right,format);if(!l||!r)return false;
        for(int i=0;i<2;++i)if(!(rect[i].right>rect[i].left&&rect[i].bottom>rect[i].top))return false;
        Com<ID3DDeviceContextState> saved;context->SwapDeviceContextState(state.Get(),&saved);
        D3D11_VIEWPORT vp{0,0,(float)width,(float)height,0,1};context->RSSetViewports(1,&vp);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
        auto smp=sampler.Get();auto cb=constants.Get();context->PSSetSamplers(0,1,&smp);context->PSSetConstantBuffers(0,1,&cb);
        ID3D11RenderTargetView* outputs[]={l.Get(),r.Get()};
        for(int eye=0;eye<2;++eye){float data[]={rect[eye].left,rect[eye].top,rect[eye].right,rect[eye].bottom,flip?1.f:0.f,std::clamp(feather,.001f,.49f),debug?1.f:0.f,0};
            context->UpdateSubresource(constants.Get(),0,nullptr,data,0,0);context->OMSetRenderTargets(1,&outputs[eye],nullptr);
            ID3D11ShaderResourceView* inputs[]={sources[eye].Get(),sources[eye+2].Get()};context->PSSetShaderResources(0,2,inputs);context->Draw(3,0);
        }
        context->SwapDeviceContextState(saved.Get(),nullptr);return true;
    }
};
