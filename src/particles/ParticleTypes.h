#pragma once

#include "math/Math.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace crimson {

// Step 1 intentionally supports one point emitter with constant values.
// Its persistent identity is independent of particle storage order.
struct ParticleEmitterDefinition {
    uint64_t id{1};
    std::string name{"Emitter 1"};
    bool enabled{true};
    float rate{30.0f};
    float lifetime{1.25f};
    Vec3 initialVelocity{0,0,0}; // Launch speed at birth; gravity accelerates afterward.
    Vec3 gravity{0,-9.81f,0}; // World-space acceleration, in m/s^2.
    float size{0.08f}; // Full billboard width/height in world meters.
    Vec3 color{1,1,1}; // Authored sRGB picker values; presentation converts to linear.
    float intensity{1.0f};
    uint32_t capacity{256};
    bool operator==(const ParticleEmitterDefinition&) const = default;
};

struct ParticleState {
    uint64_t spawnOrdinal{};
    Vec3 previousPosition{}, position{}, velocity{};
    double age{}, lifetime{}, birthTime{};
};

struct ParticlePresentation {
    Vec3 position{};
    float size{0.08f};
    Vec4 color{1,1,1,1};
    uint64_t spawnOrdinal{};
};

enum class ParticlePlaybackState { Stopped, Running, Paused };
struct ParticleSimulationLimits {
    uint32_t maximumCatchUpTicks{8};
    uint32_t maximumSpawnPerTick{4096};
};
struct ParticleStats {
    size_t live{};
    uint64_t ticks{}, spawned{}, droppedSpawns{}, droppedCatchUpTicks{}, clearedCapacityParticles{};
    double droppedWallTime{};
};

inline constexpr double ParticleFixedStep = 1.0 / 60.0;
inline constexpr uint32_t MaximumParticleCapacity = 65536;
inline constexpr float MaximumParticleRate = 100000.0f;

} // namespace crimson
