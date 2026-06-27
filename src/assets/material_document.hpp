////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_document.hpp"
#include "core/types.hpp"
#include "math/types.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quader {

namespace MaterialTextureSlot {
inline constexpr const char* BaseColor = "baseColorTexture";
inline constexpr const char* Roughness = "roughnessTexture";
} // namespace MaterialTextureSlot

/**
 * Describes one canonical material texture slot and its accepted legacy alias.
 */
struct MaterialTextureSlotDescriptor final {
    const char* id = "";
    const char* legacyAlias = "";
};

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

std::span<const MaterialTextureSlotDescriptor> materialTextureSlotDescriptors();
const MaterialTextureSlotDescriptor* materialTextureSlotDescriptor(std::string_view slot);
std::optional<std::string> canonicalMaterialTextureSlot(std::string_view slot);
bool materialTextureSlotSupported(std::string_view slot);
void normalizeMaterialTextureSlots(MaterialDocument& material);
std::optional<AssetId> materialTextureSlotAsset(const MaterialDocument& material, std::string_view slot);
bool setMaterialTextureSlot(MaterialDocument& material, std::string_view slot, AssetId texture);
std::vector<std::string> unsupportedMaterialTextureSlots(const MaterialDocument& material);

MaterialDocument makeDefaultQuaderMaterial(std::string name = "Default Quader Material");
MaterialDocument makeDefaultQuaderMaterial(std::string name, AssetId baseColorTexture, AssetId roughnessTexture);

} // namespace quader
