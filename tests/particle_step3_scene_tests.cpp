#include "editor/SceneParticles.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace crimson;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool close(const Vec3& a,const Vec3& b){return math::length(a-b)<1e-4;}
void sceneSpacesAndRevisions() {
    auto asset=std::make_shared<ParticleEffectAsset>(defaultParticleEffect());asset->id="assets/particles/step3.crimson-particle";
    auto& e=asset->emitter;e.space=ParticleSpace::Local;e.rate=120;e.gravityBehavior.enabled=false;e.initialVelocity={0,1,0};
    std::uint64_t revision=1;
    const auto query=[&](const std::string&)->ParticleDefinitionVersion{return {asset,revision,{}};};
    Mat4 affine=math::translation({7,2,-5})*math::scaling({-2,3,4});affine[4]=.8f;
    std::vector<ExtractedParticle> sources{{11,asset->id,affine},{12,asset->id,math::translation({20,0,0})}};
    SceneParticles scene;scene.reconcile(1,sources,query,ParticleFixedStep*2,NoEntity,1);
    const auto* first=scene.instance(11);require(first && scene.instance(12),"Two independent Step 3 instances were not admitted.");
    const auto local=first->particles().front().position;const auto time=first->elapsed();const auto otherTime=scene.instance(12)->elapsed();
    require(close(scene.presentation().front().position,math::transformPoint(affine,local)),"Local presentation lost a mirrored/sheared affine transform.");
    auto moved=affine;moved[12]+=10;sources[0].world=moved;scene.reconcile(1,sources,query,0,NoEntity,1);
    require(scene.instance(11)->elapsed()==time && scene.instance(12)->elapsed()==otherTime && scene.instance(11)->particles().front().position==local,
        "Moving a local instance changed its clocks or stored records.");
    const auto world=math::transformPoint(moved,local);
    require(close(scene.presentation().front().position,world),"Existing local particles did not follow their emitter.");
    const auto bounds=scene.bounds(11);require(bounds && bounds->valid,"Local particle bounds were unavailable.");
    for(std::size_t axis=0;axis<3;++axis)require(bounds->minimum[axis]<=world[axis] && bounds->maximum[axis]>=world[axis],"Local selection bounds used local coordinates as world coordinates.");
    e.color={.2f,.5f,.8f};++revision;scene.reconcile(1,sources,query,0,NoEntity,1);
    require(scene.instance(11)->elapsed()==time && scene.instance(11)->particles().front().position==local,"Appearance publication restarted local instances.");
    e.space=ParticleSpace::World;++revision;scene.reconcile(1,sources,query,0);
    require(scene.instance(11)->elapsed()==0 && scene.instance(12)->elapsed()==0 && scene.instance(11)->particles().empty(),"Space edits retained particles from the old coordinate space.");
    scene.reconcile(1,sources,query,ParticleFixedStep*2,NoEntity,1);const auto detached=scene.instance(11)->particles().front().position;
    sources[0].world=math::translation({100,200,300});scene.reconcile(1,sources,query,0,NoEntity,1);
    require(scene.instance(11)->particles().front().position==detached && close(scene.presentation().front().position,detached),"World particles followed a moved emitter.");
    scene.reconcile(2,sources,query,0);require(scene.instance(11)->particles().empty(),"Document replacement retained local/world transient state.");
}
void sceneOneShot() {
    auto asset=std::make_shared<ParticleEffectAsset>(defaultParticleEffect());asset->id="assets/particles/impact.crimson-particle";
    auto& e=asset->emitter;e.rate=0;e.finiteDuration=true;e.duration=.05f;e.lifetime=.07f;e.bursts={{9,0,8}};
    e.shape=ParticleShape::Sphere;e.velocityMode=ParticleVelocityMode::DirectionSpeed;e.directionMode=ParticleDirectionMode::Outward;
    const auto query=[&](const std::string&)->ParticleDefinitionVersion{return {asset,1,{}};};
    std::vector<ExtractedParticle> sources{{11,asset->id,Mat4{}},{12,asset->id,math::translation({4,0,0})}};
    SceneParticles scene;
    for(int tick=0;tick<10;++tick)scene.reconcile(3,sources,query,ParticleFixedStep);
    require(scene.instance(11)->completed() && scene.instance(12)->completed() && scene.presentation().empty(),"Scene one-shots did not finish and drain.");
    const auto other=scene.instance(12)->elapsed();scene.reconcile(3,sources,query,0,11);
    require(!scene.instance(11)->completed() && scene.instance(11)->elapsed()==0 && scene.instance(12)->completed() && scene.instance(12)->elapsed()==other,
        "Play Effect did not independently restart a completed scene instance.");
    scene.reconcile(3,sources,query,ParticleFixedStep);require(scene.instance(11)->stats().spawned==8 && scene.instance(12)->particles().empty(),"Scene replay repeated an unrelated completed instance.");
}
void interpolatedParentPresentation() {
    auto localAsset=std::make_shared<ParticleEffectAsset>(defaultParticleEffect());localAsset->id="assets/particles/local-attached.crimson-particle";
    auto& e=localAsset->emitter;e.space=ParticleSpace::Local;e.rate=120;e.gravityBehavior.enabled=false;e.initialVelocity={0,1,0};
    auto worldAsset=std::make_shared<ParticleEffectAsset>(*localAsset);worldAsset->id="assets/particles/world-detached.crimson-particle";
    worldAsset->emitter.space=ParticleSpace::World;
    const auto query=[&](const std::string& id)->ParticleDefinitionVersion{return {id==localAsset->id?localAsset:worldAsset,1,{}};};
    EntityRecord parent;parent.id=10;parent.name="Moving parent";
    parent.local=math::translation({10,2,3})*math::scaling({-2,3,4});parent.local[4]=.7f;
    EntityRecord attached;attached.id=11;attached.name="Local particles";attached.parent=parent.id;attached.particle=AssetRef<ParticleEffectAsset>{localAsset->id};
    attached.local=math::translation({1,0,0});
    EntityRecord detached=attached;detached.id=12;detached.name="World particles";detached.particle=AssetRef<ParticleEffectAsset>{worldAsset->id};
    Scene tickScene;tickScene.apply({parent,attached,detached},{});
    auto sources=extractScene(tickScene).particles;
    SceneParticles runtime;runtime.reconcile(4,sources,query,ParticleFixedStep*2,NoEntity,1);
    const auto localRecord=runtime.instance(11)->particles().front();const auto worldRecord=runtime.instance(12)->particles().front();
    const auto time=runtime.instance(11)->elapsed();

    // Like Play's presentation scene, interpolate the moving parent's pose and
    // compose its children from that pose rather than the current tick matrix.
    auto halfParent=parent;halfParent.local[12]=5;
    Scene presentationScene;presentationScene.apply({halfParent,attached,detached},{});
    auto presented=extractScene(presentationScene).particles;
    const auto localWorld=*presentationScene.worldTransform(11);
    runtime.reconcile(4,sources,query,0,NoEntity,.5,presented);
    require(runtime.instance(11)->elapsed()==time && runtime.instance(11)->particles().front().position==localRecord.position &&
        runtime.instance(12)->particles().front().position==worldRecord.position,
        "Interpolated presentation transforms changed simulation clocks or authoritative particle records.");
    const auto indexOf=[&](EntityId id){for(const auto& group:runtime.groups())if(group.entity==id)return group.first;throw std::runtime_error("Missing particle group.");};
    const auto localPoint=localRecord.previousPosition+(localRecord.position-localRecord.previousPosition)*.5;
    const auto worldPoint=worldRecord.previousPosition+(worldRecord.position-worldRecord.previousPosition)*.5;
    require(close(runtime.presentation()[indexOf(11)].position,math::transformPoint(localWorld,localPoint)),
        "Local particles render ahead of their interpolated moving parent.");
    require(close(runtime.presentation()[indexOf(12)].position,worldPoint),
        "World particles inherited a presentation-only parent transform.");

    // The owner must keep its own matrix, not a pointer into this frame's
    // extracted values, because selection bounds are read after reconciliation.
    for(auto& input:presented)input.world=math::translation({500,600,700});
    const auto bounds=runtime.bounds(11);const auto expectedCurrent=math::transformPoint(localWorld,localRecord.position);
    require(bounds && bounds->valid,"Interpolated local selection bounds were unavailable.");
    for(std::size_t axis=0;axis<3;++axis)require(bounds->minimum[axis]<=expectedCurrent[axis] && bounds->maximum[axis]>=expectedCurrent[axis],
        "Local bounds lost the copied presentation matrix or used a later caller mutation.");
    runtime.reconcile(4,sources,query,ParticleFixedStep,NoEntity,1);
    const auto expectedBirth=math::transformPoint(*tickScene.worldTransform(12),{});
    require(close(runtime.instance(12)->particles().back().previousPosition,expectedBirth),
        "Presentation-only transforms leaked into later authoritative world births.");
    require(close(runtime.presentation()[indexOf(11)].position,
        math::transformPoint(*tickScene.worldTransform(11),runtime.instance(11)->particles().front().position)),
        "Omitted presentation transforms did not fall back to the current source world.");
}
}
int main(){try{sceneSpacesAndRevisions();sceneOneShot();interpolatedParentPresentation();std::cout<<"Step 3 scene reconciliation, interpolated hierarchy, bounds and one-shot replay checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
