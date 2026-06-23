////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetDocument.h"
#include "core/Types.h"
#include "math/Types.h"

#include <optional>
#include <unordered_map>

namespace quader {

namespace MaterialTextureSlot {
inline constexpr const char* BaseColor = "baseColorTexture";
inline constexpr const char* Roughness = "roughnessTexture";
} // namespace MaterialTextureSlot

/**
 * Represents a Material Document value used by the asset document and registry layer.
 */
struct MaterialDocument final : AssetDocumentBase<MaterialAssetRevision> {
    QVec3 baseColor = {1.0f, 1.0f, 1.0f};
    float roughness = 1.0f;
    float metallic = 0.0f;
    std::unordered_map<std::string, AssetId> textureSlots;
    bool nodeGraphReserved = true;
};

MaterialDocument makeDefaultQuaderMaterial(std::string name = "Default Quader Material");
MaterialDocument makeDefaultQuaderMaterial(std::string name, AssetId baseColorTexture, AssetId roughnessTexture);

} // namespace quader
