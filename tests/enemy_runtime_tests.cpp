#include "scene/SceneComponents.h"
#include "resources/AssetId.h"
#include "navigation_fixture.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
using namespace crimson::testing;

void require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}

struct Scratch {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-enemy-runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch(){std::filesystem::create_directories(root);}
    ~Scratch(){std::error_code error;const auto absolute=std::filesystem::absolute(root).lexically_normal();if(std::filesystem::equivalent(absolute.parent_path(),std::filesystem::temp_directory_path(),error)&&!error)std::filesystem::remove_all(absolute,error);}
};

EntityRecord floor() {
    EntityRecord result;result.id=1;result.name="Floor";result.collider=SceneCollider{{30,1,30}};result.local=math::translation({0,-.5f,0});return result;
}
const std::string SpeedThree=canonicalAssetId("test:enemy-runtime/speed-three");
const std::string SpeedTwo=canonicalAssetId("test:enemy-runtime/speed-two");
GameplayEffectQuery fixtureEffects() {
    const auto definition=[](const std::string& id,double speed) {
        GameplayEffectAsset result;result.id=id;result.name="Enemy fixture speed";
        result.modifiers={{stats::MovementSpeed,EffectModifierOperation::Override,EffectMagnitude{speed,{},{}}}};
        return std::make_shared<const GameplayEffectAsset>(std::move(result));
    };
    return [three=definition(SpeedThree,3),two=definition(SpeedTwo,2)](const std::string& id) {
        return EffectDefinitionSnapshot{id==SpeedThree?three:id==SpeedTwo?two:nullptr,1,{}};
    };
}
EntityRecord actor(EntityId id,Vec3 position) {
    EntityRecord result;result.id=id;result.name="Actor "+std::to_string(id);result.actor.emplace();result.controller.emplace();result.movement.emplace();result.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{SpeedThree}}};result.navigation.emplace();result.local=math::translation(position);return result;
}
EntityRecord enemy(EntityId id,Vec3 position,EnemyBehaviour behaviour=EnemyBehaviour::Idle) {
    auto result=actor(id,position);result.enemy.emplace();result.enemy->behaviour=behaviour;return result;
}
EntityRecord playerActor(EntityId id,Vec3 position) {
    auto result=actor(id,position);result.playerInput.emplace();return result;
}
EntityRecord volume(std::vector<AssetRef<NavigationAgentProfile>> supported={AssetRef<NavigationAgentProfile>{DefaultNavigationAgentProfileId}}) {
    EntityRecord result;result.id=3;result.name="NavigationVolume";result.navigationVolume.emplace();result.navigationVolume->size={24,12,24};result.navigationVolume->profiles=std::move(supported);return result;
}
void author(SceneDocument& document,std::vector<EntityRecord> records,EntityId player=NoEntity) {
    require(document.edit("Enemy runtime fixture",[&](auto& destination,auto& properties){destination=std::move(records);properties={};properties.playerCharacter=player;}),document.status());
}
Vec3 position(const PlaySession& play,EntityId id) {
    const auto world=play.document().scene().worldTransform(id);require(world.has_value(),"Enemy runtime fixture lost actor "+std::to_string(id)+".");return math::transformPoint(*world,{});
}
float distanceXZ(const Vec3& a,const Vec3& b) {return std::hypot(a[0]-b[0],a[2]-b[2]);}
void tick(PlaySession& play,unsigned count=1) {for(unsigned index=0;index<count;++index)play.advance(PlaySession::FixedStep,Vec2{},Camera{});}
void start(PlaySession& play,const SceneDocument& document) {
    play.setAgentProfiles(agentProfileQuery());play.setGameplayEffects(fixtureEffects());require(play.start(document),play.status());require(waitForNavigation(play),"Enemy fixture navigation failed to prepare: "+play.status());
}
void teleport(PlaySession& play,EntityId id,Vec3 point) {
    require(play.document().edit("Teleport actor",[&](auto& records,auto&){for(auto& record:records)if(record.id==id)record.local=math::translation(point);}),play.document().status());
}
template<class Edit> void editEnemy(PlaySession& play,EntityId id,Edit edit) {
    require(play.document().edit("Configure enemy",[&](auto& records,auto&){for(auto& record:records)if(record.id==id)edit(record);}),play.document().status());
}

void idleAndIndependentActorCommands() {
    Scratch scratch;SceneDocument document(scratch.root);
    author(document,{floor(),volume(),enemy(2,{-4,.05f,-2}),enemy(4,{-5,.05f,2},EnemyBehaviour::ChasePlayer),actor(5,{-4,.05f,-4}),playerActor(10,{5,.05f,2})},10);
    const auto authored=document.scene().records();PlaySession play(scratch.root);start(play,document);
    require(play.attributeValue(4,stats::MovementSpeed)->current==3,"Enemy fixture did not apply its authored starting speed effect.");
    require(play.setActorDestination(5,{4,0,-4}),"Ordinary actor could not receive its external route.");tick(play,90);
    require(distanceXZ(position(play,2),{-4,0,-2})<.01f&&!play.actorDestination(2)&&play.actorRouteQueryCount(2)==0,"Default Idle enemy acquired autonomous movement.");
    require(position(play,4)[0]>-2&&position(play,5)[0]>.3f,"Enemy and externally routed actors did not advance independently.");
    require(play.setActorMovement(5,{0,0,2}),"Ordinary actor lost external velocity commands beside an enemy.");const auto before=position(play,5);tick(play,30);
    require(position(play,5)[2]>before[2]+.9f,"Enemy behaviour replaced another actor's external velocity.");
    require(play.setActorMovement(4,{0,0,-2}),"Enemy rejected an explicit external override.");const auto overridden=position(play,4);tick(play,30);
    require(std::abs(position(play,4)[0]-overridden[0])<.02f&&position(play,4)[2]<overridden[2]-.9f,"Enemy behaviour replaced its own external override.");
    require(play.clearActorMovement(4),"Enemy external override could not be cleared.");const auto resumed=position(play,4);tick(play,30);
    require(position(play,4)[0]>resumed[0]+.7f,"Clearing an external override did not restore enemy behaviour.");
    play.stop();require(!play.playing()&&!play.actorDestination(4)&&play.actorPath(4).empty(),"Stop retained enemy routes.");
    const auto after=document.scene().records();require(after.size()==authored.size(),"Enemy simulation changed authored entity count.");
    for(std::size_t index=0;index<after.size();++index)require(sameEntity(after[index],authored[index]),"Enemy simulation changed authored scene data.");
}

void chaseRetargetsAndRespectsCapsuleSpacing() {
    Scratch scratch;const auto large=agentProfile({1.8f,.6f,45,.3f});auto player=playerActor(10,{4,.05f,0});player.controller->profile=large;
    SceneDocument document(scratch.root);author(document,{floor(),volume({AssetRef<NavigationAgentProfile>{DefaultNavigationAgentProfileId},large}),enemy(2,{-5,.05f,0},EnemyBehaviour::ChasePlayer),player},10);
    PlaySession play(scratch.root);start(play,document);tick(play,240);
    const auto spacing=distanceXZ(position(play,2),position(play,10));
    require(spacing>.8f&&spacing<1.12f,"Chase did not stop using both actors' accepted capsule radii.");
    const auto held=position(play,2);const auto stationaryQueries=play.actorRouteQueryCount(2);tick(play,60);
    require(distanceXZ(position(play,2),held)<.02f,"Chase continued moving through a stationary player.");
    require(play.actorRouteQueryCount(2)-stationaryQueries<=6,"Stationary chase rebuilt routes every fixed tick.");
    teleport(play,10,{2,.05f,5});const auto before=distanceXZ(position(play,2),{2,0,5});const auto queries=play.actorRouteQueryCount(2);tick(play,18);
    require(distanceXZ(position(play,2),{2,0,5})<before-.2f&&play.actorRouteQueryCount(2)>queries,"Chase did not retarget the moved designated player promptly.");
    require(play.actorRouteQueryCount(2)-queries<=3,"Moving-player chase exceeded its retarget interval.");tick(play,180);
    require(distanceXZ(position(play,2),position(play,10))<1.12f,"Chase failed to reach the moving player's new approach point.");
    require(play.document().edit("Remove player designation",[](auto&,auto& properties){properties.playerCharacter=NoEntity;}),play.document().status());
    const auto noPlayer=position(play,2);tick(play,30);require(distanceXZ(position(play,2),noPlayer)<.02f&&!play.actorDestination(2),"Chase retained movement after the designated player was removed.");
}

void airborneSpawnRetriesChaseAfterLanding() {
    Scratch scratch;SceneDocument document(scratch.root);author(document,{floor(),volume(),playerActor(10,{4,.05f,0})},10);PlaySession play(scratch.root);start(play,document);
    const auto builds=play.navigationBuildCount();require(play.document().edit("Spawn airborne enemy",[](auto& records,auto&){records.push_back(enemy(2,{-4,2.5f,0},EnemyBehaviour::ChasePlayer));}),play.document().status());
    tick(play,240);
    require(distanceXZ(position(play,2),position(play,10))<.8f,"Airborne enemy failed to retry Chase after landing beside already prepared navigation.");
    require(play.actorRouteQueryCount(2)>1&&play.actorRouteQueryCount(2)<=30,"Airborne Chase did not retry its failed route at a bounded interval.");
    require(play.navigationBuildCount()==builds,"Airborne enemy spawn rebuilt the already supported navigation type.");
}

void readyChaseRetriesStallsAndRespectsZeroSpeed() {
    Scratch scratch;SceneDocument document(scratch.root);
    author(document,{floor(),volume(),enemy(2,{-4,.05f,0},EnemyBehaviour::ChasePlayer),actor(4,{0,.05f,0}),playerActor(10,{4,.05f,0})},10);
    PlaySession play(scratch.root);start(play,document);tick(play,300);
    require(position(play,2)[0]<-.55f&&!play.actorRemainingPath(2).empty(),"Idle peer did not produce an active collision-blocked Ready route.");
    const auto blocked=position(play,2);const auto queries=play.actorRouteQueryCount(2);
    tick(play,180);const auto retries=play.actorRouteQueryCount(2)-queries;
    require(distanceXZ(position(play,2),blocked)<.04f&&retries>=3&&retries<=7,"Ready chase did not retry a stalled route at a bounded interval.");
    require(play.setAttributeBase(2,stats::MovementSpeed,0),"Cannot pause the stalled enemy's MovementSpeed.");
    const auto pausedQueries=play.actorRouteQueryCount(2);tick(play,180);
    require(play.actorRouteQueryCount(2)==pausedQueries&&!play.actorRemainingPath(2).empty(),"Zero MovementSpeed spun recovery queries or discarded the route.");
    require(play.setAttributeBase(2,stats::MovementSpeed,3),"Cannot restore the enemy's MovementSpeed.");
    teleport(play,4,{0,.05f,4});tick(play,240);
    require(distanceXZ(position(play,2),position(play,10))<.8f,"Chase did not resume after its Idle peer obstruction moved away.");
    require(play.actorRouteQueryCount(2)-pausedQueries<=10,"Resuming blocked chase produced unbounded route queries.");
}

void exhaustedApproachRetriesWithoutClaimingArrival() {
    Scratch scratch;SceneDocument document(scratch.root);EntityRecord partition;partition.id=4;partition.name="Partition";
    partition.collider=SceneCollider{{1,2,30}};partition.local=math::translation({0,1,0});
    author(document,{floor(),volume(),partition,enemy(2,{-4,.05f,0},EnemyBehaviour::ChasePlayer),playerActor(10,{4,.05f,0})},10);
    PlaySession play(scratch.root);start(play,document);tick(play,240);
    require(position(play,2)[0]<-.8f&&position(play,2)[0]>-1.3f&&distanceXZ(position(play,2),position(play,10))>4,"Partition fixture did not end at a reachable approach outside contact.");
    const auto queries=play.actorRouteQueryCount(2);tick(play,180);const auto retries=play.actorRouteQueryCount(2)-queries;
    require(retries>=3&&retries<=7,"Exhausted Ready approach stopped retrying or queried every tick.");
    require(!play.actorDestination(2)&&play.actorRemainingPath(2).empty(),"Bounded approach retries falsely retained a completed steering path.");
}

void chaseStopsAtCapsuleContactOnRamp() {
    Scratch scratch;SceneDocument document(scratch.root);EntityRecord ramp;ramp.id=1;ramp.name="Contact ramp";ramp.collider=SceneCollider{{16,.2f,10}};
    ramp.local=math::eulerRotationXYZ({0,0,25*std::numbers::pi_v<float>/180});
    const auto point=[&](float x){return math::transformPoint(ramp.local,{x,.1f,0})+Vec3{0,.1f,0};};
    author(document,{ramp,volume(),enemy(2,point(-3),EnemyBehaviour::ChasePlayer),playerActor(10,point(2))},10);
    PlaySession play(scratch.root);start(play,document);tick(play,360);
    const auto enemyPosition=position(play,2),playerPosition=position(play,10);
    require(distanceXZ(enemyPosition,playerPosition)>.57f&&distanceXZ(enemyPosition,playerPosition)<.72f&&std::abs(enemyPosition[1]-playerPosition[1])>.2f,"Ramp fixture did not reach contact between vertically overlapping upright capsules.");
    require(math::length(enemyPosition-playerPosition)>.67f&&!play.actorDestination(2)&&play.actorRemainingPath(2).empty(),"Chase used feet distance instead of capsule separation to stop on the ramp.");
    const auto queries=play.actorRouteQueryCount(2);tick(play,120);
    require(distanceXZ(position(play,2),enemyPosition)<.02f&&play.actorRouteQueryCount(2)==queries,"Ramp contact kept steering or retrying into the stationary player.");
}

void rearPeersDoNotFreezeRampRouteLeader() {
    Scratch scratch;SceneDocument document(scratch.root);EntityRecord ramp;ramp.id=20;ramp.name="Saved ramp";ramp.collider.emplace();
    ramp.local={2.688914061f,-2.091098785f,0,0,.613889813f,.789392173f,0,0,0,0,1.720000029f,0,3.062107563f,.761265993f,.971487761f,1};
    std::vector<EntityRecord> records{floor(),volume(),ramp,enemy(2,{-2.806616f,.05f,2.762059f},EnemyBehaviour::ChasePlayer),
        enemy(4,{-2.806616f,.05f,3.699481f},EnemyBehaviour::ChasePlayer),enemy(5,{-2.806616f,.05f,4.558270f},EnemyBehaviour::ChasePlayer),
        enemy(6,{-2.806616f,.05f,5.420506f},EnemyBehaviour::ChasePlayer),playerActor(10,{6,.05f,-1})};
    for(auto& record:records)if(record.movement)record.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{SpeedTwo}}};
    author(document,std::move(records),10);PlaySession play(scratch.root);start(play,document);bool passedCorner=false;
    require(play.attributeValue(4,stats::MovementSpeed)->current==2,"Saved ramp fixture did not apply its authored starting speed effect.");
    for(unsigned index=0;index<1200;++index){tick(play);const auto leader=position(play,4);passedCorner|=leader[0]>4.6f&&leader[2]<2.25f;}
    require(passedCorner,"A rear peer froze the saved ramp-route leader before its first corner.");
    require(play.actorRouteQueryCount(4)<=50,"Ramp peer recovery exceeded the bounded retry rate.");
}

void randomMoveKeepsOriginAndWaitsAfterArrival() {
    Scratch scratch;auto wandering=enemy(2,{0,.05f,0},EnemyBehaviour::RandomMove);wandering.enemy->radius=2.5f;wandering.enemy->minWait=wandering.enemy->maxWait=.4f;
    SceneDocument document(scratch.root);author(document,{floor(),volume(),wandering});PlaySession play(scratch.root);start(play,document);
    bool routed=false;unsigned completed=0,waitingTicks=0;Vec3 arrival{};bool waiting=false;std::optional<Vec3> previous;
    for(unsigned index=0;index<1800;++index) {
        tick(play);const auto destination=play.actorDestination(2);
        if(destination) {
            require(distanceXZ(*destination,{})<=2.65f,"RandomMove drifted its search center to its last destination.");
            if(waiting){require(waitingTicks>=21&&waitingTicks<=28,"RandomMove did not use its authored post-arrival wait.");++completed;waiting=false;}
            routed=true;
        } else if(previous) {waiting=true;waitingTicks=0;arrival=position(play,2);}
        if(waiting){++waitingTicks;require(distanceXZ(position(play,2),arrival)<.02f,"RandomMove moved during its arrival wait.");}
        require(distanceXZ(position(play,2),{})<=2.7f,"RandomMove left the disk centered on its activation position.");previous=destination;
    }
    require(routed&&completed>=4,"RandomMove failed to complete repeated route/wait cycles.");
    teleport(play,2,{7,.05f,0});play.advance(0,Vec2{},Camera{});
    bool newDestination=false;for(unsigned index=0;index<120;++index){tick(play);if(const auto target=play.actorDestination(2)){require(distanceXZ(*target,{7,0,0})<=2.65f,"Authored teleport retained RandomMove's old origin.");newDestination=true;break;}}
    require(newDestination,"RandomMove did not resume around an authored teleport.");
    editEnemy(play,2,[](auto& record){record.enemy->radius=0;});const auto zeroRadius=position(play,2);tick(play,120);
    require(distanceXZ(position(play,2),zeroRadius)<.02f&&!play.actorDestination(2),"Zero-radius RandomMove acquired a route or moved.");
    editEnemy(play,2,[](auto& record){record.movement->enabled=false;record.enemy->radius=2;});tick(play);
    teleport(play,2,{-7,.05f,0});tick(play,10);
    require(distanceXZ(position(play,2),{-7,0,0})<.01f,"Disabled Motor did not retain the authored teleport.");
    editEnemy(play,2,[](auto& record){record.movement->enabled=true;});bool resumed=false;
    for(unsigned index=0;index<120;++index){tick(play);if(const auto target=play.actorDestination(2)){require(distanceXZ(*target,{-7,0,0})<=2.15f,"Teleport while Motor was disabled retained RandomMove's old anchor.");resumed=true;break;}}
    require(resumed,"RandomMove failed to resume after a teleport while Motor was disabled.");
}

struct Outcome {Vec3 position;std::optional<Vec3> destination;std::uint64_t ticks,queries;};
Outcome simulate(PlaySession& play,unsigned frames,double frameSeconds) {
    for(unsigned index=0;index<frames;++index)play.advance(frameSeconds,Vec2{},Camera{});
    return {position(play,2),play.actorDestination(2),play.tickCount(),play.actorRouteQueryCount(2)};
}
void behaviourUsesFixedTicksAndResetsOnPlay() {
    Scratch scratch;auto wandering=enemy(2,{0,.05f,0},EnemyBehaviour::RandomMove);wandering.enemy->radius=2;wandering.enemy->minWait=.15f;wandering.enemy->maxWait=.45f;
    SceneDocument document(scratch.root);author(document,{floor(),volume(),wandering});PlaySession slow(scratch.root),fast(scratch.root);start(slow,document);start(fast,document);
    const auto a=simulate(slow,180,1.0/30),b=simulate(fast,864,1.0/144);
    require(a.ticks==360&&b.ticks==360&&distanceXZ(a.position,b.position)<1e-4f&&a.queries==b.queries,"Render FPS changed RandomMove's fixed-tick decisions or movement.");
    require(a.destination.has_value()==b.destination.has_value()&&(!a.destination||distanceXZ(*a.destination,*b.destination)<1e-4f),"Render FPS changed RandomMove's random destination sequence.");
    slow.stop();start(slow,document);require(distanceXZ(position(slow,2),{})<1e-5f&&slow.actorRouteQueryCount(2)==0,"Restart retained an enemy's old position or route state.");
    const auto restarted=simulate(slow,180,1.0/30);
    require(distanceXZ(restarted.position,a.position)<1e-4f&&restarted.queries==a.queries,"Restart retained an enemy's previous random generator or wait state.");
    const auto pausedPosition=position(slow,2);const auto pausedTicks=slow.tickCount();slow.advance(3,Vec2{},Camera{},false);slow.advance(3,Vec2{},Camera{},true);
    require(slow.tickCount()==pausedTicks&&distanceXZ(position(slow,2),pausedPosition)<1e-5f,"Focus suspension advanced enemy behaviour time.");
}

void pendingAndUnsupportedNavigationNeverInventArrival() {
    Scratch scratch;ResourceConfig configuration;configuration.workers=1;ResourceManager resources(configuration);
    struct Gate {std::atomic_bool entered{},released{};};const auto gate=std::make_shared<Gate>();ResourceRequestOptions options;options.priority=ResourcePriority::Selected;options.retainLoader=false;
    auto blocker=resources.requestDerived<NavigationTileData>("enemy-runtime-worker-gate",[gate](std::stop_token stop){gate->entered=true;while(!gate->released&&!stop.stop_requested())std::this_thread::yield();return ResourceLoadResult<NavigationTileData>{std::make_shared<NavigationTileData>(),sizeof(NavigationTileData),1,{}};},options);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!gate->entered&&std::chrono::steady_clock::now()<deadline){resources.pump();std::this_thread::yield();}require(gate->entered,"Enemy worker gate failed to start.");
    auto wandering=enemy(2,{0,.05f,0},EnemyBehaviour::RandomMove);wandering.enemy->radius=2;wandering.enemy->minWait=wandering.enemy->maxWait=5;
    SceneDocument document(scratch.root);author(document,{floor(),volume(),wandering,enemy(4,{-4,.05f,3},EnemyBehaviour::ChasePlayer),playerActor(10,{4,.05f,3})},10);
    PlaySession play(scratch.root);play.setResourceManager(resources);play.setAgentProfiles(agentProfileQuery());play.setGameplayEffects(fixtureEffects());require(play.start(document),play.status());
    require(play.navigationReadiness(DefaultNavigationAgentProfileId)==NavigationStatus::Preparing,"Blocked worker fixture unexpectedly prepared navigation.");tick(play,120);
    require(distanceXZ(position(play,2),{})<.01f&&distanceXZ(position(play,4),{-4,0,3})<.01f&&!play.actorDestination(2)&&!play.actorDestination(4),"Pending navigation produced an enemy route or horizontal movement.");
    require(play.actorRouteQueryCount(2)<=12&&play.actorRouteQueryCount(4)<=12,"Enemies retried Preparing navigation every tick.");gate->released=true;
    const auto preparedDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(play.navigationReadiness(DefaultNavigationAgentProfileId)==NavigationStatus::Preparing&&std::chrono::steady_clock::now()<preparedDeadline){resources.pump();play.advance(0,Vec2{},Camera{});std::this_thread::yield();}
    require(play.navigationReadiness(DefaultNavigationAgentProfileId)==NavigationStatus::Ready,play.status());const auto randomQueries=play.actorRouteQueryCount(2);tick(play,40);
    require((play.actorRouteQueryCount(2)>randomQueries||play.actorDestination(2).has_value())&&distanceXZ(position(play,4),{-4,0,3})>.5f,"Preparing was treated as an arrival wait or discarded enemy movement intent.");
    play.stop();const auto unsupported=agentProfile({2,.5f,35,.2f});auto rejected=enemy(2,{-4,.05f,0},EnemyBehaviour::ChasePlayer);rejected.controller->profile=unsupported;author(document,{floor(),volume(),rejected,playerActor(10,{4,.05f,0})},10);PlaySession unsupportedPlay(scratch.root);start(unsupportedPlay,document);tick(unsupportedPlay,180);
    require(unsupportedPlay.navigationReadiness(unsupported.id)==NavigationStatus::Unavailable&&distanceXZ(position(unsupportedPlay,2),{-4,0,0})<.01f&&!unsupportedPlay.actorDestination(2),"Unsupported enemy profile fell back to another supported navigation type.");
    require(unsupportedPlay.navigationProfileCount()==1&&unsupportedPlay.actorRouteQueryCount(2)<=20,"Unsupported enemy silently added a profile or retried every tick.");
}

void liveBehaviourCapabilitiesAndPlayerOwnership() {
    Scratch scratch;auto designated=enemy(10,{4,.05f,0},EnemyBehaviour::RandomMove);designated.playerInput.emplace();
    SceneDocument document(scratch.root);author(document,{floor(),volume(),enemy(2,{-4,.05f,0}),designated},10);PlaySession play(scratch.root);start(play,document);tick(play,30);
    require(distanceXZ(position(play,10),{4,0,0})<.01f&&play.actorRouteQueryCount(10)==0,"The designated Player ran its Enemy behaviour.");
    editEnemy(play,2,[](auto& record){record.enemy->behaviour=EnemyBehaviour::ChasePlayer;});tick(play,30);require(position(play,2)[0]>-3,"Live Idle-to-Chase change did not activate enemy behaviour.");
    editEnemy(play,2,[](auto& record){record.enemy->behaviour=EnemyBehaviour::Idle;});const auto idle=position(play,2);tick(play,30);require(distanceXZ(position(play,2),idle)<.02f&&!play.actorDestination(2),"Live Chase-to-Idle retained autonomous movement.");
    editEnemy(play,2,[](auto& record){record.enemy->behaviour=EnemyBehaviour::ChasePlayer;record.movement->enabled=false;});const auto disabled=position(play,2);tick(play,30);
    require(distanceXZ(position(play,2),disabled)<.01f&&!play.actorDestination(2),"Enemy moved without an enabled Motor.");
    editEnemy(play,2,[](auto& record){record.movement->enabled=true;record.navigation->enabled=false;});tick(play,30);require(distanceXZ(position(play,2),disabled)<.01f&&!play.actorDestination(2),"Enemy bypassed a disabled Navigation Agent.");
    editEnemy(play,2,[](auto& record){record.navigation->enabled=true;});tick(play,30);require(position(play,2)[0]>disabled[0]+.8f,"Re-enabled enemy capabilities did not restore behaviour.");
    const auto builds=play.navigationBuildCount();require(play.document().edit("Spawn another enemy",[](auto& records,auto&){records.push_back(enemy(11,{-4,.05f,4},EnemyBehaviour::ChasePlayer));}),play.document().status());tick(play,30);
    require(position(play,11)[0]>-3.4f&&play.navigationBuildCount()==builds,"Spawned enemy did not reuse the existing supported navigation world.");
    require(play.document().edit("Despawn enemy",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.id==11;});}),play.document().status());tick(play);
    require(!play.actorDestination(11)&&play.actorPath(11).empty(),"Despawn retained enemy route state.");
}
}

int main() {
    try {
        idleAndIndependentActorCommands();chaseRetargetsAndRespectsCapsuleSpacing();airborneSpawnRetriesChaseAfterLanding();readyChaseRetriesStallsAndRespectsZeroSpeed();exhaustedApproachRetriesWithoutClaimingArrival();chaseStopsAtCapsuleContactOnRamp();rearPeersDoNotFreezeRampRouteLeader();randomMoveKeepsOriginAndWaitsAfterArrival();behaviourUsesFixedTicksAndResetsOnPlay();pendingAndUnsupportedNavigationNeverInventArrival();liveBehaviourCapabilitiesAndPlayerOwnership();
        std::cout<<"Enemy fixed-tick behaviour, chase spacing, random bounds/waits, readiness, actor independence and lifecycle checks passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
