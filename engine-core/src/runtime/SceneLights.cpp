#include "SceneLights.h"
#include <algorithm>
#include <cmath>

namespace crimson {
void SceneLights::clear() {generation_=0;seconds_=0;enabled_=true;paused_=restart_=false;}
void SceneLights::synchronize(std::uint64_t documentGeneration) {
    if(generation_==documentGeneration)return;
    clear();generation_=documentGeneration;
}
void SceneLights::advance(std::uint64_t documentGeneration,double seconds) {
    synchronize(documentGeneration);
    if(restart_){seconds_=0;restart_=false;return;}
    if(enabled_&&!paused_&&std::isfinite(seconds))seconds_=std::min(1.e12,seconds_+std::clamp(seconds,0.,1.));
}
}
