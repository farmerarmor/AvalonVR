#include "QuadSampling.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
    // Cancel the derivative-based mip delta from angular pixel density, not
    // the crop fraction: half-size focus has the same density as the full eye.
    for(float scale:{.25f,.5f,.75f,1.f}){
        float bias=quadsampling::PeripheralBias(uint32_t(4000*scale),uint32_t(4800*scale),4000,4800);
        assert(std::abs((std::log2(1/scale)+bias))<1e-6f);
    }
    assert(std::abs(quadsampling::PeripheralBias(1200,1432,4000,4768)-std::log2(1432.f/4768))<1e-6f);
    assert(quadsampling::PeripheralBias(4000,4768,4000,4768)==0);
    assert(quadsampling::PeripheralBias(8000,9536,4000,4768)==0);
    assert(quadsampling::PeripheralBias(0,2384,4000,4768)==0);
    assert(quadsampling::PeripheralBias(2000,2384,0,4768)==0);
    assert(quadsampling::PeripheralBias(2000,2384,4000,32768)==0);
    puts("Quad sampling: angular mip-density cancellation, conservative rounded axis and invalid-size guards pass");
}
