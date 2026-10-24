#include "ragdoll_fixture.h"
#include "runtime/PlaySession.h"
#include "runtime/AnimationPlayback.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
using namespace crimson;using namespace crimson::ragdoll_test;
struct Rig {
    std::shared_ptr<ModelAsset> model=std::make_shared<ModelAsset>(source());
    std::shared_ptr<RagdollAsset> definition;
    std::shared_ptr<AnimationAsset> animation;
    bool ready{true};
    Rig(){
        ModelAnimationChannel channel;channel.node=3;channel.path=ModelAnimationPath::Translation;channel.components=3;channel.times={0,2};channel.values={0,1,0,2,1,0};model->animations.push_back({"Arbitrary motion",0,2,{channel}});
        model->nodes[0].local=math::translation({.7f,0,0})*math::scaling({.5f,.5f,.5f});
        definition=std::make_shared<RagdollAsset>(makeRagdollAsset(*model));generateRagdollBodies(*definition,*model,{3});definition->bodies[0].shape=RagdollShape::Sphere;definition->bodies[0].position={};definition->bodies[0].radius=.1f;
        animation=std::make_shared<AnimationAsset>(importedAnimationAsset(*model,0));
    }
    void configure(PlaySession& play){play.setRagdollDefinitions([this](const auto& id)->std::shared_ptr<const RagdollAsset>{return ready&&id==definition->id?definition:nullptr;},[this](const auto& id)->std::shared_ptr<const ModelAsset>{return ready&&id==model->id?model:nullptr;},[this](const auto& id)->std::shared_ptr<const AnimationAsset>{return ready&&id==animation->id?animation:nullptr;});}
};
void scene(SceneDocument& document,const Rig& rig,bool autoStart,bool animated=false){
    require(document.edit("Ragdoll runtime fixture",[&](auto& records,auto&){
        records.clear();EntityRecord floor;floor.id=100;floor.name="Floor";floor.collider.emplace();floor.collider->size={100,1,100};floor.local=math::translation({0,-.5f,0});records.push_back(floor);
        for(EntityId id:{1ull,2ull}){EntityRecord visual;visual.id=id;visual.name="Enemy visual";visual.model=AssetRef<ModelAsset>{rig.model->id};visual.local=math::translation({id==1?-2.f:2.f,3,0});visual.modelRagdoll={AssetRef<RagdollAsset>{rig.definition->id},autoStart};if(animated)visual.modelAnimation.asset.id=rig.animation->id;records.push_back(visual);}
    }),"Could not create ragdoll runtime fixture.");
}
ExtractedModel presented(PlaySession& play,EntityId id){auto extracted=extractScene(play.presentationScene());play.prepareRagdollPresentation(extracted);const auto found=std::find_if(extracted.models.begin(),extracted.models.end(),[&](const auto& value){return value.id==id;});require(found!=extracted.models.end(),"Runtime visual disappeared.");return *found;}
void tick(PlaySession& play,int count){for(int i=0;i<count;++i)play.advance(PlaySession::FixedStep,Vec2{},Camera{});}
void headlessStartResetAndIsolation(){
    Project project;Rig rig;SceneDocument authored(project.root);scene(authored,rig,true);const auto original=authored.scene().records();const auto history=authored.history().size();
    PlaySession play(project.root);rig.configure(play);require(play.start(authored),play.status().c_str());
    // No extraction/presentation call is allowed to activate the simulation.
    tick(play,30);require(play.ragdollActive(1)&&play.ragdollActive(2),"Start Simulated depended on a visible Scene/Game viewport.");require(!play.document().dirty()&&play.document().history().size()==0,"Headless physics polluted runtime document history.");
    const auto first=presented(play,1);require(first.physicsPose&&first.physicsPose->worldTransforms[3][13]<.4f,"Headless rig did not fall under gravity.");
    require(play.resetRagdoll(1)&&!play.ragdollActive(1)&&play.ragdollActive(2),"Reset affected an independent enemy instance.");tick(play,2);require(!play.ragdollActive(1),"Start Simulated immediately restarted a manually reset rig.");
    require(play.activateRagdoll(1)&&play.applyRagdollImpulse(1,{0,2,0}),play.status().c_str());
    auto moved=*play.document().scene().find(1);moved.local=math::translation({-3,4,0});require(play.document().updateEntity(1,moved),"Runtime placement edit failed.");play.advance(0,Vec2{},Camera{});require(play.ragdollActive(1),"Meaningful placement edit did not recreate automatic rig at the edited pose.");
    require(play.document().beginTransform("Move live rig",{1}),"Transform lock failed.");tick(play,1);require(!play.ragdollActive(1)&&play.ragdollActive(2),"An active transform edit did not release only its physical rig.");play.document().cancel();tick(play,1);require(play.ragdollActive(1),"Ending a transform edit did not restore automatic setup.");
    require(authored.history().size()==history&&authored.scene().records().size()==original.size(),"Play modified authored history/entities.");for(std::size_t i=0;i<original.size();++i)require(sameEntity(original[i],authored.scene().records()[i]),"Physics publication changed an authored transform or reference.");
    require(play.document().history().size()==1,"Physics simulation itself polluted runtime history.");
    play.stop();require(!play.ragdollActive(1)&&!play.playing(),"Stop retained a physical rig.");require(play.start(authored),"Restart failed.");tick(play,1);require(play.ragdollActive(1),"Restart retained the previous run's reset latch.");
}
void animationHandoffAndFixedClocks(){
    Project project;Rig rig;SceneDocument authored(project.root);scene(authored,rig,false,true);PlaySession play(project.root);rig.configure(play);require(play.start(authored),"Animated Play start failed.");tick(play,30);
    const auto animated=presented(play,1);require(std::abs(animated.animationSeconds-.5)<1e-8&&!animated.physicsPose,"Play animation clock did not advance with headless fixed ticks.");
    const auto expected=evaluateAnimationPose(*rig.animation,*rig.model,*rig.model,.5);
    require(play.activateRagdoll(1),play.status().c_str());const auto handoff=presented(play,1);require(bool(handoff.physicsPose),"Activated rig published no render pose.");
    for(std::size_t i=0;i<expected.worldTransforms.size();++i)for(std::size_t element=0;element<16;++element)require(std::abs(handoff.physicsPose->worldTransforms[i][element]-expected.worldTransforms[i][element])<.002f,"Handoff snapped to bind pose or lost wrapper/helper transforms.");
    auto view=extractScene(play.presentationScene());for(int i=0;i<10;++i)play.prepareRagdollPresentation(view);require(std::abs(presented(play,2).animationSeconds-.5)<1e-8,"Rendering the same fixed state advanced animation repeatedly.");
    play.advance(1,Vec2{},Camera{},false);play.advance(1,Vec2{},Camera{},true);require(std::abs(presented(play,2).animationSeconds-.5)<1e-8,"Suspension replayed elapsed wall time into animation.");tick(play,1);require(presented(play,2).animationSeconds>.5,"Resuming fixed ticks did not resume animation.");
    auto updated=std::make_shared<RagdollAsset>(*rig.definition);updated->bodies[0].mass=4;rig.definition=updated;play.advance(0,Vec2{},Camera{});require(!play.ragdollActive(1),"Changed shared definition retained stale live bodies on a manually activated instance.");
}
void preparingAndInvalidSetups(){
    Project project;Rig rig;SceneDocument authored(project.root);scene(authored,rig,true,true);PlaySession play(project.root);rig.ready=false;rig.configure(play);require(play.start(authored),"Pending rig blocked Play start.");tick(play,2);require(!play.ragdollActive(1),"Unavailable rig became active.");rig.ready=true;tick(play,1);require(play.ragdollActive(1),"Ready assets did not retry a previously pending automatic rig.");
    play.stop();auto invalid=std::make_shared<RagdollAsset>(*rig.definition);invalid->bodies.clear();rig.definition=invalid;require(play.start(authored),"Empty unfinished setup blocked Play startup.");tick(play,2);require(!play.ragdollActive(1)&&!play.status().empty(),"Invalid ready setup did not publish a useful diagnostic.");
    auto repaired=std::make_shared<RagdollAsset>(*invalid);generateRagdollBodies(*repaired,*rig.model,{3});rig.definition=repaired;tick(play,1);require(play.ragdollActive(1),"Repaired setup remained permanently latched as invalid.");
}
void collisionFilterEdits(){
    Project project;Rig rig;SceneDocument authored(project.root);scene(authored,rig,true);
    auto controller=*authored.scene().find(1);controller.actor.emplace();controller.controller.emplace();controller.controller->collision={EnemyCollisionLayer,AllCollisionLayers};require(authored.updateEntity(1,controller),"Could not author ragdoll collision owner.");
    PlaySession play(project.root);rig.configure(play);require(play.start(authored),"Filtered ragdoll Play start failed.");play.advance(0,Vec2{},Camera{});require(play.ragdollActive(1),"Filtered automatic ragdoll failed to activate.");
    const auto reference=ragdollReferenceTransforms(*rig.model);const auto center=math::transformPoint(controller.local,{reference[3][12],reference[3][13],reference[3][14]});const Ray ray{center+Vec3{0,1,0},{0,-1,0}};PhysicsQueryFilter filter;filter.mask=EnemyCollisionLayer;
    const auto enemy=play.raycast(ray,2,filter);require(enemy&&enemy->entity==1,"Ragdoll did not inherit its actor collision layers.");
    auto edited=*play.document().scene().find(1);edited.controller->collision.layers=PlayerCollisionLayer;require(play.document().updateEntity(1,edited),"Could not edit physical owner filter.");play.advance(0,Vec2{},Camera{});
    require(play.ragdollActive(1)&&!play.raycast(ray,2,filter),"Live ragdoll retained stale collision layers after an authored filter edit.");filter.mask=PlayerCollisionLayer;const auto player=play.raycast(ray,2,filter);require(player&&player->entity==1,"Updated ragdoll collision layers were not published.");
}
}
int main(){try{headlessStartResetAndIsolation();animationHandoffAndFixedClocks();preparingAndInvalidSetups();collisionFilterEdits();std::cout<<"Headless ragdoll startup, current-animation handoff, fixed clocks, edits, pending assets and Play isolation passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
