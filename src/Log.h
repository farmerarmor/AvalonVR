#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <filesystem>
inline std::filesystem::path modulePath;
inline std::mutex logMutex;
inline void Log(const char* format, ...) {
    std::lock_guard lock(logMutex);
    FILE* f{};
    _wfopen_s(&f, (modulePath.parent_path()/L"AvalonVR.log").c_str(), L"a");
    if (!f) return;
    fprintf(f,"[%llu tid=%lu] ",GetTickCount64(),GetCurrentThreadId());
    va_list args; va_start(args,format); vfprintf(f,format,args); va_end(args);
    fputc('\n',f); fclose(f);
}
