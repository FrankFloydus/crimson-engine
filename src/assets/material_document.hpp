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

namespace material_texture_slot {
inline constexpr const char *kBaseColor = "baseColorTexture";
inline constexpr const char *kRoughness = "roughnessTexture";
} // namespace material_texture_slot

/**
 * Describes one canonical material texture slot and its accepted legacy alias.
 */
struct MaterialTextureSlotDescriptor final {
    const char* id = "";
    const char *legacy_alias = "";
};

/**
 * Represents a Material Document value used by the asset document and registry layer.
 */
struct MaterialDocument final : AssetDocumentBase<MaterialAssetRevision> {
  QVec3 base_color = {1.0f, 1.0f, 1.0f};
  float roughness = 1.0f;
  float metallic = 0.0f;
  std::unordered_map<std::string, AssetId> texture_slots;
  bool node_graph_reserved = true;
};

std::span<const MaterialTextureSlotDescriptor>
material_texture_slot_descriptors();
const MaterialTextureSlotDescriptor *
material_texture_slot_descriptor(std::string_view slot);
std::optional<std::string>
canonical_material_texture_slot(std::string_view slot);
bool material_texture_slot_supported(std::string_view slot);
void normalize_material_texture_slots(MaterialDocument &material);
std::optional<AssetId>
material_texture_slot_asset(const MaterialDocument &material,
                            std::string_view slot);
bool set_material_texture_slot(MaterialDocument &material,
                               std::string_view slot, AssetId texture);
std::vector<std::string>
unsupported_material_texture_slots(const MaterialDocument &material);

MaterialDocument
make_default_quader_material(std::string name = "Default Quader Material");
MaterialDocument make_default_quader_material(std::string name,
                                              AssetId base_color_texture,
                                              AssetId roughness_texture);

} // namespace quader
