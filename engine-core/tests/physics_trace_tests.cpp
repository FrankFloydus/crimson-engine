#include "physics/PhysicsWorld.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool near(float a,float b,float tolerance=.005f){return std::abs(a-b)<tolerance;}
EntityRecord sphere(EntityId id,const Vec3& point,float radius=1.f,std::uint32_t layers=WorldCollisionLayer) {
    EntityRecord entity;entity.id=id;entity.name="Trace sphere";entity.local=math::translation(point);
    entity.collider.emplace();entity.collider->shape=ColliderShape::Sphere;entity.collider->radius=radius;
    entity.collider->collision={WorldCollisionLayer|layers,AllCollisionLayers};return entity;
}
PhysicsTrace forward(){PhysicsTrace trace;trace.ray={{0,0,0},{0,0,-3}};return trace;}
void lineModesAndLayers() {
    constexpr auto custom=1u<<31;
    Scene scene;scene.apply({sphere(8,{0,0,-8},1,custom),sphere(3,{0,0,-4},1,EnemyCollisionLayer)},{});
    PhysicsWorld world;world.reconcile(scene);auto request=forward();
    const auto closest=world.trace(request);
    require(closest.valid && closest.hits.size()==1 && closest.hits[0].entity==3,"Closest line trace missed the nearest accepted collider.");
    const auto& hit=closest.hits[0];
    require(near(hit.distance,3) && near(hit.fraction,.3f) && near(hit.position[2],-3) && hit.normal[2]>.999f && hit.owner==3,"Line trace contact, normalized distance, normal or owner is wrong.");
    const auto all=world.trace(request,{},PhysicsTraceMode::All);
    require(all.hits.size()==2 && all.hits[0].entity==3 && all.hits[1].entity==8 && near(all.hits[1].distance,7),"All trace did not continue beyond the first collider in distance order.");
    const auto any=world.trace(request,{},PhysicsTraceMode::Any);
    require(any.valid && any.hits.size()==1,"Any trace did not report an accepted collision.");
    PhysicsQueryFilter filter;filter.mask=custom;
    const auto selected=world.trace(request,filter);
    require(selected.hits.size()==1 && selected.hits[0].entity==8,"Trace mask failed high-bit authored target layers.");
    filter.mask=WorldCollisionLayer;
    require(world.trace(request,filter,PhysicsTraceMode::All).hits.size()==2,"Trace did not preserve the solid World layer contract.");
    filter.mask=0;require(world.trace(request,filter).valid && world.trace(request,filter).hits.empty(),"Empty mask did not yield a valid miss.");
    request.maxDistance=3;
    require(world.trace(request).hits.size()==1,"A collision exactly at the trace endpoint was omitted.");
    request.maxDistance=2.9f;require(world.trace(request).hits.empty(),"Trace exceeded its finite range.");
}
void shapeSweepsAndOrientation() {
    Scene scene;scene.apply({sphere(1,{0,0,-5})},{});PhysicsWorld world;world.reconcile(scene);
    auto request=forward();request.shape=PhysicsTraceShape::Sphere;request.radius=1;
    const auto swept=world.trace(request);
    require(swept.valid && swept.hits.size()==1 && near(swept.hits[0].distance,3) && near(swept.hits[0].center[2],-3) && near(swept.hits[0].position[2],-4),"Sphere sweep confused center travel with target surface contact.");
    require(swept.hits[0].normal[2]>.999f,"Sphere sweep returned an inward contact normal.");
    scene.apply({sphere(1,{1.2f,0,-5},.25f)},{});world.reconcile(scene);
    request=forward();require(world.trace(request).hits.empty(),"Off-axis sphere fixture unexpectedly intersected the center ray.");
    request.shape=PhysicsTraceShape::Sphere;request.radius=1.1f;
    require(world.trace(request).hits.size()==1,"Sphere sweep failed to detect geometry outside its center ray.");
    request.shape=PhysicsTraceShape::Box;request.halfExtents={.1f,.1f,1.5f};
    require(world.trace(request).hits.empty(),"Thin unrotated box sweep reached an off-axis target.");
    request.orientation=math::axisAngle({0,1,0},std::numbers::pi/2);
    require(world.trace(request).hits.size()==1,"Oriented box sweep ignored its world rotation.");
    request=forward();request.shape=PhysicsTraceShape::Capsule;request.radius=.15f;request.capsuleHalfHeight=1.2f;
    require(world.trace(request).hits.empty(),"Upright capsule sweep reached an off-axis target.");
    request.orientation=math::axisAngle({0,0,1},std::numbers::pi/2);
    require(world.trace(request).hits.size()==1,"Capsule sweep ignored its world rotation or cylindrical height.");
    request.capsuleHalfHeight=0;request.radius=1.1f;
    require(world.trace(request).hits.size()==1,"Zero-cylinder capsule did not behave as a sphere.");
}
void ownerFilteringCharactersAndTriggers() {
    EntityRecord actor;actor.id=10;actor.name="Actor";actor.actor.emplace();
    auto first=sphere(11,{0,0,-3},.5f);first.parent=10;
    auto second=sphere(12,{0,0,-5},.5f);second.parent=10;
    Scene scene;scene.apply({actor,first,second},{});PhysicsWorld world;world.reconcile(scene);
    const auto all=world.trace(forward(),{},PhysicsTraceMode::All);
    require(all.hits.size()==2 && all.hits[0].owner==10 && all.hits[1].owner==10,"All trace merged separate collider IDs that share one actor owner.");
    PhysicsQueryFilter filter;const std::array<EntityId,1> ignored{10};filter.ignored=ignored;
    require(world.trace(forward(),filter).hits.empty(),"Ignoring an actor did not exclude its owned collider descendants.");
    const std::array<EntityId,1> ignoredCollider{11};filter.ignored=ignoredCollider;
    require(world.trace(forward(),filter).hits.size()==1 && world.trace(forward(),filter).hits[0].entity==12,"Ignoring an exact collider incorrectly excluded its sibling.");
    EntityRecord nestedActor;nestedActor.id=40;nestedActor.name="Nested independent actor";nestedActor.parent=10;nestedActor.actor.emplace();
    auto nestedCollider=sphere(41,{0,0,-7},.5f);nestedCollider.parent=40;
    scene.apply({actor,first,second,nestedActor,nestedCollider},{});world.reconcile(scene);filter.ignored=ignored;
    const auto nested=world.trace(forward(),filter,PhysicsTraceMode::All);
    require(nested.hits.size()==1 && nested.hits[0].entity==41 && nested.hits[0].owner==40,
        "Ignoring an outer actor crossed a nested actor ownership boundary.");
    EntityRecord character;character.id=20;character.name="Character";character.actor.emplace();character.controller.emplace();
    character.local=math::translation({0,-.9f,-4});character.controller->collision.layers=PlayerCollisionLayer;
    auto trigger=sphere(30,{0,0,-2},.5f);trigger.collider->mode=ColliderMode::Trigger;trigger.collider->collision={TriggerCollisionLayer,0};
    scene.apply({character,trigger},{});world.reconcile(scene);filter={};
    require(world.trace(forward(),filter).hits.size()==1 && world.trace(forward(),filter).hits[0].entity==20,"Default trace did not query a virtual character or included a trigger implicitly.");
    filter.includeCharacters=false;require(world.trace(forward(),filter).hits.empty(),"Character exclusion did not remove virtual character hits.");
    filter.includeWorld=false;filter.includeTriggers=true;
    require(world.trace(forward(),filter).hits.size()==1 && world.trace(forward(),filter).hits[0].entity==30,"Explicit trigger query depended on rigid-world inclusion or the target's movement mask.");
    auto sweep=forward();sweep.shape=PhysicsTraceShape::Sphere;sweep.radius=.1f;
    require(world.trace(sweep,filter).hits.size()==1 && world.trace(sweep,filter).hits[0].entity==30,"Shape sweep omitted explicit triggers.");
    filter.includeTriggers=false;filter.includeCharacters=true;
    require(world.trace(sweep,filter).hits.size()==1 && world.trace(sweep,filter).hits[0].entity==20,"Shape sweep omitted virtual characters.");
    // One entity may own a controller capsule and an independently filtered
    // trigger primitive. Their physical memberships must stay separate.
    character.local=math::translation({0,0,-4});character.collider=trigger.collider;character.collider->radius=1.f;
    scene.apply({character},{});world.reconcile(scene);auto elevated=forward();elevated.ray.origin[1]=.9f;
    filter={};filter.includeTriggers=true;filter.mask=TriggerCollisionLayer;
    require(world.trace(elevated,filter).hits.size()==1 && world.trace(elevated,filter).hits[0].entity==20,"Same-entity controller layers overwrote its trigger's independent query layers.");
    filter.includeTriggers=false;
    require(world.trace(elevated,filter).hits.empty(),"Controller capsule inherited its same-entity trigger's layers.");
    filter.mask=PlayerCollisionLayer;
    require(world.trace(elevated,filter).hits.size()==1,"Same-entity trigger layers overwrote its controller's independent query layers.");
}
void triangleDedupAndInitialContacts() {
    EntityRecord mesh;mesh.id=1;mesh.name="Two triangle sheets";mesh.collider.emplace();mesh.collider->shape=ColliderShape::Trimesh;
    mesh.collider->mesh=makeCollisionMesh({{-2,-2,-3},{2,-2,-3},{0,2,-3},{-2,-2,-6},{2,-2,-6},{0,2,-6}},{0,1,2,3,4,5});
    Scene scene;scene.apply({mesh,sphere(2,{0,0,-8},.5f)},{});PhysicsWorld world;world.reconcile(scene);
    const auto all=world.trace(forward(),{},PhysicsTraceMode::All);
    require(all.hits.size()==2 && all.hits[0].entity==1 && near(all.hits[0].distance,3) && all.hits[1].entity==2,"All trace emitted multiple triangle hits for one collider or lost geometry beyond it.");
    auto backwards=forward();backwards.ray={{0,0,-4},{0,0,1}};
    require(world.trace(backwards).hits.size()==1 && near(world.trace(backwards).hits[0].distance,1),"Triangle trace did not detect a backface.");
    auto swept=forward();swept.shape=PhysicsTraceShape::Sphere;swept.radius=.25f;
    const auto sweepAll=world.trace(swept,{},PhysicsTraceMode::All);
    require(sweepAll.hits.size()==2 && sweepAll.hits[0].entity==1 && near(sweepAll.hits[0].distance,2.75f),"All shape sweep did not deduplicate triangle contacts or preserve continuation.");
    scene.apply({sphere(5,{0,0,0},1)},{});world.reconcile(scene);
    const auto interior=world.trace(forward());
    require(interior.hits.size()==1 && interior.hits[0].startedInside && interior.hits[0].distance==0 && math::finite(interior.hits[0].normal) && near(static_cast<float>(math::length(interior.hits[0].normal)),1),"Interior line contact lost its zero distance or finite fallback normal.");
    const auto overlap=world.trace(swept);
    require(overlap.hits.size()==1 && overlap.hits[0].startedInside && overlap.hits[0].distance==0 && math::finite(overlap.hits[0].position),"Initially overlapping sweep lost its contact.");
}
void validationAndReconciliation() {
    Scene scene;scene.apply({sphere(1,{0,0,-4})},{});PhysicsWorld world;world.reconcile(scene);
    auto invalid=forward();invalid.maxDistance=0;require(!world.trace(invalid).valid,"Zero length trace was accepted.");
    invalid=forward();invalid.ray.direction={};require(!world.trace(invalid).valid,"Zero direction trace was accepted.");
    invalid=forward();invalid.ray.origin[0]=std::numeric_limits<float>::quiet_NaN();require(!world.trace(invalid).valid,"Nonfinite trace origin was accepted.");
    invalid=forward();invalid.maxDistance=std::numeric_limits<float>::infinity();require(!world.trace(invalid).valid,"Infinite trace distance was accepted.");
    invalid=forward();invalid.shape=PhysicsTraceShape::Sphere;invalid.radius=-1;require(!world.trace(invalid).valid,"Negative sweep radius was accepted.");
    invalid=forward();invalid.shape=PhysicsTraceShape::Box;invalid.halfExtents[0]=0;require(!world.trace(invalid).valid,"Degenerate box sweep was accepted.");
    invalid=forward();invalid.shape=PhysicsTraceShape::Capsule;invalid.capsuleHalfHeight=-1;require(!world.trace(invalid).valid,"Negative capsule cylinder height was accepted.");
    invalid=forward();invalid.shape=PhysicsTraceShape::Box;invalid.orientation={0,0,0,0};require(!world.trace(invalid).valid,"Zero rotation quaternion was accepted.");
    scene.apply({sphere(1,{0,0,-7})},{});world.reconcile(scene);
    require(near(world.trace(forward()).hits[0].distance,6),"Live trace retained stale collider geometry.");
    scene.apply({},{});world.reconcile(scene);require(world.trace(forward()).hits.empty(),"Removed collider survived in trace query state.");
}
void initialOverlapOrdering() {
    // Change insertion history as well as contact depth: native broadphase
    // traversal must not select the distance-zero winner for the public API.
    for(bool deepestFirst:{false,true}) {
        const auto deep=sphere(90,{0,0,0},4),shallow=sphere(1,{.8f,0,0},1);
        Scene scene;scene.apply({deepestFirst?deep:shallow},{});PhysicsWorld world;world.reconcile(scene);
        scene.apply({deep,shallow},{});world.reconcile(scene);
        auto request=forward();request.shape=PhysicsTraceShape::Sphere;request.radius=.25f;
        const auto closest=world.trace(request);
        require(closest.hits.size()==1 && closest.hits[0].entity==1 && closest.hits[0].startedInside && closest.hits[0].distance==0,
            "Initial-overlap penetration depth pruned the deterministic distance/entity winner.");
        const auto all=world.trace(request,{},PhysicsTraceMode::All);
        require(all.hits.size()==2 && all.hits[0].entity==1 && all.hits[1].entity==90 && all.hits[0].distance==0 && all.hits[1].distance==0,
            "All initial overlaps were not retained and sorted by distance/entity.");
    }
}
}
int main() {
    try {lineModesAndLayers();shapeSweepsAndOrientation();ownerFilteringCharactersAndTriggers();triangleDedupAndInitialContacts();validationAndReconciliation();initialOverlapOrdering();std::cout<<"Physics trace checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
