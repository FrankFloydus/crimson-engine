#pragma once

#include "math/Math.h"
#include "resources/AssetRef.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crimson {
struct MaterialAsset;

enum class ParticleShape { Point, Cone, Sphere, Box };
enum class ParticleSpace { World, Local };
enum class ParticleVelocityMode { Vector, DirectionSpeed };
enum class ParticleDirectionMode { Fixed, RandomCone, Outward };
struct ParticleRandomRange {
    bool enabled{false};
    float minimum{1.25f},maximum{1.25f};
    bool operator==(const ParticleRandomRange&) const = default;
};
// Built-in behavior identities are their stable names (gravity, acceleration,
// drag). Required initialization/motion/render ordering is engine-owned.
struct ParticleBehavior {
    bool present{false},enabled{true};
    bool operator==(const ParticleBehavior&) const = default;
};
struct ParticleBurst {
    std::uint64_t id{1};
    float time{}; // Seconds after delay, relative to each emission cycle.
    std::uint32_t count{30};
    bool operator==(const ParticleBurst&) const = default;
};
inline constexpr std::size_t MaximumParticleBursts=64;
inline constexpr float MinimumParticleDuration=.001f,MaximumParticleTime=3600.f;
// The scalar limit also accommodates every valid legacy XYZ velocity.
inline constexpr float MaximumParticleShapeSize=1000.f,MaximumParticleSpeed=200000.f;
inline constexpr float MaximumParticleForce=100000.f,MaximumParticleDrag=1000.f;

// One emitter with stable identity, independent of particle storage order.
struct ParticleEmitterDefinition {
    uint64_t id{1};
    std::string name{"Emitter 1"};
    bool enabled{true};
    float rate{30.0f};
    float lifetime{1.25f};
    Vec3 initialVelocity{0,0,0}; // Emitter-coordinate launch velocity at birth.
    Vec3 gravity{0,-9.81f,0}; // Acceleration in the selected simulation space, m/s^2.
    float size{0.08f}; // Full billboard width/height in world meters.
    Vec3 color{1,1,1}; // Authored sRGB picker values; presentation converts to linear.
    float intensity{1.0f};
    uint32_t capacity{256};
    AssetRef<MaterialAsset> material{"builtin:material/particle"};
    float opacity{1.0f};
    ParticleShape shape{ParticleShape::Point};
    ParticleSpace space{ParticleSpace::World};
    float radius{.25f}; // Sphere volume or cone's base disk on local XZ.
    Vec3 boxSize{1,1,1}; // Full dimensions, centered at the emitter.
    ParticleRandomRange lifetimeRange{};
    ParticleVelocityMode velocityMode{ParticleVelocityMode::Vector};
    Vec3 direction{0,1,0};
    ParticleDirectionMode directionMode{ParticleDirectionMode::Fixed};
    float directionSpread{0}; // Launch cone half-angle, degrees (0..180).
    float directionFlatness{0}; // 0=3D spread, 1=axis/tangent plane fan.
    float speed{1};
    ParticleRandomRange speedRange{false,1,1};
    float delay{};
    bool finiteDuration{false}; // False retains continuous legacy emission.
    float duration{1};
    bool loop{false}; // Applies only to a finite duration; delay occurs once.
    std::vector<ParticleBurst> bursts;
    ParticleBehavior gravityBehavior{true,true};
    ParticleBehavior accelerationBehavior{},dragBehavior{};
    Vec3 acceleration{}; // Gravity/acceleration use the selected simulation space.
    float drag{1}; // Exponential damping coefficient, in 1/s.
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
    uint32_t maximumScheduleSegmentsPerTick{32};
};
struct ParticleStats {
    size_t live{};
    uint64_t ticks{}, spawned{}, droppedSpawns{}, droppedCatchUpTicks{}, clearedCapacityParticles{};
    uint64_t burstsFired{},droppedScheduleSegments{};
    double droppedWallTime{};
};

inline constexpr double ParticleFixedStep = 1.0 / 60.0;
inline constexpr uint32_t MaximumParticleCapacity = 65536;
inline constexpr float MaximumParticleRate = 100000.0f;

} // namespace crimson
