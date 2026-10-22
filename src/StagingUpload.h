#pragma once
#include <d3d11.h>
#include <cstdint>
namespace upload {
inline constexpr UINT Capacity=4*1024*1024;
inline bool Eligible(const D3D11_BUFFER_DESC& gpu,const D3D11_BUFFER_DESC& staging,uint64_t offset,uint64_t bytes){
 bool supportedGPU=(gpu.Usage==D3D11_USAGE_DEFAULT&&gpu.CPUAccessFlags==0)||(gpu.Usage==D3D11_USAGE_DYNAMIC&&gpu.CPUAccessFlags==D3D11_CPU_ACCESS_WRITE&&!(gpu.BindFlags&D3D11_BIND_UNORDERED_ACCESS));
 return gpu.ByteWidth==Capacity&&staging.ByteWidth==gpu.ByteWidth&&supportedGPU&&
  (gpu.BindFlags&D3D11_BIND_SHADER_RESOURCE)&&!(gpu.BindFlags&(D3D11_BIND_CONSTANT_BUFFER|D3D11_BIND_DEPTH_STENCIL))&&
  staging.Usage==D3D11_USAGE_STAGING&&staging.BindFlags==0&&staging.CPUAccessFlags==(D3D11_CPU_ACCESS_READ|D3D11_CPU_ACCESS_WRITE)&&
  offset<=gpu.ByteWidth&&bytes<=gpu.ByteWidth-offset;
}
inline void Copy(ID3D11DeviceContext* context,ID3D11Buffer* gpu,const void* mappedBase,UINT offset,UINT bytes){
 if(!bytes)return;
 D3D11_BOX box{offset,0,0,offset+bytes,1,1};
 context->UpdateSubresource(gpu,0,&box,(const uint8_t*)mappedBase+offset,0,0);
}
}
