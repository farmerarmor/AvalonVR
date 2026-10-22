#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace quadsampling {
// The focus crop retains full-eye angular pixel density. A peripheral pixel
// spans 1/scale as much texture, so log2(scale) cancels its extra mip LOD.
// Use the denser axis when even target rounding makes the scales unequal.
inline float PeripheralBias(uint32_t width,uint32_t height,uint32_t fullWidth,uint32_t fullHeight){
    if(width<32||height<32||fullWidth<32||fullHeight<32||width>16384||height>16384||fullWidth>16384||fullHeight>16384)return 0;
    float scale=std::max(float(width)/fullWidth,float(height)/fullHeight);
    return scale>0&&scale<1?std::max(-4.f,std::log2(scale)):0.f;
}
}
