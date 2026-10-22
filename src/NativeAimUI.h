#pragma once
#include "VRMath.h"
#include <cstdint>
struct NativeAimUIState {bool visible{};Vec3 worldPoint{};float pixelX{},pixelY{},minX{},minY{},maxX{},maxY{};};
namespace aimui {
NativeAimUIState Prepare(void* camera,void* hero);
bool HideCrosshair();
void RestoreCrosshair();
}
