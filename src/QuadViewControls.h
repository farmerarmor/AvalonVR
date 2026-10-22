#pragma once
#include <algorithm>
#include <cstdint>
namespace quadview {
enum class Tune {None,SizeUp,SizeDown,Stronger,Weaker};
struct Settings {
    int horizontal=50,vertical=50,peripheral=40;
    bool operator==(const Settings&)const=default;
};
inline Settings Clamp(Settings s){return {std::clamp(s.horizontal,10,95),std::clamp(s.vertical,10,95),std::clamp(s.peripheral,20,100)};}
inline Settings Adjust(Settings s,Tune action){
    s=Clamp(s);
    if(action==Tune::SizeUp){s.horizontal+=5;s.vertical+=5;}
    if(action==Tune::SizeDown){s.horizontal-=5;s.vertical-=5;}
    if(action==Tune::Stronger)s.peripheral-=5;
    if(action==Tune::Weaker)s.peripheral+=5;
    return Clamp(s);
}
class TuningKeys {
    Tune held=Tune::None;uint64_t next{};
public:
    Tune Update(uint64_t now,bool plus,bool minus,bool control,bool shift,bool focused){
        Tune action=Tune::None;
        if(focused&&plus!=minus){
            // Ctrl wins when Shift is needed to type '+' on the main keyboard.
            if(control)action=plus?Tune::SizeUp:Tune::SizeDown;
            else if(shift)action=plus?Tune::Stronger:Tune::Weaker;
        }
        if(action==Tune::None){held=action;next=0;return action;}
        if(action!=held){held=action;next=now+350;return action;}
        if(now>=next){next=now+200;return action;}
        return Tune::None;
    }
};
}
