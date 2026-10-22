#include "StagingUpload.h"
#include "UploadHandoff.h"
#include "UploadPool.h"
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
unsigned checks{};
void Check(bool okay,const char* what){++checks;if(!okay){fprintf(stderr,"FAIL %s\n",what);ExitProcess(1);}}
int main(){
 upload::Handoff handoff;upload::Pending entry{(void*)1,nullptr,nullptr,(void*)0x1000,128,256,true};
 struct Job {upload::Handoff* handoff;upload::Pending* value;bool result;} job{&handoff,&entry,false};
 auto thread=CreateThread(nullptr,0,[](void* p)->DWORD{auto& j=*(Job*)p;j.result=j.handoff->Store(*j.value);return 0;},&job,0,nullptr);
 Check(thread!=nullptr,"writer thread created");Check(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0,"writer completed");CloseHandle(thread);
 auto received=handoff.Take(entry.owner);Check(job.result&&received.base==entry.base&&received.offset==128&&received.capacity==256,"cross-thread buffer handoff");Check(!handoff.Take(entry.owner).accepted,"handoff consumed once");
 ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level{};
 Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&level,&ctx)),"D3D11 hardware");
 for(bool raw:{false,true}){
  std::vector<uint8_t> expected(upload::Capacity,0x25);D3D11_SUBRESOURCE_DATA initial{expected.data(),0,0};
  D3D11_BUFFER_DESC gd{};gd.ByteWidth=upload::Capacity;gd.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;gd.MiscFlags=raw?D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS:D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;gd.StructureByteStride=raw?0:16;
  ComPtr<ID3D11Buffer> gpu,stage,read;Check(SUCCEEDED(dev->CreateBuffer(&gd,&initial,&gpu)),"native destination");
  D3D11_BUFFER_DESC sd{};sd.ByteWidth=gd.ByteWidth;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ|D3D11_CPU_ACCESS_WRITE;
  Check(SUCCEEDED(dev->CreateBuffer(&sd,nullptr,&stage)),"persistent staging");sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(SUCCEEDED(dev->CreateBuffer(&sd,nullptr,&read)),"readback validation");sd.CPUAccessFlags|=D3D11_CPU_ACCESS_WRITE;
  Check(upload::Eligible(gd,sd,0,gd.ByteWidth),"eligible full upload");Check(!upload::Eligible(gd,sd,gd.ByteWidth,1),"reject overflow");auto bad=gd;bad.Usage=D3D11_USAGE_DYNAMIC;Check(!upload::Eligible(bad,sd,0,16),"reject dynamic");bad=gd;bad.BindFlags|=D3D11_BIND_CONSTANT_BUFFER;Check(!upload::Eligible(bad,sd,0,16),"reject partial constant buffer");
  D3D11_MAPPED_SUBRESOURCE map{};Check(SUCCEEDED(ctx->Map(stage.Get(),0,D3D11_MAP_READ_WRITE,0,&map)),"native staging map");
  for(unsigned n=0;n<20;++n){UINT offset=n==19?gd.ByteWidth-256:(n*17312)%65536;UINT bytes=256;memset((uint8_t*)map.pData+offset,0x40+n,bytes);memset(expected.data()+offset,0x40+n,bytes);upload::Copy(ctx.Get(),gpu.Get(),map.pData,offset,bytes);memset((uint8_t*)map.pData+offset,0xEE,bytes);}
  upload::Copy(ctx.Get(),gpu.Get(),map.pData,gd.ByteWidth,0);
  ctx->CopyResource(read.Get(),gpu.Get());D3D11_MAPPED_SUBRESOURCE result{};Check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&result)),"result map");Check(!memcmp(result.pData,expected.data(),expected.size()),"partial, overlapping, final-byte and snapped-input data preserved");ctx->Unmap(read.Get(),0);
  // Restore the exact native unmap/copy/remap sequence, then use the candidate
  // again. This exercises OFF and ON without recreating resources or views.
  memset(map.pData,0x51,128);memset(expected.data(),0x51,128);ctx->Unmap(stage.Get(),0);D3D11_BOX box{0,0,0,128,1,1};ctx->CopySubresourceRegion(gpu.Get(),0,0,0,0,stage.Get(),0,&box);Check(SUCCEEDED(ctx->Map(stage.Get(),0,D3D11_MAP_READ_WRITE,0,&map)),"native OFF remap");
  memset((uint8_t*)map.pData+128,0x62,128);memset(expected.data()+128,0x62,128);upload::Copy(ctx.Get(),gpu.Get(),map.pData,128,128);ctx->CopyResource(read.Get(),gpu.Get());Check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&result)),"toggle result map");Check(!memcmp(result.pData,expected.data(),expected.size()),"OFF/ON preserves all buffer data");ctx->Unmap(read.Get(),0);ctx->Unmap(stage.Get(),0);
 }
 for(bool dynamic:{false,true}){
  upload::Pool pool;Check(pool.Init(dev.Get()),"rotating upload pool");
  std::vector<uint8_t> expected(upload::Capacity,0x29);D3D11_SUBRESOURCE_DATA initial{expected.data(),0,0};
  D3D11_BUFFER_DESC gd{};gd.ByteWidth=upload::Capacity;gd.Usage=dynamic?D3D11_USAGE_DYNAMIC:D3D11_USAGE_DEFAULT;gd.CPUAccessFlags=dynamic?D3D11_CPU_ACCESS_WRITE:0;gd.BindFlags=D3D11_BIND_SHADER_RESOURCE;gd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  ComPtr<ID3D11Buffer> gpu,stage,read;Check(SUCCEEDED(dev->CreateBuffer(&gd,&initial,&gpu)),"raw dynamic/default destination");
  D3D11_BUFFER_DESC sd{};sd.ByteWidth=gd.ByteWidth;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ|D3D11_CPU_ACCESS_WRITE;
  Check(upload::Eligible(gd,sd,0,gd.ByteWidth),"live dynamic descriptor eligible");Check(SUCCEEDED(dev->CreateBuffer(&sd,nullptr,&stage)),"native persistent CPU staging");sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(SUCCEEDED(dev->CreateBuffer(&sd,nullptr,&read)),"pool result buffer");
  D3D11_MAPPED_SUBRESOURCE map{};Check(SUCCEEDED(ctx->Map(stage.Get(),0,D3D11_MAP_READ_WRITE,0,&map)),"native CPU map");
  unsigned fast{},fallback{};
  for(unsigned n=0;n<100;++n){UINT offset=n==99?gd.ByteWidth-1024:(n*256)%65536,bytes=1024;memset((uint8_t*)map.pData+offset,n+7,bytes);memset(expected.data()+offset,n+7,bytes);
   if(n!=50&&pool.Copy(ctx.Get(),gpu.Get(),map.pData,offset,bytes))++fast;
   else{++fallback;ctx->Unmap(stage.Get(),0);D3D11_BOX box{offset,0,0,offset+bytes,1,1};ctx->CopySubresourceRegion(gpu.Get(),0,offset,0,0,stage.Get(),0,&box);Check(SUCCEEDED(ctx->Map(stage.Get(),0,D3D11_MAP_READ_WRITE,0,&map)),"native fallback/OFF remap");}
   memset((uint8_t*)map.pData+offset,0xEF,bytes);
  }
  Check(fast>0&&fallback>0,"candidate and native fallback both executed");Check(pool.Copy(ctx.Get(),gpu.Get(),map.pData,gd.ByteWidth,0),"empty update does not consume upload slot");
  ctx->CopyResource(read.Get(),gpu.Get());D3D11_MAPPED_SUBRESOURCE result{};Check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&result)),"pool GPU result map");Check(!memcmp(result.pData,expected.data(),expected.size()),"100 pool/native updates preserve exact full-buffer data");ctx->Unmap(read.Get(),0);ctx->Unmap(stage.Get(),0);
 }
 printf("Staging upload: %u checks passed on hardware D3D11\n",checks);return 0;
}
