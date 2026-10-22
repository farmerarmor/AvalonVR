#pragma once
#include <cstdint>
#include <cstddef>
namespace characterlife {
constexpr uint32_t SlotMask=0x1fffffffu;
inline bool ShouldQueue(uint32_t id,size_t slots,bool owned,bool fullyRegistered,bool queued){
 return !(id&0xc0000000u)&&(id&SlotMask)<slots&&owned&&fullyRegistered&&!queued;
}
struct Mask {const uint64_t* data;uint64_t allocation;uint32_t length,allocator;};
static_assert(sizeof(Mask)==24);
inline bool Has(const Mask& mask,uint32_t slot){return mask.data&&mask.length<=100000&&slot<mask.length&&((mask.data[slot/64]>>(slot%64))&1)!=0;}
}
