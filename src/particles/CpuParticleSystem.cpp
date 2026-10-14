#include "CpuParticleSystem.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace crimson {
namespace {
void integrate(ParticleState& particle,const Vec3& acceleration,double drag,double step) {
    if (drag<=0) {
        particle.position=particle.position+particle.velocity*step+acceleration*(0.5*step*step);
        particle.velocity=particle.velocity+acceleration*step;
    } else {
        // Exact constant-force/exponential-drag solution. expm1 and the small
        // argument series keep tiny newborn intervals numerically stable.
        const auto k=drag*step;
        const auto velocityFactor=-std::expm1(-k)/drag;
        const auto accelerationFactor=k<1e-4 ? step*step*(.5-k/6+k*k/24) : (step-velocityFactor)/drag;
        particle.position=particle.position+particle.velocity*velocityFactor+acceleration*accelerationFactor;
        particle.velocity=particle.velocity*std::exp(-k)+acceleration*velocityFactor;
    }
    particle.age+=step;
}
uint64_t hash(uint64_t value) {
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;
    return value^(value>>31);
}
double random(uint64_t seed,uint64_t ordinal,uint64_t lane) {
    // A counter-based seed separates birth values from compaction, emitter
    // iteration and dropped particles. Fixed lanes make unrelated edits stable.
    const auto bits=hash(seed+0x9e3779b97f4a7c15ULL*(ordinal+1)+0xd1b54a32d192ed03ULL*(lane+1));
    return static_cast<double>(bits>>11)*0x1.0p-53;
}
Vec3 sampleUnit(uint64_t seed,uint64_t ordinal,uint64_t lane) {
    const auto y=1-2*random(seed,ordinal,lane);
    const auto radius=std::sqrt(std::max(0.0,1-y*y));
    const auto angle=2*std::numbers::pi*random(seed,ordinal,lane+1);
    return {radius*std::cos(angle),y,radius*std::sin(angle)};
}
Vec3 samplePosition(const ParticleEmitterDefinition& emitter,uint64_t seed,uint64_t ordinal) {
    switch (emitter.shape) {
    case ParticleShape::Cone: {
        const auto radius=emitter.radius*std::sqrt(random(seed,ordinal,0));
        const auto angle=2*std::numbers::pi*random(seed,ordinal,1);
        return {radius*std::cos(angle),0,radius*std::sin(angle)};
    }
    case ParticleShape::Sphere:
        return sampleUnit(seed,ordinal,0)*(emitter.radius*std::cbrt(random(seed,ordinal,2)));
    case ParticleShape::Box:
        return {(random(seed,ordinal,0)-.5)*emitter.boxSize[0],(random(seed,ordinal,1)-.5)*emitter.boxSize[1],
            (random(seed,ordinal,2)-.5)*emitter.boxSize[2]};
    default:return {};
    }
}
Vec3 scatterDirection(const Vec3& axis,const Vec3& tangent,const Vec3& bitangent,double spreadCosine,double flattenFactor,
                      uint64_t seed,uint64_t ordinal) {
    const auto cosine=1-random(seed,ordinal,3)*(1-spreadCosine);
    const auto radial=std::sqrt(std::max(0.0,1-cosine*cosine));
    const auto angle=2*std::numbers::pi*random(seed,ordinal,4);
    if (flattenFactor==1) {
        // Preserve the exact previous arithmetic for legacy flatness zero.
        return axis*cosine+tangent*(radial*std::cos(angle))+bitangent*(radial*std::sin(angle));
    }
    // Flatten only the second perpendicular axis, then restore unit direction
    // so flatness changes the fan without changing sampled speed.
    auto direction=math::normalized(axis*cosine+tangent*(radial*std::cos(angle))+
        bitangent*(radial*std::sin(angle)*flattenFactor),axis);
    const auto axisDot=math::dot(direction,axis);
    // For cones wider than a hemisphere, normalization can move a backward
    // sample outside its original angular bound. Keep it on that cone's edge.
    if (axisDot<spreadCosine) {
        const auto perpendicular=math::normalized(direction-axis*axisDot,tangent);
        direction=math::normalized(axis*spreadCosine+perpendicular*std::sqrt(std::max(0.0,1-spreadCosine*spreadCosine)),axis);
    }
    return direction;
}
Vec3 sampleVelocity(const ParticleEmitterDefinition& emitter,const Vec3& position,uint64_t seed,uint64_t ordinal,
                    const Vec3& axis,const Vec3& tangent,const Vec3& bitangent,double spreadCosine,double flattenFactor,double speedMinimum,double speedSpan) {
    if (emitter.velocityMode==ParticleVelocityMode::Vector) return emitter.initialVelocity;
    auto direction=axis;
    if (emitter.directionMode==ParticleDirectionMode::Outward) {
        direction=math::normalized(position,axis);
        // Zero spread retains the original radial arithmetic and speed. With
        // spread, the basis follows each particle's sampled radial direction.
        if (spreadCosine<1) {
            const auto radialTangent=math::normalized(math::cross(direction,std::abs(direction[1])<.99f?Vec3{0,1,0}:Vec3{1,0,0}));
            direction=scatterDirection(direction,radialTangent,math::cross(direction,radialTangent),spreadCosine,flattenFactor,seed,ordinal);
        }
    } else if (emitter.directionMode==ParticleDirectionMode::RandomCone)
        direction=scatterDirection(axis,tangent,bitangent,spreadCosine,flattenFactor,seed,ordinal);
    const auto speed=speedMinimum+speedSpan*random(seed,ordinal,5);
    return direction*speed;
}
std::vector<size_t> prepareBurstOrder(const ParticleEmitterDefinition& emitter) {
    std::vector<size_t> result(emitter.bursts.size());std::iota(result.begin(),result.end(),0);
    std::stable_sort(result.begin(),result.end(),[&](size_t a,size_t b){return emitter.bursts[a].time<emitter.bursts[b].time;});
    return result;
}
uint64_t scheduledCount(double amount) { return static_cast<uint64_t>(std::max(0.0,std::floor(amount+1e-12))); }
}

CpuParticleSystem::CpuParticleSystem(ParticleSimulationLimits limits):limits_(limits) {
    if (!limits.maximumCatchUpTicks || limits.maximumCatchUpTicks>256 || !limits.maximumSpawnPerTick || limits.maximumSpawnPerTick>MaximumParticleCapacity ||
        !limits.maximumScheduleSegmentsPerTick || limits.maximumScheduleSegmentsPerTick>256)
        throw std::runtime_error("Particle simulation work limits must be nonzero and bounded.");
}
void CpuParticleSystem::restart(const ParticleEffectAsset& definition,uint64_t seed,const Mat4& world) {
    restartInternal(definition,seed,world,true);
}
void CpuParticleSystem::restartInternal(const ParticleEffectAsset& definition,uint64_t seed,const Mat4& world,bool preprocess) {
    validateParticleAsset(definition);
    if (!math::finite(world) || !math::isAffine(world)) throw std::runtime_error("Particle emitter requires a finite affine world matrix.");
    auto prepared=definition;
    auto preparedBursts=prepareBurstOrder(prepared.emitter);
    auto preparedAppearance=prepareParticleAppearance(prepared.emitter);
    std::vector<ParticleState> preparedParticles;
    preparedParticles.reserve(prepared.emitter.capacity);
    definition_=std::move(prepared); world_=world; seed_=seed;
    burstOrder_=std::move(preparedBursts);
    appearance_=std::move(preparedAppearance);appearanceSeed_=particleAppearanceSeed(seed_,definition_.emitter.id);
    particles_.swap(preparedParticles); stats_={}; nextSpawnOrdinal_=0;
    elapsed_=remainder_=emissionRemainder_=0;
    // Decimal UI values such as 0.1 or 4.05 seconds can land a float ULP above
    // an exact tick. Remove only that representational noise before ceiling.
    const auto rawTicks=preprocess?static_cast<double>(definition_.preprocessSeconds)*60:0;
    const auto nearestTick=std::round(rawTicks);
    const auto tolerance=2*std::numeric_limits<float>::epsilon()*std::max(1.0,rawTicks);
    const auto ticks=std::abs(rawTicks-nearestTick)<=tolerance ? nearestTick:rawTicks;
    const auto totalTicks=preprocess && definition_.preprocessSeconds>0 ? std::max(1u,static_cast<uint32_t>(std::ceil(ticks))) : 0;
    presentCurrentTick_=false;
    state_=ParticlePlaybackState::Running;
    // Like Godot CPU particles, pre-process finishes before rendering. This is
    // ordinary deterministic simulation at the restart transform, not wall-time
    // catch-up; configured duration and the usual per-tick limits bound work.
    for(uint32_t tick=0;tick<totalTicks && !completed();++tick)simulateTick();
    if(totalTicks)presentCurrentTick_=true;
}
void CpuParticleSystem::updateDefinition(const ParticleEffectAsset& definition) {
    validateParticleAsset(definition);
    if (definition.id!=definition_.id || definition.emitter.id!=definition_.emitter.id)
        throw std::runtime_error("Changing particle asset or emitter identity requires an explicit instance restart.");
    // Prepare every allocation before changing the published settings or live
    // state. Ordinary setters never touch particle records or playback clocks.
    auto prepared=definition;
    auto preparedAppearance=prepareParticleAppearance(prepared.emitter);
    const bool capacityChanged=prepared.emitter.capacity!=definition_.emitter.capacity;
    const bool seedChanged=prepared.defaultSeed!=definition_.defaultSeed;
    const bool preprocessChanged=prepared.preprocessSeconds!=definition_.preprocessSeconds;
    const auto& old=definition_.emitter;const auto& next=prepared.emitter;
    const bool scheduleChanged=old.space!=next.space || old.delay!=next.delay || old.finiteDuration!=next.finiteDuration ||
        old.duration!=next.duration || old.loop!=next.loop || old.bursts!=next.bursts;
    if (seedChanged || scheduleChanged || preprocessChanged) {
        const auto oldState=state_;
        restartInternal(prepared,seedChanged?prepared.defaultSeed:seed_,world_,oldState!=ParticlePlaybackState::Stopped);
        state_=oldState;
        if(oldState==ParticlePlaybackState::Stopped)stop();
        return;
    }
    std::vector<ParticleState> replacement;
    if (capacityChanged) replacement.reserve(prepared.emitter.capacity);
    definition_=std::move(prepared);
    appearance_=std::move(preparedAppearance);
    if (capacityChanged) {
        stats_.clearedCapacityParticles+=particles_.size();
        particles_.swap(replacement);
        stats_.live=0;
    }
}
void CpuParticleSystem::play() {
    if (state_==ParticlePlaybackState::Stopped || completed()) { restart(definition_,seed_,world_); return; }
    if (state_==ParticlePlaybackState::Paused) state_=ParticlePlaybackState::Running;
}
void CpuParticleSystem::pause() {
    if (state_==ParticlePlaybackState::Running) state_=ParticlePlaybackState::Paused;
}
void CpuParticleSystem::stop() {
    particles_.clear(); elapsed_=remainder_=emissionRemainder_=0;
    nextSpawnOrdinal_=0; stats_={}; state_=ParticlePlaybackState::Stopped;
    presentCurrentTick_=false;
}
bool CpuParticleSystem::setTransform(const Mat4& world) {
    if (!math::finite(world) || !math::isAffine(world)) return false;
    world_=world;
    return true;
}
bool CpuParticleSystem::emissionFinished() const {
    const auto& emitter=definition_.emitter;
    return state_!=ParticlePlaybackState::Stopped && emitter.finiteDuration && !emitter.loop &&
        elapsed_+1e-12>=static_cast<double>(emitter.delay)+emitter.duration;
}
Vec3 CpuParticleSystem::particleWorldPosition(const ParticleState& particle,const Mat4* presentationWorld) const {
    return definition_.emitter.space==ParticleSpace::Local ? math::transformPoint(presentationWorld?*presentationWorld:world_,particle.position) : particle.position;
}
void CpuParticleSystem::advance(double wallSeconds) {
    if (state_!=ParticlePlaybackState::Running || completed() || !std::isfinite(wallSeconds) || wallSeconds<=0) return;
    // A pathological timer value must neither overflow tick arithmetic nor
    // request unbounded work. Effect time advances only for executed ticks.
    const auto accepted=std::min(wallSeconds,3600.0);
    const auto accumulated=remainder_+accepted;
    const auto available=static_cast<uint64_t>(std::floor((accumulated+1e-12)/ParticleFixedStep));
    const auto executed=std::min<uint64_t>(available,limits_.maximumCatchUpTicks);
    uint64_t actual{};
    for (;actual<executed && !completed();++actual) {presentCurrentTick_=false;simulateTick();}
    // Time after natural completion is idle, not an overloaded simulation
    // backlog. Preserve earlier counters but do not invent discarded work.
    if (completed()) { remainder_=0;return; }
    const auto dropped=available-actual;
    stats_.droppedCatchUpTicks+=dropped;
    stats_.droppedWallTime=std::min(std::numeric_limits<double>::max(),stats_.droppedWallTime+(wallSeconds-accepted)+dropped*ParticleFixedStep);
    remainder_=std::clamp(accumulated-available*ParticleFixedStep,0.0,ParticleFixedStep);
}
void CpuParticleSystem::tick() {
    if (state_==ParticlePlaybackState::Running && !completed()) {
        presentCurrentTick_=false;simulateTick();remainder_=0;
    }
}
void CpuParticleSystem::emitBatch(uint64_t count,double offset,double spacing) {
    const auto& emitter=definition_.emitter;
    const bool local=emitter.space==ParticleSpace::Local;
    const auto firstOrdinal=nextSpawnOrdinal_;
    nextSpawnOrdinal_+=count; // Dropped births never become a future backlog.
    const auto allowed=std::min<uint64_t>(count,limits_.maximumSpawnPerTick-spawnedThisTick_);
    spawnedThisTick_+=static_cast<uint32_t>(allowed);
    stats_.droppedSpawns+=count-allowed;
    for (uint64_t i=0;i<allowed;++i) {
        if (particles_.size()>=emitter.capacity) { stats_.droppedSpawns+=allowed-i;break; }
        ParticleState particle;
        particle.spawnOrdinal=firstOrdinal+i;
        const auto localPosition=samplePosition(emitter,spawn_.seed,particle.spawnOrdinal);
        const auto localVelocity=sampleVelocity(emitter,localPosition,spawn_.seed,particle.spawnOrdinal,
            spawn_.direction,spawn_.tangent,spawn_.bitangent,spawn_.spreadCosine,spawn_.flattenFactor,spawn_.speedMinimum,spawn_.speedSpan);
        particle.position=particle.previousPosition=local ? localPosition : math::transformPoint(world_,localPosition);
        particle.velocity=local ? localVelocity : math::transformDirection(world_,localVelocity);
        particle.lifetime=spawn_.lifetimeMinimum+spawn_.lifetimeSpan*random(spawn_.seed,particle.spawnOrdinal,6);
        const auto birthOffset=std::clamp(offset+spacing*static_cast<double>(i),0.0,ParticleFixedStep);
        particle.birthTime=elapsed_+birthOffset;
        integrate(particle,acceleration_,drag_,ParticleFixedStep-birthOffset);
        if (particle.age+1e-12>=particle.lifetime) continue;
        if (!math::finite(particle.position) || !math::finite(particle.velocity)) { ++stats_.droppedSpawns;continue; }
        particles_.push_back(particle);++stats_.spawned;
    }
}
void CpuParticleSystem::scheduleSegment(double begin,double end,double cycleStart) {
    const auto& emitter=definition_.emitter;
    const double rate=emitter.rate;
    const auto previousRemainder=emissionRemainder_;
    const auto amount=previousRemainder+rate*(end-begin);
    const auto scheduled=scheduledCount(amount);
    emissionRemainder_=std::max(0.0,amount-static_cast<double>(scheduled));
    uint64_t emittedRate{};
    const auto rateOffset=rate>0 ? begin-elapsed_+(1-previousRemainder)/rate : 0;
    for (const auto index:burstOrder_) {
        const auto& burst=emitter.bursts[index];
        const auto time=cycleStart+burst.time;
        // Burst intervals are half-open: a boundary at tick/loop end belongs
        // to the next interval. A time-zero burst belongs to the first tick.
        if (time+1e-12<begin || time>=end-1e-12) continue;
        if (rate>0) {
            const auto prefix=static_cast<uint64_t>(std::clamp(std::ceil(previousRemainder+rate*(time-begin)-1e-10)-1,0.0,
                static_cast<double>(scheduled)));
            if (prefix>emittedRate) {
                emitBatch(prefix-emittedRate,rateOffset+static_cast<double>(emittedRate)/rate,1/rate);
                emittedRate=prefix;
            }
        }
        emitBatch(burst.count,time-elapsed_,0);++stats_.burstsFired;
    }
    if (scheduled>emittedRate) emitBatch(scheduled-emittedRate,rateOffset+static_cast<double>(emittedRate)/rate,1/rate);
}
void CpuParticleSystem::dropSchedule(double begin,double end) {
    const auto& emitter=definition_.emitter;
    const auto amount=emissionRemainder_+emitter.rate*(end-begin);
    auto dropped=scheduledCount(amount);
    emissionRemainder_=std::max(0.0,amount-static_cast<double>(dropped));
    if (emitter.finiteDuration && emitter.loop) {
        const double duration=emitter.duration;
        const auto firstCycle=std::floor((begin-emitter.delay)/duration+1e-10);
        const auto lastCycle=std::ceil((end-emitter.delay)/duration-1e-10)-1;
        stats_.droppedScheduleSegments+=static_cast<uint64_t>(std::max(0.0,lastCycle-firstCycle+1));
        for (const auto& burst:emitter.bursts) {
            const auto origin=static_cast<double>(emitter.delay)+burst.time;
            const auto first=std::max(0.0,std::ceil((begin-origin)/duration-1e-10));
            const auto last=std::ceil((end-origin)/duration-1e-10)-1;
            const auto crossings=static_cast<uint64_t>(std::max(0.0,last-first+1));
            dropped+=crossings*burst.count;
        }
    } else {
        ++stats_.droppedScheduleSegments;
        for (const auto& burst:emitter.bursts) {
            const auto time=static_cast<double>(emitter.delay)+burst.time;
            if (time+1e-12>=begin && time<end-1e-12) dropped+=burst.count;
        }
    }
    nextSpawnOrdinal_+=dropped;stats_.droppedSpawns+=dropped;
}
void CpuParticleSystem::simulateTick() {
    const auto& emitter=definition_.emitter;
    // Prepare the fixed behavior stages once per emitter tick. The particle
    // loop consumes only the resolved force and drag, never module definitions.
    acceleration_={};
    if (emitter.gravityBehavior.present && emitter.gravityBehavior.enabled) acceleration_=emitter.gravity;
    if (emitter.accelerationBehavior.present && emitter.accelerationBehavior.enabled) acceleration_=acceleration_+emitter.acceleration;
    drag_=emitter.dragBehavior.present && emitter.dragBehavior.enabled ? emitter.drag : 0;
    spawn_.seed=hash(seed_^hash(emitter.id));
    spawn_.direction=math::normalized(emitter.direction);
    spawn_.tangent=math::normalized(math::cross(spawn_.direction,std::abs(spawn_.direction[1])<.99f?Vec3{0,1,0}:Vec3{1,0,0}));
    spawn_.bitangent=math::cross(spawn_.direction,spawn_.tangent);
    spawn_.spreadCosine=std::cos(emitter.directionSpread*std::numbers::pi/180);
    spawn_.flattenFactor=1-static_cast<double>(emitter.directionFlatness);
    spawn_.lifetimeMinimum=emitter.lifetimeRange.enabled ? emitter.lifetimeRange.minimum : emitter.lifetime;
    spawn_.lifetimeSpan=emitter.lifetimeRange.enabled ? static_cast<double>(emitter.lifetimeRange.maximum)-emitter.lifetimeRange.minimum : 0;
    spawn_.speedMinimum=emitter.speedRange.enabled ? emitter.speedRange.minimum : emitter.speed;
    spawn_.speedSpan=emitter.speedRange.enabled ? static_cast<double>(emitter.speedRange.maximum)-emitter.speedRange.minimum : 0;
    spawnedThisTick_=0;
    for (size_t i=0;i<particles_.size();) {
        auto& particle=particles_[i];
        particle.previousPosition=particle.position;
        integrate(particle,acceleration_,drag_,ParticleFixedStep);
        if (particle.age+1e-12>=particle.lifetime || !math::finite(particle.position) || !math::finite(particle.velocity)) {
            particle=particles_.back(); particles_.pop_back();
        } else ++i;
    }
    if (emitter.enabled) {
        const auto end=elapsed_+ParticleFixedStep;
        auto begin=std::max(elapsed_,static_cast<double>(emitter.delay));
        const auto emissionEnd=emitter.finiteDuration && !emitter.loop ? std::min(end,static_cast<double>(emitter.delay)+emitter.duration) : end;
        uint32_t segments{};
        while (begin<emissionEnd-1e-12) {
            if (segments++>=limits_.maximumScheduleSegmentsPerTick) { dropSchedule(begin,emissionEnd);break; }
            double cycleStart=emitter.delay,segmentEnd=emissionEnd;
            if (emitter.finiteDuration && emitter.loop) {
                const auto cycle=std::floor((begin-emitter.delay)/emitter.duration+1e-10);
                cycleStart=static_cast<double>(emitter.delay)+cycle*emitter.duration;
                segmentEnd=std::min(segmentEnd,cycleStart+emitter.duration);
            }
            scheduleSegment(begin,segmentEnd,cycleStart);
            begin=segmentEnd;
        }
    }
    elapsed_+=ParticleFixedStep; ++stats_.ticks; stats_.live=particles_.size();
}
void CpuParticleSystem::appendPresentation(std::vector<ParticlePresentation>& out,std::optional<double> interpolationOverride,const Mat4* presentationWorld) const {
    const auto& emitter=definition_.emitter;
    const bool interpolate=state_!=ParticlePlaybackState::Stopped && !presentCurrentTick_;
    const auto alpha=std::clamp(interpolationOverride.value_or(interpolation()),0.0,1.0);
    const auto displayTime=interpolate?std::max(0.0,elapsed_-ParticleFixedStep+alpha*ParticleFixedStep):elapsed_;
    for (const auto& particle:particles_) {
        if (particle.birthTime>displayTime+1e-12) continue;
        double factor=alpha;
        if (!interpolate) factor=1;
        else if (particle.birthTime>elapsed_-ParticleFixedStep) {
            const auto duration=elapsed_-particle.birthTime;
            factor=duration>1e-12?std::clamp((displayTime-particle.birthTime)/duration,0.0,1.0):1;
        }
        auto position=particle.previousPosition+(particle.position-particle.previousPosition)*factor;
        if (emitter.space==ParticleSpace::Local) position=math::transformPoint(presentationWorld?*presentationWorld:world_,position);
        if (!math::finite(position)) continue;
        const auto normalizedAge=std::clamp((displayTime-particle.birthTime)/particle.lifetime,0.0,1.0);
        const auto appearance=appearance_.sample(normalizedAge,appearanceSeed_,particle.spawnOrdinal);
        if(appearance.size<=0)continue; // Authored disappearance is not a simulation overflow.
        out.push_back({position,appearance.size,appearance.color,particle.spawnOrdinal,appearance.rotation});
    }
}

} // namespace crimson
