#pragma once
#include <cstdint>

namespace perf {
// The quality values are the exact build's UnityEngine.NVIDIA.DLSSQuality enum.
inline constexpr uint32_t PresetCount=7;
inline constexpr uint32_t ModeCount=(PresetCount+1)*512;
inline uint32_t Preset(bool nativeDLSS,uint32_t quality){
    if(!nativeDLSS)return 5;
    return quality==2?1:quality==1?2:quality==0?3:4;
}
inline uint32_t Quality(uint32_t preset){return preset==1?2:preset==2?1:(preset==3||preset==7)?0:3;}
inline bool UsesDLSS(uint32_t preset){return (preset>=1&&preset<=4)||preset==7;}
inline bool FixedScale(uint32_t preset){return preset>=5&&preset<=7;}
inline int Upscaler(uint32_t preset){return preset==6?2:UsesDLSS(preset)?0:-1;}
inline const char* Name(uint32_t preset){
    switch(preset){case 0:return "FullResolution";case 1:return "DLSSQuality";
    case 2:return "DLSSBalanced";case 3:return "DLSSPerformance";
    case 4:return "DLSSUltraPerformance";case 5:return "Spatial40Percent";
    case 6:return "STP40Percent";case 7:return "DLSS40Percent";default:return "Invalid";}
}
inline const char* DisplayName(uint32_t preset){
    switch(preset){case 1:return "DLSS Quality";case 2:return "DLSS Balanced";case 3:return "DLSS Performance";
    case 4:return "DLSS Ultra Performance";case 5:return "Spatial 40%";case 6:return "STP 40%";case 7:return "DLSS fixed 40%";default:return "Full resolution";}
}
}
