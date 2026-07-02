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
#include "scene/document/project_session.hpp"

#include <functional>
#include <utility>

namespace quader {
namespace {

ProjectSession &project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::project_session(session);
}

bool same_vec3(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

QuaderApiResult<QuaderAssetRef> asset_result(QuaderApiMutationRunner &mutations,
                                             AssetId id, QuaderAssetKind kind) {
  const QuaderApiVoidResult status =
      mutations.run_asset_result(true, "Asset mutation");
  return quader_api_result_from_status(status,
                                       QuaderAssetRef{.id = id, .kind = kind});
}

QuaderApiResult<QuaderAssetRef>
asset_failure_result(QuaderApiMutationRunner &mutations, std::string message) {
  const QuaderApiVoidResult status =
      mutations.run_asset_failure(std::move(message));
  return quader_api_result_from_status<QuaderAssetRef>(status);
}

QuaderApiVoidResult update_material_or_fail(
    ProjectSession &session, QuaderApiMutationRunner &mutations,
    AssetId material,
    const std::function<bool(const MaterialDocument &)> &should_change,
    const std::function<void(MaterialDocument &)> &update) {
  const MaterialDocument *document = session.assets().material(material);
  if (document == nullptr) {
    return mutations.run_asset_failure("Material asset does not exist");
  }
  if (!should_change(*document)) {
    return mutations.run_asset_result(false, "Material unchanged");
  }
  MaterialDocument updated = *document;
  update(updated);
  const AssetRegistryValidationResult validation =
      session.assets().validate_material(updated);
  if (!validation.ok) {
    return mutations.run_asset_failure(validation.message);
  }
  if (!session.assets().replace_material(material, std::move(updated))) {
    return mutations.run_asset_failure("Material asset does not exist");
  }
  return mutations.run_asset_result(true, "Material updated");
}

} // namespace

QuaderAssetApi::QuaderAssetApi(QuaderApiSession session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiResult<QuaderAssetRef>
QuaderAssetApi::add_material(QuaderMaterialAsset material) {
  MaterialDocument document = to_material_document(material);
  const AssetRegistryValidationResult validation =
      project_session(session_).assets().validate_material(document);
  if (!validation.ok) {
    return asset_failure_result(mutations_, validation.message);
  }
  const AssetId id =
      project_session(session_).assets().add_material(std::move(document));
  if (id == kInvalidAssetId) {
    return asset_failure_result(mutations_,
                                "Material asset could not be added");
  }
  return asset_result(mutations_, id, QuaderAssetKind::Material);
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
  const AssetId id =
      project_session(session_).assets().add_texture(to_texture_asset(texture));
  return asset_result(mutations_, id, QuaderAssetKind::Texture);
}

QuaderApiResult<QuaderAssetRef>
QuaderAssetApi::add_standalone_mesh(QuaderMeshAsset mesh) {
  const AssetId id =
      project_session(session_).assets().add_mesh(to_mesh_asset(mesh));
  return asset_result(mutations_, id, QuaderAssetKind::Mesh);
}

QuaderApiVoidResult
QuaderAssetApi::replace_standalone_mesh(AssetId mesh,
                                        QuaderMeshAsset replacement) {
  const ProjectAssetMutationResult result =
      project_session(session_).replace_standalone_mesh(
          mesh, to_mesh_asset(replacement));
  if (!result.ok) {
    return mutations_.run_asset_failure(result.message);
  }
  return mutations_.run_asset_result(result.changed, result.message);
}

QuaderApiVoidResult
QuaderAssetApi::replace_material(AssetId material,
                                 QuaderMaterialAsset replacement) {
  MaterialDocument document = to_material_document(replacement);
  const AssetRegistryValidationResult validation =
      project_session(session_).assets().validate_material(document);
  if (!validation.ok) {
    return mutations_.run_asset_failure(validation.message);
  }
  if (!project_session(session_).assets().replace_material(
          material, std::move(document))) {
    return mutations_.run_asset_failure("Material asset does not exist");
  }
  return mutations_.run_asset_result(true, "Material replaced");
}

QuaderApiVoidResult
QuaderAssetApi::replace_texture(AssetId texture,
                                QuaderTextureAsset replacement) {
  if (!project_session(session_).assets().replace_texture(
          texture, to_texture_asset(replacement))) {
    return mutations_.run_asset_failure("Texture asset does not exist");
  }
  return mutations_.run_asset_result(true, "Texture replaced");
}

QuaderApiVoidResult QuaderAssetApi::set_material_base_color(AssetId material,
                                                            QVec3 color) {
  return update_material_or_fail(
      project_session(session_), mutations_, material,
      [color](const MaterialDocument &document) {
        return !same_vec3(document.base_color, color);
      },
      [color](MaterialDocument &document) { document.base_color = color; });
}

QuaderApiVoidResult QuaderAssetApi::set_material_roughness(AssetId material,
                                                           float roughness) {
  return update_material_or_fail(
      project_session(session_), mutations_, material,
      [roughness](const MaterialDocument &document) {
        return document.roughness != roughness;
      },
      [roughness](MaterialDocument &document) {
        document.roughness = roughness;
      });
}

QuaderApiVoidResult QuaderAssetApi::set_material_metallic(AssetId material,
                                                          float metallic) {
  return update_material_or_fail(
      project_session(session_), mutations_, material,
      [metallic](const MaterialDocument &document) {
        return document.metallic != metallic;
      },
      [metallic](MaterialDocument &document) { document.metallic = metallic; });
}

QuaderApiVoidResult QuaderAssetApi::set_material_texture(AssetId material,
                                                         std::string slot,
                                                         AssetId texture) {
  const AssetRegistryMutationResult result =
      project_session(session_).assets().set_material_texture(material, slot,
                                                              texture);
  if (!result.ok) {
    return mutations_.run_asset_failure(result.message);
  }
  return mutations_.run_asset_result(result.changed, result.message);
}

QuaderApiVoidResult
QuaderAssetApi::assign_material_to_entity(EntityId entity, AssetId material) {
  return mutations_.run_document_result(
      project_session(session_).document_commands().set_material(entity,
                                                                 material),
      "Assign Material");
}

QuaderApiVoidResult
QuaderAssetApi::assign_material_to_object(QuaderModelingObjectRef object,
                                          AssetId material) {
  if (object.entity == kInvalidEntityId) {
    return mutations_.run_asset_failure("Modeling object entity is invalid");
  }
  return assign_material_to_entity(object.entity, material);
}

} // namespace quader
