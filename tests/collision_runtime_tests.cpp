#include "scene/SceneComponents.h"
#include "navigation_fixture.h"
#include "editor/NavigationPreview.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
using namespace crimson::testing;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-collision-runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error);if(error)return;const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&path.parent_path()==temp&&path.filename().string().starts_with("crimson-collision-runtime-"))std::filesystem::remove_all(path,error);}
};
const auto SpeedSixty=canonicalAssetId("test:collision-runtime/speed-sixty");
EntityRecord floor(){EntityRecord result;result.id=1;result.name="Floor";result.collider=SceneCollider{{30,1,30}};result.local=math::translation({0,-.5f,0});return result;}
EntityRecord character(EntityId id,Vec3 position){EntityRecord result;result.id=id;result.name="Character";result.actor.emplace();result.controller.emplace();result.controller->collision.layers=PlayerCollisionLayer;result.motor.emplace();result.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{SpeedSixty}}};result.local=math::translation(position);return result;}
EntityRecord trigger(){EntityRecord result;result.id=3;result.name="Detector";result.collider=SceneCollider{{2,3,2}};result.collider->mode=ColliderMode::Trigger;result.collider->collision={TriggerCollisionLayer,PlayerCollisionLayer};result.local=math::translation({0,1.5f,0});return result;}
EntityRecord volume(){EntityRecord result;result.id=4;result.name="NavigationVolume";result.navigationVolume.emplace();result.navigationVolume->size={20,8,20};return result;}
void install(SceneDocument& document,std::vector<EntityRecord> records){require(document.edit("Fixture",[&](auto& destination,auto& properties){destination=std::move(records);properties={};}),document.status());}
void start(PlaySession& play,const SceneDocument& document){
    play.setAgentProfiles(agentProfileQuery());
    play.setGameplayEffects([](const std::string& id) {
        static const auto speed=[] {
            auto effect=std::make_shared<GameplayEffectAsset>();effect->id=SpeedSixty;effect->name="Fast collision fixture";
            effect->modifiers.push_back({stats::MovementSpeed,EffectModifierOperation::Override,{60,{},{}}});return effect;
        }();
        return id==SpeedSixty?EffectDefinitionSnapshot{speed,1,{}}:EffectDefinitionSnapshot{{},0,"Unknown fixture effect"};
    });require(play.start(document),play.status());}
bool contains(const std::vector<EntityId>& values,EntityId id){return std::find(values.begin(),values.end(),id)!=values.end();}

void batchesAndQueries(){
    Project project;SceneDocument document(project.root);install(document,{floor(),character(2,{-2,.05f,0}),trigger()});PlaySession play(project.root);start(play,document);
    require(play.setActorMovement(2,{60,0,0}),play.status());play.advance(4*PlaySession::FixedStep,Vec2{},Camera{});
    std::vector<TriggerEvent> transitions;for(const auto& event:play.triggerEvents())if(event.trigger==3&&event.other==2)transitions.push_back(event);
    require(transitions.size()==2&&transitions[0].kind==TriggerEventKind::Enter&&transitions[1].kind==TriggerEventKind::Exit,"An advance lost trigger transitions from one of its fixed ticks.");
    require(transitions[0].world!=0&&transitions[0].world==transitions[1].world&&transitions[0].tick<transitions[1].tick,"Trigger events lost their world or fixed-tick identity.");
    require(!contains(play.triggerOverlaps(3),2)&&play.triggerEvents().size()>=2,"Observing trigger state drained the frame event batch.");
    play.advance(0,Vec2{},Camera{});require(play.triggerEvents().empty(),"A zero-tick advance replayed prior trigger events.");
    require(play.clearActorMovement(2),play.status());require(play.document().edit("Teleport",[](auto& records,auto&){for(auto& record:records)if(record.id==2)record.local=math::translation({0,.05f,0});}),play.document().status());
    play.advance(PlaySession::FixedStep,Vec2{},Camera{});require(contains(play.triggerOverlaps(3),2),"Teleport did not update trigger membership.");
    const Ray ray{{0,5,0},{0,-1,0}};const auto body=play.raycast(ray,10);require(body&&body->entity==2,"Generic Play raycast did not expose the accepted controller capsule.");
    const auto ground=play.raycast(ray,10,PhysicsQueryFilter{WorldCollisionLayer,{},true,false,false});require(ground&&ground->entity==1,"World-only movement query hit a character or trigger.");
    const std::array<EntityId,1> ignored{2};const auto ignoredBody=play.raycast(ray,10,PhysicsQueryFilter{AllCollisionLayers,ignored,true,true,false});require(ignoredBody&&ignoredBody->entity==1,"Query entity exclusions did not omit the character.");
    const auto detector=play.raycast(ray,10,PhysicsQueryFilter{TriggerCollisionLayer,{},false,false,true});require(detector&&detector->entity==3,"Explicit trigger raycast did not hit the detector shape.");
    play.stop();require(play.triggerEvents().empty()&&play.triggerOverlaps(3).empty()&&!play.raycast(ray,10),"Stop retained trigger observations or the old query world.");
    start(play,document);play.advance(4*PlaySession::FixedStep,Vec2{},Camera{});require(play.triggerEvents().empty(),"Restart replayed old-world trigger transitions.");
}

void initialMembershipAndConfiguration(){
    Project project;SceneDocument document(project.root);install(document,{floor(),character(2,{0,.05f,0}),trigger()});PlaySession play(project.root);start(play,document);
    play.advance(3*PlaySession::FixedStep,Vec2{},Camera{});const auto events=play.triggerEvents();
    require(events.size()==1&&events[0].kind==TriggerEventKind::Enter&&events[0].reason==TriggerEventReason::Initial,"Spawn inside a detector did not emit one Initial Enter.");
    require(play.document().edit("Disable detection",[](auto& records,auto&){for(auto& record:records)if(record.id==3)record.collider->collision.mask=0;}),play.document().status());
    play.advance(PlaySession::FixedStep,Vec2{},Camera{});require(play.triggerEvents().size()==1&&play.triggerEvents()[0].kind==TriggerEventKind::Exit&&play.triggerEvents()[0].reason==TriggerEventReason::Configuration,"Mask edit did not publish a configuration Exit.");
    require(play.document().undo(),play.document().status());play.advance(PlaySession::FixedStep,Vec2{},Camera{});require(contains(play.triggerOverlaps(3),2),"Undo did not restore detection membership.");
    const auto before=play.tickCount();play.advance(1,Vec2{},Camera{},false);require(play.tickCount()==before&&play.triggerEvents().empty(),"Focus suspension advanced or repeated trigger events.");
}

void triggerNavigationIsolation(){
    Project project;SceneDocument document(project.root);install(document,{floor(),volume(),trigger()});PlaySession play(project.root);start(play,document);require(waitForNavigation(play),play.status());
    const auto initial=play.navigationBuildCount();const auto revision=play.navigationWorld()->revision(DefaultNavigationAgentProfileId);
    require(play.document().edit("Move trigger",[](auto& records,auto&){for(auto& record:records)if(record.id==3){record.local=math::translation({5,1.5f,0});record.collider->collision.mask=EnemyCollisionLayer;}}),play.document().status());
    play.advance(0,Vec2{},Camera{});require(play.navigationBuildCount()==initial&&play.navigationWorld()->revision(DefaultNavigationAgentProfileId)==revision,"Trigger movement or mask edit invalidated static navigation.");
    require(play.document().edit("Make solid",[](auto& records,auto&){for(auto& record:records)if(record.id==3){record.collider->mode=ColliderMode::Solid;record.collider->collision={WorldCollisionLayer,AllCollisionLayers};}}),play.document().status());
    require(waitForNavigation(play),play.status());require(play.navigationBuildCount()>initial,"Trigger-to-Solid did not add accepted navigation geometry.");const auto solidBuilds=play.navigationBuildCount();
    require(play.document().undo(),play.document().status());require(waitForNavigation(play),play.status());require(play.navigationBuildCount()>solidBuilds,"Undo to Trigger did not remove accepted navigation geometry.");
    ResourceManager resources;NavigationPreview preview(resources,project.root);
    const auto settle=[&]{const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);do{resources.pump();preview.update(document,{},agentProfileQuery());if(preview.world().readiness(DefaultNavigationAgentProfileId)!=NavigationStatus::Preparing)return;std::this_thread::yield();}while(std::chrono::steady_clock::now()<deadline);throw std::runtime_error("Edit trigger navigation fixture timed out.");};
    settle();require(preview.world().readiness(DefaultNavigationAgentProfileId)==NavigationStatus::Ready,"Actor-free trigger preview failed to bake the common world.");const auto editBuilds=preview.world().buildCount();
    require(document.edit("Move edit trigger",[](auto& records,auto&){for(auto& record:records)if(record.id==3)record.local=math::translation({-5,1.5f,0});}),document.status());settle();
    require(preview.world().buildCount()==editBuilds,"Edit preview treated a trigger as solid navigation input.");
}
}
int main(){try{batchesAndQueries();initialMembershipAndConfiguration();triggerNavigationIsolation();std::cout<<"Collision Play event batches, queries, lifecycle and navigation isolation checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
