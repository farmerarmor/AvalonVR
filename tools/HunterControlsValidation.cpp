#include "HunterSelection.h"
#include "HunterRepeat.h"
#include <cstdio>
#include <cstdlib>
#include <array>
static unsigned checks{};
static void require(bool good,const char* reason){++checks;if(!good){fprintf(stderr,"FAIL: %s\n",reason);exit(1);}}
int main(){
 HunterSelection state;
 require(!state.HasSelection(5),"disabled at startup");
 state.Move(1,5);require(!state.enabled && state.index==-1,"Page keys inactive until End");
 state.Enable();state.Scanned(5);require(state.enabled && state.index==0,"End selects first shader after scan");
 state.Move(1,5);require(state.index==1,"Page Down advances");
 state.Move(-1,5);require(state.index==0,"Page Up goes backward");
 state.Move(-1,5);require(state.index==4,"backward wraps");
 state.Move(1,5);require(state.index==0,"forward wraps");
 state.Home();require(!state.enabled && state.index==-1 && !state.HasSelection(5),"Home resets and disables");
 state.Move(-1,5);require(state.index==-1,"Page Up after Home has no effect");
 state.Enable();state.Scanned(0);require(state.enabled && !state.HasSelection(0),"empty scene has no selection");
 state.Home();state.Scanned(5);require(!state.HasSelection(5),"scan cannot reenable hunter after Home");
 const std::array<unsigned,6> mixedStages{0,1,2,3,4,5};state.Enable();state.Scanned(mixedStages.size());
 for(unsigned i=0;i<6;++i){require(mixedStages[std::size_t(state.index)]==i,"Page Down visits every stage in one combined list");state.Move(1,mixedStages.size());}
 require(state.index==0,"all-stage list wraps without a pixel-only filter");
 state.Move(1,7);require(state.index==1,"late appended shader preserves current navigation position");
 state.Move(-1,7);state.Move(-1,7);require(state.index==6,"late appended shader is reachable through backward wrap");
 state.Home();require(!state.HasSelection(7),"Home restores combined list selection");
 HunterRepeat repeat;
 require(repeat.Step(false,false,true,0)==0,"idle navigation does nothing");
 require(repeat.Step(false,true,true,1)==1,"initial Page Down moves immediately");
 require(repeat.Step(false,true,true,200)==0,"held key waits a full 200ms");
 require(repeat.Step(false,true,true,201)==1,"held Page Down repeats at 200ms");
 require(repeat.Step(false,true,true,401)==1,"repeat cadence continues");
 require(repeat.Step(false,true,true,5000)==1,"long frame causes one visible step");
 require(repeat.Step(false,true,true,5000)==0,"no catch-up burst after long frame");
 require(repeat.Step(false,false,true,5001)==0,"release stops navigation");
 require(repeat.Step(false,true,true,5002)==1,"repress moves immediately");
 require(repeat.Step(true,false,true,5003)==-1,"reverse direction moves immediately");
 require(repeat.Step(true,false,true,5202)==0,"reverse direction resets repeat deadline");
 require(repeat.Step(true,false,true,5203)==-1,"Page Up repeats at 200ms");
 require(repeat.Step(true,true,true,5403)==0,"opposing page keys pause navigation");
 require(repeat.Step(false,true,true,5404)==1,"release one opposing key resumes immediately");
 require(repeat.Step(false,true,false,5604)==0,"focus loss or disabled hunting stops repeat");
 require(repeat.Step(false,true,true,5605)==1,"reactivation starts a fresh repeat sequence");
 require(repeat.Step(false,true,false,6000)==0,"inactive state cannot repeat");
 printf("PASS: %u requested hunter keyboard state checks.\n",checks);return 0;
}
