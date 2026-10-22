#include "LoadingGatePolicy.h"
#include <cassert>
int main(){
 loading::Gate gate;
 auto d=gate.Observe(true,false,1000);assert(!d.nativeOnly&&!d.entered&&!d.exited);
 // Map travel retains the hero identity: only the native loading flag changes.
 d=gate.Observe(true,true,1100);assert(d.nativeOnly&&d.entered&&!d.exited);
 d=gate.Observe(true,true,2100);assert(d.nativeOnly&&!d.entered&&!d.exited);
 // A slow HDD/load must keep the original renderer for the entire load,
 // regardless of the old eight-second hero-change delay.
 d=gate.Observe(true,true,601100);assert(d.nativeOnly&&!d.entered);
 d=gate.Observe(true,false,601101);assert(d.nativeOnly&&d.exited);
 d=gate.Observe(true,false,601349);assert(d.nativeOnly&&!d.exited);
 d=gate.Observe(true,false,601350);assert(!d.nativeOnly);
 d=gate.Observe(true,true,601351);assert(d.nativeOnly&&d.entered);
 d=gate.Observe(true,false,601352);assert(d.nativeOnly&&d.exited);
 d=gate.Observe(true,true,601400);assert(d.nativeOnly&&d.entered);
 d=gate.Observe(true,false,601401);assert(d.nativeOnly&&d.exited);
 d=gate.Observe(true,false,601650);assert(!d.nativeOnly);
 // Unsupported metadata/read failure must not substitute eye cameras.
 d=gate.Observe(false,false,700000);assert(d.nativeOnly&&d.entered);
 d=gate.Observe(false,false,900000);assert(d.nativeOnly&&!d.entered);
 d=gate.Observe(true,false,900001);assert(d.nativeOnly&&d.exited);
 d=gate.Observe(true,false,900250);assert(!d.nativeOnly);
}
