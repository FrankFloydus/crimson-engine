#include "SceneAnimations.h"
#include <algorithm>
#include <cmath>

namespace crimson {
void SceneAnimations::clear() {states_.clear();generation_=0;}
void SceneAnimations::advance(ExtractedScene& scene,std::uint64_t documentGeneration,double seconds) {
    if(generation_!=documentGeneration) {states_.clear();generation_=documentGeneration;}
    const auto delta=std::isfinite(seconds)?std::clamp(seconds,0.,1.):0.;
    for(auto& [id,state]:states_)state.seen=false;
    for(auto& model:scene.models) {
        auto [iterator,inserted]=states_.try_emplace(model.id);
        auto& state=iterator->second;
        const auto& settings=model.animation;
        const bool restarted=inserted||state.asset!=model.assetId||state.settings.clip!=settings.clip||state.settings.startTime!=settings.startTime;
        if(restarted)state.seconds=settings.startTime;
        else if(settings.playing&&state.settings.playing&&settings.clip>=0)
            state.seconds=std::min(1.e12,state.seconds+delta*settings.speed);
        state.asset=model.assetId;state.settings=settings;state.seen=true;
        model.animationSeconds=state.seconds;
    }
    std::erase_if(states_,[](const auto& value){return !value.second.seen;});
}
}
