#pragma once
#include "VRMath.h"
#include <algorithm>
namespace stereocull {
struct Shared {Matrix matrix{},projection{};Vec3 position{};Quat rotation{};float nearClip{},farClip{},backwardShift{},horizontalTangent{},verticalTangent{};bool valid{};};
inline Vec3 Scale(Vec3 p,float s){return {p.x*s,p.y*s,p.z*s};}
inline Quat Normalize(Quat q){float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return n>1e-6f?Quat{q.x/n,q.y/n,q.z/n,q.w/n}:Quat{};}
inline Matrix Product(const Matrix& a,const Matrix& b){Matrix result{};for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)result.m[c*4+r]+=a.m[k*4+r]*b.m[c*4+k];return result;}
inline Matrix View(Vec3 position,Quat rotation){Matrix m{};auto inv=Inv(rotation);for(int c=0;c<3;++c){Vec3 basis{c==0?1.0f:0.0f,c==1?1.0f:0.0f,c==2?1.0f:0.0f};auto v=Rotate(inv,basis);m.m[c*4]=v.x;m.m[c*4+1]=v.y;m.m[c*4+2]=-v.z;}auto t=Rotate(inv,Scale(position,-1));m.m[12]=t.x;m.m[13]=t.y;m.m[14]=-t.z;m.m[15]=1;return m;}
// A shared symmetric CPU frustum encloses both native eye frusta. Moving its
// apex slightly backward includes IPD without widening the far shadow fit.
// The actual per-eye view/projection matrices and render targets are untouched.
inline Shared Build(const Vec3 positions[2],const Quat rotations[2],const float fovs[2][4],float nearClip,float farClip){
 Shared result;auto second=rotations[1];auto first=rotations[0];if(first.x*second.x+first.y*second.y+first.z*second.z+first.w*second.w<0)second={-second.x,-second.y,-second.z,-second.w};auto q=Normalize({first.x+second.x,first.y+second.y,first.z+second.z,first.w+second.w});auto inv=Inv(q);auto center=Scale(positions[0]+positions[1],.5f);Vec3 offsets[2],rays[2][4];
 for(int eye=0;eye<2;++eye){offsets[eye]=Rotate(inv,positions[eye]-center);for(int corner=0;corner<4;++corner){auto ray=Rotate(inv,Rotate(rotations[eye],{std::tan(fovs[eye][corner&1]),std::tan(fovs[eye][(corner&2)?2:3]),1}));if(ray.z<=.001f)return result;rays[eye][corner]=ray;result.horizontalTangent=std::max(result.horizontalTangent,std::abs(ray.x/ray.z));result.verticalTangent=std::max(result.verticalTangent,std::abs(ray.y/ray.z));}}
 if(result.horizontalTangent<=.001f||result.verticalTangent<=.001f)return result;
 float farPlane=farClip,minimumDepth=farClip;
 for(auto offset:offsets)result.backwardShift=std::max({result.backwardShift,std::abs(offset.x)/result.horizontalTangent-offset.z,std::abs(offset.y)/result.verticalTangent-offset.z});
 result.backwardShift+=.001f;
 for(int eye=0;eye<2;++eye)for(auto ray:rays[eye]){minimumDepth=std::min(minimumDepth,offsets[eye].z+nearClip*ray.z+result.backwardShift);farPlane=std::max(farPlane,offsets[eye].z+farClip*ray.z+result.backwardShift+1);}
 if(minimumDepth<=.001f)return result;
 float n=std::min(nearClip,minimumDepth*.5f),h=std::atan(result.horizontalTangent),v=std::atan(result.verticalTangent);result.position=center-Rotate(q,{0,0,result.backwardShift});result.rotation=q;result.nearClip=n;result.farClip=farPlane;result.projection=Projection(-h,h,v,-v,n,farPlane);result.matrix=Product(result.projection,View(result.position,q));result.valid=true;return result;
}
}
