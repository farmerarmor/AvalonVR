#pragma once
#include "VRMath.h"
namespace shadowres {
// Main-thread, process-local sun-light override; restore when the experiment ends.
bool Update(bool enabled,int observedResolution,int minimum,Vec3 observedLightZ);
}
