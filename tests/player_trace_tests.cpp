#include "runtime/PlaySession.h"
#include "runtime/GameInput.h"
#include "scene/CameraAuthoring.h"
#include "scene/SceneExtraction.h"
#include "runtime/AnimationPlayback.h"
#include "ragdoll_fixture.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void near(Vec3 value,Vec3 expected,const char* message){require(math::length(value-expected)<1e-3f,message);}
void key(GameInput& input,bool down=true,bool repeat=false) {
    SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
    event.key.windowID=9;event.key.scancode=SDL_SCANCODE_F;event.key.repeat=repeat;
    require(input.handleEvent(event),"Owned F was not consumed.");
}
void fixture(SceneDocument& document) {
    require(document.edit("Trace fixture",[](auto& records,auto& properties){
        records.clear();properties={};
        EntityRecord floor;floor.id=1;floor.name="Floor";floor.collider=SceneCollider{{100,1,100}};
        floor.local=math::translation({0,-.5f,0});records.push_back(floor);
        EntityRecord player;player.id=2;player.name="Player";player.actor.emplace();player.controller.emplace();
        player.movement.emplace();player.playerInput.emplace();player.local=math::translation({0,.05f,0});records.push_back(player);
        const auto settings=defaultPlayerCameraTemplate();
        EntityRecord camera;camera.id=3;camera.name="Camera";camera.parent=2;camera.camera=settings.lens;
        camera.cameraFollow=SceneCameraFollow{2,settings.offset,settings.worldBasis};records.push_back(camera);
        EntityRecord target;target.id=4;target.name="Target";target.collider.emplace();
        target.collider->shape=ColliderShape::Sphere;target.collider->radius=1;
        target.collider->collision.layers=WorldCollisionLayer|EnemyCollisionLayer;
        target.local=math::translation({0,1.7f,-5});records.push_back(target);
        EntityRecord other;other.id=6;other.name="Other actor";other.actor.emplace();other.collider.emplace();
        other.collider->collision.layers=WorldCollisionLayer|CharacterCollisionLayer;
        other.local=math::translation({0,1.7f,-8});records.push_back(other);
        EntityRecord side=target;side.id=7;side.name="Side target";side.local=math::translation({5,1.7f,0});records.push_back(side);
        properties.playerCharacter=2;properties.activeCamera=3;
    }),"Could not author trace fixture.");
}
void playerCameraTraceAndLifetime() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-player-traces";
    SceneDocument authored(root);fixture(authored);const auto original=authored.scene().records();
    const auto history=authored.history().size();PlaySession play(root);require(play.start(authored),"Could not start trace fixture.");
    GameInput input;input.setViewport({0,0,800,600,true,false,9},true,false);input.acquire();
    play.advance(0,input,Camera{});const auto camera=resolveGameCamera(play.presentationScene());require(camera.has_value(),"Missing Player camera.");
    key(input);play.advance(0,input,Camera{});
    require(play.tickCount()==0,"The test trace required a physics tick.");
    auto records=play.traceVisualization().records();require(records.size()==1,"F did not record exactly one trace.");
    require(records[0].segments.size()==2 && records[0].contacts.size()==1,"Hit did not split the line or mark its contact.");
    const auto& clear=records[0].segments[0];const auto& blocked=records[0].segments[1];
    require(!clear.afterHit && blocked.afterHit,"Trace colors changed before the impact.");
    near(clear.start,camera->position,"Trace did not start at the eye camera.");
    near(clear.end,{0,1.7f,-4},"Player's own capsule blocked the trace or the target surface was missed.");
    near(blocked.start,clear.end,"Green and red segments did not meet at the hit.");
    near(blocked.end,camera->position+camera->forward()*10,"Trace did not retain the full configured length.");
    require(!play.document().dirty() && play.document().history().size()==0,"Trace visualization changed runtime scene/history.");
    key(input,true,true);play.advance(0,input,Camera{});
    require(play.traceVisualization().records().size()==1,"F key repeat generated another trace.");
    // Expiration advances on render frames, independently of fixed simulation.
    for(int i=0;i<49;++i)play.advance(.01,input,Camera{});
    require(play.traceVisualization().records().size()==1,"Default duration expired before half a second.");
    play.advance(.011,input,Camera{});require(play.traceVisualization().records().empty(),"Default duration did not hide the trace.");
    require(play.tickCount()==30,"The runtime stopped advancing while checking trace expiry.");
    require(play.document().edit("Select only Character layers",[](auto& values,auto&){
        for(auto& value:values)if(value.id==2)value.playerInput->traceTest.collisionMask=CharacterCollisionLayer;
    }),"Could not change trace mask.");
    key(input,false);key(input);play.advance(0,input,Camera{});
    records=play.traceVisualization().records();require(records.size()==1&&records[0].segments.size()==2,"The Character-only mask failed to detect another actor.");
    require(records[0].segments[0].end[2]<-7,"A trace mask selected an excluded target.");
    play.stop();require(play.traceVisualization().records().empty(),"Stop retained trace geometry.");
    const auto after=authored.scene().records();
    require(after.size()==original.size() && authored.history().size()==history,"Play traces changed authored document history.");
    for(std::size_t i=0;i<after.size();++i)require(sameEntity(after[i],original[i]),"Play traces changed authored scene data.");
}
void currentLookMaskAndOptionalDrawing() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-player-trace-look";
    SceneDocument authored(root);fixture(authored);PlaySession play(root);require(play.start(authored),"Could not start look fixture.");
    GameInput input;input.setViewport({0,0,800,600,true,false,9},true,false);input.acquire();play.advance(0,input,Camera{});
    SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.windowID=9;motion.motion.xrel=900;
    require(input.handleEvent(motion),"Mouse look was rejected.");key(input);play.advance(0,input,Camera{});
    auto records=play.traceVisualization().records();require(records.size()==1 && records[0].contacts.size()==1,"Same-frame camera look missed the side target.");
    require(records[0].segments[0].end[0]>3.99f && std::abs(records[0].segments[0].end[2])<1e-3,
        "Trace used last frame's view instead of current mouse look.");
    PhysicsTrace query;query.ray={{0,1.7f,0},{0,0,-1}};query.maxDistance=10;
    const auto count=records.size();const auto result=play.trace(query,PhysicsQueryFilter{EnemyCollisionLayer});
    require(result.valid&&result.hits.size()==1&&result.hits[0].entity==4,"Public Play trace ignored query layers.");
    require(play.traceVisualization().records().size()==count,"A query without visualization created a drawing.");
    require(play.trace(query,PhysicsQueryFilter{0}).hits.empty(),"A zero mask detected collision.");
    require(play.document().edit("Hide test traces",[](auto& values,auto&){
        for(auto& value:values)if(value.id==2)value.playerInput->traceTest.duration=0;
    }),"Could not change trace duration.");
    key(input,false);key(input);play.advance(0,input,Camera{});
    require(play.traceVisualization().records().size()==count,"Duration zero recorded visible geometry.");
    key(input,false);key(input);input.cancel();play.advance(0,input,Camera{});
    require(play.traceVisualization().records().size()==count,"A canceled queued F press fired a trace.");
    play.advance(0,input,Camera{},false);require(play.traceVisualization().records().empty(),"Suspension retained temporary traces.");
    play.stop();require(!play.trace(query).valid,"An inactive Play session queried a destroyed physics world.");
}
struct RagdollTraceRig {
    std::shared_ptr<ModelAsset> model=std::make_shared<ModelAsset>();
    std::shared_ptr<RagdollAsset> definition;
    std::shared_ptr<AnimationAsset> animation;
    explicit RagdollTraceRig(bool animated=false) {
        model->id=canonicalAssetId("player-trace-ragdoll-source");model->name="Trace creature";
        model->nodes.resize(3);model->nodes[0].name="Wrapper";model->nodes[0].children={1,2};
        model->nodes[1].name="Off-ray bone";model->nodes[1].parent=0;model->nodes[1].local=math::translation({3,1.7f,0});
        model->nodes[2].name="Hit bone";model->nodes[2].parent=0;model->nodes[2].local=math::translation({animated?-2.f:0.f,1.7f,0});
        model->roots={0};model->skins.push_back({"Trace skin",{1,2},{Mat4{},Mat4{}},0});
        if(animated) {
            ModelAnimationChannel channel;channel.node=2;channel.path=ModelAnimationPath::Translation;channel.components=3;
            channel.times={0,2};channel.values={-2,1.7f,0,2,1.7f,0};model->animations.push_back({"Cross the ray",0,2,{channel}});
            animation=std::make_shared<AnimationAsset>(importedAnimationAsset(*model,0));
        }
        definition=std::make_shared<RagdollAsset>(makeRagdollAsset(*model));generateRagdollBodies(*definition,*model);
        definition->gravityFactor=0;
        // Separate physical roots expose an accidental whole-rig impulse without
        // relying on a constrained limb's exact solver response. The hit is body 1.
        require(definition->bodies.size()==2&&definition->bodies[1].node==2,"Unexpected trace rig body order.");
        for(auto& body:definition->bodies){body.shape=RagdollShape::Sphere;body.radius=.3f;body.position={};body.rotation={};body.mass=2;}
    }
    void configure(PlaySession& play) const {
        play.setRagdollDefinitions([this](const auto& id)->std::shared_ptr<const RagdollAsset>{return id==definition->id?definition:nullptr;},
            [this](const auto& id)->std::shared_ptr<const ModelAsset>{return id==model->id?model:nullptr;},
            [this](const auto& id)->std::shared_ptr<const AnimationAsset>{return animation&&id==animation->id?animation:nullptr;});
    }
};
void ragdollFixture(SceneDocument& document,const RagdollTraceRig& rig,float impulse,bool side=false) {
    fixture(document);
    require(document.edit("Inactive trace ragdoll",[&](auto& records,auto&){
        std::erase_if(records,[](const auto& record){return record.id==4||record.id==6||record.id==7;});
        for(auto& record:records)if(record.id==2)record.playerInput->traceTest.hitImpulse=impulse;
        EntityRecord visual;visual.id=4;visual.name="Inactive enemy";visual.model=AssetRef<ModelAsset>{rig.model->id};
        visual.local=math::translation(side?Vec3{5,0,0}:Vec3{0,0,-5});visual.modelRagdoll={AssetRef<RagdollAsset>{rig.definition->id},false};
        if(rig.animation)visual.modelAnimation.asset.id=rig.animation->id;records.push_back(visual);
    }),"Could not author an inactive ragdoll trace fixture.");
}
void ownInput(GameInput& input){input.setViewport({0,0,800,600,true,false,9},true,false);input.acquire();}
void fire(PlaySession& play,GameInput& input){key(input,false);key(input);play.advance(0,input,Camera{});}
void tick(PlaySession& play,int count){for(int i=0;i<count;++i)play.advance(PlaySession::FixedStep,Vec2{},Camera{});}
ExtractedModel ragdollPresentation(PlaySession& play) {
    auto scene=extractScene(play.presentationScene());play.prepareRagdollPresentation(scene);
    const auto found=std::find_if(scene.models.begin(),scene.models.end(),[](const auto& model){return model.id==4;});
    require(found!=scene.models.end(),"The hit model vanished from presentation.");return *found;
}
Vec3 worldBone(const ExtractedModel& model,std::size_t node) {
    require(bool(model.physicsPose),"The activated model has no physical presentation pose.");
    const auto& bone=model.physicsPose->worldTransforms.at(node);return math::transformPoint(model.world,{bone[12],bone[13],bone[14]});
}
Vec3 impulseResponse(float impulse,int repeats=1,bool side=false) {
    ragdoll_test::Project project;RagdollTraceRig rig;SceneDocument authored(project.root);ragdollFixture(authored,rig,impulse,side);
    const auto original=authored.scene().records();const auto history=authored.history().size();
    PlaySession play(project.root);rig.configure(play);require(play.start(authored),"Could not start ragdoll trace fixture.");
    GameInput input;ownInput(input);play.advance(0,input,Camera{});require(!play.ragdollActive(4),"Start Simulated=false activated before a trace.");
    if(side){SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.windowID=9;motion.motion.xrel=900;require(input.handleEvent(motion),"Side-look input was rejected.");}
    for(int i=0;i<repeats;++i)fire(play,input);
    require(play.ragdollActive(4),"F did not activate an assigned inactive ragdoll.");
    require(play.tickCount()==0,"Ragdoll hit activation required a physics tick.");
    const auto visuals=play.traceVisualization().records();require(visuals.size()==std::size_t(repeats)&&visuals.back().contacts.size()==1,"A ragdoll hit did not create its contact visualization.");
    const auto camera=resolveGameCamera(play.presentationScene());require(camera.has_value(),"Ragdoll test lost its eye camera.");
    PhysicsTrace query;query.ray={camera->position,camera->forward()};query.maxDistance=10;
    const std::array<EntityId,1> ignored{2};const auto hit=play.trace(query,PhysicsQueryFilter{AllCollisionLayers,ignored});
    require(hit.valid&&hit.hits.size()==1&&hit.hits[0].entity==4&&hit.hits[0].ragdollBodyIndex==1,"The activated collision world lost the exact hit body index.");
    const auto initial=ragdollPresentation(play);const auto hitStart=worldBone(initial,2),otherStart=worldBone(initial,1);
    tick(play,12);const auto final=ragdollPresentation(play);const auto displacement=worldBone(final,2)-hitStart;
    near(worldBone(final,1),otherStart,"The trace kicked another physical body instead of just the selected bone.");
    if(impulse==0)near(displacement,{},"A zero hit impulse imparted motion.");
    else require(math::dot(displacement,camera->forward())>.1f,"The hit impulse did not move the selected body along the trace ray.");
    require(!play.document().dirty()&&play.document().history().size()==0,"A trace impulse changed runtime scene/history.");
    require(play.resetRagdoll(4)&&!play.ragdollActive(4),"Reset retained the hit ragdoll.");
    play.advance(0,input,Camera{});require(!ragdollPresentation(play).physicsPose,"Reset retained a simulated render pose.");
    play.stop();require(!play.ragdollActive(4)&&play.traceVisualization().records().empty(),"Stop retained ragdoll bodies or trace drawings.");
    require(authored.history().size()==history&&authored.scene().records().size()==original.size(),"Trace impulses changed authored history/entities.");
    for(std::size_t i=0;i<original.size();++i)require(sameEntity(original[i],authored.scene().records()[i]),"Trace impulses changed authored transforms or assignment.");
    require(play.start(authored),"Could not restart the inactive ragdoll fixture.");play.advance(0,Vec2{},Camera{});require(!play.ragdollActive(4),"A trace activation survived Stop/restart despite Start Simulated=false.");
    return displacement;
}
void ragdollImpulseMagnitudeDirectionAndRepeat() {
    const auto weak=impulseResponse(4),strong=impulseResponse(12),repeat=impulseResponse(4,2),side=impulseResponse(4,1,true);
    require(weak[2]<-.1f&&std::abs(weak[0])<.001f,"Forward trace impulse used an unrelated direction.");
    require(strong[2]<weak[2]*2.5f,"Configured impulse magnitude did not increase the selected body's response.");
    require(repeat[2]<weak[2]*1.7f,"A repeat F hit did not apply another impulse to the active rig.");
    require(side[0]>.1f&&std::abs(side[2])<.001f,"The impulse ignored same-frame Player look direction.");
    impulseResponse(0);
}
void ragdollCurrentPoseHit() {
    ragdoll_test::Project project;RagdollTraceRig rig(true);SceneDocument authored(project.root);ragdollFixture(authored,rig,0);
    PlaySession play(project.root);rig.configure(play);require(play.start(authored),"Could not start animated trace fixture.");GameInput input;ownInput(input);
    fire(play,input);require(!play.ragdollActive(4),"An off-ray bind-pose body was treated as a hit.");
    tick(play,60);const auto before=ragdollPresentation(play);require(std::abs(before.animationSeconds-1)<1e-7,"Animation did not reach the aimed current pose.");
    const auto expected=evaluateAnimationPose(*rig.animation,*rig.model,*rig.model,before.animationSeconds);
    const auto ticks=play.tickCount();fire(play,input);require(play.ragdollActive(4)&&play.tickCount()==ticks,"Current-pose hit did not activate immediately between physics ticks.");
    const auto after=ragdollPresentation(play);require(bool(after.physicsPose),"Current-pose activation published no physical pose.");
    for(std::size_t node=0;node<expected.worldTransforms.size();++node)for(std::size_t element=0;element<16;++element)
        require(std::abs(after.physicsPose->worldTransforms[node][element]-expected.worldTransforms[node][element])<.002f,"F activation snapped the hit rig from its current animation to bind pose.");
    const auto contact=play.traceVisualization().records().back();require(contact.contacts.size()==1&&std::abs(contact.segments[0].end[0])<.001f,"The current animation body's ray contact was missed.");
}
void ragdollOcclusionMaskAndSelfIgnore() {
    ragdoll_test::Project project;RagdollTraceRig rig;SceneDocument authored(project.root);ragdollFixture(authored,rig,0);
    require(authored.edit("Occluder and owned visual",[&](auto& records,auto&){
        EntityRecord wall;wall.id=8;wall.name="Foreground wall";wall.collider.emplace();wall.collider->size={2,2,.4f};wall.local=math::translation({0,1.7f,-2});records.push_back(wall);
        EntityRecord owned;owned.id=10;owned.name="Player's attached rig";owned.parent=2;owned.local=math::translation({0,0,-1});owned.model=AssetRef<ModelAsset>{rig.model->id};owned.modelRagdoll={AssetRef<RagdollAsset>{rig.definition->id},false};records.push_back(owned);
    }),"Could not author trace blockers.");
    PlaySession play(project.root);rig.configure(play);require(play.start(authored),"Could not start occlusion fixture.");GameInput input;ownInput(input);fire(play,input);
    require(!play.ragdollActive(4)&&!play.ragdollActive(10),"The trace activated a ragdoll behind a wall or in the Player's owned hierarchy.");
    require(play.traceVisualization().records().back().contacts.size()==1&&std::abs(play.traceVisualization().records().back().segments[0].end[2]+1.8f)<.001f,"The trace did not retain the foreground wall contact.");
    require(play.document().edit("Remove wall and exclude World",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.id==8;});for(auto& record:records)if(record.id==2)record.playerInput->traceTest.collisionMask=EnemyCollisionLayer;}),"Could not change the trace mask.");
    fire(play,input);require(!play.ragdollActive(4)&&!play.ragdollActive(10),"An inactive posed candidate bypassed the Player trace collision mask.");
    require(play.traceVisualization().records().back().contacts.empty(),"An excluded ragdoll retained a hit contact.");
    require(play.document().edit("Restore all trace layers",[](auto& records,auto&){for(auto& record:records)if(record.id==2)record.playerInput->traceTest.collisionMask=AllCollisionLayers;}),"Could not restore trace mask.");
    fire(play,input);require(play.ragdollActive(4)&&!play.ragdollActive(10),"Self-ignore did not skip the Player's closer attached rig and hit the enemy.");
    require(std::abs(play.traceVisualization().records().back().segments[0].end[2]+4.7f)<.001f,"Self-ignore changed the actual enemy contact.");
}
void ragdollReplacesEnemyCapsuleWithFallback() {
    // Hit, valid current-pose miss, incompatible binding, unavailable setup.
    for(int scenario=0;scenario<4;++scenario) {
        ragdoll_test::Project project;RagdollTraceRig rig(scenario==1);SceneDocument authored(project.root);ragdollFixture(authored,rig,0);
        auto profile=std::make_shared<NavigationAgentProfile>(*defaultNavigationAgentProfile());
        profile->id=canonicalAssetId("trace-large-enemy-capsule");profile->traversal.height=3;profile->traversal.radius=1;
        const bool authoredEnemy=authored.edit("Enemy capsule and child rig",[&](auto& records,auto&){
            EntityRecord enemy;enemy.id=20;enemy.name="Enemy";enemy.actor.emplace();enemy.enemy.emplace();enemy.controller.emplace();
            enemy.movement.emplace();enemy.navigation.emplace();
            enemy.controller->profile.id=profile->id;enemy.controller->collision={EnemyCollisionLayer,AllCollisionLayers};enemy.local=math::translation({0,0,-5});records.push_back(enemy);
            for(auto& record:records){if(record.id==2)record.playerInput->traceTest.collisionMask=EnemyCollisionLayer;if(record.id==4){record.parent=20;record.local=Mat4{};}}
        });
        require(authoredEnemy,("Could not author the enemy capsule replacement fixture: "+authored.status()).c_str());
        if(scenario==2)rig.definition->bindingSignature=canonicalAssetId("incompatible-trace-skeleton");
        PlaySession play(project.root);rig.configure(play);play.setAgentProfiles([profile](const auto& id)->std::shared_ptr<const NavigationAgentProfile>{return id==profile->id?profile:nullptr;});
        if(scenario==3)play.setRagdollDefinitions([](const auto&)->std::shared_ptr<const RagdollAsset>{return nullptr;},
            [&rig](const auto& id)->std::shared_ptr<const ModelAsset>{return id==rig.model->id?rig.model:nullptr;});
        require(play.start(authored),"Could not start enemy capsule replacement fixture.");GameInput input;ownInput(input);play.advance(0,input,Camera{});
        const auto camera=resolveGameCamera(play.presentationScene());require(camera.has_value(),"Enemy capsule fixture has no eye camera.");
        PhysicsTrace query;query.ray={camera->position,camera->forward()};query.maxDistance=10;
        const auto coarse=play.trace(query,PhysicsQueryFilter{EnemyCollisionLayer});
        require(coarse.valid&&coarse.hits.size()==1&&coarse.hits[0].entity==20&&coarse.hits[0].ragdollBodyIndex==-1&&coarse.hits[0].distance<4.5f,
            "The fixture did not put a coarse Enemy/controller capsule in front of its child bone.");
        fire(play,input);const auto& visual=play.traceVisualization().records().back();
        if(scenario==0) {
            require(play.ragdollActive(4)&&play.tickCount()==0,"A coarse enemy capsule blocked immediate child-ragdoll activation.");
            require(visual.contacts.size()==1&&std::abs(visual.segments[0].end[2]+4.7f)<.001f,"F used the enemy capsule contact instead of the posed child bone.");
            const auto active=play.trace(query,PhysicsQueryFilter{EnemyCollisionLayer});
            require(active.hits.size()==1&&active.hits[0].entity==4&&active.hits[0].owner==20&&active.hits[0].ragdollBodyIndex==1,
                "The child ragdoll did not retain its Enemy owner, query mask or exact hit-body index.");
        }else {
            require(!play.ragdollActive(4),"A capsule-only hit activated a missed or unusable ragdoll setup.");
            if(scenario==1)require(visual.contacts.empty()&&visual.segments.size()==1,"A valid posed-rig miss retained its coarse capsule-only hit.");
            else {require(visual.contacts.size()==1,"An incompatible or unavailable rig suppressed its original capsule fallback.");near(visual.segments[0].end,coarse.hits[0].position,"The fallback contact did not match the native Enemy capsule.");}
        }
    }
}
}
int main(){try{playerCameraTraceAndLifetime();currentLookMaskAndOptionalDrawing();ragdollImpulseMagnitudeDirectionAndRepeat();ragdollCurrentPoseHit();ragdollOcclusionMaskAndSelfIgnore();ragdollReplacesEnemyCapsuleWithFallback();std::cout<<"Player camera traces, posed ragdoll hit impulses, occlusion and isolation checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
