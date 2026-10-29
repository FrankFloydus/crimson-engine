#pragma once
#include "math/Math.h"
#include "resources/AssetRef.h"
#include <cstdint>
#include <optional>
#include <string>

namespace crimson {
struct DecalAsset;
struct SceneDecalNativeSource {
    std::string memberUuid; // Native reference metadata owns UUID remapping.
    std::uint32_t indexOffset{},indexCount{};
    bool operator==(const SceneDecalNativeSource&) const = default;
};
struct SceneDecalSeed {
    std::uint64_t receiver{};
    std::string modelId;
    std::uint64_t fingerprint{};
    std::uint32_t draw{},primitive{},triangle{};
    Vec3 barycentric{1,0,0},localPoint{},localNormal{0,0,1};
    std::uint64_t sourceInstanceId{}; // Native occurrence/member provenance; zero for asset models.
    std::optional<SceneDecalNativeSource> nativeSource;
    bool operator==(const SceneDecalSeed&) const = default;
};
enum class SceneDecalProjection : std::uint8_t { Surface, Box };
struct SceneDecal {
    AssetRef<DecalAsset> asset;
    SceneDecalSeed seed;
    SceneDecalProjection projection{SceneDecalProjection::Surface};
    Vec3 tint{1,1,1};
    float opacity{1},edgeFeather{.025f};
    std::int32_t priority{};
    // Transform owns the metre-square surface footprint or centred metre-cube box.
    // Seed data never applies
    // another placement transform; missing receivers remain repairable records.
    bool operator==(const SceneDecal&) const = default;
};
}
