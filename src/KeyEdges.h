#pragma once
#include <array>
#include <cstdint>

class KeyEdges {
    std::array<bool,256> held{};
public:
    bool Pressed(uint32_t key,uint16_t snapshot){
        if(key>=held.size())return false;
        bool down=(snapshot&0x8000u)!=0,pressed=down&&!held[key];held[key]=down;return pressed;
    }
};
