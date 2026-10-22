#include "FoveationMask.h"
#include <cassert>
#include <cstdio>
using namespace vrsmask;
static Mesh Rectangle(float left,float right,float bottom,float top){return {{{left,bottom},{right,bottom},{right,top},{left,top}},{0,1,2,0,2,3},true};}
int main(){
    Fov fovs[2]={{-.785398f,.785398f,.785398f,-.785398f},{-.785398f,.785398f,.785398f,-.785398f}};Mesh meshes[2]={Rectangle(-.65f,.65f,-.65f,.65f),Rectangle(-.65f,.65f,-.65f,.65f)};Mask m;
    assert(Build(1024,1024,1024,1024,fovs,meshes,.4f,32,m));assert(m.hidden>0&&m.coarse>0&&m.full>0);assert(m.pixels[32*m.width+32]==0&&m.pixels[0]==2);
    auto hidden=m.hidden;assert(Build(1024,1024,1024,1024,fovs,meshes,.4f,64,m)&&m.hidden<hidden);
    // Expanding either eye's visible mesh may only reduce common hidden tiles.
    meshes[1]=Rectangle(-1,1,-1,1);assert(Build(1024,1024,1024,1024,fovs,meshes,.4f,32,m)&&m.hidden==0);
    meshes[1]={};assert(Build(1024,1024,1024,1024,fovs,meshes,.4f,32,m)&&m.hidden==0);
    // Hidden-only triangles are accepted only if the optical center is visible.
    meshes[0]={{{-1,-1},{1,-1},{-1,-.6f}},{0,1,2},false};meshes[1]=meshes[0];assert(Build(1024,1024,1024,1024,fovs,meshes,.4f,16,m));assert(m.hidden==0); // vertical-union guard preserves this one-sided edge
    meshes[0]=Rectangle(-1,1,-1,1);meshes[0].visible=false;meshes[1]=meshes[0];assert(Build(1024,1024,1024,1024,fovs,meshes,.4f,32,m)&&m.hidden==0);
    // RTHandle capacity may exceed the active DLSS viewport; padding stays 1x1.
    meshes[0]=meshes[1]={};assert(Build(2048,2048,1024,1024,fovs,meshes,.4f,32,m));assert(m.pixels[100*m.width+100]==0);
    fovs[0]={-1.f,.6f,.8f,-.7f};assert(Build(1024,1024,1024,1024,fovs,meshes,.4f,32,m));assert(!Build(1024,1024,1024,1024,fovs,meshes,.1f,32,m));
    fovs[1].right=0;assert(!Build(1024,1024,1024,1024,fovs,meshes,.4f,32,m));
    assert(!Build(0,0,0,0,fovs,meshes,.4f,32,m));puts("PASS: foveation center, stereo/vertical visibility union, filter margin, unavailable/invalid meshes, RTHandle padding and input guards");
}
