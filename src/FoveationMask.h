#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vrsmask {
struct Point {float x{},y{};};
struct Mesh {std::vector<Point> vertices;std::vector<uint32_t> indices;bool visible=true;};
struct Fov {float left{},right{},up{},down{};};
struct Triangle {Point p[3];};
struct Mask {unsigned width{},height{},full{},coarse{},hidden{};std::vector<uint8_t> pixels;};
inline bool Intersects(const Triangle& t,float x0,float y0,float x1,float y1){
    constexpr double eps=1e-6;
    for(unsigned axis=0;axis<5;++axis){double nx=axis==0?1:0,ny=axis==1?1:0;if(axis>=2){auto a=t.p[axis-2],b=t.p[(axis-1)%3];nx=-(b.y-a.y);ny=b.x-a.x;}
        double lo=1e300,hi=-1e300;for(auto p:t.p){auto v=nx*p.x+ny*p.y;lo=std::min(lo,v);hi=std::max(hi,v);}
        auto rlo=nx*(nx>=0?x0:x1)+ny*(ny>=0?y0:y1),rhi=nx*(nx>=0?x1:x0)+ny*(ny>=0?y1:y0);
        if(hi<rlo-eps||lo>rhi+eps)return false;
    }return true;
}
inline bool Contains(const Triangle& t,float x,float y){double sign{};for(unsigned i=0;i<3;++i){auto a=t.p[i],b=t.p[(i+1)%3];double side=(b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);if(std::abs(side)<1e-5)return false;if(sign&&sign*side<0)return false;sign=side;}return true;}
inline bool Project(const Mesh& mesh,const Fov& f,unsigned width,unsigned height,std::vector<Triangle>& triangles){
    if(mesh.vertices.size()<3||mesh.vertices.size()>16384||mesh.indices.empty()||mesh.indices.size()>65535||mesh.indices.size()%3)return false;
    float l=std::tan(f.left),r=std::tan(f.right),u=std::tan(f.up),d=std::tan(f.down);
    if(!std::isfinite(l+r+u+d)||l>=0||r<=0||u<=0||d>=0)return false;
    std::vector<Point> vertices;for(auto p:mesh.vertices){Point v{(p.x-l)/(r-l)*width,(u-p.y)/(u-d)*height};if(!std::isfinite(v.x+v.y)||std::abs(v.x)>width*16.f||std::abs(v.y)>height*16.f)return false;vertices.push_back(v);}
    bool centerVisible=false;for(size_t i=0;i<mesh.indices.size();i+=3){Triangle t;for(int n=0;n<3;++n){if(mesh.indices[i+n]>=vertices.size())return false;t.p[n]=vertices[mesh.indices[i+n]];}
        auto a=t.p[0],b=t.p[1],c=t.p[2];if(std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))<1e-7)continue;
        centerVisible|=Intersects(t,-l/(r-l)*width,u/(u-d)*height,-l/(r-l)*width,u/(u-d)*height);triangles.push_back(t);
    }
    return !triangles.empty()&&(mesh.visible?centerVisible:!centerVisible);
}
inline bool Hidden(const Mesh& mesh,const std::vector<Triangle>& triangles,float x0,float y0,float x1,float y1){
    if(mesh.visible){for(auto& t:triangles)if(Intersects(t,x0,y0,x1,y1))return false;return true;}
    // A convex tile wholly inside one hidden triangle is safe even when the
    // runtime supplies only the hidden mesh. Triangle seams simply stay visible.
    for(auto& t:triangles)if(Contains(t,x0,y0)&&Contains(t,x1,y0)&&Contains(t,x0,y1)&&Contains(t,x1,y1))return true;
    return false;
}
inline bool Build(unsigned targetWidth,unsigned targetHeight,unsigned viewWidth,unsigned viewHeight,const Fov fovs[2],const Mesh meshes[2],float radius,unsigned margin,Mask& result){
    if(viewWidth<16||viewHeight<16||targetWidth<viewWidth||targetHeight<viewHeight||targetWidth>16384||targetHeight>16384||!std::isfinite(radius)||radius<.2f||radius>.75f||margin>256)return false;
    std::array<Point,4> centers{};std::vector<Triangle> triangles[2];bool visibility=true;
    for(int e=0;e<2;++e){auto f=fovs[e];float l=std::tan(f.left),r=std::tan(f.right),u=std::tan(f.up),d=std::tan(f.down);if(!std::isfinite(l+r+u+d)||l>=0||r<=0||u<=0||d>=0)return false;
        centers[e*2]={-l/(r-l),u/(u-d)};centers[e*2+1]={centers[e*2].x,1-centers[e*2].y};visibility=Project(meshes[e],f,viewWidth,viewHeight,triangles[e])&&visibility;
    }
    Mask out;out.width=(targetWidth+15)/16;out.height=(targetHeight+15)/16;out.pixels.resize(size_t(out.width)*out.height);
    for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x){
        // Outside the active viewport (HDRP can retain a larger RTHandle) stays full rate.
        if(x*16>=viewWidth||y*16>=viewHeight){++out.full;continue;}
        float x0=float(x*16)/viewWidth,x1=float(std::min((x+1)*16,viewWidth))/viewWidth,y0=float(y*16)/viewHeight,y1=float(std::min((y+1)*16,viewHeight))/viewHeight;bool full=false;
        for(auto p:centers){float dx=p.x<x0?x0-p.x:p.x>x1?p.x-x1:0,dy=p.y<y0?y0-p.y:p.y>y1?p.y-y1:0;full|=dx*dx+dy*dy<=radius*radius;}
        bool hidden=visibility;float a=float(x*16)-margin,b=float(y*16)-margin,c=float(std::min((x+1)*16,viewWidth))+margin,d=float(std::min((y+1)*16,viewHeight))+margin;
        // Without reliable per-eye command attribution, preserve the union of
        // BOTH eyes and BOTH vertical orientations. Only their common hidden
        // area may be culled, with an additional filter margin.
        for(int e=0;e<2&&hidden;++e)hidden=Hidden(meshes[e],triangles[e],a,b,c,d)&&Hidden(meshes[e],triangles[e],a,float(viewHeight)-d,c,float(viewHeight)-b);
        uint8_t value=hidden?2:full?0:1;out.pixels[size_t(y)*out.width+x]=value;if(hidden)++out.hidden;else if(full)++out.full;else ++out.coarse;
    }
    result=std::move(out);return true;
}
}
