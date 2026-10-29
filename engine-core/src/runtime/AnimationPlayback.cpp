#include "AnimationPlayback.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace crimson {
double animationDuration(const AnimationAsset& animation) {
    return double(animation.endTime)-animation.startTime;
}
PreparedAnimationBinding prepareAnimationBinding(const AnimationAsset& animation,const ModelAsset& source,const ModelAsset& target) {
    validateAnimationSource(animation,source);
    if(animation.bindingSignature!=animationBindingSignature(target))
        throw std::runtime_error("Animation and model have incompatible node hierarchies or reference poses.");
    PreparedAnimationBinding binding;
    binding.clip=animation.sourceClip;
    binding.first=source.animations.at(animation.sourceClip).start+animation.startTime;
    binding.duration=animationDuration(animation);binding.loop=animation.loop;
    binding.rootLock.enabled=animation.forceRootLock;binding.rootLock.node=animation.rootNode;
    binding.rootLock.reference=static_cast<ModelRootLockReference>(animation.rootLockReference);
    binding.rootLock.first_frame_seconds=binding.first;
    return binding;
}
ModelPose evaluateAnimationPose(const PreparedAnimationBinding& binding,const ModelAsset& source,
    const ModelAsset& target,double elapsedSeconds,std::optional<bool> loopOverride) {
    if(!std::isfinite(elapsedSeconds))throw std::runtime_error("Animation sample time must be finite.");
    const double duration=binding.duration;
    double time=elapsedSeconds;
    if(loopOverride.value_or(binding.loop) && duration>0) {
        time=std::fmod(time,duration);if(time<0)time+=duration;
    } else time=std::clamp(time,0.,std::max(0.,duration));
    auto local=sample_model_local_pose(source,binding.clip,binding.first+time,false);
    apply_model_root_lock(source,local,binding.clip,binding.rootLock);
    return finalize_model_pose(target,local);
}
ModelPose evaluateAnimationPose(const AnimationAsset& animation,const ModelAsset& source,
    const ModelAsset& target,double elapsedSeconds,std::optional<bool> loopOverride) {
    return evaluateAnimationPose(prepareAnimationBinding(animation,source,target),source,target,elapsedSeconds,loopOverride);
}
}
