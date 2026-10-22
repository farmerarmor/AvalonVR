#pragma once
#include <cmath>
struct Vec3 { float x{},y{},z{}; };
struct Quat { float x{},y{},z{},w{1}; };
struct Matrix { float m[16]{}; };
inline Vec3 operator+(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 operator-(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Quat Mul(Quat a,Quat b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
inline Quat Inv(Quat q){return {-q.x,-q.y,-q.z,q.w};}
inline Vec3 Rotate(Quat q,Vec3 v){auto r=Mul(Mul(q,{v.x,v.y,v.z,0}),Inv(q));return {r.x,r.y,r.z};}
inline Quat Yaw(Quat q){auto f=Rotate(q,{0,0,1});float a=std::atan2(f.x,f.z)*.5f;return {0,std::sin(a),0,std::cos(a)};}
inline Vec3 GamePointToXR(Vec3 point,Vec3 gameCamera,Quat heading,Vec3 recenterOrigin){auto local=recenterOrigin+Rotate(Inv(heading),point-gameCamera);return {local.x,local.y,-local.z};}
inline Matrix Projection(float left,float right,float up,float down,float nearZ,float farZ){
    float l=std::tan(left),r=std::tan(right),t=std::tan(up),b=std::tan(down);
    Matrix p{};p.m[0]=2/(r-l);p.m[5]=2/(t-b);p.m[8]=(r+l)/(r-l);p.m[9]=(t+b)/(t-b);
    p.m[10]=-(farZ+nearZ)/(farZ-nearZ);p.m[11]=-1;p.m[14]=-2*farZ*nearZ/(farZ-nearZ);return p;
}
