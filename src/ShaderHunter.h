#pragma once
#include <d3d11.h>
#include <filesystem>
#include <string>
#include <cstdint>
namespace hunter {
// Context command and creation coverage is independent of Unity metadata.
bool Install(ID3D11Device*,ID3D11DeviceContext*,const std::filesystem::path&);
void Tick(bool gameplay);
std::string Label();
bool CaptureRequested();
// Private F4 diagnostic: two native pairs, at most 32 read-only candidate records.
void RequestShadowConsumers();
// Bounded read-only inputs for a shader the user already marked; no suppression.
void CaptureNativeInputs(uint64_t hash,unsigned stage,const std::filesystem::path& output,unsigned samples=8);
struct FrameInfo {uint64_t pair{};float poses[2][7]{},fovs[2][4]{},gamePositions[2][3]{},projections[2][16]{};uint32_t inputWidth{},inputHeight{},outputWidth{},outputHeight{},vrsFlags{};};
void QueueFrame(const FrameInfo&);
void BeginFrame(uint32_t);
void EndScene();
void Eye(int);
void Poll(ID3D11DeviceContext*);
// Suppress the mod's own transfers/overlay from catalog and hunting.
struct Bypass {Bypass();~Bypass();};
// Shared with the standalone validation; the application uses keyboard edges.
void Control(unsigned key);
std::string StatusJSON();
}
