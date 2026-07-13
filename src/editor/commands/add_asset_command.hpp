////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "editor/commands/asset_command.hpp"

#include <utility>

namespace quader {

/**
 * Applies Add Mesh Asset Command behavior for undoable mesh asset creation.
 */
class AddMeshAssetCommand final : public AssetCommand {
public:
  AddMeshAssetCommand(AssetRegistry &assets, MeshAsset mesh)
      : AssetCommand(assets, "Add Mesh"), mesh_(std::move(mesh)) {}

  [[nodiscard]] AssetId asset() const { return asset_; }

  AssetCommandResult undo() override {
    if (asset_ == kInvalidAssetId) {
      return asset_command_invalid_request_result("Mesh asset is invalid");
    }
    if (!assets().remove_mesh(asset_)) {
      return asset_command_invalid_request_result("Mesh asset does not exist");
    }
    return asset_command_changed_result();
  }

  AssetCommandResult redo() override {
    const AssetRegistryValidationResult validation =
        assets().validate_mesh(mesh_);
    if (!validation.ok) {
      return asset_command_invalid_request_result(validation.message);
    }
    const AssetId added = assets().add_mesh(asset_, mesh_);
    if (added == kInvalidAssetId) {
      return asset_command_invalid_request_result(
          "Mesh asset could not be added");
    }
    asset_ = added;
    return asset_command_changed_result();
  }

private:
  MeshAsset mesh_;
  AssetId asset_ = kInvalidAssetId;
};

/**
 * Applies Add Material Asset Command behavior for undoable material asset creation.
 */
class AddMaterialAssetCommand final : public AssetCommand {
public:
  AddMaterialAssetCommand(AssetRegistry &assets, MaterialDocument material)
      : AssetCommand(assets, "Add Material"), material_(std::move(material)) {}

  [[nodiscard]] AssetId asset() const { return asset_; }

  AssetCommandResult undo() override {
    if (asset_ == kInvalidAssetId) {
      return asset_command_invalid_request_result("Material asset is invalid");
    }
    if (!assets().remove_material(asset_)) {
      return asset_command_invalid_request_result(
          "Material asset does not exist");
    }
    return asset_command_changed_result();
  }

  AssetCommandResult redo() override {
    const AssetRegistryValidationResult validation =
        assets().validate_material(material_);
    if (!validation.ok) {
      return asset_command_invalid_request_result(validation.message);
    }
    const AssetId added = assets().add_material(asset_, material_);
    if (added == kInvalidAssetId) {
      return asset_command_invalid_request_result(
          "Material asset could not be added");
    }
    asset_ = added;
    return asset_command_changed_result();
  }

private:
  MaterialDocument material_;
  AssetId asset_ = kInvalidAssetId;
};

/**
 * Applies Add Texture Asset Command behavior for undoable texture asset creation.
 */
class AddTextureAssetCommand final : public AssetCommand {
public:
  AddTextureAssetCommand(AssetRegistry &assets, TextureAsset texture)
      : AssetCommand(assets, "Add Texture"), texture_(std::move(texture)) {}

  [[nodiscard]] AssetId asset() const { return asset_; }

  AssetCommandResult undo() override {
    if (asset_ == kInvalidAssetId) {
      return asset_command_invalid_request_result("Texture asset is invalid");
    }
    if (!assets().remove_texture(asset_)) {
      return asset_command_invalid_request_result(
          "Texture asset does not exist");
    }
    return asset_command_changed_result();
  }

  AssetCommandResult redo() override {
    const AssetId added = assets().add_texture(asset_, texture_);
    if (added == kInvalidAssetId) {
      return asset_command_invalid_request_result(
          "Texture asset could not be added");
    }
    asset_ = added;
    return asset_command_changed_result();
  }

private:
  TextureAsset texture_;
  AssetId asset_ = kInvalidAssetId;
};

} // namespace quader
