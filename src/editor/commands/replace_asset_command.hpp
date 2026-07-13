////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "editor/commands/asset_command.hpp"

#include <string>
#include <utility>

namespace quader {

/**
 * Applies Replace Mesh Asset Command behavior for undoable mesh asset replacement.
 */
class ReplaceMeshAssetCommand final : public AssetCommand {
public:
  ReplaceMeshAssetCommand(AssetRegistry &assets, AssetId asset,
                          MeshAsset before, MeshAsset after,
                          std::string label = "Replace Mesh")
      : AssetCommand(assets, std::move(label)), asset_(asset),
        before_(std::move(before)), after_(std::move(after)) {}

  AssetCommandResult undo() override { return apply(before_); }

  AssetCommandResult redo() override { return apply(after_); }

private:
  AssetCommandResult apply(const MeshAsset &mesh) {
    const AssetRegistryValidationResult validation =
        assets().validate_mesh(mesh);
    if (!validation.ok) {
      return asset_command_invalid_request_result(validation.message);
    }
    if (!assets().replace_mesh(asset_, mesh)) {
      return asset_command_invalid_request_result("Mesh asset does not exist");
    }
    return asset_command_changed_result();
  }

  AssetId asset_ = kInvalidAssetId;
  MeshAsset before_;
  MeshAsset after_;
};

/**
 * Applies Replace Material Asset Command behavior for undoable material asset replacement.
 */
class ReplaceMaterialAssetCommand final : public AssetCommand {
public:
  ReplaceMaterialAssetCommand(AssetRegistry &assets, AssetId asset,
                              MaterialDocument before, MaterialDocument after,
                              std::string label = "Replace Material")
      : AssetCommand(assets, std::move(label)), asset_(asset),
        before_(std::move(before)), after_(std::move(after)) {}

  AssetCommandResult undo() override { return apply(before_); }

  AssetCommandResult redo() override { return apply(after_); }

private:
  AssetCommandResult apply(const MaterialDocument &material) {
    const AssetRegistryValidationResult validation =
        assets().validate_material(material);
    if (!validation.ok) {
      return asset_command_invalid_request_result(validation.message);
    }
    if (!assets().replace_material(asset_, material)) {
      return asset_command_invalid_request_result(
          "Material asset does not exist");
    }
    return asset_command_changed_result();
  }

  AssetId asset_ = kInvalidAssetId;
  MaterialDocument before_;
  MaterialDocument after_;
};

/**
 * Applies Replace Texture Asset Command behavior for undoable texture asset replacement.
 */
class ReplaceTextureAssetCommand final : public AssetCommand {
public:
  ReplaceTextureAssetCommand(AssetRegistry &assets, AssetId asset,
                             TextureAsset before, TextureAsset after,
                             std::string label = "Replace Texture")
      : AssetCommand(assets, std::move(label)), asset_(asset),
        before_(std::move(before)), after_(std::move(after)) {}

  AssetCommandResult undo() override { return apply(before_); }

  AssetCommandResult redo() override { return apply(after_); }

private:
  AssetCommandResult apply(const TextureAsset &texture) {
    if (!assets().replace_texture(asset_, texture)) {
      return asset_command_invalid_request_result(
          "Texture asset does not exist");
    }
    return asset_command_changed_result();
  }

  AssetId asset_ = kInvalidAssetId;
  TextureAsset before_;
  TextureAsset after_;
};

/**
 * Applies Set Material Texture Asset Command behavior for undoable material texture edits.
 */
class SetMaterialTextureAssetCommand final : public AssetCommand {
public:
  SetMaterialTextureAssetCommand(AssetRegistry &assets, AssetId material,
                                 MaterialDocument before, std::string slot,
                                 AssetId texture)
      : AssetCommand(assets, "Update Material"), material_(material),
        before_(std::move(before)), slot_(std::move(slot)), texture_(texture) {}

  AssetCommandResult undo() override {
    const AssetRegistryValidationResult validation =
        assets().validate_material(before_);
    if (!validation.ok) {
      return asset_command_invalid_request_result(validation.message);
    }
    if (!assets().replace_material(material_, before_)) {
      return asset_command_invalid_request_result(
          "Material asset does not exist");
    }
    return asset_command_changed_result();
  }

  AssetCommandResult redo() override {
    const AssetRegistryMutationResult result =
        assets().set_material_texture(material_, slot_, texture_);
    return {
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  }

private:
  AssetId material_ = kInvalidAssetId;
  MaterialDocument before_;
  std::string slot_;
  AssetId texture_ = kInvalidAssetId;
};

} // namespace quader
