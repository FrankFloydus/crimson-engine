#pragma once
#include "math/Math.h"
#include "resources/AssetRef.h"
#include <cstdint>
#include <string>

namespace crimson {
struct DecalAsset;
struct SceneDecalSeed {
    std::uint64_t receiver{};
    std::string modelId;
    std::uint64_t fingerprint{};
    std::uint32_t draw{},primitive{},triangle{};
    Vec3 barycentric{1,0,0},localPoint{},localNormal{0,0,1};
    bool operator==(const SceneDecalSeed&) const = default;
};
struct SceneDecal {
    AssetRef<DecalAsset> asset;
    SceneDecalSeed seed;
    Vec3 tint{1,1,1};
    float opacity{1},edgeFeather{.025f};
    std::int32_t priority{};
    // Transform alone owns the metre-square footprint. Seed data never applies
    // another placement transform; missing receivers remain repairable records.
    bool operator==(const SceneDecal&) const = default;
};
}
