#include "CpuParticleSystem.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace crimson {
namespace {
void integrate(ParticleState& particle,const Vec3& gravity,double step) {
    particle.position=particle.position+particle.velocity*step+gravity*(0.5*step*step);
    particle.velocity=particle.velocity+gravity*step;
    particle.age+=step;
}
}

float particleSrgbToLinear(float value) {
    return value<=0.04045f ? value/12.92f : std::pow((value+0.055f)/1.055f,2.4f);
}
CpuParticleSystem::CpuParticleSystem(ParticleSimulationLimits limits):limits_(limits) {
    if (!limits.maximumCatchUpTicks || limits.maximumCatchUpTicks>256 || !limits.maximumSpawnPerTick || limits.maximumSpawnPerTick>MaximumParticleCapacity)
        throw std::runtime_error("Particle simulation work limits must be nonzero and bounded.");
}
void CpuParticleSystem::restart(const ParticleEffectAsset& definition,uint64_t seed,const Mat4& world) {
    validateParticleAsset(definition);
    if (!math::finite(world) || !math::isAffine(world)) throw std::runtime_error("Particle emitter requires a finite affine world matrix.");
    auto prepared=definition;
    std::vector<ParticleState> preparedParticles;
    preparedParticles.reserve(prepared.emitter.capacity);
    definition_=std::move(prepared); world_=world; seed_=seed;
    particles_.swap(preparedParticles); stats_={}; nextSpawnOrdinal_=0;
    elapsed_=remainder_=emissionRemainder_=0;
    state_=ParticlePlaybackState::Running;
}
void CpuParticleSystem::updateDefinition(const ParticleEffectAsset& definition) {
    validateParticleAsset(definition);
    if (definition.id!=definition_.id || definition.emitter.id!=definition_.emitter.id)
        throw std::runtime_error("Changing particle asset or emitter identity requires an explicit instance restart.");
    // Prepare every allocation before changing the published settings or live
    // state. Ordinary setters never touch particle records or playback clocks.
    auto prepared=definition;
    const bool capacityChanged=prepared.emitter.capacity!=definition_.emitter.capacity;
    const bool seedChanged=prepared.defaultSeed!=definition_.defaultSeed;
    std::vector<ParticleState> replacement;
    if (capacityChanged) replacement.reserve(prepared.emitter.capacity);
    if (seedChanged) seed_=prepared.defaultSeed;
    definition_=std::move(prepared);
    if (capacityChanged) {
        stats_.clearedCapacityParticles+=particles_.size();
        particles_.swap(replacement);
        stats_.live=0;
    }
}
void CpuParticleSystem::play() {
    if (state_==ParticlePlaybackState::Stopped) { restart(definition_,seed_,world_); return; }
    if (state_==ParticlePlaybackState::Paused) state_=ParticlePlaybackState::Running;
}
void CpuParticleSystem::pause() {
    if (state_==ParticlePlaybackState::Running) state_=ParticlePlaybackState::Paused;
}
void CpuParticleSystem::stop() {
    particles_.clear(); elapsed_=remainder_=emissionRemainder_=0;
    nextSpawnOrdinal_=0; stats_={}; state_=ParticlePlaybackState::Stopped;
}
bool CpuParticleSystem::setTransform(const Mat4& world) {
    if (!math::finite(world) || !math::isAffine(world)) return false;
    world_=world;
    return true;
}
void CpuParticleSystem::advance(double wallSeconds) {
    if (state_!=ParticlePlaybackState::Running || !std::isfinite(wallSeconds) || wallSeconds<=0) return;
    // A pathological timer value must neither overflow tick arithmetic nor
    // request unbounded work. Effect time advances only for executed ticks.
    const auto accepted=std::min(wallSeconds,3600.0);
    stats_.droppedWallTime=std::min(std::numeric_limits<double>::max(),stats_.droppedWallTime+(wallSeconds-accepted));
    const auto accumulated=remainder_+accepted;
    const auto available=static_cast<uint64_t>(std::floor((accumulated+1e-12)/ParticleFixedStep));
    const auto executed=std::min<uint64_t>(available,limits_.maximumCatchUpTicks);
    for (uint64_t i=0;i<executed;++i) simulateTick();
    const auto dropped=available-executed;
    stats_.droppedCatchUpTicks+=dropped;
    stats_.droppedWallTime=std::min(std::numeric_limits<double>::max(),stats_.droppedWallTime+dropped*ParticleFixedStep);
    remainder_=std::clamp(accumulated-available*ParticleFixedStep,0.0,ParticleFixedStep);
}
void CpuParticleSystem::tick() {
    if (state_==ParticlePlaybackState::Running) { simulateTick(); remainder_=0; }
}
void CpuParticleSystem::simulateTick() {
    const auto& emitter=definition_.emitter;
    for (size_t i=0;i<particles_.size();) {
        auto& particle=particles_[i];
        particle.previousPosition=particle.position;
        integrate(particle,emitter.gravity,ParticleFixedStep);
        if (particle.age+1e-12>=particle.lifetime || !math::finite(particle.position) || !math::finite(particle.velocity)) {
            particle=particles_.back(); particles_.pop_back();
        } else ++i;
    }
    if (emitter.enabled && emitter.rate>0) {
        const double rate=emitter.rate;
        const auto previousRemainder=emissionRemainder_;
        const auto amount=previousRemainder+rate*ParticleFixedStep;
        const auto scheduled=static_cast<uint64_t>(std::floor(amount+1e-12));
        emissionRemainder_=std::max(0.0,amount-static_cast<double>(scheduled));
        const auto allowed=std::min<uint64_t>(scheduled,limits_.maximumSpawnPerTick);
        const auto firstOrdinal=nextSpawnOrdinal_;
        nextSpawnOrdinal_+=scheduled; // Dropped births never become a future backlog.
        for (uint64_t i=0;i<allowed;++i) {
            const auto birthOffset=std::clamp((1.0-previousRemainder+static_cast<double>(i))/rate,0.0,ParticleFixedStep);
            const auto aliveTime=ParticleFixedStep-birthOffset;
            if (particles_.size()>=emitter.capacity) { ++stats_.droppedSpawns; continue; }
            ParticleState particle;
            particle.spawnOrdinal=firstOrdinal+i;
            particle.position=particle.previousPosition=math::transformPoint(world_,{0,0,0});
            particle.velocity=math::transformDirection(world_,emitter.initialVelocity);
            particle.lifetime=emitter.lifetime; particle.birthTime=elapsed_+birthOffset;
            integrate(particle,emitter.gravity,aliveTime);
            if (particle.age+1e-12>=particle.lifetime) continue;
            if (!math::finite(particle.position) || !math::finite(particle.velocity)) { ++stats_.droppedSpawns; continue; }
            particles_.push_back(particle); ++stats_.spawned;
        }
        stats_.droppedSpawns+=scheduled-allowed;
    }
    elapsed_+=ParticleFixedStep; ++stats_.ticks; stats_.live=particles_.size();
}
void CpuParticleSystem::appendPresentation(std::vector<ParticlePresentation>& out,std::optional<double> interpolationOverride) const {
    const auto& emitter=definition_.emitter;
    const Vec4 radiance{particleSrgbToLinear(emitter.color[0])*emitter.intensity,
        particleSrgbToLinear(emitter.color[1])*emitter.intensity,particleSrgbToLinear(emitter.color[2])*emitter.intensity,emitter.opacity};
    const bool interpolate=state_!=ParticlePlaybackState::Stopped;
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
        const auto position=particle.previousPosition+(particle.position-particle.previousPosition)*factor;
        out.push_back({position,emitter.size,radiance,particle.spawnOrdinal});
    }
}

} // namespace crimson
