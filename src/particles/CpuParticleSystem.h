#pragma once

#include "assets/ParticleAsset.h"
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
    // cached by live particles. Capacity changes clear only the live storage.
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
    // Append presentation packets without disturbing authoritative particle order.
    void appendPresentation(std::vector<ParticlePresentation>& out,std::optional<double> interpolationOverride = {}) const;
private:
    ParticleEffectAsset definition_;
    ParticleSimulationLimits limits_;
    Mat4 world_;
    std::vector<ParticleState> particles_;
    ParticleStats stats_;
    ParticlePlaybackState state_{ParticlePlaybackState::Stopped};
    uint64_t seed_{1}, nextSpawnOrdinal_{};
    double elapsed_{}, remainder_{}, emissionRemainder_{};
    void simulateTick();
};

// Named color boundary: authored picker values are sRGB, emitted radiance linear.
float particleSrgbToLinear(float value);

} // namespace crimson
