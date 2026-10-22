#pragma once
#include "VRMath.h"
namespace shadowcull {
bool Install();
void ConfigureRadial(Vec3 center,bool enabled,int resolutionFloor);
void StopRadial();
bool RadialActive();
bool Prepare(void* commonCamera,void* leftCamera,void* rightCamera,bool enabled);
bool PrepareViews(void* commonCamera,void* const* cameras,unsigned count,bool enabled);
void End();
}
