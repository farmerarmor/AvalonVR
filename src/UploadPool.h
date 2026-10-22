#pragma once
#include "StagingUpload.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstring>
namespace upload {
class Pool {
 struct Slot {Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;Microsoft::WRL::ComPtr<ID3D11Query> done;bool pending{};};
 std::array<Slot,8> slots;unsigned next{};
public:
 std::atomic<uint64_t> successes{},busy{},mapDeclines{};
 bool Init(ID3D11Device* device){
  D3D11_BUFFER_DESC d{};d.ByteWidth=Capacity;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
  for(auto& s:slots)if(FAILED(device->CreateBuffer(&d,nullptr,&s.buffer))||FAILED(device->CreateQuery(&q,&s.done)))return false;
  return true;
 }
 bool Copy(ID3D11DeviceContext* context,ID3D11Buffer* gpu,const void* base,UINT offset,UINT bytes){
  if(!context||!gpu||!base||offset>Capacity||bytes>Capacity-offset)return false;
  if(!bytes)return true;
  for(unsigned n=0;n<slots.size();++n){unsigned index=(next+n)%slots.size();auto& s=slots[index];
   if(s.pending){BOOL done{};if(context->GetData(s.done.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK||!done)continue;s.pending=false;}
   D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(context->Map(s.buffer.Get(),0,D3D11_MAP_WRITE,D3D11_MAP_FLAG_DO_NOT_WAIT,&map))){++mapDeclines;continue;}
   memcpy((uint8_t*)map.pData+offset,(const uint8_t*)base+offset,bytes);context->Unmap(s.buffer.Get(),0);
   D3D11_BOX region{offset,0,0,offset+bytes,1,1};context->CopySubresourceRegion(gpu,0,offset,0,0,s.buffer.Get(),0,&region);context->End(s.done.Get());s.pending=true;next=(index+1)%slots.size();++successes;return true;
  }
  ++busy;return false;
 }
};
}
