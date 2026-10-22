#include "runtime/SceneAnimations.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void near(double a,double b) {require(std::abs(a-b)<1.e-6,"Animation clock differs from expected playback time.");}
void playback() {
    using namespace crimson;
    SceneAnimations edit,play;ExtractedScene scene;
    ExtractedModel first;first.id=1;first.assetId="model-a";first.animation.asset.id="animation-a";first.animation.startTime=.5f;
    auto second=first;second.id=2;second.animation.speed=2;
    scene.models={first,second};edit.advance(scene,100,.1);near(scene.models[0].animationSeconds,.5);near(scene.models[1].animationSeconds,.5);
    edit.advance(scene,100,.25);near(scene.models[0].animationSeconds,.75);near(scene.models[1].animationSeconds,1.);
    scene.models[0].animation.playing=false;edit.advance(scene,100,.2);near(scene.models[0].animationSeconds,.75);near(scene.models[1].animationSeconds,1.4);
    scene.models[0].animation.playing=true;edit.advance(scene,100,.1);near(scene.models[0].animationSeconds,.75);
    scene.models[0].animation.speed=3;edit.advance(scene,100,.1);near(scene.models[0].animationSeconds,1.05);
    scene.models[0].world=math::translation({10,20,30});scene.models[0].animation.loopOverride=false;
    edit.advance(scene,100,.1);near(scene.models[0].animationSeconds,1.35);
    scene.models[0].animation.asset.id="animation-b";edit.advance(scene,100,.1);near(scene.models[0].animationSeconds,.5);
    scene.models[0].animation.startTime=2;edit.advance(scene,100,.1);near(scene.models[0].animationSeconds,2);
    const auto saved=scene.models[0].animation;
    play.advance(scene,200,.2);near(scene.models[0].animationSeconds,2);
    play.advance(scene,200,.1);near(scene.models[0].animationSeconds,2.3);
    edit.advance(scene,100,0);near(scene.models[0].animationSeconds,2);
    require(scene.models[0].animation==saved,"Playback mutated authored animation settings.");
    edit.advance(scene,100,std::numeric_limits<double>::quiet_NaN());near(scene.models[0].animationSeconds,2);
    edit.advance(scene,100,-1);near(scene.models[0].animationSeconds,2);
    scene.models[0].assetId="model-b";edit.advance(scene,100,.1);near(scene.models[0].animationSeconds,2);
    edit.advance(scene,101,.1);near(scene.models[0].animationSeconds,2);
    scene.models[0].animation.asset.id.clear();edit.advance(scene,101,.1);edit.advance(scene,101,.1);near(scene.models[0].animationSeconds,2);
    const auto removed=scene.models[1];scene.models.resize(1);edit.advance(scene,101,.1);
    scene.models.push_back(removed);edit.advance(scene,101,.1);near(scene.models[1].animationSeconds,.5);
    play.clear();play.advance(scene,200,.1);near(scene.models[0].animationSeconds,2);
}
void hiddenPlayback() {
    using namespace crimson;
    EntityRecord parent;parent.id=1;parent.name="Animated group";
    EntityRecord model;model.id=2;model.parent=1;model.name="Animated model";model.model=AssetRef<ModelAsset>{"animated-model"};
    model.modelAnimation.asset.id="animation-a";model.modelAnimation.startTime=.5f;model.modelAnimation.speed=2;
    Scene scene;scene.apply({parent,model},{});SceneAnimations clocks;
    auto extracted=extractScene(scene);clocks.advance(extracted,10,0);near(extracted.models.front().animationSeconds,.5);
    clocks.advance(extracted,10,.25);near(extracted.models.front().animationSeconds,1);
    parent.visible=false;scene.apply({parent,model},{});extracted=extractScene(scene);
    require(extracted.models.size()==1&&!extracted.models.front().visible,"Hidden model lost its animation reconciliation identity.");
    clocks.advance(extracted,10,.25);near(extracted.models.front().animationSeconds,1.5);
    parent.visible=true;scene.apply({parent,model},{});extracted=extractScene(scene);clocks.advance(extracted,10,.25);
    require(extracted.models.front().visible,"Showing a group did not restore its model's render visibility.");
    near(extracted.models.front().animationSeconds,2);
}
void assetPlaybackDefaults() {
    using namespace crimson;
    auto asset=std::make_shared<AnimationAsset>();asset->id="animation-a";asset->playbackRate=2;asset->endTime=3;
    SceneAnimations clocks;ExtractedScene scene;ExtractedModel model;model.id=1;model.assetId="model";
    model.animation.asset.id=asset->id;model.animation.speed=3;scene.models={model};
    const SceneAnimations::Query query=[&](const std::string& id){return id==asset->id?asset:nullptr;};
    clocks.advance(scene,1,0,query);clocks.advance(scene,1,.1,query);near(scene.models[0].animationSeconds,.6);
    asset=std::make_shared<AnimationAsset>(*asset);asset->forceRootLock=true;
    clocks.advance(scene,1,.1,query);near(scene.models[0].animationSeconds,1.2);
    asset=std::make_shared<AnimationAsset>(*asset);asset->startTime=.5f;
    clocks.advance(scene,1,.1,query);near(scene.models[0].animationSeconds,0);
    asset=std::make_shared<AnimationAsset>(*asset);asset->playbackRate=0;
    clocks.advance(scene,1,.1,query);near(scene.models[0].animationSeconds,0);
}
}
int main(){try{playback();hiddenPlayback();assetPlaybackDefaults();std::cout<<"Independent scene animation clocks, asset defaults, visibility, edits, pause, reload and Play isolation passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
