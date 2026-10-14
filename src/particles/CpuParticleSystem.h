#pragma once

#include "assets/ParticleAsset.h"
#include "ParticleAppearance.h"
#include <optional>
#include <span>
#include <vector>

namespace crimson {

// CPU-only instance. Scene/document IDs live in its concrete session owner.
// It never consumes camera state and owns no ECS, ImGui or GPU resources.
class CpuParticleSystem {
public:
    explicit CpuParticleSystem(ParticleSimulationLimits limits = {});
    void restart(const ParticleEffectAsset& definition, uint64_t seed = 1, const Mat4& world = {});
    // Publish same-identity settings without restarting playback. Appearance is
    // immediate, forces/emission use the next tick, and birth values remain
    // cached by live particles. Capacity changes clear only the live storage;
    // seed, timing, burst and simulation-space edits restart the same instance.
    void updateDefinition(const ParticleEffectAsset& definition);
    void play();
    void pause();
    void stop();
    bool setTransform(const Mat4& world);
    void advance(double wallSeconds);
    void tick(); // One exact fixed tick; usable by focused CPU tests.
    ParticlePlaybackState state() const { return state_; }
    double elapsed() const { return elapsed_; }
    double interpolation() const { return remainder_ / ParticleFixedStep; }
    uint64_t seed() const { return seed_; }
    const ParticleStats& stats() const { return stats_; }
    std::span<const ParticleState> particles() const { return particles_; }
    size_t reservedCapacity() const { return particles_.capacity(); }
    const ParticleEffectAsset& definition() const { return definition_; }
    float maxPresentationSize() const { return appearance_.maximumSize(); }
    bool emissionFinished() const;
    bool completed() const { return emissionFinished() && particles_.empty(); }
    bool draining() const { return emissionFinished() && !particles_.empty(); }
    // Presentation transforms are copied/validated by the scene owner. They
    // affect only local-space extraction, never birth or authoritative motion.
    Vec3 particleWorldPosition(const ParticleState& particle,const Mat4* presentationWorld = nullptr) const;
    // Append presentation packets without disturbing authoritative particle order.
    void appendPresentation(std::vector<ParticlePresentation>& out,std::optional<double> interpolationOverride = {},
                            const Mat4* presentationWorld = nullptr) const;
private:
    ParticleEffectAsset definition_;
    ParticleSimulationLimits limits_;
    Mat4 world_;
    std::vector<ParticleState> particles_;
    ParticleStats stats_;
    PreparedParticleAppearance appearance_;
    uint64_t appearanceSeed_{};
    ParticlePlaybackState state_{ParticlePlaybackState::Stopped};
    uint64_t seed_{1}, nextSpawnOrdinal_{};
    double elapsed_{}, remainder_{}, emissionRemainder_{};
    bool presentCurrentTick_{}; // Show the warmed final state until an ordinary tick advances it.
    std::vector<size_t> burstOrder_; // Authored identities remain stable; only this execution index is sorted.
    struct PreparedSpawn {
        uint64_t seed{};
        Vec3 direction{},tangent{},bitangent{};
        double spreadCosine{},flattenFactor{1},lifetimeMinimum{},lifetimeSpan{},speedMinimum{},speedSpan{};
    } spawn_;
    Vec3 acceleration_{};
    double drag_{};
    uint32_t spawnedThisTick_{};
    void restartInternal(const ParticleEffectAsset&,uint64_t,const Mat4&,bool preprocess);
    void simulateTick();
    void emitBatch(uint64_t count,double offset,double spacing);
    void scheduleSegment(double begin,double end,double cycleStart);
    void dropSchedule(double begin,double end);
};

} // namespace crimson
