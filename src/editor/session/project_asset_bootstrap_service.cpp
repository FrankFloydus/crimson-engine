////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/session/project_asset_bootstrap_service.hpp"

#include "assets/default_material_catalog.hpp"
#include "assets/material_document.hpp"
#include "assets/texture_asset.hpp"
#include "editor/session/project_session.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace quader {

ProjectDefaultAssets ProjectAssetBootstrapService::bootstrap_default_assets(
    ProjectSession &session) const {
  const auto find_texture = [&](std::string_view name,
                                std::string_view package_path,
                                TextureUsage usage,
                                TextureColorSpace color_space) {
    for (const AssetId id : session.assets().texture_ids()) {
      const TextureAsset *texture = session.assets().texture(id);
      if (texture != nullptr && texture->name == name &&
          texture->source_path.generic_string() == package_path &&
          texture->usage == usage && texture->color_space == color_space &&
          !texture->generated) {
        return id;
      }
    }
    return kInvalidAssetId;
  };
  const auto find_material = [&](const MaterialDocument &expected) {
    for (const AssetId id : session.assets().material_ids()) {
      const MaterialDocument *material = session.assets().material(id);
      if (material != nullptr && material->name == expected.name &&
          material->base_color.x == expected.base_color.x &&
          material->base_color.y == expected.base_color.y &&
          material->base_color.z == expected.base_color.z &&
          material->roughness == expected.roughness &&
          material->metallic == expected.metallic &&
          material->texture_slots == expected.texture_slots &&
          material->node_graph_reserved == expected.node_graph_reserved) {
        return id;
      }
    }
    return kInvalidAssetId;
  };

  AssetId albedo_texture = find_texture(
      kDefaultMaterialAlbedoTextureName,
      kDefaultMaterialAlbedoTexturePackagePath, TextureUsage::BaseColor,
      TextureColorSpace::Srgb);
  if (albedo_texture == kInvalidAssetId) {
    TextureAsset albedo;
    albedo.name = std::string(kDefaultMaterialAlbedoTextureName);
    albedo.usage = TextureUsage::BaseColor;
    albedo.color_space = TextureColorSpace::Srgb;
    albedo.source_path =
        std::string(kDefaultMaterialAlbedoTexturePackagePath);
    albedo_texture = session.assets().add_texture(std::move(albedo));
  }

  AssetId roughness_texture = find_texture(
      kDefaultMaterialRoughnessTextureName,
      kDefaultMaterialRoughnessTexturePackagePath, TextureUsage::Roughness,
      TextureColorSpace::Linear);
  if (roughness_texture == kInvalidAssetId) {
    TextureAsset roughness;
    roughness.name = std::string(kDefaultMaterialRoughnessTextureName);
    roughness.usage = TextureUsage::Roughness;
    roughness.color_space = TextureColorSpace::Linear;
    roughness.source_path =
        std::string(kDefaultMaterialRoughnessTexturePackagePath);
    roughness_texture = session.assets().add_texture(std::move(roughness));
  }

  MaterialDocument expected_default =
      make_default_quader_material("Default Quader");
  set_material_texture_slot(expected_default,
                            material_texture_slot::kBaseColor,
                            albedo_texture);
  AssetId default_material_id = find_material(expected_default);
  if (default_material_id == kInvalidAssetId) {
    default_material_id =
        session.assets().add_material(std::move(expected_default));
  }

  return {
      .albedo_texture = albedo_texture,
      .roughness_texture = roughness_texture,
      .default_material = default_material_id,
  };
}

} // namespace quader
