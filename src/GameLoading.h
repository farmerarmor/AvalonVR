#pragma once
namespace loading {
struct State {bool available{},active{};};
bool Initialize();
State Read();
}
