#pragma once

#include "quader/core/Types.h"
#include "quader/math/Types.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace quader {

namespace MaterialTextureSlot {
inline constexpr const char* BaseColor = "baseColorTexture";
inline constexpr const char* Roughness = "roughnessTexture";
} // namespace MaterialTextureSlot

struct MaterialDocument {
    AssetId id = InvalidAssetId;
    std::string name;
    Vec3 baseColor = {1.0f, 1.0f, 1.0f};
    float roughness = 1.0f;
    float metallic = 0.0f;
    std::unordered_map<std::string, AssetId> textureSlots;
    bool nodeGraphReserved = true;
};

MaterialDocument makeDefaultQuaderMaterial(std::string name = "Default Quader Material");
MaterialDocument makeDefaultQuaderMaterial(std::string name, AssetId baseColorTexture, AssetId roughnessTexture);

} // namespace quader
