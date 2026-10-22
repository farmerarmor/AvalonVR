#include "QuadFogMath.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){using namespace quadfog;Grid native{55,66,113};auto match=MatchGrid(native,500,596,9.03169f,4.f);assert(match.valid&&match.grid.x==221&&match.grid.y==264&&match.grid.z==113);assert(std::abs(match.voxel-2.2579225f)<.00001f);assert(std::abs(match.grid.x*.4f-89.f)<1.f);assert(std::abs(match.grid.y*.4f-106.f)<1.f);
 auto same=MatchGrid(native,500,596,9.03169f,1);assert(same.valid&&same.grid.x==native.x&&same.grid.y==native.y&&same.grid.z==native.z&&same.voxel==9.03169f);
 for(auto scale:{0.f,.5f,6.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){auto bad=MatchGrid(native,500,596,9.03169f,scale);assert(!bad.valid&&bad.grid.x==55&&bad.grid.y==66&&bad.grid.z==113&&bad.voxel==9.03169f);}
 assert(!MatchGrid(native,0,596,9.f,4).valid);assert(!MatchGrid(native,500,596,0,4).valid);assert(!MatchGrid({55,66,1024},500,596,9.03169f,4).valid);assert(!MatchGrid(native,500,596,std::numeric_limits<float>::quiet_NaN(),4).valid);assert(!MatchGrid({500,596,113},500,596,9.03169f,4).valid);assert(!MatchGrid({1024,1024,512},1024,1024,1,4).valid);
 puts("Quad fog grid matching checks passed");return 0;}
