#include "VRMath.h"
#include "KeyEdges.h"
#include "StereoCulling.h"
#include "ShadowCullingData.h"
#include <array>
#include <cassert>
#include <cstdio>
static bool Near(float a,float b){return std::abs(a-b)<1e-4f;}
int main(){
    Quat yaw{0,std::sin(.5f),0,std::cos(.5f)};
    Vec3 v{.2f,.3f,.4f};auto back=Rotate(Inv(yaw),Rotate(yaw,v));assert(Near(back.x,v.x)&&Near(back.y,v.y)&&Near(back.z,v.z));
    auto head=Mul(yaw,Inv(yaw));assert(Near(head.w,1)&&Near(head.y,0));
    float l=-.9f,r=.7f,u=.8f,d=-.6f,n=.05f,f=1000;
    auto p=Projection(l,r,u,d,n,f);
    auto ndc=[&](float x,float y,float z){float w=-z;return Vec3{(p.m[0]*x+p.m[8]*z)/w,(p.m[5]*y+p.m[9]*z)/w,(p.m[10]*z+p.m[14])/w};};
    assert(Near(ndc(n*std::tan(l),0,-n).x,-1));assert(Near(ndc(n*std::tan(r),0,-n).x,1));
    assert(Near(ndc(0,n*std::tan(u),-n).y,1));assert(Near(ndc(0,n*std::tan(d),-n).y,-1));
    assert(Near(ndc(0,0,-n).z,-1));assert(Near(ndc(0,0,-f).z,1));
    // Stage31 captured a .03 projection and .01 HDRP depth parameters. Check
    // depth reconstruction over the actual game range using the same clips.
    auto depthProjection=Projection(l,r,u,d,.03f,5000.0f);
    auto linearDepth=[](double raw,double nearClip,double farClip){return 1.0/((1.0/nearClip-1.0/farClip)*raw+1.0/farClip);};
    for(double distance:{.03,.1,1.0,10.0,100.0,1000.0,5000.0}){
        double projected=(double(depthProjection.m[10])*-distance+depthProjection.m[14])/distance;
        double raw=(1.0-projected)*.5;
        // Near/far precision from the float projection is visible at 5 km.
        assert(std::abs(linearDepth(raw,.03,5000.0)-distance)<distance*.02);
        if(distance>=1.0&&distance<=100.0)assert(linearDepth(raw,.01,5000.0)<distance*.34);
    }
    // The OpenXR-to-Unity handedness conversion preserves left/right IPD.
    Vec3 left{-.032f,0,0},right{.032f,0,0};auto delta=Rotate(yaw,right)-Rotate(yaw,left);
    assert(Near(std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z),.064f));
    // A native target and an eye use inverse transforms of the same game-to-XR
    // mapping, including translated recenter origins and arbitrary game yaw.
    Vec3 base{40,-36,-20},origin{.1f,1.6f,-.2f},target{35,-35,-8};
    auto xrPoint=GamePointToXR(target,base,yaw,origin);
    auto reconstructed=base+Rotate(yaw,Vec3{xrPoint.x,xrPoint.y,-xrPoint.z}-origin);
    assert(Near(reconstructed.x,target.x)&&Near(reconstructed.y,target.y)&&Near(reconstructed.z,target.z));
    auto centered=GamePointToXR(base,base,yaw,origin);assert(Near(centered.x,origin.x)&&Near(centered.y,origin.y)&&Near(centered.z,-origin.z));
    // The shared 'recently pressed' bit must not create an event. A held key
    // produces one event, while another key can be pressed independently.
    KeyEdges keys;assert(!keys.Pressed(122,1));assert(keys.Pressed(122,0x8000));
    assert(!keys.Pressed(122,0x8001));assert(keys.Pressed(121,0x8000));
    assert(!keys.Pressed(122,0));assert(!keys.Pressed(122,1));assert(keys.Pressed(122,0x8000));
    // A shared CPU culling volume must retain every native eye corner, including
    // very near geometry, asymmetric lenses, head rotation and eye cant.
    Vec3 eyePositions[2];Quat eyeRotations[2];float fovs[2][4]={{-.76755f,.620897f,.726983f,-.837898f},{-.620897f,.76755f,.726983f,-.837898f}};
    auto pose=stereocull::Normalize({.14f,.4f,-.07f,.85f});
    for(bool cant:{false,true}){for(int eye=0;eye<2;++eye){eyePositions[eye]=Vec3{42,-4,71}+Rotate(pose,{eye?.032f:-.032f,eye?.001f:-.001f,0});float angle=cant?(eye?.015f:-.015f):0;eyeRotations[eye]=Mul(pose,{0,std::sin(angle),0,std::cos(angle)});}auto common=stereocull::Build(eyePositions,eyeRotations,fovs,.03f,5000);assert(common.valid&&common.backwardShift>.02f&&common.backwardShift<.1f);
      for(int eye=0;eye<2;++eye)for(float distance:{.03f,.1f,1.0f,100.0f,4999.0f})for(int corner=0;corner<4;++corner){auto point=eyePositions[eye]+Rotate(eyeRotations[eye],{std::tan(fovs[eye][corner&1])*distance,std::tan(fovs[eye][(corner&2)?2:3])*distance,distance});float clip[4]{};for(int row=0;row<4;++row)clip[row]=common.matrix.m[row]*point.x+common.matrix.m[4+row]*point.y+common.matrix.m[8+row]*point.z+common.matrix.m[12+row];assert(clip[3]>0);for(int axis=0;axis<3;++axis)assert(std::abs(clip[axis]/clip[3])<=1.002f);}
    }
    // The culling patch must leave all unrelated parameters, camera identity,
    // masks and bookkeeping intact. Reject unsupported layouts transactionally.
    std::array<uint8_t,shadowcull::ParametersSize> original{},shared{},patched{};
    for(size_t i=0;i<original.size();++i){original[i]=uint8_t(i*17+3);shared[i]=uint8_t(i*29+101);}patched=original;
    assert(shadowcull::CopyProperties(patched.data(),patched.size(),shared.data(),shared.size()));
    for(size_t i=0;i<patched.size();++i){bool geometry=i>=shadowcull::PropertiesOffset&&i<shadowcull::PropertiesOffset+shadowcull::PropertiesSize;auto p=i-shadowcull::PropertiesOffset;
        if((p>=624&&p<628)||(p>=968&&p<984)||p==992)geometry=false;
        assert(patched[i]==(geometry?shared[i]:original[i]));
    }
    patched=original;assert(!shadowcull::CopyProperties(patched.data(),patched.size()-1,shared.data(),shared.size()));assert(patched==original);
    assert(!shadowcull::CopyProperties(patched.data(),patched.size(),shared.data(),shared.size()-1));assert(patched==original);
    assert(!shadowcull::CopyProperties(nullptr,patched.size(),shared.data(),shared.size()));
    puts("Projection, clip planes, recenter, IPD, culling bounds/layout and key checks passed");
}
