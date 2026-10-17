#pragma once
#include <cstdint>
#include <limits>

namespace crimson {
inline constexpr std::uint32_t WorldCollisionLayer=1u<<0;
inline constexpr std::uint32_t PlayerCollisionLayer=1u<<1;
inline constexpr std::uint32_t EnemyCollisionLayer=1u<<2;
inline constexpr std::uint32_t TriggerCollisionLayer=1u<<3;
inline constexpr std::uint32_t CharacterCollisionLayer=1u<<4;
inline constexpr std::uint32_t AllCollisionLayers=std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t CharacterCollisionLayers=PlayerCollisionLayer|EnemyCollisionLayer|CharacterCollisionLayer;
enum class ColliderMode : std::uint8_t { Solid, Trigger };
struct CollisionFilter {
    std::uint32_t layers{CharacterCollisionLayer},mask{AllCollisionLayers};
    bool operator==(const CollisionFilter&) const = default;
};
}
