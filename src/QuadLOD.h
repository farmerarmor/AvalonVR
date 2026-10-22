#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "ShadowCullingData.h"
namespace quadlod {
// Exact-build LODParameters at ScriptableCullingParameters offset0. Caller
// validates IL2CPP layout, camera identity, four-view mode and owning thread.
inline constexpr size_t PixelHeightOffset=24;
inline bool Match(void* parameters,size_t bytes,int height,int& original){
    if(!parameters||bytes!=shadowcull::ParametersSize||height<64||height>16384)return false;
    int ortho{},current{};float fov{};memcpy(&ortho,parameters,4);memcpy(&fov,(char*)parameters+16,4);memcpy(&current,(char*)parameters+24,4);
    if(ortho||!std::isfinite(fov)||fov<=1||fov>=179||current<1||current>16384||current==height)return false;
    original=current;memcpy((char*)parameters+PixelHeightOffset,&height,4);return true;
}
inline void Restore(void* parameters,int original){memcpy((char*)parameters+PixelHeightOffset,&original,4);}
}
