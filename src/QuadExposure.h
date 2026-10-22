#pragma once
namespace quadexposure {
bool Install();
bool Enabled();bool Toggle();
void Begin(void* const* cameras,unsigned count,bool quad);
void End();void Report();
}
