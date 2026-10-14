#include "assets/ParticleAsset.h"
#include "particles/CpuParticleSystem.h"
#include "particles/ParticleAppearance.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace crimson;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message,double tolerance=1e-5) {
    require(std::isfinite(actual) && std::abs(actual-expected)<tolerance,message);
}
void nearAngle(double actual,double expected,const char* message) {near(std::remainder(actual-expected,2*std::numbers::pi),0,message);}
template<class Action> void rejects(Action action) {
    bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid appearance input was accepted.");
}
ParticleScalarCurve curve(float start,float end,ParticleInterpolation interpolation=ParticleInterpolation::Linear) {
    return {interpolation,{{1,0,start},{2,1,end}}};
}
ParticleEffectAsset pulse(uint32_t count=3) {
    auto asset=defaultParticleEffect();auto& e=asset.emitter;
    e.rate=0;e.finiteDuration=true;e.duration=.1f;e.bursts={{1,0,count}};e.lifetime=1;
    e.gravityBehavior.enabled=false;e.initialVelocity={0,1,0};return asset;
}
std::vector<ParticlePresentation> presentation(const CpuParticleSystem& system,std::optional<double> alpha=1) {
    std::vector<ParticlePresentation> result;system.appendPresentation(result,alpha);return result;
}
void sameMotion(const CpuParticleSystem& a,const CpuParticleSystem& b) {
    require(a.elapsed()==b.elapsed() && a.seed()==b.seed() && a.particles().size()==b.particles().size(),"Appearance altered simulated time, seed or live count.");
    for(size_t i=0;i<a.particles().size();++i) {
        const auto& x=a.particles()[i];const auto& y=b.particles()[i];
        require(x.spawnOrdinal==y.spawnOrdinal && x.previousPosition==y.previousPosition && x.position==y.position && x.velocity==y.velocity &&
            x.age==y.age && x.lifetime==y.lifetime && x.birthTime==y.birthTime,"Appearance altered authoritative birth/motion state.");
    }
}
struct LiveState {
    std::vector<ParticleState> particles;const ParticleState* storage;ParticleStats stats;
    ParticlePlaybackState state;double time,interpolation;uint64_t seed;
    explicit LiveState(const CpuParticleSystem& system):particles(system.particles().begin(),system.particles().end()),storage(system.particles().data()),
        stats(system.stats()),state(system.state()),time(system.elapsed()),interpolation(system.interpolation()),seed(system.seed()){}
    void unchanged(const CpuParticleSystem& system) const {
        require(storage==system.particles().data() && particles.size()==system.particles().size() && state==system.state() && time==system.elapsed() &&
            interpolation==system.interpolation() && seed==system.seed(),"Appearance editing reset live allocation, playback or clock.");
        for(size_t i=0;i<particles.size();++i) {
            const auto& before=particles[i];const auto& after=system.particles()[i];
            require(before.spawnOrdinal==after.spawnOrdinal && before.position==after.position && before.previousPosition==after.previousPosition &&
                before.velocity==after.velocity && before.age==after.age && before.lifetime==after.lifetime && before.birthTime==after.birthTime,
                "Appearance edit changed an existing authoritative particle record.");
        }
        const auto& current=system.stats();require(current.live==stats.live && current.ticks==stats.ticks && current.spawned==stats.spawned &&
            current.droppedSpawns==stats.droppedSpawns && current.droppedCatchUpTicks==stats.droppedCatchUpTicks &&
            current.clearedCapacityParticles==stats.clearedCapacityParticles && current.burstsFired==stats.burstsFired &&
            current.droppedScheduleSegments==stats.droppedScheduleSegments && current.droppedWallTime==stats.droppedWallTime,
            "Appearance edit altered runtime counters.");
    }
};
void pureCurveEvaluation() {
    ParticleScalarCurve authored{ParticleInterpolation::Linear,{{1,0,0},{7,.25f,2},{9,1,0}}};
    const auto linear=prepareParticleCurve(authored);
    near(linear.evaluate(-5),0,"Curve did not clamp its start.");near(linear.evaluate(.125),1,"Linear curve midpoint is incorrect.");
    near(linear.evaluate(.25),2,"Curve key boundary is incorrect.");near(linear.evaluate(.625),1,"Unequal curve intervals evaluated incorrectly.");
    near(linear.evaluate(5),0,"Curve did not clamp its end.");near(linear.maximum(),2,"Prepared curve maximum missed an interior key.");
    authored.keys[1].value=100;near(linear.evaluate(.25),2,"Prepared curve retained mutable authoring references.");
    authored.keys[1].value=2;authored.interpolation=ParticleInterpolation::Constant;
    const auto constant=prepareParticleCurve(authored);
    near(constant.evaluate(.249999),0,"Constant curve interpolated before the next key.");
    near(constant.evaluate(.25),2,"Constant curve did not advance at an exact key boundary.");
    near(constant.evaluate(.999999),2,"Constant curve did not hold its last interval.");near(constant.evaluate(1),0,"Constant curve did not include its endpoint.");
    near(constant.evaluate(std::numeric_limits<double>::quiet_NaN()),0,"Invalid sample age did not use the finite start value.");
    auto invalid=authored;invalid.keys[1].age=0;rejects([&]{prepareParticleCurve(invalid);});
    invalid=authored;invalid.keys.back().age=1.01f;rejects([&]{prepareParticleCurve(invalid);});
    invalid=authored;invalid.keys[1].value=std::numeric_limits<float>::infinity();rejects([&]{prepareParticleCurve(invalid);});
    invalid=authored;invalid.keys.resize(MaximumParticleAppearanceKeys+1);rejects([&]{prepareParticleCurve(invalid);});
    const ParticleScalarCurve roundedSpacing{ParticleInterpolation::Linear,{{1,0,0},{2,.5001f,1},{3,.5002f,2},{4,1,3}}};
    near(prepareParticleCurve(roundedSpacing).evaluate(.5002f),2,"Runtime preparation rejected adjacent keys accepted by float-spacing validation.");
}
void gradientsUseLinearLight() {
    ParticleColorGradient authored{ParticleInterpolation::Linear,{{1,0,{0,.5f,1,.2f}},{2,1,{1,.5f,0,.8f}}}};
    const auto gradient=prepareParticleGradient(authored);const auto middle=gradient.evaluate(.5);
    near(middle[0],.5,"Gradient interpolated sRGB instead of linear light.");near(middle[1],.5f,"Gradient decoded canonical linear color again.");
    near(middle[2],.5,"Gradient blue interpolation is incorrect.");near(middle[3],.5,"Gradient opacity did not interpolate linearly.");
    authored.stops[0].color={1,0,0,0};near(gradient.evaluate(0)[2],1,"Prepared gradient retained mutable source stops.");
    authored={ParticleInterpolation::Constant,{{1,0,{.5f,0,0,.4f}},{2,.5f,{0,1,0,.8f}},{3,1,{0,0,1,0}}}};
    const auto constant=prepareParticleGradient(authored);
    near(constant.evaluate(.49)[0],.5f,"Constant gradient unexpectedly blended stops.");
    near(constant.evaluate(.5)[1],1,"Constant gradient did not advance at the stop boundary.");
    near(constant.evaluate(1)[2],1,"Gradient endpoint did not select the final stop.");near(constant.evaluate(1)[3],0,"Gradient endpoint lost zero opacity.");
    auto invalid=authored;invalid.stops[1].color[0]=MaximumParticleColor+1;rejects([&]{prepareParticleGradient(invalid);});
    invalid=authored;invalid.stops[1].age=0;rejects([&]{prepareParticleGradient(invalid);});
}
void hdrRadianceAndLiveEdits() {
    // Stored HDR RGB is linear radiance, rather than extended sRGB. Different
    // endpoint powers must be applied before interpolation; alpha is separate.
    ParticleColorGradient source{ParticleInterpolation::Linear,{{11,.25f,{0,0,32,.2f}},{22,.75f,{0,2,0,.8f}}}};
    const auto prepared=prepareParticleGradient(source);
    near(prepared.evaluate(.5)[1],1,"HDR ramp interpolated hue without each stop's brightness.");
    near(prepared.evaluate(.5)[2],16,"HDR ramp clamped or decoded an overbright stop.");
    near(prepared.evaluate(.5)[3],.5,"HDR intensity changed linear opacity.");
    require(prepared.evaluate(0)==source.stops.front().color && prepared.evaluate(1)==source.stops.back().color,
        "Moved HDR ramp edges were clamped or extrapolated.");
    source.interpolation=ParticleInterpolation::Constant;const auto held=prepareParticleGradient(source);
    require(held.evaluate(.74999)==source.stops.front().color && held.evaluate(.75)==source.stops.back().color,
        "HDR constant ramp lost left-hold or exact stop boundaries.");
    near(particleSrgbToLinear(.5f),.214041140482,"Legacy sRGB migration reference changed.");
    near(particleSrgbToLinear(.04045f),.00313080495356,"Legacy transfer threshold changed.");

    auto effect=pulse();effect.emitter.color={.5f,4,32};effect.emitter.opacity=.3f;
    CpuParticleSystem system;system.restart(effect,781);for(int tick=0;tick<30;++tick)system.tick();system.pause();
    const LiveState live(system);const auto before=presentation(system);
    near(before.front().color[0],.5,"Canonical sub-white RGB was decoded twice.");
    near(before.front().color[1],4,"Constant HDR color was limited to display white.");
    effect.emitter.colorMode=ParticleColorMode::Gradient;effect.emitter.colorGradient=source;
    effect.emitter.intensityTrack.mode=ParticleAppearanceMode::Curve;effect.emitter.intensityTrack.curve=curve(2,4);
    system.updateDefinition(effect);live.unchanged(system);const auto after=presentation(system);
    near(after.front().color[2],96,"HDR gradient and compatibility intensity did not compose once.");
    near(after.front().color[3],.2,"Gradient did not replace constant opacity on a live HDR edit.");
    effect.emitter.colorMode=ParticleColorMode::Constant;effect.emitter.color={MaximumParticleColor,0,0};
    effect.emitter.intensityTrack.mode=ParticleAppearanceMode::Constant;effect.emitter.intensity=100000;
    system.updateDefinition(effect);live.unchanged(system);
    require(presentation(system).front().color[0]==MaximumParticleRadiance,
        "Supported HDR color/global intensity endpoints overflowed or were clamped.");
    auto invalid=effect;invalid.emitter.color[0]=MaximumParticleColor+1;
    rejects([&]{system.updateDefinition(invalid);});live.unchanged(system);
    require(system.definition()==effect,"Failed HDR edit replaced the live valid definition.");
}
void movableAppearanceEdges() {
    ParticleScalarCurve authored{ParticleInterpolation::Linear,{{3,.2f,3},{9,.5f,9},{12,.8f,1}}};
    const auto linear=prepareParticleCurve(authored);
    for(const auto age:{-2.0,0.0,.1,.2})near(linear.evaluate(age),3,"Moved first curve key extrapolated instead of holding its value.");
    near(linear.evaluate(.35),6,"Curve interpolation changed after moving the first key.");
    near(linear.evaluate(.65),5,"Curve interpolation changed after moving the last key.");
    for(const auto age:{.8, .9,1.0,2.0,std::numeric_limits<double>::infinity()})
        near(linear.evaluate(age),1,"Moved last curve key did not hold through particle death.");
    near(linear.maximum(),9,"Movable curve edge preparation lost an interior maximum.");
    authored.interpolation=ParticleInterpolation::Constant;const auto constant=prepareParticleCurve(authored);
    near(constant.evaluate(.49999),3,"Constant moved-edge curve blended before its next key.");
    near(constant.evaluate(.5),9,"Constant moved-edge curve missed its interior boundary.");
    near(constant.evaluate(.79999),9,"Constant curve did not retain its final interval.");
    near(constant.evaluate(.8f),1,"Constant curve did not advance at its moved final key.");
    near(constant.evaluate(1),1,"Constant curve lost its final held value.");
    auto invalid=authored;invalid.keys.front().age=-.001f;rejects([&]{prepareParticleCurve(invalid);});
    invalid=authored;invalid.keys.back().age=std::numeric_limits<float>::infinity();rejects([&]{prepareParticleCurve(invalid);});

    ParticleColorGradient colors{ParticleInterpolation::Linear,{{4,.2f,{.5f,0,0,.3f}},{8,.8f,{0,1,1,.7f}}}};
    const auto gradient=prepareParticleGradient(colors);const auto first=gradient.evaluate(0),last=gradient.evaluate(1),middle=gradient.evaluate(.5);
    near(first[0],.5f,"Gradient's held first linear color changed.");
    near(first[3],.3,"Moved first gradient stop did not hold opacity before its age.");
    near(last[0],0,"Gradient extrapolated its red component after the final stop.");near(last[1],1,"Moved last gradient stop lost its held color.");
    near(last[3],.7,"Moved last gradient stop did not hold opacity through age one.");
    near(middle[0],.5f*.5,"Moved-stop gradient did not interpolate linear RGB.");
    near(middle[1],.5,"Moved-stop gradient midpoint is incorrect.");near(middle[3],.5,"Moved-stop opacity interpolation is incorrect.");
    colors.interpolation=ParticleInterpolation::Constant;const auto step=prepareParticleGradient(colors);
    near(step.evaluate(.79999)[0],first[0],"Constant gradient blended before its moved final stop.");
    require(step.evaluate(.8f)==last && step.evaluate(1)==last,"Constant gradient did not include and hold its final stop.");

    auto effect=pulse();auto& e=effect.emitter;e.sizeTrack.mode=ParticleAppearanceMode::Curve;
    e.sizeTrack.curve={ParticleInterpolation::Linear,{{1,0,.1f},{2,1,.9f}}};
    CpuParticleSystem system;system.restart(effect);for(int tick=0;tick<30;++tick)system.tick();system.pause();const LiveState live(system);
    near(presentation(system).front().size,.5,"Moved-edge live-edit fixture sampled the wrong age.");
    e.sizeTrack.curve.keys.front().age=.6f;e.sizeTrack.curve.keys.back().age=.9f;
    e.rotationTrack.mode=ParticleAppearanceMode::Curve;e.rotationTrack.curve={ParticleInterpolation::Linear,{{5,.6f,45},{6,.9f,90}}};
    e.colorMode=ParticleColorMode::Gradient;e.colorGradient={ParticleInterpolation::Linear,{{7,.6f,{.5f,0,0,.3f}},{8,.9f,{0,1,0,.8f}}}};
    system.updateDefinition(effect);live.unchanged(system);const auto held=presentation(system).front();
    near(held.size,.1,"Moving the first key did not immediately hold its value on existing particles.");
    near(held.rotation,std::numbers::pi/4,"Moved rotation edge was not sampled immediately.");
    near(held.color[0],.5f,"Moved gradient edge did not immediately replace existing particle color.");
    near(held.color[3],.3,"Moved gradient edge did not immediately replace opacity.");
    near(system.maxPresentationSize(),.9,"Moved key intervals changed conservative appearance bounds.");
}
void constantCompatibilityAndLifetimeTracks() {
    auto effect=pulse();auto& e=effect.emitter;e.size=.12f;e.color={.5f,.04045f,1};e.intensity=3;e.opacity=.4f;e.rotation=-30;
    CpuParticleSystem system;system.restart(effect);system.tick();const auto original=presentation(system);
    require(original.size()==3,"Default constant appearance omitted valid particles.");
    for(const auto& packet:original) {
        near(packet.size,.12,"Constant size changed.");near(packet.color[0],.5f*3,"Constant linear radiance changed.");
        near(packet.color[1],.04045f*3,"Canonical linear color was decoded again.");near(packet.color[2],3,"Constant intensity was clamped or applied twice.");
        near(packet.color[3],.4,"Constant alpha was premultiplied into opacity.");nearAngle(packet.rotation,-std::numbers::pi/6,"Authored degrees were not converted to radians.");
    }
    e.sizeTrack.mode=ParticleAppearanceMode::Curve;e.sizeTrack.curve=curve(.1f,.3f);
    e.intensityTrack.mode=ParticleAppearanceMode::Curve;e.intensityTrack.curve=curve(2,0);
    e.rotationTrack.mode=ParticleAppearanceMode::Curve;e.rotationTrack.curve=curve(0,360);
    e.colorMode=ParticleColorMode::Gradient;e.colorGradient={ParticleInterpolation::Linear,{{1,0,{0,0,0,1}},{2,1,{1,1,1,0}}}};
    system.restart(effect);for(int tick=0;tick<30;++tick)system.tick();
    const auto half=presentation(system);require(half.size()==3,"Lifetime curves removed live halfway particles.");
    for(const auto& packet:half) {
        near(packet.size,.2,"Absolute size curve was multiplied by the retained constant.");
        near(packet.color[0],.5,"Gradient replacement or intensity curve did not produce halfway radiance.");
        near(packet.color[1],.5,"Gradient retained the unused effect tint.");near(packet.color[3],.5,"Gradient opacity retained the unused constant opacity.");
        nearAngle(packet.rotation,std::numbers::pi,"Rotation curve was integrated incrementally instead of sampled as absolute age.");
    }
    const auto interpolated=presentation(system,.5);const auto age=.5-ParticleFixedStep*.5;
    near(interpolated.front().size,.1+.2*age,"Appearance was evaluated at the current tick instead of interpolated particle age.");
    near(interpolated.front().position[1],age,"Appearance interpolation disagrees with positional interpolation.");
    nearAngle(interpolated.front().rotation,2*std::numbers::pi*age,"Rotation ignored presentation interpolation.");
    e.lifetimeRange={true,.6f,1.5f};system.restart(effect,33);for(int tick=0;tick<30;++tick)system.tick();
    const auto varied=presentation(system);
    for(size_t i=0;i<varied.size();++i) {
        const auto ageFraction=.5/system.particles()[i].lifetime;
        near(varied[i].size,.1+.2*ageFraction,"Lifetime variation did not normalize each particle independently.");
        nearAngle(varied[i].rotation,2*std::numbers::pi*ageFraction,"Rotation used emitter time instead of cached particle lifetime.");
    }
    e.delay=.5f;e.lifetimeRange.enabled=false;system.restart(effect);for(int tick=0;tick<36;++tick)system.tick();
    near(presentation(system).front().size,.12,"Delayed particles sampled appearance from total emitter elapsed time.");
    system.advance(ParticleFixedStep*.25);system.pause();const auto frozen=presentation(system,{});
    system.advance(100);const auto paused=presentation(system,{});require(frozen.size()==paused.size(),"Pause changed appearance sample count.");
    for(size_t i=0;i<frozen.size();++i)require(frozen[i].size==paused[i].size && frozen[i].color==paused[i].color && frozen[i].rotation==paused[i].rotation,
        "Pause advanced a curve or gradient clock.");
    e.delay=0;e.finiteDuration=false;e.bursts.clear();e.rate=90;system.restart(effect);system.tick();
    require(presentation(system,.5).empty(),"Presentation displayed a newborn before its within-tick birth time.");
    const auto newborn=presentation(system,.8);
    require(newborn.size()==1,"Fractional-birth appearance fixture did not present its eligible newborn.");
    near(newborn.front().size,.1+.2*(ParticleFixedStep*.8-1.0/90),"Newborn appearance incorrectly advanced for a full tick.");
    e.rotationTrack.mode=ParticleAppearanceMode::Constant;e.rotation=359999.9f;system.updateDefinition(effect);
    const auto manyTurns=presentation(system,1).front();
    require(std::abs(manyTurns.rotation)<=std::numbers::pi+1e-6,"Many-turn rotation was not bounded for shader precision.");
    nearAngle(manyTurns.rotation,static_cast<double>(e.rotation)*std::numbers::pi/180,"Rotation folding changed its authored orientation.");
}
void seededAppearanceAndLiveEdits() {
    auto effect=pulse(20);auto& e=effect.emitter;e.shape=ParticleShape::Sphere;e.lifetimeRange={true,.6f,1.5f};
    e.velocityMode=ParticleVelocityMode::DirectionSpeed;e.directionMode=ParticleDirectionMode::Outward;e.speedRange={true,1,3};
    CpuParticleSystem baseline,ranged;baseline.restart(effect,91);
    e.sizeTrack.mode=ParticleAppearanceMode::RandomRange;e.sizeTrack.range=ParticleAppearanceRange{.01f,1};
    e.intensityTrack.mode=ParticleAppearanceMode::RandomRange;e.intensityTrack.range=ParticleAppearanceRange{.1f,4};
    e.rotationTrack.mode=ParticleAppearanceMode::RandomRange;e.rotationTrack.range=ParticleAppearanceRange{-45,45};
    ranged.restart(effect,91);for(int tick=0;tick<20;++tick){baseline.tick();ranged.tick();}sameMotion(baseline,ranged);
    const auto before=presentation(ranged);
    for(size_t i=0;i<before.size();++i) {
        const auto ordinal=before[i].spawnOrdinal;
        near(before[i].size,particleAppearanceRange(*e.sizeTrack.range,91,e.id,ordinal,7),"Random size uses the wrong stable appearance lane.");
        near(before[i].color[0],particleAppearanceRange(*e.intensityTrack.range,91,e.id,ordinal,8),"Random intensity uses the wrong stable appearance lane.");
        near(before[i].rotation,particleAppearanceRange(*e.rotationTrack.range,91,e.id,ordinal,9)*std::numbers::pi/180,"Random rotation uses the wrong stable appearance lane.");
    }
    for(int tick=0;tick<10;++tick){baseline.tick();ranged.tick();}sameMotion(baseline,ranged);
    const auto later=presentation(ranged);
    require(before.size()==later.size(),"Stable random appearance fixture unexpectedly expired.");
    for(size_t i=0;i<before.size();++i)require(before[i].size==later[i].size && before[i].color==later[i].color && before[i].rotation==later[i].rotation,
        "Random appearance resampled while a particle aged.");
    ranged.pause();const LiveState live(ranged);
    e.sizeTrack.range=ParticleAppearanceRange{2,4};e.rotationTrack.mode=ParticleAppearanceMode::Constant;e.rotation=120;
    e.colorMode=ParticleColorMode::Gradient;e.colorGradient={ParticleInterpolation::Linear,{{1,0,{1,0,0,.8f}},{2,1,{1,0,0,.2f}}}};
    ranged.updateDefinition(effect);live.unchanged(ranged);const auto changed=presentation(ranged);
    for(size_t i=0;i<changed.size();++i) {
        const auto unit=(later[i].size-.01f)/(.99f);
        near(changed[i].size,2+unit*2,"Edited range did not reuse the particle's stable random fraction.");
        near(changed[i].rotation,2*std::numbers::pi/3,"Mode switching did not update paused existing particles.");
        require(changed[i].color[1]==0 && changed[i].color[2]==0,"Gradient edit retained constant color on existing particles.");
    }
    near(ranged.maxPresentationSize(),4,"Range maximum size was not prepared for conservative bounds.");
    e.sizeTrack.mode=ParticleAppearanceMode::Curve;e.sizeTrack.curve={ParticleInterpolation::Linear,{{1,0,0},{2,.25f,9},{3,1,0}}};
    ranged.updateDefinition(effect);live.unchanged(ranged);near(ranged.maxPresentationSize(),9,"Curve maximum size ignored an interior key.");
    e.sizeTrack.curve=curve(0,0);ranged.updateDefinition(effect);live.unchanged(ranged);
    require(presentation(ranged).empty() && ranged.stats().droppedSpawns==0,"Zero size was rendered or counted as simulation overflow.");
    near(ranged.maxPresentationSize(),0,"Zero curve did not prepare its selected size bound.");
    e.sizeTrack.mode=ParticleAppearanceMode::Constant;e.size=.2f;ranged.updateDefinition(effect);live.unchanged(ranged);const auto valid=presentation(ranged);
    near(ranged.maxPresentationSize(),.2,"Inactive range/curve values changed constant-mode bounds.");
    auto invalid=effect;invalid.emitter.colorGradient.stops[0].color[0]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{ranged.updateDefinition(invalid);});live.unchanged(ranged);const auto retained=presentation(ranged);
    require(retained.size()==valid.size() && retained.front().color==valid.front().color && retained.front().size==valid.front().size,
        "Failed appearance preparation discarded the working revision.");
}
void independentClocksAndCompaction() {
    auto effect=pulse();auto& e=effect.emitter;e.sizeTrack.mode=ParticleAppearanceMode::Curve;e.sizeTrack.curve=curve(.1f,.3f);
    CpuParticleSystem older,younger;older.restart(effect,8);younger.restart(effect,8);
    for(int tick=0;tick<30;++tick)older.tick();for(int tick=0;tick<10;++tick)younger.tick();
    require(presentation(older).front().size>presentation(younger).front().size,"Instances shared a mutable curve clock.");
    e.rotationTrack.mode=ParticleAppearanceMode::Curve;e.rotationTrack.curve=curve(0,180);const LiveState old(older),young(younger);
    older.updateDefinition(effect);younger.updateDefinition(effect);old.unchanged(older);young.unchanged(younger);
    require(presentation(older).front().rotation>presentation(younger).front().rotation,"Rotation edits coupled independent instance playback.");

    e.rate=120;e.finiteDuration=false;e.bursts.clear();e.lifetimeRange={true,.05f,1.5f};
    e.sizeTrack.mode=ParticleAppearanceMode::RandomRange;e.sizeTrack.range=ParticleAppearanceRange{.02f,.3f};
    older.restart(effect,123);for(int tick=0;tick<90;++tick)older.tick();
    require(!older.particles().empty() && older.particles().front().spawnOrdinal>0,"Compaction fixture did not remove old births.");
    for(const auto& packet:presentation(older))near(packet.size,particleAppearanceRange(*e.sizeTrack.range,123,e.id,packet.spawnOrdinal,7),
        "Compaction changed random appearance by indexing temporary storage order.");
}
}
int main() {
    try {
        pureCurveEvaluation();gradientsUseLinearLight();hdrRadianceAndLiveEdits();movableAppearanceEdges();constantCompatibilityAndLifetimeTracks();seededAppearanceAndLiveEdits();independentClocksAndCompaction();
        std::cout<<"Particle Step 4 immutable curve/gradient, presentation age, appearance RNG and live-edit checks passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
