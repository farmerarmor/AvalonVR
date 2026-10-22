#pragma once
#include "Log.h"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <algorithm>

class UIBlit {
    template<class T>using Com=Microsoft::WRL::ComPtr<T>;
    Com<ID3D11Device> device;Com<ID3D11DeviceContext1> context;Com<ID3DDeviceContextState> state;
    Com<ID3D11VertexShader> vertex;Com<ID3D11PixelShader> difference,cursorPixel,copyPixel;Com<ID3D11Buffer> constants;
    Com<ID3D11SamplerState> sampler;Com<ID3D11BlendState> blend;
    Com<ID3D11ShaderResourceView> fullView,hudView,cursorView;Com<ID3D11Texture2D> cursorTexture;
    ID3D11Texture2D* cachedFull{};ID3D11Texture2D* cachedHUD{};HCURSOR cachedCursor{};uint64_t cursorRefresh{};
    int cursorWidth{},cursorHeight{},hotX{},hotY{};
    struct Params {float rect[4];float uv[4];float mask[4]{};float mode[4]{};};
    bool View(ID3D11Texture2D* texture,DXGI_FORMAT format,Com<ID3D11ShaderResourceView>& view){D3D11_SHADER_RESOURCE_VIEW_DESC d{};d.Format=format;d.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;return SUCCEEDED(device->CreateShaderResourceView(texture,&d,&view));}
    bool Draw(ID3D11Texture2D* target,DXGI_FORMAT format,ID3D11PixelShader* pixel,ID3D11ShaderResourceView** views,int viewCount,const Params& p,bool alpha){
        if(!context)return false;Com<ID3D11RenderTargetView> rtv;D3D11_RENDER_TARGET_VIEW_DESC d{};d.Format=format;d.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        if(FAILED(device->CreateRenderTargetView(target,&d,&rtv)))return false;
        D3D11_TEXTURE2D_DESC td{};target->GetDesc(&td);Com<ID3DDeviceContextState> saved;context->SwapDeviceContextState(state.Get(),&saved);
        context->UpdateSubresource(constants.Get(),0,nullptr,&p,0,0);ID3D11Buffer* cb=constants.Get();context->VSSetConstantBuffers(0,1,&cb);context->PSSetConstantBuffers(0,1,&cb);
        ID3D11RenderTargetView* output=rtv.Get();context->OMSetRenderTargets(1,&output,nullptr);float factor[4]{};context->OMSetBlendState(alpha?blend.Get():nullptr,factor,~0u);
        D3D11_VIEWPORT vp{0,0,(float)td.Width,(float)td.Height,0,1};context->RSSetViewports(1,&vp);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel,nullptr,0);context->PSSetShaderResources(0,viewCount,views);auto smp=sampler.Get();context->PSSetSamplers(0,1,&smp);context->Draw(6,0);
        context->SwapDeviceContextState(saved.Get(),nullptr);return true;
    }
    bool CursorImage(HCURSOR cursor){
        auto now=GetTickCount64();if(cursor==cachedCursor&&cursorTexture&&now<cursorRefresh)return true;cursorRefresh=now+150;
        ICONINFO icon{};if(!GetIconInfo(cursor,&icon))return false;
        BITMAP bitmap{};GetObjectW(icon.hbmColor?icon.hbmColor:icon.hbmMask,sizeof(bitmap),&bitmap);
        int w=bitmap.bmWidth,h=icon.hbmColor?bitmap.bmHeight:bitmap.bmHeight/2;hotX=(int)icon.xHotspot;hotY=(int)icon.yHotspot;
        if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);
        if(w<1||h<1||w>256||h>256)return false;
        HDC dc=CreateCompatibleDC(nullptr);if(!dc)return false;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* bits{};HBITMAP dib=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!dib){DeleteDC(dc);return false;}auto old=SelectObject(dc,dib);
        const size_t pixels=size_t(w)*h;std::vector<uint32_t> black(pixels),rgba(pixels);
        memset(bits,0,pixels*4);bool ok=DrawIconEx(dc,0,0,cursor,w,h,0,nullptr,DI_NORMAL)!=0;GdiFlush();memcpy(black.data(),bits,pixels*4);
        memset(bits,255,pixels*4);ok&=DrawIconEx(dc,0,0,cursor,w,h,0,nullptr,DI_NORMAL)!=0;GdiFlush();auto white=(uint32_t*)bits;
        for(size_t i=0;i<pixels;++i){auto b=(uint8_t*)&black[i],v=(uint8_t*)&white[i];int trans=std::clamp(((int)v[0]-b[0]+(int)v[1]-b[1]+(int)v[2]-b[2])/3,0,255);int a=255-trans;
            // GDI's black/white pair handles both alpha and legacy mask cursors.
            int r=a?std::clamp((int)b[2]*255/a,0,255):0,g=a?std::clamp((int)b[1]*255/a,0,255):0,blue=a?std::clamp((int)b[0]*255/a,0,255):0;
            rgba[i]=(uint32_t)(r|(g<<8)|(blue<<16)|(a<<24));
        }
        SelectObject(dc,old);DeleteObject(dib);DeleteDC(dc);if(!ok)return false;
        if(!cursorTexture||w!=cursorWidth||h!=cursorHeight){cursorTexture.Reset();cursorView.Reset();D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;if(FAILED(device->CreateTexture2D(&td,nullptr,&cursorTexture))||!View(cursorTexture.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,cursorView))return false;cursorWidth=w;cursorHeight=h;}
        context->UpdateSubresource(cursorTexture.Get(),0,nullptr,rgba.data(),w*4,0);cachedCursor=cursor;return true;
    }
public:
    bool Initialize(ID3D11Device* d,ID3D11DeviceContext* c){
        if(blend)return true;device=d;Com<ID3D11Device1> d1;if(FAILED(device.As(&d1))||FAILED(c->QueryInterface(IID_PPV_ARGS(&context))))return false;
        D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0},selected{};
        if(FAILED(d1->CreateDeviceContextState(0,levels,2,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&selected,&state)))return false;
        const char* vs="cbuffer C:register(b0){float4 rect;float4 uvRect;}struct V{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};V main(uint id:SV_VertexID){float2 points[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};float2 q=points[id];V o;o.p=float4((rect.xy+q*rect.zw)*float2(2,-2)+float2(-1,1),0,1);o.uv=uvRect.xy+q*uvRect.zw;return o;}";
        const char* diff="Texture2D full:register(t0);Texture2D hud:register(t1);SamplerState s:register(s0);float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{float4 a=full.SampleLevel(s,uv,0),b=hud.SampleLevel(s,uv,0);float alpha=saturate((a.a-b.a)/max(1-b.a,0.0001));return float4(max(a.rgb-b.rgb*(1-alpha),0),alpha);}";
        const char* cursor="Texture2D icon:register(t0);SamplerState s:register(s0);float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{float4 c=icon.SampleLevel(s,uv,0);return float4(c.rgb*c.a,c.a);}";
        const char* copy="cbuffer C:register(b0){float4 rect;float4 uvRect;float4 mask;float4 mode;}Texture2D src:register(t0);SamplerState s:register(s0);float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{bool inside=all(uv>=mask.xy)&&all(uv<=mask.zw);if((mode.x==1&&inside)||(mode.x==2&&!inside))return 0;return src.SampleLevel(s,uv,0);}";
        Com<ID3DBlob> v,p,e;if(FAILED(D3DCompile(vs,strlen(vs),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&v,&e))||FAILED(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vertex)))return false;
        for(int i=0;i<3;++i){const char* code=i==2?copy:i?cursor:diff;if(FAILED(D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&p,&e)))return false;auto& output=i==2?copyPixel:i?cursorPixel:difference;if(FAILED(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,output.ReleaseAndGetAddressOf())))return false;}
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(Params);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(FAILED(device->CreateBuffer(&bd,nullptr,&constants)))return false;
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;if(FAILED(device->CreateSamplerState(&sd,&sampler)))return false;
        D3D11_BLEND_DESC b{};b.RenderTarget[0].BlendEnable=TRUE;b.RenderTarget[0].SrcBlend=b.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;b.RenderTarget[0].DestBlend=b.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;b.RenderTarget[0].BlendOp=b.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        return SUCCEEDED(device->CreateBlendState(&b,&blend));
    }
    bool ExtractAim(ID3D11Texture2D* full,ID3D11Texture2D* hud,ID3D11Texture2D* target,DXGI_FORMAT format,float centerX,float centerY,bool flip){
        if(full!=cachedFull||hud!=cachedHUD){fullView.Reset();hudView.Reset();if(!View(full,format,fullView)||!View(hud,format,hudView))return false;cachedFull=full;cachedHUD=hud;}
        D3D11_TEXTURE2D_DESC src{},dst{};full->GetDesc(&src);target->GetDesc(&dst);float cy=flip?centerY:src.Height-centerY;
        Params p{{0,0,1,1},{(centerX-dst.Width*.5f)/src.Width,(cy+(flip?1.f:-1.f)*dst.Height*.5f)/src.Height,(float)dst.Width/src.Width,(flip?-1.f:1.f)*dst.Height/src.Height}};
        ID3D11ShaderResourceView* views[]={fullView.Get(),hudView.Get()};return Draw(target,format,difference.Get(),views,2,p,false);
    }
    bool NativeUI(ID3D11Texture2D* source,ID3D11Texture2D* target,DXGI_FORMAT format,float centerX,float centerY,const float bounds[4],bool nativeFlip,bool outputFlip,bool aim){
        if(source!=cachedFull){fullView.Reset();if(!View(source,format,fullView))return false;cachedFull=source;}
        D3D11_TEXTURE2D_DESC src{},dst{};source->GetDesc(&src);target->GetDesc(&dst);Params p{{0,0,1,1},{0,outputFlip?1.f:0.f,1,outputFlip?-1.f:1.f}};
        p.mask[0]=bounds[0]/src.Width;p.mask[2]=bounds[2]/src.Width;p.mask[1]=(nativeFlip?bounds[1]:src.Height-bounds[3])/src.Height;p.mask[3]=(nativeFlip?bounds[3]:src.Height-bounds[1])/src.Height;p.mode[0]=aim?2.f:1.f;
        if(aim){float cy=nativeFlip?centerY:src.Height-centerY;p.uv[0]=(centerX-dst.Width*.5f)/src.Width;p.uv[1]=(cy+(outputFlip?1.f:-1.f)*dst.Height*.5f)/src.Height;p.uv[2]=(float)dst.Width/src.Width;p.uv[3]=(outputFlip?-1.f:1.f)*dst.Height/src.Height;}
        auto view=fullView.Get();return Draw(target,format,copyPixel.Get(),&view,1,p,false);
    }
    void Cursor(IDXGISwapChain* swap,ID3D11Texture2D* target,DXGI_FORMAT format,bool flip=false){
        DXGI_SWAP_CHAIN_DESC desc{};if(FAILED(swap->GetDesc(&desc))||!desc.OutputWindow)return;
        DWORD process{};GetWindowThreadProcessId(GetForegroundWindow(),&process);if(process!=GetCurrentProcessId())return;
        CURSORINFO cursor{sizeof(cursor)};if(!GetCursorInfo(&cursor)||!(cursor.flags&CURSOR_SHOWING)||!cursor.hCursor)return;
        POINT pt=cursor.ptScreenPos;RECT client{};if(!ScreenToClient(desc.OutputWindow,&pt)||!GetClientRect(desc.OutputWindow,&client)||client.right<=0||client.bottom<=0||pt.x<0||pt.y<0||pt.x>=client.right||pt.y>=client.bottom)return;
        if(!CursorImage(cursor.hCursor))return;Params p{{(float)(pt.x-hotX)/client.right,(float)(pt.y-hotY)/client.bottom,(float)cursorWidth/client.right,(float)cursorHeight/client.bottom},{0,0,1,1}};
        if(flip){p.rect[1]=1-p.rect[1]-p.rect[3];p.uv[1]=1;p.uv[3]=-1;}
        auto view=cursorView.Get();Draw(target,format,cursorPixel.Get(),&view,1,p,true);
    }
};
