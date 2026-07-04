////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_document.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/texture_asset.hpp"
#include "core/types.hpp"
#include "editor/commands/asset_command_result.hpp"

#include <functional>
#include <string>

namespace quader {

class AssetRegistry;
class EditorHistoryService;
class SceneDocument;

/**
 * Coordinates Asset Command Service responsibilities for user-visible asset edits.
 */
class AssetCommandService final {
public:
  AssetCommandService(AssetRegistry &assets, EditorHistoryService &history,
                      const SceneDocument *scene = nullptr);

  AssetCreateResult add_mesh(MeshAsset mesh);
  AssetCreateResult add_material(MaterialDocument material);
  AssetCreateResult add_texture(TextureAsset texture);

  AssetCommandResult replace_mesh(AssetId mesh, MeshAsset replacement);
  AssetCommandResult replace_material(AssetId material,
                                      MaterialDocument replacement);
  AssetCommandResult replace_texture(AssetId texture, TextureAsset replacement);

  AssetCommandResult
  update_mesh(AssetId mesh, const std::function<void(MeshAsset &)> &update,
              std::string label = "Update Mesh");
  AssetCommandResult update_material(
      AssetId material, const std::function<void(MaterialDocument &)> &update,
      std::string label = "Update Material");
  AssetCommandResult update_texture(
      AssetId texture, const std::function<void(TextureAsset &)> &update,
      std::string label = "Update Texture");
  AssetCommandResult set_material_texture(AssetId material, std::string slot,
                                          AssetId texture);

private:
  AssetCommandResult replace_mesh_with_label(AssetId mesh,
                                             MeshAsset replacement,
                                             std::string label);
  AssetCommandResult replace_material_with_label(AssetId material,
                                                 MaterialDocument replacement,
                                                 std::string label);
  AssetCommandResult replace_texture_with_label(AssetId texture,
                                                TextureAsset replacement,
                                                std::string label);

  AssetRegistry &assets_;
  EditorHistoryService &history_;
  const SceneDocument *scene_ = nullptr;
};

} // namespace quader
