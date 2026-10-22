#include "RadialShadowMath.h"
#include "StereoCulling.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <span>

static float PlaneDistance(radialshadow::Plane p,Vec3 v){return p.x*v.x+p.y*v.y+p.z*v.z+p.w;}
int main(){
 constexpr float range=33.384617f;Vec3 center{42,-4,71},ratios{.05f,.15f,.3f};
 std::array<unsigned char,1592> bytes;bytes.fill(0xa5);const auto saved=bytes;
 assert(radialshadow::ReceiverBounds(bytes.data(),bytes.size(),center,range));
 for(size_t i=0;i<bytes.size();++i)if(!(i>=1068&&i<1164)&&!(i>=1264&&i<1276))assert(bytes[i]==saved[i]);
 radialshadow::Plane cube[6];memcpy(cube,bytes.data()+1068,sizeof(cube));
 for(float x:{-1.0f,1.0f})for(float y:{-1.0f,1.0f})for(float z:{-1.0f,1.0f}){auto corner=center+Vec3{x*range,y*range,z*range};for(auto p:cube)assert(PlaneDistance(p,corner)>-.0001f);}
 bytes=saved;assert(!radialshadow::ReceiverBounds(bytes.data(),1591,center,range));assert(bytes==saved);assert(!radialshadow::ReceiverBounds(bytes.data(),bytes.size(),center,-1));assert(bytes==saved);
 auto lightRotation=stereocull::Normalize({.3f,.2f,.1f,.9f});
 Matrix reference[4]{};bool first=true;unsigned points{};
 for(float yaw:{-3.14f,-2.1f,-1.2f,0.0f,.8f,1.9f,3.14f}){
   // Native view-fitted centers move with either camera direction. The radial
   // replacement must keep world XY coverage and receiver selection invariant.
   auto nativeCenter=center+Rotate({0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},{0,2,12});
   for(int cascade=0;cascade<4;++cascade){
     auto view=stereocull::View(nativeCenter+Rotate(lightRotation,{0,0,30}),lightRotation);
     float nativeRadius[]={1.3f,3.9f,7.8f,26};Matrix projection{};projection.m[0]=projection.m[5]=1/(nativeRadius[cascade]*1.02f);projection.m[10]=-2.0f/260;projection.m[14]=140.0f/260;projection.m[15]=1;
     radialshadow::Split split{};split.sphere[0]=nativeCenter.x;split.sphere[1]=nativeCenter.y;split.sphere[2]=nativeCenter.z;split.sphere[3]=nativeRadius[cascade];split.blend=.6f;split.nearPlane=nativeRadius[cascade]*.15f;
     const auto originalView=view,originalProjection=projection;const auto originalSplit=split;
     assert(radialshadow::Fit(view,projection,split,center,range,ratios,cascade,4,1024));
     float fractions[]={.05f,.15f,.3f,1};const float radius=range*fractions[cascade];assert(std::abs(split.sphere[3]-radius)<1e-5f);assert(split.sphere[0]==center.x&&split.sphere[1]==center.y&&split.sphere[2]==center.z);assert(split.planeCount==6&&split.blend==originalSplit.blend);
     if(first)reference[cascade]=split.matrix;else for(int col=0;col<4;++col)for(int row=0;row<2;++row)assert(std::abs(split.matrix.m[col*4+row]-reference[cascade].m[col*4+row])<.0001f);
     const float newNear=(projection.m[14]+1)/projection.m[10],newFar=(projection.m[14]-1)/projection.m[10];assert(newNear<=-199.999f&&newFar>=59.999f);
     // Every receiver direction stays inside raster and caster planes, including
     // behind the original native view and above it.
     for(int azimuth=0;azimuth<360;azimuth+=30)for(int elevation=-90;elevation<=90;elevation+=30){float a=azimuth*.01745329252f,b=elevation*.01745329252f;auto point=center+Vec3{std::cos(b)*std::cos(a)*radius,std::sin(b)*radius,std::cos(b)*std::sin(a)*radius};
       for(auto p:std::span(split.planes,split.planeCount))assert(PlaneDistance(p,point)>-.001f);
       for(int row=0;row<3;++row){float clip=split.matrix.m[row]*point.x+split.matrix.m[row+4]*point.y+split.matrix.m[row+8]*point.z+split.matrix.m[row+12];assert(std::abs(clip)<=1.0001f);}++points;
       // The shader's fade-side dot is nonpositive for both eye positions when
       // sphere and map center follow their mean, independent of head/aim yaw.
       auto delta=point-center;float length=std::sqrt(radialshadow::Dot(delta,delta));auto unit=Vec3{delta.x/length,delta.y/length,delta.z/length};
       for(float ipd:{-.032f,.032f}){auto eye=center+Rotate({0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},{ipd,0,0});assert(radialshadow::Dot(eye-point,unit)<0);}
     }
     // Invalid inputs must not partially replace native outputs.
     auto invalidView=originalView,invalidProjection=originalProjection;auto invalidSplit=originalSplit;
     assert(!radialshadow::Fit(invalidView,invalidProjection,invalidSplit,center,range,{.2f,.1f,.3f},cascade,4,1024));
     assert(!memcmp(&invalidView,&originalView,sizeof(Matrix))&&!memcmp(&invalidProjection,&originalProjection,sizeof(Matrix))&&!memcmp(&invalidSplit,&originalSplit,sizeof(split)));
   }first=false;
 }
 printf("PASS: radial shadow direction invariance, %u receiver points, caster planes, depth coverage, two-eye fade side, and transactional layout checks.\n",points);
}
