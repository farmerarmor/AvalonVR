#pragma once
#include <cstdint>

// Select native DLSS, STP or spatial upscaling during the VR render only.
// Restore every changed asset field, including the priority list's GC barrier.
class NativeDLSSScope {
    void* asset{};void** prioritySlot{};void* original{};uintptr_t originalRoot{};
    bool* forcedSlot{};float* percentageSlot{};uint8_t* filterSlot{};
    bool originalForced{};float originalPercentage{};uint8_t originalFilter{};
    uint32_t* injectionSlot{};uint32_t originalInjection{};
    void(*barrier)(void*,void**,void*);void(*freeRoot)(uintptr_t);
public:
    NativeDLSSScope(void* pipeline,bool requested,int upscaler,float fixedPercentage=0,bool afterPost=false);
    bool AppliedAfterPost()const{return injectionSlot!=nullptr;}
    ~NativeDLSSScope();
    NativeDLSSScope(const NativeDLSSScope&)=delete;
    NativeDLSSScope& operator=(const NativeDLSSScope&)=delete;
};
