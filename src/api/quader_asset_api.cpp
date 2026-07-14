////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_asset_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_api_mutation_runner.hpp"
#include "editor/commands/asset_command_service.hpp"
#include "editor/session/scene_session.hpp"

#include <string_view>
#include <utility>

namespace quader {
namespace {

SceneSession &scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::scene_session(session);
}

QuaderApiResult<QuaderAssetRef> asset_result(QuaderApiMutationRunner &mutations,
                                             AssetCreateResult command,
                                             QuaderAssetKind kind,
                                             std::string_view label) {
  const AssetId id = command.asset;
  const QuaderApiVoidResult status =
      mutations.run_asset_result(command, label);
  return quader_api_result_from_status(status,
                                       QuaderAssetRef{.id = id, .kind = kind});
}

QuaderApiResult<QuaderAssetRef>
asset_failure_result(QuaderApiMutationRunner &mutations, std::string message) {
  const QuaderApiVoidResult status =
      mutations.run_asset_failure(std::move(message));
  return quader_api_result_from_status<QuaderAssetRef>(status);
}

} // namespace

QuaderAssetApi::QuaderAssetApi(QuaderApiSession session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiResult<QuaderAssetRef>
QuaderAssetApi::add_material(QuaderMaterialAsset material) {
  MaterialDocument document = to_material_document(material);
  AssetCreateResult result =
      scene_session(session_).asset_commands().add_material(
          std::move(document));
  if (!result.ok) {
    return asset_failure_result(mutations_, std::move(result.message));
  }
  return asset_result(mutations_, std::move(result), QuaderAssetKind::Material,
                      "Add Material");
}

QuaderApiResult<QuaderAssetRef>
QuaderAssetApi::add_material(const QuaderMaterialArgs &args) {
  QuaderMaterialAsset material;
  material.name = args.name;
  material.base_color = args.base_color;
  material.roughness = args.roughness;
  material.metallic = args.metallic;
  return add_material(std::move(material));
}

QuaderApiResult<QuaderAssetRef>
QuaderAssetApi::add_texture(QuaderTextureAsset texture) {
  AssetCreateResult result =
      scene_session(session_).asset_commands().add_texture(
          to_texture_asset(texture));
  if (!result.ok) {
    return asset_failure_result(mutations_, std::move(result.message));
  }
  return asset_result(mutations_, std::move(result), QuaderAssetKind::Texture,
                      "Add Texture");
}

QuaderApiResult<QuaderAssetRef>
QuaderAssetApi::add_standalone_mesh(QuaderMeshAsset mesh) {
  AssetCreateResult result =
      scene_session(session_).asset_commands().add_mesh(to_mesh_asset(mesh));
  if (!result.ok) {
    return asset_failure_result(mutations_, std::move(result.message));
  }
  return asset_result(mutations_, std::move(result), QuaderAssetKind::Mesh,
                      "Add Mesh");
}

QuaderApiVoidResult
QuaderAssetApi::replace_standalone_mesh(AssetId mesh,
                                        QuaderMeshAsset replacement) {
  return mutations_.run_asset_result(
      scene_session(session_).asset_commands().replace_mesh(
          mesh, to_mesh_asset(replacement)),
      "Mesh replaced");
}

QuaderApiVoidResult
QuaderAssetApi::replace_material(AssetId material,
                                 QuaderMaterialAsset replacement) {
  return mutations_.run_asset_result(
      scene_session(session_).asset_commands().replace_material(
          material, to_material_document(replacement)),
      "Material replaced");
}

QuaderApiVoidResult
QuaderAssetApi::replace_texture(AssetId texture,
                                QuaderTextureAsset replacement) {
  return mutations_.run_asset_result(
      scene_session(session_).asset_commands().replace_texture(
          texture, to_texture_asset(replacement)),
      "Texture replaced");
}

QuaderApiVoidResult QuaderAssetApi::set_material_base_color(AssetId material,
                                                            QVec3 color) {
  return mutations_.run_asset_result(
      scene_session(session_).asset_commands().update_material(
          material, [color](MaterialDocument &document) {
            document.base_color = color;
          }),
      "Material updated");
}

QuaderApiVoidResult QuaderAssetApi::set_material_roughness(AssetId material,
                                                           float roughness) {
  return mutations_.run_asset_result(
      scene_session(session_).asset_commands().update_material(
          material, [roughness](MaterialDocument &document) {
            document.roughness = roughness;
          }),
      "Material updated");
}

QuaderApiVoidResult QuaderAssetApi::set_material_metallic(AssetId material,
                                                          float metallic) {
  return mutations_.run_asset_result(
      scene_session(session_).asset_commands().update_material(
          material, [metallic](MaterialDocument &document) {
            document.metallic = metallic;
          }),
      "Material updated");
}

QuaderApiVoidResult QuaderAssetApi::set_material_texture(AssetId material,
                                                         std::string slot,
                                                         AssetId texture) {
  return mutations_.run_asset_result(
      scene_session(session_).asset_commands().set_material_texture(
          material, std::move(slot), texture),
      "Material updated");
}

QuaderApiVoidResult
QuaderAssetApi::assign_material_to_entity(EntityId entity, AssetId material) {
  return mutations_.run_document_result(
      scene_session(session_).document_commands().set_material(entity,
                                                                 material),
      "Assign Material");
}

QuaderApiVoidResult
QuaderAssetApi::assign_material_to_object(QuaderModelingObjectRef object,
                                          AssetId material) {
  if (!mutations_.belongs_to_active_scene(object.scene_session_id)) {
    return mutations_.stale_scene_reference_status("Modeling object");
  }
  if (object.entity == kInvalidEntityId) {
    return mutations_.run_asset_failure("Modeling object entity is invalid");
  }
  return assign_material_to_entity(object.entity, material);
}

} // namespace quader
