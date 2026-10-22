#include "QuadLOD.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
int main(){
    using Data=std::array<uint8_t,shadowcull::ParametersSize>;
    auto make=[](int height=1670,int ortho=0,float fov=88.50716f){Data p{};for(size_t i=0;i<p.size();++i)p[i]=(uint8_t)(i*37+11);memcpy(p.data(),&ortho,4);memcpy(p.data()+16,&fov,4);memcpy(p.data()+24,&height,4);return p;};
    unsigned checks{};
    for(int original:{836,1670,1908,2624,4530}){
        auto native=make(original),patched=native;int saved{};assert(quadlod::Match(patched.data(),patched.size(),4768,saved));assert(saved==original);
        for(size_t i=0;i<patched.size();++i)if(i<24||i>=28)assert(patched[i]==native[i]);
        int effective{};memcpy(&effective,patched.data()+24,4);assert(effective==4768);
        quadlod::Restore(patched.data(),saved);assert(patched==native);++checks;
    }
    auto refuse=[&](Data p,int height,size_t size){auto before=p;int saved=47;assert(!quadlod::Match(p.data(),size,height,saved));assert(p==before&&saved==47);++checks;};
    refuse(make(),0,1592);refuse(make(),63,1592);refuse(make(),16385,1592);refuse(make(),4768,1591);
    refuse(make(1670,1),4768,1592);refuse(make(1670,0,0),4768,1592);refuse(make(1670,0,180),4768,1592);
    refuse(make(1670,0,std::numeric_limits<float>::quiet_NaN()),4768,1592);
    refuse(make(0),4768,1592);refuse(make(16385),4768,1592);refuse(make(4768),4768,1592);
    int saved{};assert(!quadlod::Match(nullptr,1592,4768,saved));++checks;
    printf("Quad LOD: %u checks pass; only native pixel-height bytes change, complete restoration, identity/FOV/position/masks preserved, unsupported inputs refused\n",checks);
}
