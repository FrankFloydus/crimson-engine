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
#include <utility>

namespace quader {

ProjectDefaultAssets ProjectAssetBootstrapService::bootstrap_default_assets(
    ProjectSession &session) const {
  TextureAsset albedo;
  albedo.name = std::string(kDefaultMaterialAlbedoTextureName);
  albedo.usage = TextureUsage::BaseColor;
  albedo.color_space = TextureColorSpace::Srgb;
  albedo.source_path = std::string(kDefaultMaterialAlbedoTexturePackagePath);
  const AssetId albedo_texture =
      session.assets().add_texture(std::move(albedo));

  TextureAsset roughness;
  roughness.name = std::string(kDefaultMaterialRoughnessTextureName);
  roughness.usage = TextureUsage::Roughness;
  roughness.color_space = TextureColorSpace::Linear;
  roughness.source_path =
      std::string(kDefaultMaterialRoughnessTexturePackagePath);
  const AssetId roughness_texture =
      session.assets().add_texture(std::move(roughness));

  MaterialDocument default_material =
      make_default_quader_material("Default Quader");
  set_material_texture_slot(default_material, material_texture_slot::kBaseColor,
                            albedo_texture);
  const AssetId default_material_id =
      session.assets().add_material(std::move(default_material));

  return {
      .albedo_texture = albedo_texture,
      .roughness_texture = roughness_texture,
      .default_material = default_material_id,
  };
}

} // namespace quader
