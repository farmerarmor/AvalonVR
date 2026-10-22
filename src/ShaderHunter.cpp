#include "ShaderHunter.h"
#include "HunterSelection.h"
#include "HunterRepeat.h"
#include "MarkedEffectRemoval.h"
#include "MarkedGroundFogRemoval.h"
#include "Foveation.h"
#include "KeyEdges.h"
#include <MinHook.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <windows.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace hunter { namespace {
const GUID tag={0x2b59db30,0x7880,0x4c13,{0xaf,0x66,0x13,0xd4,0x9e,0x74,0x40,0x11}};
struct ID {uint64_t hash{};uint32_t stage{},bytecode{};bool operator==(const ID&)const=default;};
struct Key {uint64_t hash{};uint32_t stage{};bool operator==(const Key&)const=default;};
struct KeyHash {size_t operator()(const Key& k)const{return size_t(k.hash^(uint64_t(k.stage)<<58));}};
struct Entry {ID id;std::shared_ptr<std::vector<uint8_t>> code;uint64_t uses[3]{},first{},created{};};
std::filesystem::path directory;
MarkedEffectRemoval markedEffect;
MarkedGroundFogRemoval groundFog;
uint64_t effectConfigAt{};
std::wstring session;
std::mutex catalogMutex;std::vector<Entry> entries;std::unordered_map<Key,size_t,KeyHash> lookup;std::vector<size_t> catalog;
HunterSelection selection;KeyEdges keys;HunterRepeat navigation;
std::atomic<bool> enabled{},wantCapture{},haveCapture{};std::atomic<uint64_t> hidden{},commandCount{},suppressed{},nextToken{1};std::atomic<unsigned> hiddenStage{};
std::atomic<uint64_t> lastPair{},controlGeneration{};uint64_t sequence{},markNumber{},lastStatus{},writtenGeneration{};bool labelDirty{};ID lastMark{};std::filesystem::path lastMarkPath;
thread_local int bypass{},commandDepth{},eye=-1;thread_local bool nativeScene{};thread_local FrameInfo frame{};
bool Context(ID3D11DeviceContext* c);
struct CommandScope {ID3D11DeviceContext* context;unsigned slot,key;void* target;CommandScope(ID3D11DeviceContext* c,unsigned s,void* t,unsigned k):context(c),slot(s),key(k),target(t){++commandDepth;}~CommandScope();};
std::mutex frameMutex;std::array<FrameInfo,32> frames;FrameInfo mainFrame{};
const char* Stage(unsigned i){const char* names[]={"PS","VS","CS","GS","HS","DS"};return i<6?names[i]:"?";}
std::string Hex(uint64_t v){std::ostringstream s;s<<std::hex<<std::setfill('0')<<std::setw(16)<<v;return s.str();}
std::string Q(const std::string& s){std::string v="\"";for(unsigned char c:s){if(c=='"'||c=='\\')v+='\\';if(c>=32)v+=(char)c;}return v+'"';}
struct Write {std::filesystem::path path;std::vector<uint8_t> bytes;bool append{};};
std::mutex writeMutex;std::condition_variable writeReady;std::deque<Write> writes;
void Enqueue(std::filesystem::path path,std::vector<uint8_t> bytes,bool append=false){std::lock_guard lock(writeMutex);writes.push_back({std::move(path),std::move(bytes),append});writeReady.notify_one();}
void Text(std::filesystem::path path,const std::string& text,bool append=false){Enqueue(std::move(path),{text.begin(),text.end()},append);}
void Writer(){for(;;){Write job;{std::unique_lock lock(writeMutex);writeReady.wait(lock,[]{return !writes.empty();});job=std::move(writes.front());writes.pop_front();}
 try{std::filesystem::create_directories(job.path.parent_path());{std::ofstream f(job.path,std::ios::binary|(job.append?std::ios::app:std::ios::trunc));f.write((char*)job.bytes.data(),job.bytes.size());}
 if(job.path.extension()==L".dxbc"){
 ComPtr<ID3DBlob> assembly;if(SUCCEEDED(D3DDisassemble(job.bytes.data(),job.bytes.size(),D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING,nullptr,&assembly))){auto path=job.path;path.replace_extension(L".asm");std::ofstream f(path);f.write((char*)assembly->GetBufferPointer(),assembly->GetBufferSize());}
 ComPtr<ID3D11ShaderReflection> reflection;if(SUCCEEDED(D3DReflect(job.bytes.data(),job.bytes.size(),IID_PPV_ARGS(&reflection)))){D3D11_SHADER_DESC desc{};if(SUCCEEDED(reflection->GetDesc(&desc))){std::ostringstream s;s<<"{\"resources\":[";for(UINT i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC d{};reflection->GetResourceBindingDesc(i,&d);if(i)s<<',';s<<"{\"name\":"<<Q(d.Name?d.Name:"")<<",\"type\":"<<d.Type<<",\"slot\":"<<d.BindPoint<<",\"count\":"<<d.BindCount<<'}';}s<<"],\"constantBuffers\":[";for(UINT i=0;i<desc.ConstantBuffers;++i){auto cb=reflection->GetConstantBufferByIndex(i);D3D11_SHADER_BUFFER_DESC bd{};cb->GetDesc(&bd);if(i)s<<',';s<<"{\"name\":"<<Q(bd.Name?bd.Name:"")<<",\"bytes\":"<<bd.Size<<",\"variables\":[";for(UINT j=0;j<bd.Variables;++j){D3D11_SHADER_VARIABLE_DESC v{};cb->GetVariableByIndex(j)->GetDesc(&v);if(j)s<<',';s<<"{\"name\":"<<Q(v.Name?v.Name:"")<<",\"offset\":"<<v.StartOffset<<",\"bytes\":"<<v.Size<<'}';}s<<"]}";}s<<"]}";auto path=job.path;path.replace_extension(L"reflection.json");std::ofstream f(path);f<<s.str();}}
 }
 }catch(...){/* Diagnostics must not interrupt the game. */}}}
std::string PoseJSON(const FrameInfo& f){std::ostringstream s;s<<"{\"pair\":"<<f.pair<<",\"poses\":[";for(int e=0;e<2;++e){if(e)s<<',';s<<'[';for(int i=0;i<7;++i){if(i)s<<',';s<<f.poses[e][i];}s<<']';}s<<"],\"fovs\":[";for(int e=0;e<2;++e){if(e)s<<',';s<<'[';for(int i=0;i<4;++i){if(i)s<<',';s<<f.fovs[e][i];}s<<']';}s<<"],\"projections\":[";for(int e=0;e<2;++e){if(e)s<<',';s<<'[';for(int i=0;i<16;++i){if(i)s<<',';s<<f.projections[e][i];}s<<']';}s<<"],\"gamePositions\":[";for(int e=0;e<2;++e){if(e)s<<',';s<<'['<<f.gamePositions[e][0]<<','<<f.gamePositions[e][1]<<','<<f.gamePositions[e][2]<<']';}s<<"]}";return s.str();}
void Event(const std::string& s){Text(directory/L"shader-hunter.log",s+"\n",true);}
ID Identity(ID3D11DeviceChild* shader,unsigned stage){if(!shader)return {};ID id{};UINT size=sizeof(id);if(SUCCEEDED(shader->GetPrivateData(tag,&size,&id))&&size==sizeof(id)&&id.hash)return id;
 std::lock_guard lock(catalogMutex);size=sizeof(id);if(SUCCEEDED(shader->GetPrivateData(tag,&size,&id))&&size==sizeof(id)&&id.hash)return id;
 id={0xf000000000000000ull|nextToken.fetch_add(1),stage,0};lookup[{id.hash,stage}]=entries.size();Entry e{};e.id=id;e.created=entries.size();entries.push_back(std::move(e));shader->SetPrivateData(tag,sizeof(id),&id);return id;}
void Register(ID3D11DeviceChild* shader,const void* code,SIZE_T bytes,unsigned stage){if(bypass||!shader||!code||!bytes||stage>=6)return;foveation::RegisterShader(shader,code,bytes,stage);ID id{14695981039346656037ull,stage,1};for(size_t i=0;i<bytes;++i){id.hash^=((const uint8_t*)code)[i];id.hash*=1099511628211ull;}std::lock_guard lock(catalogMutex);if(!lookup.contains({id.hash,stage})){Entry e{};e.id=id;e.created=entries.size();e.code=std::make_shared<std::vector<uint8_t>>((const uint8_t*)code,(const uint8_t*)code+bytes);lookup[{id.hash,stage}]=entries.size();entries.push_back(std::move(e));}shader->SetPrivateData(tag,sizeof(id),&id);}
void AppendCatalog(){std::vector<bool> known(entries.size());for(auto i:catalog)known[i]=true;for(size_t i=0;i<entries.size();++i)if(!known[i])catalog.push_back(i);}
void PublishSelection(){if(selection.HasSelection(catalog.size())){auto id=entries[catalog[selection.index]].id;hiddenStage=id.stage;hidden.store(id.hash,std::memory_order_release);}else hidden=0;enabled=selection.enabled;labelDirty=true;++controlGeneration;}
std::string Resource(ID3D11Resource* resource){if(!resource)return "null";std::ostringstream s;D3D11_RESOURCE_DIMENSION type{};resource->GetType(&type);s<<"{\"object\":"<<Q(Hex((uintptr_t)resource))<<",\"dimension\":"<<type;
 ComPtr<ID3D11Texture2D> tex;ComPtr<ID3D11Buffer> buf;if(SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&tex)))){D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);s<<",\"width\":"<<d.Width<<",\"height\":"<<d.Height<<",\"format\":"<<d.Format<<",\"mips\":"<<d.MipLevels<<",\"array\":"<<d.ArraySize<<",\"samples\":"<<d.SampleDesc.Count;}
 else if(SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&buf)))){D3D11_BUFFER_DESC d{};buf->GetDesc(&d);s<<",\"bytes\":"<<d.ByteWidth<<",\"stride\":"<<d.StructureByteStride;}s<<'}';return s.str();}
template<class View>std::string ViewResource(View* view){if(!view)return "null";ComPtr<ID3D11Resource> r;view->GetResource(&r);return Resource(r.Get());}
void ConstantBuffers(ID3D11DeviceContext* c,unsigned stage,ID3D11Buffer** out){switch(stage){case 0:c->PSGetConstantBuffers(0,14,out);break;case 1:c->VSGetConstantBuffers(0,14,out);break;case 2:c->CSGetConstantBuffers(0,14,out);break;case 3:c->GSGetConstantBuffers(0,14,out);break;case 4:c->HSGetConstantBuffers(0,14,out);break;case 5:c->DSGetConstantBuffers(0,14,out);break;}}
void ShaderResources(ID3D11DeviceContext* c,unsigned stage,ID3D11ShaderResourceView** out){switch(stage){case 0:c->PSGetShaderResources(0,128,out);break;case 1:c->VSGetShaderResources(0,128,out);break;case 2:c->CSGetShaderResources(0,128,out);break;case 3:c->GSGetShaderResources(0,128,out);break;case 4:c->HSGetShaderResources(0,128,out);break;case 5:c->DSGetShaderResources(0,128,out);break;}}
struct Request {ID id;std::filesystem::path path;uint64_t deadline{};std::atomic<unsigned> taken{},outsideTaken{};unsigned outsideLimit=1;};
std::atomic<std::shared_ptr<Request>> request;
struct BufferRead {ComPtr<ID3D11Buffer> staging;std::filesystem::path path;UINT bytes{};};
struct Readback {ComPtr<ID3D11Query> done;std::vector<BufferRead> buffers;uint64_t deadline{};};
std::mutex readMutex;std::vector<Readback> readbacks;
bool shadowConsumerDiagnostics{};std::atomic<bool> shadowConsumerRequest{};
thread_local unsigned consumerPairs{},consumerRecords{};
thread_local uint64_t consumerPair{},consumerStamp{};
thread_local std::unordered_map<Key,unsigned,KeyHash> consumerSeen;
bool ConsumerActive(){return shadowConsumerDiagnostics&&consumerPairs&&consumerRecords<32&&nativeScene;}
void Arm(ID id,const std::filesystem::path& path,unsigned samples=1){auto r=std::make_shared<Request>();r->id=id;r->path=path;r->outsideLimit=std::clamp(samples,1u,8u);r->deadline=GetTickCount64()+15000;request.store(r);haveCapture=true;wantCapture=true;}
void CaptureState(ID3D11DeviceContext* c,ID id,const char* command,const std::filesystem::path& path,bool shadowBuffers=false){
 std::ostringstream s;s<<"{\"shader\":"<<Q(Hex(id.hash))<<",\"stage\":"<<Q(Stage(id.stage))<<",\"identityKind\":"<<Q(id.bytecode?"bytecode":"object-token")<<",\"command\":"<<Q(command)<<",\"eye\":"<<eye<<",\"contextType\":"<<c->GetType()<<",\"thread\":"<<GetCurrentThreadId()<<",\"frame\":"<<PoseJSON(frame)<<",\"renderTargets\":[";
 ID3D11RenderTargetView* rt[8]{};ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(8,rt,&depth);for(int i=0;i<8;++i){if(i)s<<',';s<<ViewResource(rt[i]);if(rt[i])rt[i]->Release();}s<<"],\"depth\":"<<ViewResource(depth.Get());
 UINT count=16;D3D11_VIEWPORT vp[16]{};c->RSGetViewports(&count,vp);s<<",\"viewports\":[";for(UINT i=0;i<count;++i){if(i)s<<',';s<<'['<<vp[i].TopLeftX<<','<<vp[i].TopLeftY<<','<<vp[i].Width<<','<<vp[i].Height<<','<<vp[i].MinDepth<<','<<vp[i].MaxDepth<<']';}s<<"],\"SRVs\":[";
 ID3D11ShaderResourceView* views[128]{};ShaderResources(c,id.stage,views);bool comma{};for(int i=0;i<128;++i)if(views[i]){D3D11_SHADER_RESOURCE_VIEW_DESC d{};views[i]->GetDesc(&d);if(comma)s<<',';comma=true;s<<"{\"slot\":"<<i<<",\"viewFormat\":"<<d.Format<<",\"viewDimension\":"<<d.ViewDimension<<",\"resource\":"<<ViewResource(views[i])<<'}';views[i]->Release();}s<<"],\"UAVs\":[";
 ID3D11UnorderedAccessView* uavs[8]{};if(id.stage==2)c->CSGetUnorderedAccessViews(0,8,uavs);else c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,8,uavs);comma=false;for(int i=0;i<8;++i)if(uavs[i]){if(comma)s<<',';comma=true;s<<"{\"slot\":"<<i<<",\"resource\":"<<ViewResource(uavs[i])<<'}';uavs[i]->Release();}s<<"],\"constantBuffers\":[";
 Readback batch;batch.deadline=GetTickCount64()+10000;ComPtr<ID3D11Device> device;c->GetDevice(&device);ID3D11Buffer* cb[14]{};ConstantBuffers(c,id.stage,cb);comma=false;
 UINT first[14]{},length[14]{};ComPtr<ID3D11DeviceContext1> context1;if(SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&context1)))){ID3D11Buffer* ranges[14]{};switch(id.stage){case 0:context1->PSGetConstantBuffers1(0,14,ranges,first,length);break;case 1:context1->VSGetConstantBuffers1(0,14,ranges,first,length);break;case 2:context1->CSGetConstantBuffers1(0,14,ranges,first,length);break;case 3:context1->GSGetConstantBuffers1(0,14,ranges,first,length);break;case 4:context1->HSGetConstantBuffers1(0,14,ranges,first,length);break;case 5:context1->DSGetConstantBuffers1(0,14,ranges,first,length);break;}for(auto b:ranges)if(b)b->Release();}
 for(int i=0;i<14;++i)if(cb[i]){D3D11_BUFFER_DESC d{};cb[i]->GetDesc(&d);bool copied=false;if(c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE&&d.ByteWidth&&d.ByteWidth<=65536){D3D11_BUFFER_DESC sd{};sd.ByteWidth=d.ByteWidth;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;BufferRead read;read.bytes=d.ByteWidth;read.path=path/(L"cb"+std::to_wstring(i)+L".bin");if(SUCCEEDED(device->CreateBuffer(&sd,nullptr,&read.staging))){c->CopyResource(read.staging.Get(),cb[i]);batch.buffers.push_back(std::move(read));copied=true;}}
 if(comma)s<<',';comma=true;s<<"{\"slot\":"<<i<<",\"bytes\":"<<d.ByteWidth<<",\"firstConstant\":"<<first[i]<<",\"numConstants\":"<<length[i]<<",\"readbackScheduled\":"<<(copied?"true":"false")<<'}';cb[i]->Release();}
 s<<"],\"structuredReadbacks\":[";comma=false;
 if(shadowBuffers&&c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE){ID3D11ShaderResourceView* bound[128]{};ShaderResources(c,id.stage,bound);for(unsigned slot=0;slot<128;++slot)if(bound[slot]){
    ComPtr<ID3D11Resource> resource;bound[slot]->GetResource(&resource);ComPtr<ID3D11Buffer> buffer;D3D11_BUFFER_DESC d{};
    if(SUCCEEDED(resource.As(&buffer))){buffer->GetDesc(&d);if((d.StructureByteStride==104||d.StructureByteStride==240)&&d.ByteWidth&&d.ByteWidth<=1048576){
        D3D11_BUFFER_DESC sd{};sd.ByteWidth=d.ByteWidth;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;BufferRead read;read.bytes=d.ByteWidth;auto name=L"srv"+std::to_wstring(slot)+L".bin";read.path=path/name;bool copied=SUCCEEDED(device->CreateBuffer(&sd,nullptr,&read.staging));
        if(copied){c->CopyResource(read.staging.Get(),buffer.Get());batch.buffers.push_back(std::move(read));}
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};bound[slot]->GetDesc(&vd);if(comma)s<<',';comma=true;s<<"{\"slot\":"<<slot<<",\"bytes\":"<<d.ByteWidth<<",\"stride\":"<<d.StructureByteStride<<",\"firstElement\":"<<vd.Buffer.FirstElement<<",\"numElements\":"<<vd.Buffer.NumElements<<",\"readbackScheduled\":"<<(copied?"true":"false")<<'}';
    }}bound[slot]->Release();}}
 s<<"],\"note\":"<<Q(shadowBuffers?"Read-only candidate: a 104-byte-stride buffer was bound. Shader reflection must establish actual use. Selected-stage CBs and bounded 104/240-byte-stride buffers copied asynchronously; eye is unclassified unless a native marker establishes it.":"Only selected-stage CB bytes are copied; deferred contexts record bindings only. No GPU wait or texture readback in shader hooks.")<<'}';Text(path/L"bindings.json",s.str());
 if(!batch.buffers.empty()){D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};if(SUCCEEDED(device->CreateQuery(&q,&batch.done))){c->End(batch.done.Get());std::lock_guard lock(readMutex);readbacks.push_back(std::move(batch));}}
 Event("captured inputs shader="+Hex(id.hash)+" stage="+Stage(id.stage)+" eye="+std::to_string(eye)+" command="+command);
}
void Capture(ID3D11DeviceContext* c,ID id,const char* command){auto r=request.load();if(!r||r->id.hash!=id.hash||r->id.stage!=id.stage)return;if(GetTickCount64()>r->deadline){haveCapture=false;return;}
 unsigned label=eye>=0&&eye<2?eye:2,bit=1u<<label;unsigned sample{};if(label==2){sample=r->outsideTaken.fetch_add(1);if(sample>=r->outsideLimit)return;}else if(r->taken.fetch_or(bit)&bit)return;
 auto leaf=label==2?(r->outsideLimit==1?std::wstring(L"outside-eye"):L"unclassified-"+std::to_wstring(sample)):label?std::wstring(L"right"):std::wstring(L"left");CaptureState(c,id,command,r->path/leaf);
}
void InspectShadowConsumer(ID3D11DeviceContext* c,ID id,const char* command){
 if(!ConsumerActive()||(id.stage!=0&&id.stage!=2)||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||consumerSeen[{id.hash,id.stage}]>=2)return;
 ID3D11ShaderResourceView* views[128]{};ShaderResources(c,id.stage,views);bool candidate=false;
 for(auto view:views)if(view){ComPtr<ID3D11Resource> resource;view->GetResource(&resource);ComPtr<ID3D11Buffer> buffer;if(SUCCEEDED(resource.As(&buffer))){D3D11_BUFFER_DESC d{};buffer->GetDesc(&d);if(d.StructureByteStride==104&&d.ByteWidth>=104&&d.ByteWidth<=1664)candidate=true;}view->Release();}
 if(!candidate)return;
 ++consumerSeen[{id.hash,id.stage}];auto index=consumerRecords++;
 auto path=directory/L"shadow-consumers"/(session+L"-"+std::to_wstring(consumerStamp))/(L"pair-"+std::to_wstring(frame.pair))/(std::to_wstring(index)+L"-"+std::filesystem::path(std::string(Stage(id.stage))+"-"+Hex(id.hash)).wstring());
 std::shared_ptr<std::vector<uint8_t>> code;{std::lock_guard lock(catalogMutex);auto found=lookup.find({id.hash,id.stage});if(found!=lookup.end())code=entries[found->second].code;}
 if(code)Enqueue(path/L"shader.dxbc",*code);CaptureState(c,id,command,path,true);
}
bool Allowed(ID3D11DeviceContext* c,bool compute,const char* command){
 if(commandDepth>1||bypass)return true;
 bool diagnostics=enabled.load(std::memory_order_relaxed)||haveCapture.load(std::memory_order_relaxed)||ConsumerActive();
 bool removeGround=!compute&&groundFog.Enabled();
 if(!diagnostics&&!(compute&&markedEffect.Enabled())&&!removeGround)return true;
 ++commandCount;ID ids[5]{};int n=compute?1:diagnostics?5:2;
 if(compute){ComPtr<ID3D11ComputeShader> cs;c->CSGetShader(&cs,nullptr,nullptr);ids[0]=Identity(cs.Get(),2);}else{ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11VertexShader> vs;c->PSGetShader(&ps,nullptr,nullptr);c->VSGetShader(&vs,nullptr,nullptr);ids[0]=Identity(ps.Get(),0);ids[1]=Identity(vs.Get(),1);if(diagnostics){ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;c->GSGetShader(&gs,nullptr,nullptr);c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);ids[2]=Identity(gs.Get(),3);ids[3]=Identity(hs.Get(),4);ids[4]=Identity(ds.Get(),5);}}
 unsigned groundMatches{};
 bool allow=true;auto target=hidden.load(std::memory_order_acquire);for(int i=0;i<n;++i)if(ids[i].hash){if(enabled){std::lock_guard lock(catalogMutex);auto& entry=entries[lookup.at({ids[i].hash,ids[i].stage})];++entry.uses[eye>=0&&eye<2?eye:2];if(!entry.first)entry.first=++sequence;}
 if(haveCapture&&(nativeScene||c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED))Capture(c,ids[i],command);
 InspectShadowConsumer(c,ids[i],command);
 if(markedEffect.Apply(c,ids[i].hash,ids[i].stage,ids[i].bytecode!=0))allow=false;
 if(removeGround)groundMatches|=MarkedGroundFogRemoval::Match(ids[i].hash,ids[i].stage,ids[i].bytecode!=0);
 if(enabled&&(nativeScene||c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED)&&ids[i].hash==target&&ids[i].stage==hiddenStage)allow=false;
 }if(groundMatches){groundFog.Record(groundMatches);allow=false;}if(!allow)++suppressed;return allow;
}
// D3D11 can call a hooked implementation with an internal object whose vtable
// differs from the public context. Each target therefore gets its own thunk;
// the incoming object's vtable must never choose the trampoline.
constexpr unsigned variantCount=16;
std::mutex hooksMutex;std::unordered_map<void*,void*> originals;
std::array<std::array<void*,variantCount>,128> trampolines{};
std::array<std::array<void*,variantCount>,128> hookTargets{};
std::array<unsigned,128> nextVariant{};
template<class F,unsigned family,unsigned variant>F Original(){return reinterpret_cast<F>(trampolines[family][variant]);}
template<class Factory,size_t... I>auto Variants(Factory f,std::index_sequence<I...>){return std::array<void*,sizeof...(I)>{f(std::integral_constant<size_t,I>{})...};}
template<class Factory>auto Variants(Factory f){return Variants(f,std::make_index_sequence<variantCount>{});}
#define HUNTER_THUNKS(...) Variants([]<size_t I>(std::integral_constant<size_t,I>){return (void*)&__VA_ARGS__;})
bool Add(void* target,unsigned family,const std::array<void*,variantCount>& variants,const char* name){std::lock_guard lock(hooksMutex);if(originals.contains(target))return true;unsigned variant=nextVariant[family];if(variant==variantCount){Event(std::string("hook implementation capacity reached ")+name);return false;}void* saved{};auto result=MH_CreateHook(target,variants[variant],&saved);if(result!=MH_OK){Event(std::string("hook failed ")+name+" code="+std::to_string(result));return false;}trampolines[family][variant]=saved;hookTargets[family][variant]=target;++nextVariant[family];originals[target]=saved;result=MH_EnableHook(target);Event(std::string("hook ")+name+" target="+Hex((uint64_t)target)+" thunk="+Hex((uint64_t)variants[variant])+" implementation="+std::to_string(variant)+" code="+std::to_string(result));return result==MH_OK;}
template<unsigned slot,unsigned stage,class Shader,unsigned I>HRESULT STDMETHODCALLTYPE Create(ID3D11Device* d,const void* p,SIZE_T n,ID3D11ClassLinkage* link,Shader** shader){using F=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,Shader**);auto hr=Original<F,slot*2,I>()(d,p,n,link,shader);if(SUCCEEDED(hr)&&shader)Register(*shader,p,n,stage);return hr;}
template<unsigned I>HRESULT STDMETHODCALLTYPE CreateSO(ID3D11Device* d,const void* p,SIZE_T n,const D3D11_SO_DECLARATION_ENTRY* e,UINT count,const UINT* strides,UINT strideCount,UINT stream,ID3D11ClassLinkage* link,ID3D11GeometryShader** shader){using F=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,const D3D11_SO_DECLARATION_ENTRY*,UINT,const UINT*,UINT,UINT,ID3D11ClassLinkage*,ID3D11GeometryShader**);auto hr=Original<F,28,I>()(d,p,n,e,count,strides,strideCount,stream,link,shader);if(SUCCEEDED(hr)&&shader)Register(*shader,p,n,3);return hr;}
template<unsigned I>void STDMETHODCALLTYPE Draw(ID3D11DeviceContext* c,UINT n,UINT first){using F=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);CommandScope commandScope(c,13,hookTargets[27][I],27*variantCount+I);if(Allowed(c,false,"Draw")){foveation::Command rate(c,!bypass&&nativeScene,n>3,commandDepth==1);Original<F,27,I>()(c,n,first);}}
template<unsigned I>void STDMETHODCALLTYPE Indexed(ID3D11DeviceContext* c,UINT n,UINT first,INT base){using F=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);CommandScope commandScope(c,12,hookTargets[25][I],25*variantCount+I);if(Allowed(c,false,"DrawIndexed")){foveation::Command rate(c,!bypass&&nativeScene,true,commandDepth==1);Original<F,25,I>()(c,n,first,base);}}
template<unsigned I>void STDMETHODCALLTYPE Instanced(ID3D11DeviceContext* c,UINT n,UINT count,UINT first,UINT instance){using F=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);CommandScope commandScope(c,21,hookTargets[43][I],43*variantCount+I);if(Allowed(c,false,"DrawInstanced")){foveation::Command rate(c,!bypass&&nativeScene,n>3,commandDepth==1);Original<F,43,I>()(c,n,count,first,instance);}}
template<unsigned I>void STDMETHODCALLTYPE IndexedInstanced(ID3D11DeviceContext* c,UINT n,UINT count,UINT first,INT base,UINT instance){using F=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);CommandScope commandScope(c,20,hookTargets[41][I],41*variantCount+I);if(Allowed(c,false,"DrawIndexedInstanced")){foveation::Command rate(c,!bypass&&nativeScene,true,commandDepth==1);Original<F,41,I>()(c,n,count,first,base,instance);}}
template<unsigned I>void STDMETHODCALLTYPE Auto(ID3D11DeviceContext* c){using F=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);CommandScope commandScope(c,38,hookTargets[77][I],77*variantCount+I);if(Allowed(c,false,"DrawAuto")){foveation::Command rate(c,!bypass&&nativeScene,true,commandDepth==1);Original<F,77,I>()(c);}}
template<unsigned slot,unsigned I>void STDMETHODCALLTYPE Indirect(ID3D11DeviceContext* c,ID3D11Buffer* args,UINT offset){using F=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);CommandScope commandScope(c,slot,hookTargets[slot*2+1][I],(slot*2+1)*variantCount+I);if(Allowed(c,slot==42,slot==42?"DispatchIndirect":slot==39?"DrawIndexedInstancedIndirect":"DrawInstancedIndirect")){foveation::Command rate(c,slot!=42&&!bypass&&nativeScene,true,commandDepth==1);Original<F,slot*2+1,I>()(c,args,offset);}}
template<unsigned I>void STDMETHODCALLTYPE Dispatch(ID3D11DeviceContext* c,UINT x,UINT y,UINT z){using F=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT);CommandScope commandScope(c,41,hookTargets[83][I],83*variantCount+I);if(Allowed(c,true,"Dispatch")){foveation::Command rate(c,false,false,commandDepth==1);Original<F,83,I>()(c,x,y,z);}}
template<unsigned I>void STDMETHODCALLTYPE ExecuteList(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore){using F=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);CommandScope commandScope(c,58,hookTargets[117][I],117*variantCount+I);foveation::Command rate(c,false,false,commandDepth==1);Original<F,117,I>()(c,list,restore);}
bool Context(ID3D11DeviceContext* c){auto v=*(void***)c;bool ok=true;struct Hook{unsigned slot;std::array<void*,variantCount> fn;const char* name;};const Hook list[]={{12,HUNTER_THUNKS(Indexed<I>),"DrawIndexed"},{13,HUNTER_THUNKS(Draw<I>),"Draw"},{20,HUNTER_THUNKS(IndexedInstanced<I>),"DrawIndexedInstanced"},{21,HUNTER_THUNKS(Instanced<I>),"DrawInstanced"},{38,HUNTER_THUNKS(Auto<I>),"DrawAuto"},{39,HUNTER_THUNKS(Indirect<39,I>),"DrawIndexedInstancedIndirect"},{40,HUNTER_THUNKS(Indirect<40,I>),"DrawInstancedIndirect"},{41,HUNTER_THUNKS(Dispatch<I>),"Dispatch"},{42,HUNTER_THUNKS(Indirect<42,I>),"DispatchIndirect"},{58,HUNTER_THUNKS(ExecuteList<I>),"ExecuteCommandList"}};for(auto& h:list)ok=Add(v[h.slot],h.slot*2+1,h.fn,h.name)&&ok;return ok;}
CommandScope::~CommandScope(){if(commandDepth==1){static thread_local std::array<void*,128*variantCount> seen{};auto current=(*(void***)context)[slot];if(current!=target&&seen[key]!=current){Context(context);seen[key]=current;Event("context implementations refreshed after runtime forwarding");}}--commandDepth;}
template<unsigned slot,unsigned I>HRESULT STDMETHODCALLTYPE Deferred(ID3D11Device* d,UINT flags,ID3D11DeviceContext** out){using F=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,UINT,ID3D11DeviceContext**);auto hr=Original<F,slot*2,I>()(d,flags,out);if(SUCCEEDED(hr)&&out&&*out)Context(*out);return hr;}
} // namespace
bool Install(ID3D11Device* d,ID3D11DeviceContext* c,const std::filesystem::path& dir){directory=dir;auto ini=dir.parent_path()/L"AvalonVR.ini";markedEffect.Enable(GetPrivateProfileIntW(L"Effects",L"DisableMarkedFogLens",0,ini.c_str())!=0);groundFog.Enable(GetPrivateProfileIntW(L"Effects",L"DisableGroundFog",0,ini.c_str())!=0);shadowConsumerDiagnostics=GetPrivateProfileIntW(L"Debug",L"ShadowConsumerDiagnostics",0,ini.c_str())!=0;FILETIME time{};GetSystemTimeAsFileTime(&time);ULARGE_INTEGER stamp{};stamp.LowPart=time.dwLowDateTime;stamp.HighPart=time.dwHighDateTime;session=std::to_wstring(stamp.QuadPart)+L"-"+std::to_wstring(GetCurrentProcessId());std::filesystem::create_directories(directory);std::thread(Writer).detach();Event(std::string("Marked RGB effect removal initial=")+(markedEffect.Enabled()?"1":"0")+" CS=7728ef9458651dad; exact bytecode and output guards");Event(std::string("Ground fog removal initial=")+(groundFog.Enabled()?"1":"0")+" VS=326cadd68150c1f1 PS=afdb0f0cc22f9ef0; exact bytecode graphics draws only");auto v=*(void***)d;bool ok=true;
 struct Hook{unsigned slot;std::array<void*,variantCount> fn;const char* name;};const Hook list[]={{12,HUNTER_THUNKS(Create<12,1,ID3D11VertexShader,I>),"CreateVertexShader"},{13,HUNTER_THUNKS(Create<13,3,ID3D11GeometryShader,I>),"CreateGeometryShader"},{14,HUNTER_THUNKS(CreateSO<I>),"CreateGeometryShaderWithStreamOutput"},{15,HUNTER_THUNKS(Create<15,0,ID3D11PixelShader,I>),"CreatePixelShader"},{16,HUNTER_THUNKS(Create<16,4,ID3D11HullShader,I>),"CreateHullShader"},{17,HUNTER_THUNKS(Create<17,5,ID3D11DomainShader,I>),"CreateDomainShader"},{18,HUNTER_THUNKS(Create<18,2,ID3D11ComputeShader,I>),"CreateComputeShader"}};for(auto& h:list)ok=Add(v[h.slot],h.slot*2,h.fn,h.name)&&ok;
 ok=Context(c)&&ok;ComPtr<ID3D11DeviceContext> deferred;if(SUCCEEDED(d->CreateDeferredContext(0,&deferred)))ok=Context(deferred.Get())&&ok;else ok=false;
 ok=Add(v[27],54,HUNTER_THUNKS(Deferred<27,I>),"CreateDeferredContext")&&ok;
 ComPtr<ID3D11Device1> d1;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&d1))))ok=Add((*(void***)d1.Get())[44],88,HUNTER_THUNKS(Deferred<44,I>),"CreateDeferredContext1")&&ok;
 ComPtr<ID3D11Device2> d2;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&d2))))ok=Add((*(void***)d2.Get())[51],102,HUNTER_THUNKS(Deferred<51,I>),"CreateDeferredContext2")&&ok;
 ComPtr<ID3D11Device3> d3;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&d3))))ok=Add((*(void***)d3.Get())[62],124,HUNTER_THUNKS(Deferred<62,I>),"CreateDeferredContext3")&&ok;
 Event(std::string("Shader hunter installed; dormant, controls End/PgUp/PgDn/Delete/Home; full hook coverage=")+(ok?"1":"0"));return ok;}
void Control(unsigned key){std::lock_guard lock(catalogMutex);AppendCatalog();
 if(key==VK_HOME){selection.Home();PublishSelection();if(lastMark.hash)Arm(lastMark,lastMarkPath/L"restored");Event("Home: restored hunted shader commands; configured effect removal="+std::to_string(markedEffect.Enabled())+" ground fog removal="+std::to_string(groundFog.Enabled()));return;}
 if(key==VK_END){selection.Enable();std::stable_sort(catalog.begin(),catalog.end(),[](size_t a,size_t b){auto& x=entries[a];auto& y=entries[b];if(bool(x.first)!=bool(y.first))return bool(x.first);return x.first&&y.first?x.first<y.first:x.created<y.created;});selection.Scanned(catalog.size());PublishSelection();Event("End: enabled cumulative catalog "+std::to_string(catalog.size()));return;}
 if(!selection.enabled)return;
 if(key==VK_PRIOR||key==VK_NEXT){selection.Move(key==VK_PRIOR?-1:1,catalog.size());PublishSelection();Event(std::string(key==VK_PRIOR?"PageUp ":"PageDown ")+Hex(hidden));return;}
 if(key==VK_DELETE&&selection.HasSelection(catalog.size())){auto& e=entries[catalog[selection.index]];lastMark=e.id;lastMarkPath=directory/(L"mark-"+session+L"-"+std::to_wstring(++markNumber)+L"-"+std::filesystem::path(std::string(Stage(e.id.stage))+"-"+Hex(e.id.hash)).wstring());
 std::ostringstream s;s<<"{\"mark\":"<<markNumber<<",\"hash\":"<<Q(Hex(e.id.hash))<<",\"stage\":"<<Q(Stage(e.id.stage))<<",\"identityKind\":"<<Q(e.id.bytecode?"bytecode":"object-token")<<",\"index\":"<<selection.index<<",\"total\":"<<catalog.size()<<",\"frame\":"<<PoseJSON(mainFrame)<<",\"note\":\"Selected command is suppressed during hunt; Home restores and captures native inputs.\"}\n";Text(directory/L"shader-hunter-marks.jsonl",s.str(),true);Text(lastMarkPath/L"mark.json",s.str());if(e.code)Enqueue(lastMarkPath/L"shader.dxbc",*e.code);Arm(e.id,lastMarkPath/L"suppressed");labelDirty=true;Event("Delete: marked "+std::string(Stage(e.id.stage))+" "+Hex(e.id.hash));}}
void Tick(bool gameplay){DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);bool focused=pid==GetCurrentProcessId();auto now=GetTickCount64();
 if(now>=effectConfigAt){effectConfigAt=now+1000;auto ini=directory.parent_path()/L"AvalonVR.ini";bool remove=GetPrivateProfileIntW(L"Effects",L"DisableMarkedFogLens",0,ini.c_str())!=0;if(remove!=markedEffect.Enabled()){markedEffect.Enable(remove);++controlGeneration;labelDirty=true;Event("Marked RGB effect removal="+std::to_string(remove));}bool removeGround=GetPrivateProfileIntW(L"Effects",L"DisableGroundFog",0,ini.c_str())!=0;if(removeGround!=groundFog.Enabled()){groundFog.Enable(removeGround);++controlGeneration;labelDirty=true;Event("Ground fog removal="+std::to_string(removeGround));}}
 for(unsigned key:{VK_HOME,VK_END})if(keys.Pressed(key,(uint16_t)GetAsyncKeyState(key))&&focused)Control(key);
 bool up=(GetAsyncKeyState(VK_PRIOR)&0x8000)!=0,down=(GetAsyncKeyState(VK_NEXT)&0x8000)!=0;
 if(auto direction=navigation.Step(up,down,focused&&gameplay&&enabled,now))Control(direction<0?VK_PRIOR:VK_NEXT);
 if(keys.Pressed(VK_DELETE,(uint16_t)GetAsyncKeyState(VK_DELETE))&&focused)Control(VK_DELETE);
 if(!gameplay&&enabled){std::lock_guard lock(catalogMutex);selection.Home();PublishSelection();Event("Loading/flat mode: restored native commands");}
 bool changed=writtenGeneration!=controlGeneration.load();if(now-lastStatus<1500&&!changed)return;lastStatus=now;writtenGeneration=controlGeneration.load();
 if(enabled){std::lock_guard lock(catalogMutex);AppendCatalog();if(!selection.HasSelection(catalog.size())&&!catalog.empty()){selection.Scanned(catalog.size());PublishSelection();}}
 if(enabled||haveCapture||changed||markedEffect.Enabled()||groundFog.Enabled())Text(directory/L"shader-hunter-status.json",StatusJSON());
 if(enabled||haveCapture||changed){std::ostringstream s;s<<"{\"scope\":\"cumulative six-stage creations and actual bindings across contexts\",\"shaders\":[";{std::lock_guard lock(catalogMutex);AppendCatalog();for(size_t i=0;i<catalog.size();++i){auto& e=entries[catalog[i]];if(i)s<<',';s<<"{\"index\":"<<i<<",\"hash\":"<<Q(Hex(e.id.hash))<<",\"stage\":"<<Q(Stage(e.id.stage))<<",\"identityKind\":"<<Q(e.id.bytecode?"bytecode":"object-token")<<",\"firstUse\":"<<e.first<<",\"left\":"<<e.uses[0]<<",\"right\":"<<e.uses[1]<<",\"outsideEyes\":"<<e.uses[2]<<'}';}}s<<"]}";Text(directory/L"shader-hunter-catalog.json",s.str());}}
std::string StatusJSON(){std::lock_guard lock(catalogMutex);unsigned registered[6]{},observed[6]{},tokens{};for(auto& e:entries){++registered[e.id.stage];if(e.first)++observed[e.id.stage];if(!e.id.bytecode)++tokens;}std::ostringstream s;s<<"{\"enabled\":"<<(enabled?"true":"false")<<",\"index\":"<<selection.index<<",\"total\":"<<catalog.size()<<",\"selected\":"<<Q(Hex(hidden))<<",\"stage\":"<<Q(Stage(hiddenStage))<<",\"registered\":[";for(int i=0;i<6;++i){if(i)s<<',';s<<registered[i];}s<<"],\"observed\":[";for(int i=0;i<6;++i){if(i)s<<',';s<<observed[i];}s<<"],\"objectTokens\":"<<tokens<<",\"commands\":"<<commandCount<<",\"suppressed\":"<<suppressed<<",\"pair\":"<<lastPair<<",\"markedEffectRemoval\":"<<(markedEffect.Enabled()?"true":"false")<<",\"markedEffectClears\":"<<markedEffect.Cleared()<<",\"markedEffectUnsupported\":"<<markedEffect.Unsupported()<<",\"groundFogRemoval\":"<<(groundFog.Enabled()?"true":"false")<<",\"groundFogDraws\":"<<groundFog.Draws()<<",\"groundFogPixelMatches\":"<<groundFog.Pixels()<<",\"groundFogVertexMatches\":"<<groundFog.Vertices()<<'}';return s.str();}
std::string Label(){std::lock_guard lock(catalogMutex);if(!selection.enabled){if(labelDirty){labelDirty=false;std::string text="Shader hunter OFF";if(markedEffect.Enabled())text+=" | marked effect removal ON";if(groundFog.Enabled())text+=" | ground fog removed";if(!markedEffect.Enabled()&&!groundFog.Enabled())text+=" - all shaders restored";return text;}return {};}
 std::ostringstream s;s<<"Shader hunter "<<(selection.index+1)<<'/'<<catalog.size()<<" | "<<Stage(hiddenStage)<<" | "<<Hex(hidden)<<"\nHold PgUp/PgDn: 0.2s | Delete mark | Home restore\n"<<"Marks: "<<markNumber<<" | Suppressed commands: "<<suppressed;labelDirty=false;return s.str();}
bool CaptureRequested(){return wantCapture.exchange(false);}
void RequestShadowConsumers(){if(shadowConsumerDiagnostics){shadowConsumerRequest=true;Event("F4: bounded shadow-consumer candidates requested; no suppression");}}
void CaptureNativeInputs(uint64_t hash,unsigned stage,const std::filesystem::path& output,unsigned samples){if(hash&&stage<6)Arm({hash,stage,1},output,samples);}
void QueueFrame(const FrameInfo& f){std::lock_guard lock(frameMutex);frames[f.pair%frames.size()]=f;mainFrame=f;lastPair=f.pair;}
void BeginFrame(uint32_t pair){nativeScene=true;eye=-1;{std::lock_guard lock(frameMutex);auto& f=frames[pair%frames.size()];frame=f.pair==pair?f:FrameInfo{};}
 foveation::Frame vrsFrame{frame.inputWidth,frame.inputHeight,frame.outputWidth,frame.outputHeight,frame.vrsFlags};for(int e=0;e<2;++e)vrsFrame.fovs[e]={frame.fovs[e][0],frame.fovs[e][1],frame.fovs[e][2],frame.fovs[e][3]};foveation::BeginFrame(vrsFrame);
 if(shadowConsumerRequest.exchange(false)){consumerPairs=2;consumerPair=pair;consumerStamp=GetTickCount64();consumerRecords=0;consumerSeen.clear();}
 else if(consumerPairs&&consumerPair!=pair){--consumerPairs;consumerPair=pair;consumerSeen.clear();if(!consumerPairs)Event("Shadow-consumer candidate window finished: records="+std::to_string(consumerRecords));}
}
void EndScene(){foveation::EndFrame();nativeScene=false;eye=-1;}
void Eye(int i){eye=i;}
Bypass::Bypass(){++bypass;}Bypass::~Bypass(){--bypass;}
void Poll(ID3D11DeviceContext* c){if(!c)return;std::lock_guard lock(readMutex);for(auto it=readbacks.begin();it!=readbacks.end();){auto ready=c->GetData(it->done.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);if(ready==S_FALSE&&GetTickCount64()<it->deadline){++it;continue;}
 if(ready==S_OK)for(auto& b:it->buffers){D3D11_MAPPED_SUBRESOURCE data{};if(SUCCEEDED(c->Map(b.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&data))){std::vector<uint8_t> bytes((uint8_t*)data.pData,(uint8_t*)data.pData+b.bytes);c->Unmap(b.staging.Get(),0);Enqueue(b.path,std::move(bytes));}}
 it=readbacks.erase(it);}auto r=request.load();if(r&&GetTickCount64()>r->deadline){request.store(nullptr);haveCapture=false;}}
} // namespace hunter

