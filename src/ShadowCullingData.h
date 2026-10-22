#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
namespace shadowcull {
// Verified from this build's IL2CPP value layouts, excluding boxed headers.
inline constexpr size_t ParametersSize=1592,PropertiesOffset=440,PropertiesSize=996;
inline bool CopyProperties(void* parameters,size_t bytes,const void* shared,size_t sharedBytes){
    if(!parameters||!shared||bytes!=ParametersSize||sharedBytes!=ParametersSize)return false;
    auto dst=static_cast<uint8_t*>(parameters)+PropertiesOffset;
    auto src=static_cast<const uint8_t*>(shared)+PropertiesOffset;
    uint8_t original[PropertiesSize];memcpy(original,dst,PropertiesSize);memcpy(dst,src,PropertiesSize);
    // Keep per-camera identity/filtering and bookkeeping. Only culling camera
    // geometry is shared; the render camera and its native matrices stay intact.
    struct Range{size_t offset,bytes;};
    for(auto r:{Range{624,4},Range{968,12},Range{980,4},Range{992,1}})memcpy(dst+r.offset,original+r.offset,r.bytes);
    return true;
}
}
