#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Fixed quad views: outer L/R cover the entire eye, inner L/R are actual
// narrower projections at the same eye poses. All coordinates here are upright.
namespace quadview {
struct Fov {float left{},right{},up{},down{};};
struct Rect {float left{},top{},right{},bottom{};};
struct Focus {Fov fov{};Rect rect{};bool valid{};};
struct Size {uint32_t width{},height{};};
inline bool Valid(Fov f){
    return std::isfinite(f.left)&&std::isfinite(f.right)&&std::isfinite(f.up)&&std::isfinite(f.down)&&
        f.left> -1.56f&&f.right<1.56f&&f.down> -1.56f&&f.up<1.56f&&f.left<f.right&&f.down<f.up;
}
inline Focus MakeFocus(Fov outer,float horizontal,float vertical){
    if(!Valid(outer)||!std::isfinite(horizontal)||!std::isfinite(vertical)||horizontal<.1f-1e-6f||horizontal>.95f+1e-6f||vertical<.1f-1e-6f||vertical>.95f+1e-6f)return {};
    horizontal=std::clamp(horizontal,.1f,.95f);vertical=std::clamp(vertical,.1f,.95f);
    float l=std::tan(outer.left),r=std::tan(outer.right),t=std::tan(outer.up),b=std::tan(outer.down);
    float w=(r-l)*horizontal,h=(t-b)*vertical;
    // Center on the optical axis where possible. Clamp the whole inset into
    // asymmetric outer frusta rather than silently clipping one of its edges.
    float x=std::clamp(-w*.5f,l,r-w),y=std::clamp(-h*.5f,b,t-h);
    return {{std::atan(x),std::atan(x+w),std::atan(y+h),std::atan(y)},
        {(x-l)/(r-l),(t-y-h)/(t-b),(x+w-l)/(r-l),(t-y)/(t-b)},true};
}
inline uint32_t Even(float n){return std::max(32u,((uint32_t)std::ceil(n)+1u)&~1u);}
inline Size Peripheral(Size output,int percent){float s=std::clamp(percent,20,100)*.01f;return {Even(output.width*s),Even(output.height*s)};}
inline Size Inset(Size output,int horizontalPercent,int verticalPercent){return {Even(output.width*std::clamp(horizontalPercent,10,95)*.01f),Even(output.height*std::clamp(verticalPercent,10,95)*.01f)};}
inline float Weight(Rect r,float u,float v,float feather){
    float x=(u-r.left)/(r.right-r.left),y=(v-r.top)/(r.bottom-r.top);
    float d=std::min({x,1-x,y,1-y});if(d<=0)return 0;
    float a=std::clamp(d/std::clamp(feather,.001f,.49f),0.f,1.f);return a*a*(3-2*a);
}
}
