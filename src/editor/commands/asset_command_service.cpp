////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/asset_command_service.hpp"

#include "core/release_features.hpp"
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "editor/commands/add_asset_command.hpp"
#include "editor/commands/replace_asset_command.hpp"
#include "editor/history/editor_history_service.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace quader {
namespace {

void assign_asset_command_status(AssetCommandResult &target,
                                 AssetCommandResult source) {
  target.ok = source.ok;
  target.changed = source.changed;
  target.message = std::move(source.message);
}

bool same_vec3(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool same_material_payload(const MaterialDocument &left,
                           const MaterialDocument &right) {
  return left.name == right.name && same_vec3(left.base_color, right.base_color) &&
         left.roughness == right.roughness &&
         left.metallic == right.metallic &&
         left.diffuse_model == right.diffuse_model &&
         left.texture_slots == right.texture_slots &&
         left.node_graph_reserved == right.node_graph_reserved;
}

bool same_texture_payload(const TextureAsset &left,
                          const TextureAsset &right) {
  return left.name == right.name && left.usage == right.usage &&
         left.color_space == right.color_space && left.width == right.width &&
         left.height == right.height && left.source_path == right.source_path &&
         left.generated == right.generated;
}

bool scene_references_modeling_owned_mesh(const SceneDocument *scene,
                                          AssetId mesh) {
  if (scene == nullptr) {
    return false;
  }
  for (const EntityId entity : scene->entities()) {
    const MeshComponent *mesh_component = scene->mesh(entity);
    if (mesh_component != nullptr && mesh_component->mesh == mesh &&
        scene->modeling_object(entity) != nullptr) {
      return true;
    }
  }
  return false;
}

} // namespace

AssetCommandService::AssetCommandService(AssetRegistry &assets,
                                         EditorHistoryService &history,
                                         const SceneDocument *scene)
    : assets_(assets), history_(history), scene_(scene) {}

AssetCreateResult AssetCommandService::add_mesh(MeshAsset mesh) {
  const AssetRegistryValidationResult validation = assets_.validate_mesh(mesh);
  if (!validation.ok) {
    AssetCreateResult result;
    assign_asset_command_status(
        result, asset_command_invalid_request_result(validation.message));
    return result;
  }
  auto command =
      std::make_unique<AddMeshAssetCommand>(assets_, std::move(mesh));
  AddMeshAssetCommand *add_command = command.get();
  AssetCreateResult result;
  assign_asset_command_status(result, history_.execute(std::move(command)));
  if (result.ok && result.changed) {
    result.asset = add_command->asset();
  }
  return result;
}

AssetCreateResult
AssetCommandService::add_material(MaterialDocument material) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    AssetCreateResult result;
    assign_asset_command_status(result, asset_command_invalid_request_result(
        release_feature_unavailable_message()));
    return result;
  }

  const AssetRegistryValidationResult validation =
      assets_.validate_material(material);
  if (!validation.ok) {
    AssetCreateResult result;
    assign_asset_command_status(
        result, asset_command_invalid_request_result(validation.message));
    return result;
  }

  auto command =
      std::make_unique<AddMaterialAssetCommand>(assets_, std::move(material));
  AddMaterialAssetCommand *add_command = command.get();
  AssetCreateResult result;
  assign_asset_command_status(result, history_.execute(std::move(command)));
  if (result.ok && result.changed) {
    result.asset = add_command->asset();
  }
  return result;
}

AssetCreateResult AssetCommandService::add_texture(TextureAsset texture) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    AssetCreateResult result;
    assign_asset_command_status(result, asset_command_invalid_request_result(
        release_feature_unavailable_message()));
    return result;
  }

  auto command =
      std::make_unique<AddTextureAssetCommand>(assets_, std::move(texture));
  AddTextureAssetCommand *add_command = command.get();
  AssetCreateResult result;
  assign_asset_command_status(result, history_.execute(std::move(command)));
  if (result.ok && result.changed) {
    result.asset = add_command->asset();
  }
  return result;
}

AssetCommandResult AssetCommandService::replace_mesh(AssetId mesh,
                                                     MeshAsset replacement) {
  return replace_mesh_with_label(mesh, std::move(replacement), "Replace Mesh");
}

AssetCommandResult
AssetCommandService::replace_material(AssetId material,
                                      MaterialDocument replacement) {
  return replace_material_with_label(material, std::move(replacement),
                                     "Replace Material");
}

AssetCommandResult
AssetCommandService::replace_texture(AssetId texture, TextureAsset replacement) {
  return replace_texture_with_label(texture, std::move(replacement),
                                    "Replace Texture");
}

AssetCommandResult
AssetCommandService::update_mesh(
    AssetId mesh, const std::function<void(MeshAsset &)> &update,
    std::string label) {
  const MeshAsset *current = assets_.mesh(mesh);
  if (current == nullptr) {
    return asset_command_invalid_request_result("Mesh asset does not exist");
  }
  MeshAsset replacement = clone_mesh_asset_payload(*current);
  update(replacement);
  return replace_mesh_with_label(mesh, std::move(replacement),
                                 std::move(label));
}

AssetCommandResult AssetCommandService::update_material(
    AssetId material, const std::function<void(MaterialDocument &)> &update,
    std::string label) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return asset_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  const MaterialDocument *current = assets_.material(material);
  if (current == nullptr) {
    return asset_command_invalid_request_result("Material asset does not exist");
  }
  MaterialDocument replacement = *current;
  update(replacement);
  return replace_material_with_label(material, std::move(replacement),
                                     std::move(label));
}

AssetCommandResult AssetCommandService::update_texture(
    AssetId texture, const std::function<void(TextureAsset &)> &update,
    std::string label) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return asset_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  const TextureAsset *current = assets_.texture(texture);
  if (current == nullptr) {
    return asset_command_invalid_request_result("Texture asset does not exist");
  }
  TextureAsset replacement = *current;
  update(replacement);
  return replace_texture_with_label(texture, std::move(replacement),
                                    std::move(label));
}

AssetCommandResult
AssetCommandService::set_material_texture(AssetId material, std::string slot,
                                          AssetId texture) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return asset_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  const MaterialDocument *current = assets_.material(material);
  if (current == nullptr) {
    return asset_command_invalid_request_result("Material asset does not exist");
  }
  return history_.execute(std::make_unique<SetMaterialTextureAssetCommand>(
      assets_, material, *current, std::move(slot), texture));
}

AssetCommandResult
AssetCommandService::replace_mesh_with_label(AssetId mesh,
                                             MeshAsset replacement,
                                             std::string label) {
  if (mesh == kInvalidAssetId) {
    return asset_command_invalid_request_result("Mesh asset is invalid");
  }
  if (scene_references_modeling_owned_mesh(scene_, mesh)) {
    return asset_command_invalid_request_result(
        "Mesh asset is owned by a modeling object");
  }
  const AssetRegistryValidationResult validation =
      assets_.validate_mesh(replacement);
  if (!validation.ok) {
    return asset_command_invalid_request_result(validation.message);
  }
  const MeshAsset *current = assets_.mesh(mesh);
  if (current == nullptr) {
    return asset_command_invalid_request_result("Mesh asset does not exist");
  }
  if (mesh_asset_payload_equal(*current, replacement)) {
    return asset_command_unchanged_result("Mesh unchanged");
  }
  return history_.execute(std::make_unique<ReplaceMeshAssetCommand>(
      assets_, mesh, clone_mesh_asset_payload(*current),
      clone_mesh_asset_payload(replacement), std::move(label)));
}

AssetCommandResult
AssetCommandService::replace_material_with_label(AssetId material,
                                                 MaterialDocument replacement,
                                                 std::string label) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return asset_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  MaterialDocument normalized = std::move(replacement);
  const AssetRegistryValidationResult validation =
      assets_.validate_material(normalized);
  if (!validation.ok) {
    return asset_command_invalid_request_result(validation.message);
  }
  normalize_material_texture_slots(normalized);

  const MaterialDocument *current = assets_.material(material);
  if (current == nullptr) {
    return asset_command_invalid_request_result("Material asset does not exist");
  }
  if (same_material_payload(*current, normalized)) {
    return asset_command_unchanged_result("Material unchanged");
  }
  return history_.execute(std::make_unique<ReplaceMaterialAssetCommand>(
      assets_, material, *current, std::move(normalized), std::move(label)));
}

AssetCommandResult
AssetCommandService::replace_texture_with_label(AssetId texture,
                                                TextureAsset replacement,
                                                std::string label) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return asset_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  const TextureAsset *current = assets_.texture(texture);
  if (current == nullptr) {
    return asset_command_invalid_request_result("Texture asset does not exist");
  }
  if (same_texture_payload(*current, replacement)) {
    return asset_command_unchanged_result("Texture unchanged");
  }
  return history_.execute(std::make_unique<ReplaceTextureAssetCommand>(
      assets_, texture, *current, std::move(replacement), std::move(label)));
}

} // namespace quader
