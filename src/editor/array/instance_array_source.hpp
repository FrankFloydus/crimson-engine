////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "assets/asset_revision.hpp"
#include "assets/material_reference.hpp"
#include "assets/mesh_asset.hpp"
#include "core/types.hpp"
#include "math/types.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace quader {

/// Captures one visible canonical mesh used to preview an occurrence.
struct InstanceArrayPreviewMeshSnapshot final {
  EntityId member = kInvalidEntityId;
  AssetId mesh = kInvalidAssetId;
  MaterialReference material;
  MeshAssetRevision mesh_revision;
  MaterialAssetRevision material_revision;
  QMat4 member_from_definition;
};

inline MaterialReference instance_array_entity_material(
    const SceneDocument &scene, EntityId entity) {
  const MaterialComponent *material = scene.material(entity);
  return material != nullptr ? material->material : MaterialReference{};
}

inline bool capture_instance_array_preview_meshes(
    const SceneDocument &scene, const AssetRegistry &assets,
    EntityId occurrence, EntityId &definition,
    std::vector<InstanceArrayPreviewMeshSnapshot> &meshes) {
  definition = kInvalidEntityId;
  meshes.clear();
  const InstanceOccurrenceComponent *component =
      scene.instance_occurrence(occurrence);
  if (component == nullptr || scene.kind(occurrence) != EntityKind::Instance ||
      scene.has_children(occurrence) ||
      !scene.is_instance_definition_entity(component->definition)) {
    return false;
  }
  const std::optional<QMat4> definition_inverse =
      try_scene_matrix_inverse(
          scene.world_transform_matrix(component->definition));
  if (!definition_inverse.has_value()) {
    return false;
  }

  definition = component->definition;
  for (const EntityId member : scene.descendants(definition, false)) {
    if (scene.containing_instance_definition(member) != definition ||
        !scene_hierarchy_segment_is_visible(scene, member, definition)) {
      continue;
    }
    const MeshComponent *mesh = scene.mesh(member);
    const MeshAsset *asset = mesh != nullptr ? assets.mesh(mesh->mesh) : nullptr;
    if (mesh == nullptr) {
      continue;
    }
    if (asset == nullptr) {
      meshes.clear();
      definition = kInvalidEntityId;
      return false;
    }
    const MaterialReference material =
        instance_array_entity_material(scene, member);
    meshes.push_back(InstanceArrayPreviewMeshSnapshot{
        .member = member,
        .mesh = mesh->mesh,
        .material = material,
        .mesh_revision = asset->revision,
        .material_revision =
            material.embedded()
                ? assets.material_revision(material.embedded_asset())
                : MaterialAssetRevision{},
        .member_from_definition = scene_transform_multiply(
            *definition_inverse, scene.world_transform_matrix(member)),
    });
  }
  return !meshes.empty();
}

} // namespace quader
