#include "QuadViewMath.h"
#include "VRMath.h"
#include "PerformanceModes.h"
#include "QuadViewControls.h"
#include <cassert>
#include <cstdio>
#include <limits>
static bool Close(float a,float b){return std::abs(a-b)<.00002f;}
int main(){
    unsigned points{};
    for(auto outer:{quadview::Fov{-.9f,.7f,.8f,-.6f},quadview::Fov{-.7f,.9f,.6f,-.8f},quadview::Fov{.1f,1.f,1.1f,-.2f}}){
        auto f=quadview::MakeFocus(outer,.5f,.45f);assert(f.valid);
        assert(f.rect.left>=0&&f.rect.top>=0&&f.rect.right<=1.00001f&&f.rect.bottom<=1.00001f);
        assert(Close(f.rect.right-f.rect.left,.5f)&&Close(f.rect.bottom-f.rect.top,.45f));
        auto p=Projection(outer.left,outer.right,outer.up,outer.down,.03f,5000);
        auto q=Projection(f.fov.left,f.fov.right,f.fov.up,f.fov.down,.03f,5000);
        // Match full and cropped projection of exactly the same world rays.
        for(int y=0;y<=20;++y)for(int x=0;x<=20;++x){
            float fx=x/20.f,fy=y/20.f;
            float tx=std::tan(f.fov.left)+(std::tan(f.fov.right)-std::tan(f.fov.left))*fx;
            float ty=std::tan(f.fov.up)+(std::tan(f.fov.down)-std::tan(f.fov.up))*fy;
            auto project=[](Matrix m,float x,float y){return quadview::Rect{(m.m[0]*x-m.m[8]+1)*.5f,(1-(m.m[5]*y-m.m[9]))*.5f,0,0};};
            auto a=project(p,tx,ty),b=project(q,tx,ty);
            assert(Close(a.left,f.rect.left+fx*(f.rect.right-f.rect.left)));
            assert(Close(a.top,f.rect.top+fy*(f.rect.bottom-f.rect.top)));
            assert(Close(b.left,fx)&&Close(b.top,fy));++points;
        }
        assert(quadview::Weight(f.rect,f.rect.left,f.rect.top,.12f)==0);
        assert(quadview::Weight(f.rect,(f.rect.left+f.rect.right)*.5f,(f.rect.top+f.rect.bottom)*.5f,.12f)==1);
    }
    assert(!quadview::MakeFocus({0,0,0,0},.5f,.5f).valid);
    assert(!quadview::MakeFocus({-.8f,.8f,.8f,-.8f},0,.5f).valid);
    assert(!quadview::MakeFocus({-.8f,.8f,std::numeric_limits<float>::quiet_NaN(),-.8f},.5f,.5f).valid);
    auto p=quadview::Peripheral({4000,4768},40),f=quadview::Inset({4000,4768},50,50);
    assert(p.width==1600&&p.height==1908&&f.width==2000&&f.height==2384);
    double fraction=(double(p.width)*p.height+double(f.width)*f.height)/(4000.*4768);
    assert(fraction>.409&&fraction<.411);
    assert(perf::ModeCount==4096&&((1u<<11)|(1u<<10)|(1u<<9)|(1u<<8)|255u)<perf::ModeCount);
    quadview::Settings settings{};
    settings=quadview::Adjust(settings,quadview::Tune::SizeUp);assert(settings.horizontal==55&&settings.vertical==55&&settings.peripheral==40);
    settings=quadview::Adjust(settings,quadview::Tune::Stronger);assert(settings.peripheral==35&&settings.horizontal==55);
    settings=quadview::Adjust(settings,quadview::Tune::Weaker);assert(settings.peripheral==40);
    for(int i=0;i<30;++i)settings=quadview::Adjust(settings,quadview::Tune::SizeDown);
    assert(settings.horizontal==10&&settings.vertical==10);
    for(int i=0;i<30;++i)settings=quadview::Adjust(settings,quadview::Tune::SizeUp);
    assert(settings.horizontal==95&&settings.vertical==95);
    for(int i=0;i<30;++i)settings=quadview::Adjust(settings,quadview::Tune::Stronger);assert(settings.peripheral==20);
    for(int i=0;i<30;++i)settings=quadview::Adjust(settings,quadview::Tune::Weaker);assert(settings.peripheral==100);
    quadview::TuningKeys keys;
    assert(keys.Update(0,true,false,true,true,true)==quadview::Tune::SizeUp); // Ctrl+Shift+'+' is still size.
    assert(keys.Update(100,true,false,true,true,true)==quadview::Tune::None);
    assert(keys.Update(350,true,false,true,true,true)==quadview::Tune::SizeUp);
    assert(keys.Update(549,true,false,true,true,true)==quadview::Tune::None);
    assert(keys.Update(550,true,false,true,true,true)==quadview::Tune::SizeUp);
    assert(keys.Update(600,true,false,false,true,true)==quadview::Tune::Stronger);
    assert(keys.Update(650,false,true,false,true,true)==quadview::Tune::Weaker);
    assert(keys.Update(700,true,true,true,true,true)==quadview::Tune::None);
    assert(keys.Update(750,true,false,true,false,false)==quadview::Tune::None); // Never tune another foreground app.
    assert(keys.Update(800,true,false,false,false,true)==quadview::Tune::None);
    assert(keys.Update(850,false,true,true,false,true)==quadview::Tune::SizeDown);
    for(int percent=10;percent<=95;percent+=5){auto inset=quadview::MakeFocus({-.9f,.7f,.8f,-.6f},percent*.01f,percent*.01f);assert(inset.valid);assert(Close(inset.rect.right-inset.rect.left,percent*.01f));}
    printf("Quad view math: %u matched projection rays; inset containment, blend, dimensions, timing isolation, tuning bounds, modifier priority, hold repeat and focus gating pass (%.4f pixel fraction)\n",points,fraction);
}
