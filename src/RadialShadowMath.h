#pragma once
#include "VRMath.h"
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace radialshadow {
struct Plane {float x,y,z,w;};
struct Split {int planeCount;Plane planes[10];float sphere[4];float blend,nearPlane;Matrix matrix;};
static_assert(sizeof(Split)==252&&offsetof(Split,sphere)==164&&offsetof(Split,matrix)==188);
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 Row(const Matrix& m,int row){return {m.m[row],m.m[row+4],m.m[row+8]};}
inline Matrix Product(const Matrix& a,const Matrix& b){Matrix result{};for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)result.m[c*4+r]+=a.m[k*4+r]*b.m[c*4+k];return result;}
inline bool Finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline bool ReceiverBounds(void* parameters,size_t bytes,Vec3 center,float distance){
 if(!parameters||bytes!=1592||!Finite(center)||!std::isfinite(distance)||distance<=.03f||distance>10000)return false;
 Plane cube[]={{1,0,0,distance-center.x},{-1,0,0,distance+center.x},
               {0,1,0,distance-center.y},{0,-1,0,distance+center.y},
               {0,0,1,distance-center.z},{0,0,-1,distance+center.z}};
 auto dst=static_cast<unsigned char*>(parameters);memcpy(dst+440+628,cube,sizeof(cube));memcpy(dst+440+824,&center,sizeof(center));return true;
}
inline bool Fit(Matrix& view,Matrix& projection,Split& split,Vec3 center,float distance,Vec3 ratios,int cascade,int count,int resolution){
 if(!Finite(center)||!Finite(ratios)||!std::isfinite(distance)||distance<=.03f||distance>10000||count<1||count>4||cascade<0||cascade>=count||resolution<64||resolution>16384)return false;
 float fractions[]={ratios.x,ratios.y,ratios.z,1};float previous=0;
 for(int i=0;i<count-1;++i){if(fractions[i]<=previous||fractions[i]>=1||distance*fractions[i]<=.01f)return false;previous=fractions[i];}
 const float nativeRadius=split.sphere[3],radius=distance*(cascade==count-1?1:fractions[cascade]);
 if(!std::isfinite(nativeRadius)||nativeRadius<=0||radius<=.01f||!std::isfinite(split.nearPlane)||!std::isfinite(split.blend)||split.blend<0||split.blend>1)return false;
 for(auto value:view.m)if(!std::isfinite(value))return false;for(auto value:projection.m)if(!std::isfinite(value))return false;
 if(std::abs(view.m[15]-1)>.001f||std::abs(projection.m[15]-1)>.001f||std::abs(projection.m[3])+std::abs(projection.m[7])+std::abs(projection.m[11])>.0001f||projection.m[0]<=0||projection.m[5]<=0||projection.m[10]>=0)return false;
 const auto x=Row(view,0),y=Row(view,1),z=Row(view,2);
 if(std::abs(Dot(x,x)-1)>.002f||std::abs(Dot(y,y)-1)>.002f||std::abs(Dot(z,z)-1)>.002f||std::abs(Dot(x,y))>.002f||std::abs(Dot(x,z))>.002f||std::abs(Dot(y,z))>.002f)return false;
 // Keep the native filter guard band, expressed as a fraction of the sphere.
 float guardX=1/(projection.m[0]*nativeRadius),guardY=1/(projection.m[5]*nativeRadius);
 if(guardX<1||guardX>2||guardY<1||guardY>2)return false;
 Matrix v=view,p=projection;Split output=split;
 const float halfX=radius*guardX,halfY=radius*guardY,texelX=2*halfX/resolution,texelY=2*halfY/resolution;
 v.m[12]=-std::round(Dot(x,center)/texelX)*texelX;v.m[13]=-std::round(Dot(y,center)/texelY)*texelY;
 // Preserve the native light-space caster depth interval; only extend it when
 // the centered receiver sphere needs more space. Raster and shader matrices
 // are both derived from these same outputs by the original HDRP code.
 const float nativeNear=(p.m[14]+1)/p.m[10],nativeFar=(p.m[14]-1)/p.m[10];
 const float depth=-Dot(z,center)-v.m[14],guardZ=radius*std::max(guardX,guardY);
 const float nearPlane=std::min(nativeNear,depth-guardZ),farPlane=std::max(nativeFar,depth+guardZ);
 if(!std::isfinite(nearPlane)||!std::isfinite(farPlane)||farPlane-nearPlane<.01f)return false;
 p.m[0]=1/halfX;p.m[5]=1/halfY;p.m[12]=p.m[13]=0;p.m[10]=-2/(farPlane-nearPlane);p.m[14]=-(farPlane+nearPlane)/(farPlane-nearPlane);
 output.matrix=Product(p,v);output.planeCount=6;
 // Native culling matrices use the unconverted -1..1 projection convention.
 for(int plane=0;plane<6;++plane){int axis=plane/2;float sign=(plane&1)?-1.0f:1.0f;Plane q{};float* dst=&q.x;for(int col=0;col<4;++col)dst[col]=output.matrix.m[col*4+3]+sign*output.matrix.m[col*4+axis];float length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z);if(length<1e-6f)return false;q.x/=length;q.y/=length;q.z/=length;q.w/=length;output.planes[plane]=q;}
 output.sphere[0]=center.x;output.sphere[1]=center.y;output.sphere[2]=center.z;output.sphere[3]=radius;
 output.nearPlane=split.nearPlane*(radius/nativeRadius);
 if(!std::isfinite(output.nearPlane))return false;
 view=v;projection=p;split=output;return true;
}
}
