#pragma once
#include "assets/AnimationAsset.h"
#include "assets/ModelAnimation.h"
#include <optional>

namespace crimson {
// Playback time is relative to the authored range. Callers advance their own
// clocks; sampling has no world mutation, event dispatch or playback state.
double animationDuration(const AnimationAsset&);
struct PreparedAnimationBinding {
    std::uint32_t clip{};
    double first{},duration{};
    bool loop{};
    ModelRootLock rootLock;
};
PreparedAnimationBinding prepareAnimationBinding(const AnimationAsset&,const ModelAsset& source,const ModelAsset& target);
ModelPose evaluateAnimationPose(const PreparedAnimationBinding&,const ModelAsset& source,
    const ModelAsset& target,double elapsedSeconds,std::optional<bool> loopOverride={});
ModelPose evaluateAnimationPose(const AnimationAsset&,const ModelAsset& source,
    const ModelAsset& target,double elapsedSeconds,std::optional<bool> loopOverride={});
}
