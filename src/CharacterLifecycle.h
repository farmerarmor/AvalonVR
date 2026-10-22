#pragma once
#include <cstdint>
namespace characterlife {
bool Install();
uint64_t Observed();
uint64_t Queued();
struct State {bool available{};uint32_t registered{},destroyedRegistered{},pendingUnregister{};};
State Inspect(void* manager);
}
