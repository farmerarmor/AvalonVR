#pragma once
namespace quadfog {
bool Install();bool Enabled();bool Toggle();
void Begin(void* const* cameras,unsigned count,bool quad,float density);
void End();void Report();
}
