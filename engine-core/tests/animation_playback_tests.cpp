#include "runtime/AnimationPlayback.h"
#include "resources/AssetId.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(float actual,float expected){require(std::abs(actual-expected)<1e-5f,"Animation range or root-lock sample was incorrect.");}
template<class F>void rejects(F&& operation){bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid animation binding was accepted.");}
void playback(){
    using namespace crimson;
    ModelAsset source;source.id=canonicalAssetId("test:animation-playback");source.name="Animated";
    source.nodes.resize(2);source.roots={0};source.nodes[0].name="Container";source.nodes[0].children={1};
    source.nodes[1].name="Motion";source.nodes[1].translation={1,0,0};source.nodes[1].local=math::translation({1,0,0});
    ModelAnimationClip clip;clip.name="Travel";clip.start=2;clip.end=4;
    ModelAnimationChannel channel;channel.node=1;channel.path=ModelAnimationPath::Translation;
    channel.components=3;channel.times={2,4};channel.values={2,0,0,6,0,0};clip.channels.push_back(channel);source.animations.push_back(clip);
    auto asset=importedAnimationAsset(source,0);asset.startTime=.5f;asset.endTime=1.5f;asset.rootNode=1;
    const auto x=[](const ModelPose& pose){return math::transformPoint(pose.worldTransforms[1],{})[0];};
    auto binding=prepareAnimationBinding(asset,source,source);
    near(x(evaluateAnimationPose(binding,source,source,0)),3);near(x(evaluateAnimationPose(binding,source,source,.5)),4);
    near(x(evaluateAnimationPose(binding,source,source,1)),3);near(x(evaluateAnimationPose(binding,source,source,9,false)),5);
    near(x(evaluateAnimationPose(binding,source,source,-.25)),4.5f);
    asset.forceRootLock=true;binding=prepareAnimationBinding(asset,source,source);
    near(x(evaluateAnimationPose(binding,source,source,.75)),3);
    asset.rootLockReference=AnimationRootLockReference::ReferencePose;binding=prepareAnimationBinding(asset,source,source);
    near(x(evaluateAnimationPose(binding,source,source,.75)),1);
    asset.rootLockReference=AnimationRootLockReference::Zero;binding=prepareAnimationBinding(asset,source,source);
    near(x(evaluateAnimationPose(binding,source,source,.75)),0);
    auto target=source;target.id=canonicalAssetId("test:compatible-animation-model");
    near(x(evaluateAnimationPose(binding,source,target,.75)),0);
    target.nodes[1].name="Other";rejects([&]{prepareAnimationBinding(asset,source,target);});
    auto changed=source;changed.animations[0].channels[0].values[0]=9;rejects([&]{prepareAnimationBinding(asset,changed,source);});
    rejects([&]{evaluateAnimationPose(binding,source,source,std::numeric_limits<double>::quiet_NaN());});
    require(source.animations[0].channels[0].values[0]==2,"Sampling mutated imported source tracks.");
}
}
int main(){try{playback();std::cout<<"Animation ranges, source bindings, loop overrides and root locking passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
