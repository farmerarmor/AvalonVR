#pragma once
#include <d3d11.h>
#include <dxgi.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "VRMath.h"
#include "QuadViewMath.h"
#include "QuadViewControls.h"
#include <cstdint>
namespace xr {
struct Frame { uint64_t id{}; XrTime time{}; XrView views[4]{{XR_TYPE_VIEW},{XR_TYPE_VIEW},{XR_TYPE_VIEW},{XR_TYPE_VIEW}}; bool render{},quad{}; uint32_t performanceMode{},viewCount=2; quadview::Rect focus[2]{}; };
bool InstallPresentHook();
bool Begin(Frame& frame);
void PrepareSources(ID3D11Texture2D* const* textures,uint32_t count);
void Publish(ID3D11Texture2D* left,ID3D11Texture2D* right,uint64_t frameId,uint32_t eyeMask);
void PublishUI(ID3D11Texture2D* texture,uint64_t frameId);
void PublishAimUI(ID3D11Texture2D* full,uint64_t frameId,float pixelX,float pixelY,Vec3 target,Quat orientation,int logicalWidth,float minX,float minY,float maxX,float maxY);
void SetMenuUIPlacement(bool menu,uint64_t frameId,Vec3 position,Quat orientation);
void ReanchorMenus();
void RequestCapture();
uint32_t Width(); uint32_t Height();
uint32_t RenderWidth(unsigned camera);uint32_t RenderHeight(unsigned camera);
bool QuadViews();bool ToggleQuadViews();bool ToggleQuadDebug();
bool TuneQuad(quadview::Tune action);quadview::Settings QuadSettings();
void SaveQuadSettings();void QuadTargetsFailed();
bool IsReady();
bool BatchTransfers();
bool ToggleBatchTransfers();
void SetMenuVisible(bool visible);
void MarkRendered(uint32_t frameId);
void MarkRenderStart(uint32_t frameId);
void MarkSceneEnd(uint32_t frameId);
}
