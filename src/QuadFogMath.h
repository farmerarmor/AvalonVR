#pragma once
#include <cmath>
#include <cstdint>
namespace quadfog {
struct Grid {int x{},y{},z{};};static_assert(sizeof(Grid)==12);
struct Match {Grid grid{};float voxel{};bool valid{};};
inline Match MatchGrid(Grid native,int width,int height,float voxel,float density){
 Match result{native,voxel,false};
 if(width<=0||height<=0||native.x<=0||native.y<=0||native.z<=0||native.z>512||!std::isfinite(voxel)||voxel<=0||!std::isfinite(density)||density<1||density>5.01f)return result;
 if(std::abs(native.x-int(std::nearbyint(width/voxel)))>1||std::abs(native.y-int(std::nearbyint(height/voxel)))>1)return result;
 double x=std::nearbyint(double(width)/voxel*density),y=std::nearbyint(double(height)/voxel*density);
 if(x<1||y<1||x>4096||y>4096||x*y*native.z>64.0*1024*1024)return result;
 result.grid={int(x),int(y),native.z};result.voxel=voxel/density;result.valid=true;return result;
}
}
