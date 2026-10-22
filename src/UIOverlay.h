#pragma once
#include <cstdint>
#include "NativeAimUI.h"
namespace ui {
void Render(void* renderContext,void* desktopCamera,uint64_t frameId,const NativeAimUIState& aim,Vec3 target,Quat orientation);
void ResetAfterLoad();
}
