#pragma once
#include <d3d11.h>
namespace bufferuploads {
bool Install(ID3D11Device* device,ID3D11DeviceContext* context);
void Report();
}
