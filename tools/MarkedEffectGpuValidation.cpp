#include "ShaderHunter.h"
#include "MarkedEffectRemoval.h"
#include <MinHook.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void Require(bool ok,const char* message){++checks;if(!ok){fprintf(stderr,"FAIL: %s\n",message);fflush(stderr);ExitProcess(1);}}
static uint64_t Counter(const char* name){auto status=hunter::StatusJSON();auto key=std::string("\"")+name+"\":";auto at=status.find(key);Require(at!=status.npos,name);return std::stoull(status.substr(at+key.size()));}
int wmain(int argc,wchar_t** argv){
 Require(argc==3||argc==4,"private output, captured shader DXBC, optional --unprotected");
 bool protect=argc==3;Require(protect||wcscmp(argv[3],L"--unprotected")==0,"context protection option");
 auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);auto ini=root.parent_path()/L"AvalonVR.ini";
 auto Configure=[&](const wchar_t* value){Require(WritePrivateProfileStringW(L"Effects",L"DisableMarkedFogLens",value,ini.c_str())!=0,"write reversible effect option");};
 Configure(L"1");Require(WritePrivateProfileStringW(L"Debug",L"ShadowConsumerDiagnostics",L"0",ini.c_str())!=0,"diagnostics off");
 std::ifstream file(argv[2],std::ios::binary);std::vector<char> code{std::istreambuf_iterator<char>(file),{}};
 uint64_t hash=14695981039346656037ull;for(auto byte:code){hash^=(uint8_t)byte;hash*=1099511628211ull;}Require(!code.empty()&&hash==MarkedEffectRemoval::Shader,"exact captured shader identity; fixture remains private");
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
 Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)),"hardware D3D11");
 ComPtr<ID3D11Multithread> multithread;Require(SUCCEEDED(context.As(&multithread)),"multithread context");multithread->SetMultithreadProtected(protect);
 Require(MH_Initialize()==MH_OK,"MinHook initialize");Require(hunter::Install(device.Get(),context.Get(),root),"production shader hooks");
 // Match the existing hunter harness: the protected runtime forwards its
 // first empty command, then exposes its final context implementations.
 context->Dispatch(0,0,0);
 ComPtr<ID3D11ComputeShader> selected;Require(SUCCEEDED(device->CreateComputeShader(code.data(),code.size(),nullptr,&selected)),"captured selected shader creation");
 struct Target {ComPtr<ID3D11Texture2D> texture,read;ComPtr<ID3D11UnorderedAccessView> uav;ComPtr<ID3D11ShaderResourceView> srv;};
 auto Create=[&](DXGI_FORMAT format){Target t;D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=32;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=format;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
  Require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&t.texture)),"RGB output texture");D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=format;u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2DARRAY;u.Texture2DArray.ArraySize=1;
  Require(SUCCEEDED(device->CreateUnorderedAccessView(t.texture.Get(),&u,&t.uav)),"captured array UAV shape");D3D11_SHADER_RESOURCE_VIEW_DESC s{};s.Format=format;s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;s.Texture2DArray.MipLevels=s.Texture2DArray.ArraySize=1;
  Require(SUCCEEDED(device->CreateShaderResourceView(t.texture.Get(),&s,&t.srv)),"neighbor input SRV");d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  Require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&t.read)),"test-only staging");return t;};
 auto output=Create(DXGI_FORMAT_R11G11B10_FLOAT),neighbor=Create(DXGI_FORMAT_R11G11B10_FLOAT),unsupported=Create(DXGI_FORMAT_R16G16B16A16_FLOAT);
 auto Seed=[&](Target& t){float v[4]={1,2,3,1};context->ClearUnorderedAccessViewFloat(t.uav.Get(),v);};
 auto AllBlack=[&](Target& t){context->CopyResource(t.read.Get(),t.texture.Get());D3D11_MAPPED_SUBRESOURCE m{};Require(SUCCEEDED(context->Map(t.read.Get(),0,D3D11_MAP_READ,0,&m)),"test-only completion");bool black=true;unsigned bpp=t.texture==unsupported.texture?8:4;for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32*bpp;++x)black&=((const uint8_t*)m.pData)[y*m.RowPitch+x]==0;context->Unmap(t.read.Get(),0);return black;};
 auto Bind=[&](ID3D11DeviceContext* c,Target& t){auto u=t.uav.Get();auto s=neighbor.srv.Get();c->CSSetShader(selected.Get(),nullptr,0);c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->CSSetShaderResources(0,1,&s);};
 auto Unchanged=[&](ID3D11DeviceContext* c){ComPtr<ID3D11ComputeShader> shader;ComPtr<ID3D11UnorderedAccessView> uav;ComPtr<ID3D11ShaderResourceView> srv;c->CSGetShader(&shader,nullptr,nullptr);c->CSGetUnorderedAccessViews(0,1,&uav);c->CSGetShaderResources(0,1,&srv);Require(shader==selected&&uav==output.uav&&srv==neighbor.srv,"shader/UAV/SRV bindings preserved");};
 Seed(neighbor);Bind(context.Get(),output);hunter::FrameInfo frame{};frame.pair=500;hunter::QueueFrame(frame);hunter::BeginFrame(500);
 for(int eye:{0,1}){hunter::Eye(eye);Seed(output);context->Dispatch(1,1,1);Require(AllBlack(output),"each eye gets black, not retained previous output");Unchanged(context.Get());}
 hunter::EndScene();Seed(output);context->Dispatch(1,1,1);Require(AllBlack(output),"same removal outside native eye callbacks");
 D3D11_BUFFER_DESC argsDesc{};argsDesc.ByteWidth=16;argsDesc.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;UINT groups[4]={1,1,1,0};D3D11_SUBRESOURCE_DATA init{groups,0,0};ComPtr<ID3D11Buffer> args;Require(SUCCEEDED(device->CreateBuffer(&argsDesc,&init,&args)),"indirect dispatch args");
 Seed(output);context->DispatchIndirect(args.Get(),0);Require(AllBlack(output),"indirect dispatch removal");
 ComPtr<ID3D11DeviceContext> deferred;Require(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"future deferred context");Bind(deferred.Get(),output);Seed(output);deferred->Dispatch(1,1,1);ComPtr<ID3D11CommandList> commands;Require(SUCCEEDED(deferred->FinishCommandList(FALSE,&commands)),"record neutral output on deferred context");context->ExecuteCommandList(commands.Get(),TRUE);Require(AllBlack(output),"deferred removal executes neutral output");
 Require(!AllBlack(neighbor),"source effect input remains untouched");hunter::Control(VK_HOME);Require(hunter::StatusJSON().find("\"markedEffectRemoval\":true")!=std::string::npos,"Home only restores the hunter selection");
 Bind(context.Get(),output);Seed(output);auto before=Counter("markedEffectClears");{hunter::Bypass bypass;context->Dispatch(0,0,0);}Require(!AllBlack(output)&&Counter("markedEffectClears")==before,"mod transfer bypass");
 Bind(context.Get(),unsupported);Seed(unsupported);context->Dispatch(0,0,0);Require(!AllBlack(unsupported)&&Counter("markedEffectUnsupported")>0,"unsupported RGBA texture preserved and native call retained");
 Bind(context.Get(),output);MarkedEffectRemoval identityGuard;identityGuard.Enable(true);Seed(output);Require(!identityGuard.Apply(context.Get(),MarkedEffectRemoval::Shader,2,false),"object token never matches permanent removal");Require(!identityGuard.Apply(context.Get(),MarkedEffectRemoval::Shader,0,true),"wrong stage never matches");Require(!AllBlack(output),"identity rejections preserve output");
 const char* text="RWTexture2DArray<float3> result:register(u0);[numthreads(8,8,1)]void main(uint3 p:SV_DispatchThreadID){result[p]=float3(3,2,1);}";ComPtr<ID3DBlob> binary,error;Require(SUCCEEDED(D3DCompile(text,strlen(text),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&binary,&error)),"unrelated RGB compute shader");ComPtr<ID3D11ComputeShader> other;Require(SUCCEEDED(device->CreateComputeShader(binary->GetBufferPointer(),binary->GetBufferSize(),nullptr,&other)),"unrelated bytecode registration");
 float zero[4]{};context->ClearUnorderedAccessViewFloat(output.uav.Get(),zero);context->CSSetShader(other.Get(),nullptr,0);before=Counter("markedEffectClears");context->Dispatch(4,4,1);Require(!AllBlack(output)&&Counter("markedEffectClears")==before,"other shader with same output format executes normally");
 Configure(L"0");Sleep(1100);hunter::Tick(true);Require(hunter::StatusJSON().find("\"markedEffectRemoval\":false")!=std::string::npos,"live INI OFF");Bind(context.Get(),output);Seed(output);context->Dispatch(0,0,0);Require(!AllBlack(output),"OFF preserves native path/output");
 Configure(L"1");Sleep(1100);hunter::Tick(true);Seed(output);context->Dispatch(1,1,1);Require(AllBlack(output),"live INI ON clears stale output immediately");Require(Counter("markedEffectClears")==6,"exact native dispatch removal count");Require(SUCCEEDED(device->GetDeviceRemovedReason()),"device healthy");
 MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();printf("PASS: %u marked-effect GPU checks (%s context).\n",checks,protect?"protected":"unprotected");fflush(stdout);ExitProcess(0);
}
