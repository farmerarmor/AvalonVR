#pragma once
#include <cstdint>

class HunterRepeat {
    int held{};uint64_t next{};
public:
    static constexpr uint64_t IntervalMs=200;
    void Reset(){held=0;next=0;}
    int Step(bool up,bool down,bool active,uint64_t now){
        const int direction=!active||up==down?0:down?1:-1;
        if(!direction){Reset();return 0;}
        if(direction!=held){held=direction;next=now+IntervalMs;return direction;}
        if(now<next)return 0;
        // Skip missed intervals after a slow frame; each visible step gets time.
        next=now+IntervalMs;return direction;
    }
};
