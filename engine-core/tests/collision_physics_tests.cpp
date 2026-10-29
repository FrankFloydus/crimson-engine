#include "scene/SceneComponents.h"
#include "physics/PhysicsWorld.h"
#include "physics/CharacterMotor.h"
#include "agent_profile_fixture.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
EntityRecord floor() {EntityRecord result;result.id=1;result.name="Floor";result.local=math::translation({0,-.5f,0});result.collider=SceneCollider{{30,1,30}};return result;}
EntityRecord actor(EntityId id,Vec3 point,CollisionFilter filter={}) {
    EntityRecord result;result.id=id;result.name="Actor";result.local=math::translation(point);result.actor.emplace();result.controller.emplace();filter.mask|=WorldCollisionLayer;result.controller->collision=filter;result.movement.emplace();return result;
}
EntityRecord trigger(EntityId id,Vec3 point,Vec3 size={2,3,2},std::uint32_t mask=CharacterCollisionLayers) {
    EntityRecord result;result.id=id;result.name="Trigger";result.local=math::translation(point);result.collider=SceneCollider{size};result.collider->mode=ColliderMode::Trigger;result.collider->collision={TriggerCollisionLayer,mask};return result;
}
void apply(Scene& scene,std::vector<EntityRecord> records){scene.apply(records,{});}
void tick(Scene& scene,PhysicsWorld& physics,EntityId id=NoEntity,Vec3 velocity={},unsigned count=1) {
    for(unsigned index=0;index<count;++index)scene.applyRuntimePoses(physics.step(scene,id,velocity,1.f/60));
}
bool event(const PhysicsWorld& physics,EntityId sensor,EntityId other,TriggerEventKind kind,TriggerEventReason reason) {
    return std::any_of(physics.triggerEvents().begin(),physics.triggerEvents().end(),[&](const auto& value){return value.trigger==sensor&&value.other==other&&value.kind==kind&&value.reason==reason&&value.world!=0&&value.tick>0;});
}
void blockingAndDirectionalMasks() {
    Scene scene;apply(scene,{floor(),actor(2,{-2,.05f,0},{PlayerCollisionLayer,AllCollisionLayers}),actor(3,{0,.05f,0},{EnemyCollisionLayer,AllCollisionLayers})});PhysicsWorld physics;
    tick(scene,physics,NoEntity,{},20);tick(scene,physics,2,{3,0,0},120);
    require((*physics.characterPosition(2))[0]<-.58f,"Default Player/Enemy capsules did not block one another.");
    require(std::abs((*physics.characterPosition(3))[0])<.08f,"Blocking introduced an unsolicited actor pushing mechanic.");
    const auto geometry=physics.geometryRevision();auto records=scene.records();for(auto& record:records)if(record.id==2){record.local=math::translation({-2,.05f,0});record.controller->collision.mask=WorldCollisionLayer;}apply(scene,std::move(records));
    tick(scene,physics,2,{3,0,0},100);
    require((*physics.characterPosition(2))[0]>2.7f&&physics.grounded(2),"Directional mask exclusion failed or removed mandatory World support.");
    require(physics.geometryRevision()==geometry,"Character filter edits invalidated static navigation geometry.");
    records=scene.records();for(auto& record:records){if(record.id==2)record.local=math::translation({0,.05f,0});if(record.id==3)record.local=math::translation({2,.05f,0});}apply(scene,std::move(records));
    tick(scene,physics,3,{-3,0,0},90);
    require((*physics.characterPosition(3))[0]>.58f,"Mover-side mask was incorrectly treated as a mutual collision rule.");
}
void followingPeersCanMoveAway() {
    // Start just inside the padded shell, within the ordinary no-pushing
    // tolerance. Its tiny recovery velocity used to make the rear contact
    // remove the leader's outward motion before either actor advanced.
    for(const bool reverse:{false,true})for(const Vec3 direction:{Vec3{1,0,0},math::normalized(Vec3{.63f,0,-.776f})})for(const float rearSpeed:{2.f,3.f}) {
        const EntityId leader=reverse?3:2,rear=reverse?2:3;
        const Vec3 start=direction*.6395f+Vec3{0,.05f,0};Scene scene;
        apply(scene,{floor(),actor(leader,start),actor(rear,{0,.05f,0})});PhysicsWorld physics;tick(scene,physics,NoEntity,{},20);
        const auto origin=*physics.characterPosition(leader);float minimumSeparation=10;
        for(unsigned index=0;index<120;++index) {
            CharacterMotor(physics,leader).move(direction*2);CharacterMotor(physics,rear).move(direction*rearSpeed);
            tick(scene,physics);
            const auto first=*physics.characterPosition(leader),second=*physics.characterPosition(rear);
            minimumSeparation=std::min(minimumSeparation,static_cast<float>(std::hypot(first[0]-second[0],first[2]-second[2])));
            require(math::finite(first)&&math::finite(second)&&std::abs(first[1])<.03f&&std::abs(second[1])<.03f,
                "Following peer correction lost finite poses or floor support.");
        }
        const auto displacement=*physics.characterPosition(leader)-origin;
        require(math::dot(displacement,direction)>3.8f,
            "A following capsule blocked the leader moving away from it (reverse="+std::to_string(reverse)+", rear speed="+std::to_string(rearSpeed)+").");
        require(minimumSeparation>.595f,"Following capsules penetrated their physical radii.");
        require(physics.grounded(leader)&&physics.grounded(rear),"Following peer motion lost grounded support.");
    }
}
void followingPeerCorrectionRetainsBlockingAndRecovery() {
    for(const bool reverse:{false,true}) {
        const EntityId leader=reverse?3:2,rear=reverse?2:3;Scene scene;
        apply(scene,{floor(),actor(leader,{.6395f,.05f,0}),actor(rear,{0,.05f,0})});PhysicsWorld physics;tick(scene,physics,NoEntity,{},20);
        const auto stationary=*physics.characterPosition(leader);
        for(unsigned index=0;index<90;++index){CharacterMotor(physics,rear).move({3,0,0});tick(scene,physics);}
        require(math::length(*physics.characterPosition(leader)-stationary)<.03,
            "The outgoing-contact correction allowed a follower to push a stationary actor.");
        require((*physics.characterPosition(leader))[0]-(*physics.characterPosition(rear))[0]>.595f,
            "A rear actor passed through a stationary leader.");

        EntityRecord wall;wall.id=4;wall.name="Wall";wall.local=math::translation({3,1,0});wall.collider=SceneCollider{{.5f,2,8}};
        auto records=scene.records();records.push_back(wall);apply(scene,std::move(records));
        for(unsigned index=0;index<180;++index){CharacterMotor(physics,leader).move({2,0,0});CharacterMotor(physics,rear).move({3,0,0});tick(scene,physics);}
        require((*physics.characterPosition(leader))[0]<2.46f&&(*physics.characterPosition(rear))[0]<2.46f,
            "Restoring outgoing peer motion overrode a solid wall constraint.");
        require((*physics.characterPosition(leader))[0]-(*physics.characterPosition(rear))[0]>.595f,
            "A following capsule penetrated its stopped leader at a wall.");

        records=scene.records();for(auto& record:records)if(record.id==leader||record.id==rear)
            record.local=math::translation({record.id==leader?.3f:0.f,.05f,0});apply(scene,std::move(records));
        tick(scene,physics,NoEntity,{},30);
        const auto first=*physics.characterPosition(leader),second=*physics.characterPosition(rear);
        require(math::finite(first)&&math::finite(second)&&std::hypot(first[0]-second[0],first[2]-second[2])>.595f,
            "The outgoing-contact correction bypassed recovery from a deep authored capsule overlap.");

        records=scene.records();for(auto& record:records)if(record.id==leader||record.id==rear)
            record.local=math::translation({record.id==leader?2.f:-2.f,.05f,0});apply(scene,std::move(records));
        for(unsigned index=0;index<120;++index){CharacterMotor(physics,leader).move({-2,0,0});CharacterMotor(physics,rear).move({2,0,0});tick(scene,physics);}
        require((*physics.characterPosition(leader))[0]>.295f&&(*physics.characterPosition(rear))[0]<-.295f,
            "The outgoing-contact correction allowed head-on capsules to pass through one another.");
    }
}
void peerReplacementDespawnAndHighBitFilters() {
    constexpr std::uint32_t high=1u<<31;Scene scene;apply(scene,{floor(),actor(2,{-2,.05f,0},{PlayerCollisionLayer,high}),actor(3,{0,.05f,0},{high,0})});PhysicsWorld physics;
    tick(scene,physics,2,{3,0,0},90);require((*physics.characterPosition(2))[0]<-.58f,"The highest collision layer bit was truncated by backend layers.");
    const auto profile=testing::agentProfile({2,.5f,40,.2f});physics.setAgentProfiles(testing::agentProfileQuery());auto records=scene.records();for(auto& record:records)if(record.id==3)record.controller->profile=profile;apply(scene,std::move(records));
    tick(scene,physics,2,{3,0,0},10);require(physics.characterCount()==2&&(*physics.characterPosition(2))[0]<-.7f,"Capsule replacement left stale peer registration.");
    records=scene.records();std::erase_if(records,[](const auto& record){return record.id==3;});apply(scene,std::move(records));tick(scene,physics,2,{3,0,0},90);
    require(physics.characterCount()==1&&(*physics.characterPosition(2))[0]>2,"Despawn left a dangling or ghost character blocker.");
}
void triggerTransitionsAndConfiguration() {
    Scene scene;apply(scene,{floor(),actor(2,{-2,.05f,0},{PlayerCollisionLayer,0}),trigger(4,{0,1,0},{1,3,2})});PhysicsWorld physics;
    tick(scene,physics);require(physics.triggerEvents().empty()&&physics.triggerOverlaps(4).empty(),"A separated trigger invented initial overlap.");
    bool entered=false,exited=false;std::uint64_t world=0,lastTick=0;
    for(unsigned index=0;index<160;++index){tick(scene,physics,2,{2,0,0});for(const auto& value:physics.triggerEvents()){require(value.tick>=lastTick,"Trigger tick ordering regressed.");lastTick=value.tick;if(world)require(value.world==world,"Trigger world identity changed in place.");world=value.world;entered|=value.trigger==4&&value.other==2&&value.kind==TriggerEventKind::Enter&&value.reason==TriggerEventReason::Motion;exited|=value.trigger==4&&value.other==2&&value.kind==TriggerEventKind::Exit&&value.reason==TriggerEventReason::Motion;}}
    require(entered&&exited&&(*physics.characterPosition(2))[0]>3,"Trigger detection was mutual-filtered, blocking, or missed its endpoint transitions.");
    auto records=scene.records();for(auto& record:records)if(record.id==2)record.local=math::translation({0,.05f,0});apply(scene,std::move(records));tick(scene,physics);
    require(event(physics,4,2,TriggerEventKind::Enter,TriggerEventReason::Configuration),"Authored teleport did not classify overlap as Configuration.");
    const auto geometry=physics.geometryRevision();records=scene.records();for(auto& record:records)if(record.id==4)record.collider->collision.mask=EnemyCollisionLayer;apply(scene,std::move(records));tick(scene,physics);
    require(event(physics,4,2,TriggerEventKind::Exit,TriggerEventReason::Configuration)&&physics.triggerOverlaps(4).empty(),"Trigger mask edit left stale overlap membership.");
    require(physics.geometryRevision()==geometry,"Trigger filter edit invalidated static navigation geometry.");
    records=scene.records();for(auto& record:records)if(record.id==4)record.collider->collision.mask=PlayerCollisionLayer;apply(scene,std::move(records));tick(scene,physics);require(event(physics,4,2,TriggerEventKind::Enter,TriggerEventReason::Configuration),"Trigger mask restoration failed to restore current overlap.");
    records=scene.records();std::erase_if(records,[](const auto& record){return record.id==2;});apply(scene,std::move(records));tick(scene,physics);
    require(event(physics,4,2,TriggerEventKind::Exit,TriggerEventReason::Removed)&&physics.triggerOverlaps(4).empty(),"Despawn did not remove overlap using stable identity.");tick(scene,physics);require(physics.triggerEvents().empty(),"Trigger events replayed after their completed step.");
}
void attachedTriggersFollowResolvedPoseAndExcludeOwner() {
    auto attached=trigger(4,{1,1,0},{2,3,2},CharacterCollisionLayers|TriggerCollisionLayer);attached.parent=2;
    auto sibling=trigger(5,{1,1,0},{2,3,2},AllCollisionLayers);sibling.parent=2;
    Scene scene;apply(scene,{floor(),actor(2,{-3,.05f,0},{PlayerCollisionLayer,0}),actor(3,{0,.05f,0},{EnemyCollisionLayer,0}),attached,sibling});PhysicsWorld physics;
    tick(scene,physics);const auto geometry=physics.geometryRevision();bool found=false;
    for(unsigned index=0;index<90;++index){tick(scene,physics,2,{2,0,0});const auto active=physics.triggerOverlaps(4);require(std::find(active.begin(),active.end(),2)==active.end()&&std::find(active.begin(),active.end(),5)==active.end(),"Attached trigger detected its owner capsule or sibling sensor.");found|=std::find(active.begin(),active.end(),3)!=active.end();}
    require(found&&physics.geometryRevision()==geometry,"Attached trigger failed to follow same-step resolved motor movement or dirtied navigation.");
    require(physics.staticBoxes().size()==1&&physics.staticMeshes().empty(),"Trigger geometry leaked into navigation snapshots.");
    auto independent=trigger(6,{0,1,0},{10,3,10},TriggerCollisionLayer);auto records=scene.records();records.push_back(independent);apply(scene,std::move(records));tick(scene,physics);
    require(event(physics,6,4,TriggerEventKind::Enter,TriggerEventReason::Initial),"Trigger-to-trigger detection ignored its selected target layer.");
}
void queryMasksKindsAndIgnoredEntities() {
    Scene scene;apply(scene,{floor(),actor(2,{0,.05f,0},{PlayerCollisionLayer,AllCollisionLayers}),trigger(4,{-2,1,0},{1,2,2})});PhysicsWorld physics;tick(scene,physics);
    const Ray ray{{-5,1,0},{1,0,0}};require(!physics.raycast(ray,10),"Ground-only movement ray hit a capsule or trigger.");
    PhysicsQueryFilter query;query.includeWorld=false;query.mask=PlayerCollisionLayer;const auto character=physics.raycast(ray,10,query);require(character&&character->entity==2,"Filtered query did not hit the accepted virtual capsule.");
    const EntityId ignored[]{2};query.ignored=ignored;require(!physics.raycast(ray,10,query),"Entity exclusion did not remove a character hit.");
    query.ignored={};query.mask=TriggerCollisionLayer;require(!physics.raycast(ray,10,query),"Triggers entered queries without explicit inclusion.");query.includeTriggers=true;const auto sensor=physics.raycast(ray,10,query);require(sensor&&sensor->entity==4,"Filtered query could not explicitly include a primitive trigger.");
    query.mask=WorldCollisionLayer;query.includeWorld=true;const auto ground=physics.raycast({{0,5,0},{0,-1,0}},10,query);require(ground&&ground->entity==1,"Query mask did not preserve shared static World geometry.");
    const EntityId ignoredFloor[]{1};query.ignored=ignoredFloor;require(!physics.raycast({{0,5,0},{0,-1,0}},10,query),"World-body query ignored entity exclusion.");
}
void staticMembershipEditsPreserveAcceptedGeometry() {
    constexpr std::uint32_t marker=1u<<30;auto ground=floor();ground.collider->collision.layers|=marker;
    // Keep the mesh above the broad primitive floor so this membership check
    // has a unique closest collider instead of relying on coplanar hit order.
    auto mesh=floor();mesh.id=6;mesh.local=math::translation({5,.25f,0});mesh.collider->shape=ColliderShape::Trimesh;
    mesh.collider->mesh=makeCollisionMesh({{-1,0,-1},{1,0,-1},{1,0,1},{-1,0,1}},{0,2,1,0,3,2});mesh.collider->collision.layers|=marker;
    auto detector=trigger(4,{0,0,0},{2,2,2},marker);Scene scene;apply(scene,{ground,mesh,detector});PhysicsWorld physics;tick(scene,physics);
    PhysicsQueryFilter query;query.mask=marker;query.includeCharacters=false;
    const auto primitive=physics.raycast({{0,4,0},{0,-1,0}},10,query),triangle=physics.raycast({{5,4,0},{0,-1,0}},10,query);
    require(primitive&&primitive->entity==1&&triangle&&triangle->entity==6,"Static solid extra memberships were lost from primitive or mesh queries.");
    require(physics.triggerOverlaps(4)==std::vector<EntityId>{1},"Trigger detector could not select a static solid's additional membership.");
    const auto geometry=physics.geometryRevision();const auto accepted=physics.staticMeshes().front().mesh;auto records=scene.records();for(auto& record:records)if(record.id==1||record.id==6)record.collider->collision.layers=WorldCollisionLayer;apply(scene,std::move(records));tick(scene,physics);
    require(physics.geometryRevision()==geometry&&physics.staticMeshes().front().mesh==accepted,"Static membership edit replaced navigation collision geometry.");
    require(!physics.raycast({{0,4,0},{0,-1,0}},10,query)&&!physics.raycast({{5,4,0},{0,-1,0}},10,query),"Static membership removal left stale accepted query filters.");
    require(event(physics,4,1,TriggerEventKind::Exit,TriggerEventReason::Configuration)&&physics.triggerOverlaps(4).empty(),"Static membership removal did not reclassify detector membership.");
    require(physics.raycast({{0,4,0},{0,-1,0}},10).has_value(),"Static custom membership edit removed mandatory World collision.");
}
void initialRemovalAndWorldIsolation() {
    Scene scene;apply(scene,{floor(),actor(2,{0,.05f,0}),trigger(4,{0,1,0})});PhysicsWorld first,second;tick(scene,first);tick(scene,second);
    require(event(first,4,2,TriggerEventKind::Enter,TriggerEventReason::Initial)&&event(second,4,2,TriggerEventKind::Enter,TriggerEventReason::Initial),"Spawn-inside trigger did not report initial endpoint overlap.");
    require(first.triggerEvents().front().world!=second.triggerEvents().front().world,"Independent physics worlds shared trigger identities.");
    auto records=scene.records();std::erase_if(records,[](const auto& record){return record.id==4;});apply(scene,std::move(records));tick(scene,first);
    require(event(first,4,2,TriggerEventKind::Exit,TriggerEventReason::Removed),"Removing a trigger lost its retained overlap identities.");
    Scene noActors;apply(noActors,{floor(),trigger(4,{0,1,0})});PhysicsWorld preview;preview.reconcileStatics(noActors);require(preview.characterCount()==0&&preview.staticBoxes().size()==1,"Static-only reconciliation published trigger geometry as world collision.");
}
}
int main(){try{blockingAndDirectionalMasks();followingPeersCanMoveAway();followingPeerCorrectionRetainsBlockingAndRecovery();peerReplacementDespawnAndHighBitFilters();triggerTransitionsAndConfiguration();attachedTriggersFollowResolvedPoseAndExcludeOwner();queryMasksKindsAndIgnoredEntities();staticMembershipEditsPreserveAcceptedGeometry();initialRemovalAndWorldIsolation();std::cout<<"Collision masks, virtual peer blocking, trigger lifecycle/attachments and filtered query checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
