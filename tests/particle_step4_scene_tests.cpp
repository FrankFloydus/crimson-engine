#include "editor/SceneParticles.h"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message,double tolerance=1e-5){require(std::isfinite(actual)&&std::abs(actual-expected)<tolerance,message);}
ParticlePresentation packet(const SceneParticles& scene,EntityId id) {
    for(const auto& group:scene.groups())if(group.entity==id && group.count)return scene.presentation()[group.first];
    throw std::runtime_error("Expected a visible particle packet for the scene instance.");
}
struct Snapshot {
    const ParticleState* storage;std::vector<ParticleState> records;double elapsed,interpolation;uint64_t seed;
    ParticlePlaybackState state;ParticleStats stats;
    explicit Snapshot(const CpuParticleSystem& system):storage(system.particles().data()),records(system.particles().begin(),system.particles().end()),
        elapsed(system.elapsed()),interpolation(system.interpolation()),seed(system.seed()),state(system.state()),stats(system.stats()){}
    void unchanged(const CpuParticleSystem& system)const {
        require(storage==system.particles().data() && records.size()==system.particles().size() && elapsed==system.elapsed() &&
            interpolation==system.interpolation() && seed==system.seed() && state==system.state(),
            "Scene appearance publication restarted an instance or changed its allocation/clock.");
        for(size_t i=0;i<records.size();++i) {
            const auto& a=records[i];const auto& b=system.particles()[i];
            require(a.spawnOrdinal==b.spawnOrdinal && a.previousPosition==b.previousPosition && a.position==b.position && a.velocity==b.velocity &&
                a.age==b.age && a.lifetime==b.lifetime && a.birthTime==b.birthTime,
                "Scene appearance publication changed authoritative particle records.");
        }
        const auto& after=system.stats();require(after.live==stats.live && after.ticks==stats.ticks && after.spawned==stats.spawned &&
            after.droppedSpawns==stats.droppedSpawns && after.droppedCatchUpTicks==stats.droppedCatchUpTicks &&
            after.droppedScheduleSegments==stats.droppedScheduleSegments && after.burstsFired==stats.burstsFired &&
            after.clearedCapacityParticles==stats.clearedCapacityParticles && after.droppedWallTime==stats.droppedWallTime,
            "Scene appearance publication changed simulation diagnostics.");
    }
};
std::shared_ptr<ParticleEffectAsset> effect() {
    auto asset=std::make_shared<ParticleEffectAsset>(defaultParticleEffect());asset->id="assets/particles/appearance-scene.crimson-particle";
    auto& e=asset->emitter;e.rate=0;e.finiteDuration=true;e.duration=.1f;e.bursts={{1,0,1}};e.lifetime=1;e.gravityBehavior.enabled=false;
    return asset;
}
void liveRevisionsPreserveIndependentInstances() {
    auto asset=effect();uint64_t revision=1;
    const auto query=[&](const std::string&)->ParticleDefinitionVersion{return {asset,revision,{}};};
    std::vector<ExtractedParticle> authored{{11,asset->id,math::translation({2,3,4})}};
    SceneParticles scene;scene.reconcile(1,authored,query,ParticleFixedStep*5,NoEntity,1);
    authored.push_back({12,asset->id,math::translation({12,3,4})});scene.reconcile(1,authored,query,0,NoEntity,1);
    scene.reconcile(1,authored,query,ParticleFixedStep*5,NoEntity,1);
    const Snapshot older(*scene.instance(11)),younger(*scene.instance(12));
    require(older.elapsed>younger.elapsed && older.records.size()==1 && younger.records.size()==1,"Scene fixture failed to create independently aged instances.");
    auto& e=asset->emitter;e.sizeTrack.mode=ParticleAppearanceMode::Curve;
    e.sizeTrack.curve={ParticleInterpolation::Linear,{{1,0,.1f},{2,1,.9f}}};
    e.intensityTrack.mode=ParticleAppearanceMode::Curve;e.intensityTrack.curve={ParticleInterpolation::Linear,{{1,0,1},{2,1,3}}};
    e.rotationTrack.mode=ParticleAppearanceMode::Curve;e.rotationTrack.curve={ParticleInterpolation::Linear,{{1,0,0},{2,1,360}}};
    e.color={0,1,0};e.opacity=.3f;e.colorMode=ParticleColorMode::Gradient;
    e.colorGradient={ParticleInterpolation::Linear,{{1,0,{1,0,0,1}},{2,1,{0,0,1,0}}}};
    ++revision;scene.reconcile(1,authored,query,0,NoEntity,1);
    older.unchanged(*scene.instance(11));younger.unchanged(*scene.instance(12));
    const auto oldPacket=packet(scene,11),youngPacket=packet(scene,12);
    require(oldPacket.size>youngPacket.size && oldPacket.rotation>youngPacket.rotation && oldPacket.color[3]<youngPacket.color[3],
        "Curve/gradient publication coupled the two instance appearance clocks.");
    for(const auto id:{11ULL,12ULL}) {
        const auto* system=scene.instance(id);const auto& p=system->particles().front();const auto value=packet(scene,id);
        const auto age=p.age/p.lifetime;
        near(value.size,.1+.8*age,"Scene size curve did not use the instance's normalized particle age.");
        near(std::remainder(value.rotation-2*std::numbers::pi*age,2*std::numbers::pi),0,"Scene packets lost absolute age rotation.");
        near(value.color[0],(1-age)*(1+2*age),"Scene gradient/intensity composition applied the wrong radiance.");
        near(value.color[1],0,"Gradient mode retained the unused authored tint.");near(value.color[3],1-age,"Gradient mode retained constant opacity.");
    }
    // A second appearance revision switches modes on already-born particles.
    e.sizeTrack.mode=ParticleAppearanceMode::RandomRange;e.sizeTrack.range=ParticleAppearanceRange{.4f,5};
    e.rotationTrack.mode=ParticleAppearanceMode::Constant;e.rotation=45;++revision;
    scene.reconcile(1,authored,query,0,NoEntity,1);older.unchanged(*scene.instance(11));younger.unchanged(*scene.instance(12));
    near(packet(scene,11).rotation,std::numbers::pi/4,"Scene appearance revision did not publish rotated presentation packets.");
    near(scene.instance(11)->maxPresentationSize(),5,"Range maximum was not published to the scene instance.");
}
void boundsCoverSelectedAppearanceModes() {
    auto asset=effect();auto& e=asset->emitter;e.space=ParticleSpace::Local;e.size=2;e.rotation=45;
    uint64_t revision=1;const auto query=[&](const std::string&)->ParticleDefinitionVersion{return {asset,revision,{}};};
    Mat4 affine=math::translation({3,4,5})*math::scaling({-50,.01f,3});affine[4]=.7f;
    std::vector<ExtractedParticle> authored{{11,asset->id,affine}};
    SceneParticles scene;scene.reconcile(2,authored,query,ParticleFixedStep,NoEntity,1);
    const Snapshot state(*scene.instance(11));
    const auto check=[&](float maximum,const Vec3& center) {
        const auto bounds=scene.bounds(11);require(bounds && bounds->valid,"Selection bounds were unavailable for a live effect.");
        const auto radius=maximum/std::sqrt(2.0);
        for(size_t axis=0;axis<3;++axis) {
            near(bounds->minimum[axis],center[axis]-radius,"Bounds did not cover the rotated sprite half-diagonal or inherited emitter scale.");
            near(bounds->maximum[axis],center[axis]+radius,"Bounds did not use the selected appearance maximum.");
        }
    };
    check(2,{3,4,5});near(packet(scene,11).rotation,std::numbers::pi/4,"Constant rotation did not reach scene presentation.");
    e.sizeTrack.mode=ParticleAppearanceMode::RandomRange;e.sizeTrack.range=ParticleAppearanceRange{.5f,6};++revision;
    scene.reconcile(2,authored,query,0,NoEntity,1);state.unchanged(*scene.instance(11));check(6,{3,4,5});
    e.sizeTrack.mode=ParticleAppearanceMode::Curve;e.sizeTrack.curve={ParticleInterpolation::Linear,{{1,0,.2f},{2,.25f,9},{3,1,.4f}}};++revision;
    scene.reconcile(2,authored,query,0,NoEntity,1);state.unchanged(*scene.instance(11));check(9,{3,4,5});
    require(packet(scene,11).size<9,"Curve maximum fixture accidentally sampled its maximum instead of testing a conservative bound.");
    auto presented=authored;presented.front().world[12]=7;
    scene.reconcile(2,authored,query,0,NoEntity,1,presented);state.unchanged(*scene.instance(11));check(9,{7,4,5});
    e.sizeTrack.mode=ParticleAppearanceMode::Constant;e.size=.4f;++revision;
    scene.reconcile(2,authored,query,0,NoEntity,1,presented);state.unchanged(*scene.instance(11));check(.4f,{7,4,5});
    near(scene.instance(11)->maxPresentationSize(),.4,"Inactive range/curve values inflated selected constant bounds.");
    e.sizeTrack.mode=ParticleAppearanceMode::Curve;e.sizeTrack.curve={ParticleInterpolation::Linear,{{1,0,0},{2,1,0}}};
    ++revision;scene.reconcile(2,authored,query,0,NoEntity,1,presented);state.unchanged(*scene.instance(11));
    require(scene.presentation().empty() && scene.instance(11)->stats().live==1 && scene.instance(11)->stats().droppedSpawns==0,
        "Invisible zero-size scene appearance changed live simulation or reported overflow.");
}
void sceneHoldsMovedEdges() {
    auto asset=effect();auto& e=asset->emitter;e.sizeTrack.mode=ParticleAppearanceMode::Curve;
    e.sizeTrack.curve={ParticleInterpolation::Linear,{{1,.3f,.2f},{2,.7f,.8f}}};
    e.rotationTrack.mode=ParticleAppearanceMode::Curve;e.rotationTrack.curve={ParticleInterpolation::Linear,{{1,.3f,45},{2,.7f,90}}};
    e.colorMode=ParticleColorMode::Gradient;e.colorGradient={ParticleInterpolation::Linear,{{1,.3f,{1,0,0,.5f}},{2,.7f,{0,1,0,.9f}}}};
    uint64_t revision=1;const auto query=[&](const std::string&)->ParticleDefinitionVersion{return {asset,revision,{}};};
    std::vector<ExtractedParticle> authored{{11,asset->id,math::translation({2,3,4})}};
    SceneParticles scene;scene.reconcile(3,authored,query,ParticleFixedStep,NoEntity,1);
    const auto first=packet(scene,11);
    near(first.size,.2,"Scene appearance extrapolated before its moved first size key.");
    near(first.rotation,std::numbers::pi/4,"Scene appearance did not hold its first rotation key.");
    near(first.color[0],1,"Scene gradient did not hold its first stop color.");near(first.color[3],.5,"Scene gradient did not hold its first stop opacity.");
    for(int tick=1;tick<30;++tick)scene.reconcile(3,authored,query,ParticleFixedStep,NoEntity,1);
    const auto middle=packet(scene,11);
    near(middle.size,.5,"Scene curve interpolation changed inside a moved-edge interval.");
    near(middle.color[0],.5,"Scene gradient did not interpolate between moved edge stops.");near(middle.color[3],.7,"Scene opacity did not interpolate between moved stops.");
    for(int tick=30;tick<45;++tick)scene.reconcile(3,authored,query,ParticleFixedStep,NoEntity,1);
    const auto last=packet(scene,11);
    near(last.size,.8,"Scene appearance did not hold the moved final size key.");
    near(last.rotation,std::numbers::pi/2,"Scene appearance did not hold its final rotation key.");
    near(last.color[1],1,"Scene appearance did not hold its final gradient stop.");near(last.color[3],.9,"Scene opacity extrapolated after its final stop.");
    const Snapshot before(*scene.instance(11));
    e.sizeTrack.curve.keys.front().age=.8f;e.sizeTrack.curve.keys.back().age=.95f;
    e.rotationTrack.curve.keys.front().age=.8f;e.rotationTrack.curve.keys.back().age=.95f;
    e.colorGradient.stops.front().age=.8f;e.colorGradient.stops.back().age=.95f;++revision;
    scene.reconcile(3,authored,query,0,NoEntity,1);before.unchanged(*scene.instance(11));
    const auto retimed=packet(scene,11);
    near(retimed.size,.2,"Live movement of a scene first key did not immediately update held appearance.");
    near(retimed.color[3],.5,"Live movement of the first gradient stop did not update scene opacity.");
    near(scene.instance(11)->maxPresentationSize(),.8,"Moving key ages lost conservative value bounds.");
}
}
int main() {
    try {
        liveRevisionsPreserveIndependentInstances();boundsCoverSelectedAppearanceModes();sceneHoldsMovedEdges();
        std::cout<<"Step 4 scene appearance revisions, independent clocks, rotation packets and conservative bounds checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
