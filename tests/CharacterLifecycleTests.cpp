#include "CharacterLifecyclePolicy.h"
#include <array>
#include <cassert>
int main(){
 using namespace characterlife;
 std::array<uint64_t,32> words{};Mask mask{words.data(),0,2000,4};
 for(uint32_t slot:{0u,63u,64u,615u,1999u}){words[slot/64]|=1ull<<(slot%64);assert(Has(mask,slot));assert(ShouldQueue(slot,2000,true,Has(mask,slot),false));assert(!ShouldQueue(slot,2000,true,true,true));}
 assert(!Has(mask,2000));assert(!Has(Mask{},0));assert(!Has(Mask{words.data(),0,100001,4},0));
 assert(!ShouldQueue(2000,2000,true,true,false));assert(!ShouldQueue(20,2000,false,true,false));assert(!ShouldQueue(20,2000,true,false,false));
 assert(!ShouldQueue(0x80000014,2000,true,true,false));assert(!ShouldQueue(0x40000014,2000,true,true,false));
 assert(ShouldQueue(0x20000014,2000,true,true,false)); // native unregister clears re-register intent
 // Normal OnDisable already queued: destruction must not submit it twice.
 bool queued=false;unsigned calls=0;for(int pass=0;pass<2;++pass)if(ShouldQueue(20,2000,true,true,queued)){++calls;queued=true;}assert(calls==1);
}
