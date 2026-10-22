#pragma once
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include "Log.h"
class FlipBlit {
    template<class T>using Com=Microsoft::WRL::ComPtr<T>;
    Com<ID3D11Device> device;Com<ID3D11DeviceContext1> context;
    Com<ID3DDeviceContextState> state;Com<ID3D11VertexShader> vertex;Com<ID3D11PixelShader> pixel;Com<ID3D11SamplerState> sampler;
    Com<ID3D11ShaderResourceView> source[2];
    struct Target {ID3D11Texture2D* texture{};DXGI_FORMAT format{};Com<ID3D11RenderTargetView> view;};
    std::vector<Target> targets;
    Com<ID3D11RenderTargetView> TargetView(ID3D11Texture2D* texture,DXGI_FORMAT format){
        for(auto& target:targets)if(target.texture==texture&&target.format==format)return target.view;
        Com<ID3D11RenderTargetView> view;D3D11_RENDER_TARGET_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        if(!texture||FAILED(device->CreateRenderTargetView(texture,&desc,&view)))return {};
        // The view holds the resource alive, so a recycled address cannot select
        // an old target. Bound the cache across swapchain/resource generations.
        if(targets.size()==16)targets.erase(targets.begin());
        targets.push_back({texture,format,view});return view;
    }
public:
    bool BindSources(ID3D11Texture2D* left,ID3D11Texture2D* right,DXGI_FORMAT format){
        if(!device||!left||!right)return false;
        Com<ID3D11ShaderResourceView> next[2];ID3D11Texture2D* textures[]={left,right};
        for(int i=0;i<2;++i){D3D11_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;desc.Texture2D.MipLevels=1;if(FAILED(device->CreateShaderResourceView(textures[i],&desc,&next[i])))return false;}
        source[0]=std::move(next[0]);source[1]=std::move(next[1]);return true;
    }
    bool Initialize(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* left,ID3D11Texture2D* right,DXGI_FORMAT format){
        targets.clear();
        device=d;Com<ID3D11Device1> d1;if(FAILED(device.As(&d1))||FAILED(c->QueryInterface(IID_PPV_ARGS(&context))))return false;
        D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0},selected{};
        if(FAILED(d1->CreateDeviceContextState(0,levels,2,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&selected,&state)))return false;
        const char* vs="struct V{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};V main(uint id:SV_VertexID){V o;float2 uv=float2((id<<1)&2,id&2);o.p=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.uv=float2(uv.x,1-uv.y);return o;}";
        const char* ps="Texture2D src:register(t0);SamplerState smp:register(s0);float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{return src.SampleLevel(smp,uv,0);}";
        Com<ID3DBlob> v,p,error;
        if(FAILED(D3DCompile(vs,strlen(vs),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&v,&error))||FAILED(D3DCompile(ps,strlen(ps),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&p,&error)))return false;
        if(FAILED(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vertex))||FAILED(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&pixel)))return false;
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        if(FAILED(device->CreateSamplerState(&sd,&sampler)))return false;
        if(!BindSources(left,right,format))return false;
        Log("Vertical GPU blit initialized with complete D3D11.1 context-state preservation");return true;
    }
    bool Copy(int eye,ID3D11Texture2D* target,DXGI_FORMAT format,uint32_t width,uint32_t height){
        if(!context||!state)return false;
        Com<ID3D11RenderTargetView> rtv;D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=format;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        if(FAILED(device->CreateRenderTargetView(target,&rd,&rtv)))return false;
        Com<ID3DDeviceContextState> saved;context->SwapDeviceContextState(state.Get(),&saved);
        ID3D11RenderTargetView* renderTarget=rtv.Get();ID3D11ShaderResourceView* view=source[eye].Get();ID3D11SamplerState* smp=sampler.Get();
        context->OMSetRenderTargets(1,&renderTarget,nullptr);D3D11_VIEWPORT viewport{0,0,(float)width,(float)height,0,1};context->RSSetViewports(1,&viewport);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->PSSetShaderResources(0,1,&view);context->PSSetSamplers(0,1,&smp);context->Draw(3,0);
        context->SwapDeviceContextState(saved.Get(),nullptr);return true;
    }
    bool CopyPair(ID3D11Texture2D* left,ID3D11Texture2D* right,DXGI_FORMAT format,uint32_t width,uint32_t height){
        if(!context||!state||!source[0]||!source[1])return false;
        auto leftView=TargetView(left,format),rightView=TargetView(right,format);
        if(!leftView||!rightView)return false;
        Com<ID3DDeviceContextState> saved;context->SwapDeviceContextState(state.Get(),&saved);
        D3D11_VIEWPORT viewport{0,0,(float)width,(float)height,0,1};context->RSSetViewports(1,&viewport);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);auto smp=sampler.Get();context->PSSetSamplers(0,1,&smp);
        ID3D11RenderTargetView* outputs[]={leftView.Get(),rightView.Get()};
        for(int eye=0;eye<2;++eye){auto input=source[eye].Get();context->OMSetRenderTargets(1,&outputs[eye],nullptr);context->PSSetShaderResources(0,1,&input);context->Draw(3,0);}
        context->SwapDeviceContextState(saved.Get(),nullptr);return true;
    }
};
