#include "SceneAnimations.h"
#include <algorithm>
#include <cmath>

namespace crimson {
void SceneAnimations::clear() {states_.clear();generation_=0;}
void SceneAnimations::advance(ExtractedScene& scene,std::uint64_t documentGeneration,double seconds,const Query& query) {
    if(generation_!=documentGeneration) {states_.clear();generation_=documentGeneration;}
    const auto delta=std::isfinite(seconds)?std::clamp(seconds,0.,1.):0.;
    for(auto& [id,state]:states_)state.seen=false;
    for(auto& model:scene.models) {
        auto [iterator,inserted]=states_.try_emplace(model.id);
        auto& state=iterator->second;
        const auto& settings=model.animation;
        const auto definition=query&&!settings.asset.empty()?query(settings.asset.id):std::shared_ptr<const AnimationAsset>{};
        const bool sourceChanged=definition && (!state.definition || definition->sourceModel!=state.definition->sourceModel ||
            definition->sourceClipIdentity!=state.definition->sourceClipIdentity || definition->startTime!=state.definition->startTime || definition->endTime!=state.definition->endTime);
        const bool restarted=inserted||state.asset!=model.assetId||state.settings.asset!=settings.asset||state.settings.startTime!=settings.startTime||sourceChanged;
        if(restarted)state.seconds=settings.startTime;
        else if(settings.playing&&state.settings.playing&&!settings.asset.empty()&&(!query||definition))
            state.seconds=std::min(1.e12,state.seconds+delta*settings.speed*(definition?definition->playbackRate:1.));
        state.definition=definition;
        state.asset=model.assetId;state.settings=settings;state.seen=true;
        model.animationSeconds=state.seconds;
    }
    std::erase_if(states_,[](const auto& value){return !value.second.seen;});
}
}
