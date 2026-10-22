#pragma once
#include <d3d11.h>
#include <filesystem>
#include <cstdint>
#include "FoveationMask.h"
namespace foveation {
struct Frame {uint32_t inputWidth{},inputHeight{},outputWidth{},outputHeight{},flags{};vrsmask::Fov fovs[2];};
bool Initialize(ID3D11Device*,ID3D11DeviceContext*,const std::filesystem::path& ini);
unsigned Requested();
unsigned Toggle(unsigned bit);
void SetVisibility(const vrsmask::Mesh meshes[2]);
void BeginFrame(const Frame&);
void EndFrame();
void RegisterShader(ID3D11DeviceChild*,const void*,size_t,unsigned stage);
void Report();
bool NativePassMode();
void Disable();
void BeginNativePass();
void EndNativePass();
// Select driver state for the next command. Compatible draws share a batch;
// incompatible draws/compute and EndFrame submit then restore full rate.
struct Command {bool applied{};Command(ID3D11DeviceContext*,bool scene,bool geometry,bool outer=true);};
}
