#include "navigation_fixture.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
using namespace crimson::testing;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Scratch {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-nav-runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch(){std::filesystem::create_directories(root);}
    ~Scratch(){std::error_code error;const auto absolute=std::filesystem::absolute(root).lexically_normal();if(std::filesystem::equivalent(absolute.parent_path(),std::filesystem::temp_directory_path(),error)&&!error)std::filesystem::remove_all(absolute,error);}
};
EntityRecord ground(){EntityRecord result;result.id=1;result.name="Ground";result.local=math::translation({0,-.5f,0});result.collider=SceneCollider{{20,1,20}};return result;}
EntityRecord actor(EntityId id=2,AssetRef<NavigationAgentProfile> selected=AssetRef<NavigationAgentProfile>{DefaultNavigationAgentProfileId}) {
    EntityRecord result;result.id=id;result.name="Enemy";result.actor.emplace();result.controller.emplace();result.controller->profile=selected;result.motor.emplace();result.navigation.emplace();result.local=math::translation({-3,.05f,0});return result;
}
EntityRecord bounds(std::vector<AssetRef<NavigationAgentProfile>> supported={AssetRef<NavigationAgentProfile>{DefaultNavigationAgentProfileId}}) {
    EntityRecord result;result.id=3;result.name="NavigationVolume";result.navigationVolume.emplace();result.navigationVolume->size={24,12,24};result.navigationVolume->profiles=std::move(supported);return result;
}
void author(SceneDocument& document,std::vector<EntityRecord> records) {
    require(document.edit("Runtime navigation fixture",[&](auto& scene,auto& properties){scene=std::move(records);properties={};}),document.status());
}
void settle(PlaySession& play,ResourceManager* resources=nullptr) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    do{
        if(resources)resources->pump();play.advance(0,Vec2{},Camera{});bool pending=false;
        for(const auto& record:play.document().scene().records())if(record.navigationVolume)for(const auto& reference:record.navigationVolume->profiles) {
            const auto status=play.navigationReadiness(reference.id);require(status!=NavigationStatus::Failed&&status!=NavigationStatus::Unavailable,play.status());pending|=status==NavigationStatus::Preparing;
        }
        if(!pending)return;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(std::chrono::steady_clock::now()<end);
    throw std::runtime_error("Runtime navigation did not settle: "+play.status());
}
void absenceAndUnsupportedTypes() {
    Scratch scratch;SceneDocument document(scratch.root);author(document,{ground(),actor()});PlaySession play(scratch.root);play.setAgentProfiles(agentProfileQuery());
    require(play.start(document),play.status());require(play.navigationReadiness(DefaultNavigationAgentProfileId)==NavigationStatus::Unavailable,"No-volume scene acquired automatic navigation.");
    require(!play.setActorDestination(2,{3,0,0})&&play.actorPath(2).empty(),"No-volume command used hidden whole-world navigation.");
    for(const auto& record:play.document().scene().records())require(!record.navigationVolume,"Starting Play authored a volume.");play.stop();
    const auto unsupported=agentProfile({2,.5f,35,.2f});author(document,{ground(),actor(2,unsupported),bounds()});
    require(play.start(document),play.status());settle(play);
    require(play.navigationReadiness(unsupported.id)==NavigationStatus::Unavailable&&!play.setActorDestination(2,{3,0,0}),"Unsupported actor type fell back to another profile.");
    require(play.navigationProfileCount()==1,"Actor silently appended its type to the supported list.");play.stop();
}
void preparingIntentResumes() {
    Scratch scratch;ResourceConfig config;config.workers=1;ResourceManager resources(config);
    std::atomic_bool entered{},released{};ResourceRequestOptions options;options.priority=ResourcePriority::Selected;
    auto blocker=resources.requestDerived<NavigationTileData>("navigation-runtime-test-blocker",[&](std::stop_token stop){entered=true;while(!released&&!stop.stop_requested())std::this_thread::yield();return ResourceLoadResult<NavigationTileData>{std::make_shared<NavigationTileData>(),sizeof(NavigationTileData),1,{}};},options);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!entered&&std::chrono::steady_clock::now()<deadline){resources.pump();std::this_thread::yield();}
    require(entered,"Fixture worker did not start.");SceneDocument document(scratch.root);author(document,{ground(),actor(),bounds()});PlaySession play(scratch.root);play.setResourceManager(resources);play.setAgentProfiles(agentProfileQuery());
    require(play.start(document),play.status());require(play.navigationReadiness(DefaultNavigationAgentProfileId)==NavigationStatus::Preparing,"Queued fixture did not remain preparing.");
    const Vec3 target{4,0,0};require(play.setActorDestination(2,target),"Preparing navigation rejected persistent movement intent.");
    require(play.setActorDestination(2,target),"Repeated preparing command lost its accepted state.");require(play.actorPath(2).empty(),"Preparing request invented a route before publication.");
    released=true;settle(play,&resources);require(play.actorDestination(2)==target&&!play.actorPath(2).empty(),"Prepared tiles did not resume the retained destination.");
    for(unsigned tick=0;tick<120;++tick)play.advance(1.0/60,Vec2{},Camera{});
    require(play.document().scene().worldTransform(2)->at(0,3)>3.7f,"Resumed intent failed to drive the motor.");
    require(document.scene().find(2)->local.at(0,3)==-3,"Play navigation changed authored actor placement.");play.stop();
}
void prepareBeforeSpawnAndReuseAfterDespawn() {
    Scratch scratch;const auto large=agentProfile({2.4f,.7f,35,.2f});SceneDocument document(scratch.root);author(document,{ground(),bounds({AssetRef<NavigationAgentProfile>{DefaultNavigationAgentProfileId},large})});
    PlaySession play(scratch.root);play.setAgentProfiles(agentProfileQuery());require(play.start(document),play.status());settle(play);
    require(play.navigationReadiness(large.id)==NavigationStatus::Ready&&play.projectToNavigation(large.id,{0,0,0}).point.has_value(),"Unused enemy type was not prepared before spawning.");
    const auto builds=play.navigationBuildCount();require(play.document().edit("Spawn enemy",[&](auto& records,auto&){records.push_back(actor(10,large));}),play.document().status());
    play.advance(0,Vec2{},Camera{});require(play.navigationBuildCount()==builds&&play.setActorDestination(10,{3,0,0}),"Spawning a supported type rebuilt navigation or could not query it.");
    require(play.document().edit("Despawn enemy",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.id==10;});}),play.document().status());
    play.advance(0,Vec2{},Camera{});require(play.navigationBuildCount()==builds&&play.navigationReadiness(large.id)==NavigationStatus::Ready,"Despawn discarded an authored supported profile.");
    require(play.document().edit("Respawn enemy",[&](auto& records,auto&){records.push_back(actor(11,large));}),play.document().status());
    play.advance(0,Vec2{},Camera{});require(play.navigationBuildCount()==builds&&play.setActorDestination(11,{3,0,0}),"Respawn failed to reuse accepted tiles.");play.stop();
}
void liveSharedProfileAndMissingDefinition() {
    Scratch scratch;const auto selected=agentProfile({1.2f,.25f,45,.3f});SceneDocument document(scratch.root);author(document,{ground(),actor(2,selected),bounds({selected})});
    PlaySession play(scratch.root);play.setAgentProfiles(agentProfileQuery());require(play.start(document),play.status());settle(play);require(play.setActorDestination(2,{4,0,0}),"Initial live-profile route failed.");
    const auto before=play.navigationBuildCount();auto replacement=std::make_shared<NavigationAgentProfile>(*agentProfiles().at(selected.id));replacement->traversal={2.2f,.5f,25,.1f};agentProfiles()[selected.id]=replacement;
    play.advance(0,Vec2{},Camera{});require(play.acceptedAgentProfile(selected.id)&&play.acceptedAgentProfile(selected.id)->traversal==replacement->traversal,"Physics and runtime did not accept the same immutable profile generation.");
    require(play.navigationReadiness(selected.id)==NavigationStatus::Preparing&&play.actorPath(2).empty()&&play.navigationBuildCount()>before,"Live profile edit continued using obsolete path clearance.");
    settle(play);require(play.actorDestination(2)==Vec3{4,0,0},"Profile edit discarded movement intent while rebuilding.");
    agentProfiles().erase(selected.id);play.advance(0,Vec2{},Camera{});require(play.navigationReadiness(selected.id)==NavigationStatus::Unavailable&&play.actorPath(2).empty(),"Missing custom profile used a default navigation definition.");
    const auto beforeMissing=*play.document().scene().worldTransform(2);require(play.setActorMovement(2,{4,0,0}),"Missing-profile actor command fixture failed.");
    for(unsigned tick=0;tick<30;++tick)play.advance(1.0/60,Vec2{},Camera{});
    const auto afterMissing=*play.document().scene().worldTransform(2);require(std::abs(afterMissing.at(0,3)-beforeMissing.at(0,3))<.01f&&std::abs(afterMissing.at(1,3))<.12f,"Missing profile lost the retained grounded capsule or allowed stale horizontal movement.");
    agentProfiles()[selected.id]=replacement;settle(play);require(play.navigationReadiness(selected.id)==NavigationStatus::Ready,"Restored custom profile did not recover navigation.");play.stop();
}
void liveVolumeEditsCoalesceAndFlush() {
    Scratch scratch;SceneDocument document(scratch.root);author(document,{ground(),actor(),bounds()});PlaySession play(scratch.root);play.setAgentProfiles(agentProfileQuery());
    require(play.start(document),play.status());settle(play);auto& edited=play.document();const auto before=play.navigationBuildCount();
    const auto resize=[&](float width){require(edited.update([width](auto& records,auto&){for(auto& record:records)if(record.id==3)record.navigationVolume->size[0]=width;}),edited.status());};
    require(edited.begin("Live navigation bounds"),edited.status());resize(22);
    // Establish a fresh accepted edit interval before the immediately following
    // update, rather than depending on the initial bake's wall-clock duration.
    std::this_thread::sleep_for(std::chrono::milliseconds(110));play.advance(0,Vec2{},Camera{});
    const auto first=play.navigationBuildCount();require(first>before,"An active gesture failed to publish after its update interval.");
    settle(play);const Vec3 retainedTarget{4,0,0};require(play.setActorDestination(2,retainedTarget)&&!play.actorPath(2).empty(),"Coalescing fixture could not establish an active route.");
    // Refresh the interval with an unchanged release after the worker wait, so
    // slow cache I/O cannot make the next live resize miss the timing window.
    require(edited.commit(),edited.status());play.advance(0,Vec2{},Camera{});require(edited.begin("Continuous navigation bounds"),edited.status());
    resize(20);require(edited.scene().find(3)->navigationVolume->size[0]==20,"Coalescing delayed the authored Inspector/handle value.");
    play.advance(0,Vec2{},Camera{});require(play.navigationBuildCount()==first&&play.navigationWorld()->summary(DefaultNavigationAgentProfileId).pendingSources,"Immediate resize rebuilt navigation before the 100 ms interval or failed to mark the old display pending.");
    require(play.actorPath(2).empty()&&!play.actorDestination(2),"Coalesced volume change continued steering an obsolete route.");
    // Repeated unchanged frames must not push the deadline forward forever.
    for(unsigned frame=0;frame<8;++frame){std::this_thread::sleep_for(std::chrono::milliseconds(10));play.advance(0,Vec2{},Camera{});}
    std::this_thread::sleep_for(std::chrono::milliseconds(40));play.advance(0,Vec2{},Camera{});
    const auto interim=play.navigationBuildCount();require(interim>first,"Unchanged frames starved an interim navigation update.");
    resize(18);play.advance(0,Vec2{},Camera{});const auto deferred=play.navigationBuildCount();
    require(edited.commit(),edited.status());play.advance(0,Vec2{},Camera{});
    require(play.navigationBuildCount()>deferred&&!play.navigationWorld()->summary(DefaultNavigationAgentProfileId).pendingSources,"Releasing a gesture did not flush its final volume immediately.");settle(play);
    require(edited.scene().find(3)->navigationVolume->size[0]==18,"Final navigation bounds were lost.");
    require(play.actorDestination(2)==retainedTarget&&!play.actorPath(2).empty(),"Coalescing discarded the paused route's original intent.");
    require(edited.begin("Cancel navigation bounds"),edited.status());resize(16);play.advance(0,Vec2{},Camera{});edited.cancel();play.advance(0,Vec2{},Camera{});settle(play);
    require(edited.scene().find(3)->navigationVolume->size[0]==18,"Cancel did not restore authored navigation bounds.");
    const auto restored=play.navigationWorld()->query(DefaultNavigationAgentProfileId,{0,.02f,0},{8,0,0});
    require(restored.path&&restored.path->corners.back()==Vec3{8,0,0},"Canceled resize retained the smaller navigation coverage.");play.stop();
}
}
int main(){try{absenceAndUnsupportedTypes();preparingIntentResumes();prepareBeforeSpawnAndReuseAfterDespawn();liveSharedProfileAndMissingDefinition();liveVolumeEditsCoalesceAndFlush();std::cout<<"Navigation volume, preparation, spawn reuse, gesture coalescing and coherent profile runtime checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
