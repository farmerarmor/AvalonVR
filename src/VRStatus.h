#pragma once
namespace vrstatus {
void Show(const char* title,int inputWidth,int inputHeight,int outputWidth,int outputHeight,bool afterPost,bool restartLifetime=true);
void Tick();
void Message(const char* text);
}
