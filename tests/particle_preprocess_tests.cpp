#include "editor/SceneParticles.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void same(const CpuParticleSystem& actual,const CpuParticleSystem& expected) {
    require(actual.elapsed()==expected.elapsed() && actual.seed()==expected.seed(),"Pre-process changed the ordinary fixed clock or seed.");
    require(actual.particles().size()==expected.particles().size(),"Pre-process changed ordinary seeded particle counts.");
    for(std::size_t i=0;i<actual.particles().size();++i) {
        const auto& a=actual.particles()[i];const auto& b=expected.particles()[i];
        require(a.spawnOrdinal==b.spawnOrdinal && a.previousPosition==b.previousPosition && a.position==b.position && a.velocity==b.velocity &&
            a.age==b.age && a.lifetime==b.lifetime && a.birthTime==b.birthTime,"Pre-process differs from the same ordinary seeded ticks.");
    }
    const auto& a=actual.stats();const auto& b=expected.stats();
    require(a.ticks==b.ticks && a.spawned==b.spawned && a.burstsFired==b.burstsFired && a.droppedSpawns==b.droppedSpawns &&
        a.droppedScheduleSegments==b.droppedScheduleSegments,"Pre-process changed ordinary emission/capacity accounting.");
    require(a.droppedCatchUpTicks==0 && a.droppedWallTime==0,"Pre-process was reported as overload or retained as backlog.");
}
ParticleEffectAsset fire() {
    auto asset=defaultParticleEffect();asset.id="assets/particles/preprocess.crimson-particle";asset.preprocessSeconds=1.001f;
    auto& e=asset.emitter;e.rate=37;e.lifetimeRange={true,.2f,.9f};e.shape=ParticleShape::Sphere;e.radius=.3f;
    e.velocityMode=ParticleVelocityMode::DirectionSpeed;e.directionMode=ParticleDirectionMode::RandomCone;e.direction={0,1,0};
    e.speedRange={true,.1f,2};e.directionSpread=40;e.directionFlatness=.4f;
    e.delay=.04f;e.finiteDuration=true;e.duration=.13f;e.loop=true;e.bursts={{1,0,2},{2,.04f,1}};
    e.accelerationBehavior={true,true};e.acceleration={1,0,0};e.dragBehavior={true,true};e.drag=.4f;
    return asset;
}
void ordinaryReplayAndSpaces() {
    for(const auto space:{ParticleSpace::World,ParticleSpace::Local}) {
        auto asset=fire();asset.emitter.space=space;auto ordinary=asset;ordinary.preprocessSeconds=0;
        auto initial=math::translation({7,3,-2})*math::scaling({-2,3,1});initial[4]=.6f;
        const auto moved=math::translation({30,9,4})*math::scaling({2,1,3});
        CpuParticleSystem warmed,reference;warmed.restart(asset,72,initial);reference.restart(ordinary,72,initial);
        require(warmed.stats().ticks==61 && !warmed.particles().empty(),"Restart returned before all configured pre-process ticks finished.");
        for(unsigned tick=0;tick<61;++tick)reference.tick();same(warmed,reference);
        require(warmed.setTransform(moved),"Valid transform was rejected after synchronous pre-process.");
        std::vector<ParticlePresentation> shown,expected;
        warmed.appendPresentation(shown,0);reference.appendPresentation(expected,1,&moved);
        require(!shown.empty() && shown.size()==expected.size(),"The first presentation did not expose the final warmed state.");
        for(std::size_t i=0;i<shown.size();++i)require(shown[i].position==expected[i].position && shown[i].color==expected[i].color,
            "Warmed presentation lost the initial birth transform or later local attachment.");
        const auto readyTime=warmed.elapsed();warmed.advance(0);require(warmed.elapsed()==readyTime,"Zero first-frame time advanced a warmed effect.");
        reference.setTransform(moved);warmed.advance(ParticleFixedStep);reference.advance(ParticleFixedStep);same(warmed,reference);
        warmed.restart(asset,72,initial);reference.restart(ordinary,72,initial);for(unsigned tick=0;tick<61;++tick)reference.tick();same(warmed,reference);
    }
}
void firstVisibleTickAndLifecycle() {
    auto asset=defaultParticleEffect();asset.preprocessSeconds=4.05f;asset.emitter.rate=60;
    CpuParticleSystem system;system.restart(asset);require(system.stats().ticks==243,"Decimal 4.05 seconds added a tick from float representation noise.");
    asset.preprocessSeconds=.1f;system.restart(asset);require(system.stats().ticks==6,"Decimal 0.1 seconds unexpectedly added a seventh tick.");
    std::vector<ParticlePresentation> visible;system.appendPresentation(visible);
    require(visible.size()==system.particles().size() && !visible.empty(),"First presentation hid final newborns for an interpolation tick.");
    system.pause();const auto elapsed=system.elapsed();const auto ticks=system.stats().ticks;
    asset.emitter.color={5,1,.1f};system.updateDefinition(asset);system.setTransform(math::translation({5,0,0}));
    require(system.elapsed()==elapsed && system.stats().ticks==ticks,"Appearance/transform edits restarted warmed playback.");
    system.play();require(system.elapsed()==elapsed && system.stats().ticks==ticks,"Resume repeated an already completed pre-process.");
    asset.preprocessSeconds=2;system.pause();system.updateDefinition(asset);
    require(system.stats().ticks==120 && system.elapsed()>1.99 && system.state()==ParticlePlaybackState::Paused,
        "Changing pre-process time did not finish synchronously while retaining Pause.");
    asset.defaultSeed=9;system.updateDefinition(asset);require(system.stats().ticks==120 && system.seed()==9 && system.state()==ParticlePlaybackState::Paused,
        "Seed edits failed to rebuild a deterministic paused warm state.");
    system.stop();require(system.elapsed()==0 && system.particles().empty() && system.stats().ticks==0,"Stop retained simulated state.");
    asset.preprocessSeconds=60;asset.emitter.delay=.05f;system.updateDefinition(asset);
    require(system.state()==ParticlePlaybackState::Stopped && system.stats().ticks==0 && system.elapsed()==0,"Editing a stopped effect unnecessarily performed warm-up work.");
    // Keep the maximum-duration check cheap while verifying its exact work bound.
    asset.emitter.rate=0;system.updateDefinition(asset);system.play();require(system.stats().ticks==3600,"Play did not synchronously apply maximum configured pre-process.");
    auto invalid=asset;invalid.preprocessSeconds=std::numeric_limits<float>::infinity();bool rejected=false;
    try{system.restart(invalid);}catch(const std::exception&){rejected=true;}
    require(rejected && system.stats().ticks==3600,"Nonfinite preparation time replaced working runtime state.");
    asset.preprocessSeconds=std::numeric_limits<float>::denorm_min();system.restart(asset);require(system.stats().ticks==1,"Positive pre-process time was rounded down to no work.");
    system.stop();asset.preprocessSeconds=0;system.restart(asset);require(system.stats().ticks==0 && system.elapsed()==0,"Zero pre-process changed legacy immediate playback.");
    system.tick();require(system.stats().ticks==1,"Zero pre-process suppressed the first ordinary tick.");
}
void naturalCompletionAndZeroCompatibility() {
    auto asset=defaultParticleEffect();asset.preprocessSeconds=60;
    auto& e=asset.emitter;e.rate=0;e.delay=.05f;e.finiteDuration=true;e.duration=.02f;e.lifetime=.03f;e.bursts={{1,0,3}};
    CpuParticleSystem system;system.restart(asset);
    require(system.stats().ticks>0 && system.stats().ticks<12 && system.completed() && system.particles().empty(),
        "A naturally drained one-shot replayed idle ticks for the remaining pre-process.");
    require(system.stats().burstsFired==1 && system.stats().droppedCatchUpTicks==0,"One-shot pre-process duplicated bursts or reported idle work as overload.");
    const auto ticks=system.stats().ticks;system.play();require(system.completed() && system.stats().ticks==ticks,"Playing a completed one-shot did not deterministically pre-process it again.");
    asset=fire();asset.preprocessSeconds=0;CpuParticleSystem exact,wall;exact.restart(asset,35);wall.restart(asset,35);
    for(unsigned tick=0;tick<120;++tick){exact.tick();wall.advance(ParticleFixedStep);}same(exact,wall);
}
void sceneFirstPresentationAndRestart() {
    auto asset=std::make_shared<ParticleEffectAsset>(defaultParticleEffect());asset->id="assets/particles/scene-preprocess.crimson-particle";
    asset->preprocessSeconds=2;asset->emitter.rate=60;std::uint64_t revision=1;
    const auto query=[&](const std::string&)->ParticleDefinitionVersion{return {asset,revision,{}};};
    std::vector<ExtractedParticle> sources;for(EntityId id=1;id<=16;++id)sources.push_back({id,asset->id,math::translation({double(id),2,0})});
    SceneParticles scene;scene.reconcile(1,sources,query,0);
    for(const auto& source:sources)require(scene.instance(source.id) && scene.instance(source.id)->stats().ticks==120 && scene.bounds(source.id).has_value(),
        "The first scene reconciliation left an emitter partially prepared or omitted warmed bounds.");
    require(scene.groups().size()==16 && !scene.presentation().empty(),"First scene presentation did not contain ready preprocessed instances.");
    for(const auto& group:scene.groups())require(group.count>0,"A later admitted scene instance remained blank during its first presentation.");
    const auto otherTime=scene.instance(2)->elapsed();scene.reconcile(1,sources,query,0,1);
    require(scene.instance(1)->stats().ticks==120 && scene.instance(2)->elapsed()==otherTime && scene.bounds(1),
        "Play Effect did not immediately restore the warm state or reset an unrelated instance.");
    ++revision;asset->emitter.color={3,.5f,.1f};scene.reconcile(1,sources,query,0);
    require(scene.instance(1)->stats().ticks==120 && scene.instance(2)->elapsed()==otherTime,"Appearance publication restarted warmed clocks.");
    scene.reconcile(2,sources,query,0);
    for(const auto& source:sources)require(scene.instance(source.id)->stats().ticks==120 && scene.bounds(source.id),"Document replacement did not immediately rebuild warm transient state.");
}
}
int main(){try{ordinaryReplayAndSpaces();firstVisibleTickAndLifecycle();naturalCompletionAndZeroCompatibility();sceneFirstPresentationAndRestart();
    std::cout<<"Synchronous deterministic particle pre-process, lifecycle, first presentation and scene checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
