#include "runtime/SceneLights.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){require(std::abs(a-b)<1.e-9,"Light preview clock differs from the expected time.");}
void controlsAndLifetime() {
    crimson::SceneLights lights;lights.synchronize(10);
    auto controls=lights.previewContext();
    require(*controls.enabled&&!*controls.paused&&!*controls.restart,"A new scene did not enable live flicker preview.");
    near(*lights.sampleTime(),0);
    lights.advance(10,.25);near(*lights.sampleTime(),.25);
    *controls.paused=true;lights.advance(10,.5);near(*lights.sampleTime(),.25);
    *controls.paused=false;lights.advance(10,.125);near(*lights.sampleTime(),.375);
    *controls.enabled=false;lights.advance(10,.5);
    require(!lights.sampleTime(),"Disabling preview continued to request animated extraction.");
    *controls.enabled=true;near(*lights.sampleTime(),.375);
    *controls.restart=true;lights.advance(10,.5);near(*lights.sampleTime(),0);
    require(!*controls.restart,"A restart request was not consumed once.");
    *controls.paused=true;*controls.restart=true;lights.advance(10,.5);near(*lights.sampleTime(),0);
    require(*controls.paused,"Restart changed the independent preview pause state.");
    *controls.enabled=false;lights.synchronize(11);
    require(*controls.enabled&&!*controls.paused&&!*controls.restart,"Document replacement retained transient preview controls.");
    near(*lights.sampleTime(),0);
    lights.advance(11,std::numeric_limits<double>::quiet_NaN());lights.advance(11,-1);near(*lights.sampleTime(),0);
    lights.advance(11,100);near(*lights.sampleTime(),1);
    lights.clear();near(*lights.sampleTime(),0);
}
void partitionAndIsolation() {
    crimson::SceneLights a,b;a.synchronize(1);b.synchronize(1);
    for(unsigned i=0;i<120;++i)a.advance(1,1./120.);
    for(unsigned i=0;i<30;++i)b.advance(1,1./30.);
    near(*a.sampleTime(),*b.sampleTime());
    auto controls=a.previewContext();*controls.paused=true;
    a.advance(1,.25);b.advance(1,.25);near(*a.sampleTime(),1);near(*b.sampleTime(),1.25);
    a.synchronize(2);near(*a.sampleTime(),0);near(*b.sampleTime(),1.25);
}
}
int main(){try{controlsAndLifetime();partitionAndIsolation();std::cout<<"Scene-wide light preview clocks, controls and isolation passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
