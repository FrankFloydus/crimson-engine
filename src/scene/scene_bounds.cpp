////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/scene_bounds.hpp"

#include "math/glm_bridge.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <array>

namespace quader {
namespace {

std::array<QVec3, 8> bounds_corners(const SceneBounds &bounds) {
  const QVec3 &min = bounds.min;
  const QVec3 &max = bounds.max;
  return {{
      {min.x, min.y, min.z},
      {max.x, min.y, min.z},
      {max.x, max.y, min.z},
      {min.x, max.y, min.z},
      {min.x, min.y, max.z},
      {max.x, min.y, max.z},
      {max.x, max.y, max.z},
      {min.x, max.y, max.z},
  }};
}

void include_bounds(std::optional<SceneBounds> &result,
                    const SceneBounds &bounds) {
  for (const QVec3 corner : bounds_corners(bounds)) {
    if (!result) {
      result = SceneBounds{.min = corner, .max = corner};
    } else {
      include_point(*result, corner);
    }
  }
}

std::optional<SceneBounds> transformed_local_bounds(
    const QMat4 &world_from_local,
    const std::optional<SceneBounds> &local_bounds) {
  if (local_bounds) {
    std::optional<SceneBounds> result;
    for (const QVec3 local_corner : bounds_corners(*local_bounds)) {
      const QVec3 world_corner =
          transform_point(world_from_local, local_corner);
      if (!result) {
        result = SceneBounds{.min = world_corner, .max = world_corner};
      } else {
        include_point(*result, world_corner);
      }
    }
    return result;
  }

  const QVec3 world_position = transform_point(world_from_local, {});
  return SceneBounds{.min = world_position, .max = world_position};
}

std::optional<SceneBounds> instance_occurrence_world_bounds(
    const SceneDocument &document, const AssetRegistry &assets,
    EntityId occurrence, EntityId definition) {
  const std::optional<QMat4> definition_from_world =
      try_scene_matrix_inverse(document.world_transform_matrix(definition));
  if (!definition_from_world.has_value()) {
    return std::nullopt;
  }

  const QMat4 occurrence_world = document.world_transform_matrix(occurrence);
  std::optional<SceneBounds> result;
  for (const EntityId member : document.descendants(definition, false)) {
    if (document.containing_instance_definition(member) != definition ||
        !scene_hierarchy_segment_is_visible(document, member, definition)) {
      continue;
    }
    const MeshComponent *component = document.mesh(member);
    const MeshAsset *mesh =
        component != nullptr ? assets.mesh(component->mesh) : nullptr;
    if (mesh == nullptr) {
      continue;
    }
    const QMat4 member_from_definition = scene_transform_multiply(
        *definition_from_world, document.world_transform_matrix(member));
    const std::optional<SceneBounds> member_bounds = transformed_local_bounds(
        scene_transform_multiply(occurrence_world, member_from_definition),
        local_mesh_bounds(*mesh));
    if (member_bounds.has_value()) {
      include_bounds(result, *member_bounds);
    }
  }
  return result;
}

} // namespace

std::optional<SceneBounds> local_mesh_bounds(const MeshAsset &mesh) {
  const MeshGeometryPayload &geometry = mesh.geometry();
  if (geometry.vertices.empty()) {
    return std::nullopt;
  }

  SceneBounds bounds{
      .min = geometry.vertices.front().position,
      .max = geometry.vertices.front().position,
  };
  for (const MeshVertex &vertex : geometry.vertices) {
    include_point(bounds, vertex.position);
  }
  return bounds;
}

std::optional<SceneBounds> world_bounds_from_local_mesh_bounds(
    const QMat4 &world_from_local,
    const std::optional<SceneBounds> &local_bounds) {
  return transformed_local_bounds(world_from_local, local_bounds);
}

void include_point(SceneBounds &bounds, QVec3 point) {
  bounds.min.x = std::min(bounds.min.x, point.x);
  bounds.min.y = std::min(bounds.min.y, point.y);
  bounds.min.z = std::min(bounds.min.z, point.z);
  bounds.max.x = std::max(bounds.max.x, point.x);
  bounds.max.y = std::max(bounds.max.y, point.y);
  bounds.max.z = std::max(bounds.max.z, point.z);
}

QVec3 bounds_center(const SceneBounds &bounds) {
  return (bounds.min + bounds.max) * 0.5f;
}

std::optional<SceneBounds> entity_world_bounds(const SceneDocument &document,
                                               const AssetRegistry &assets,
                                               EntityId entity) {
  if (!document.is_effectively_visible(entity)) {
    return std::nullopt;
  }

  const TransformComponent *transform = document.transform(entity);
  if (!transform) {
    return std::nullopt;
  }

  if (const InstanceOccurrenceComponent *occurrence =
          document.instance_occurrence(entity)) {
    if (const std::optional<SceneBounds> instance_bounds =
            instance_occurrence_world_bounds(document, assets, entity,
                                             occurrence->definition)) {
      return instance_bounds;
    }
  }

  std::optional<SceneBounds> local_bounds;
  const MeshComponent *mesh_component = document.mesh(entity);
  if (mesh_component) {
    if (const MeshAsset *mesh = assets.mesh(mesh_component->mesh)) {
      local_bounds = local_mesh_bounds(*mesh);
    }
  }

  return world_bounds_from_local_mesh_bounds(
      document.world_transform_matrix(entity), local_bounds);
}

std::optional<SceneBounds> entity_world_bounds_from_local_mesh_bounds(
    const SceneDocument &document, EntityId entity,
    const std::optional<SceneBounds> &local_bounds) {
  if (!document.is_effectively_visible(entity) ||
      document.transform(entity) == nullptr) {
    return std::nullopt;
  }
  return world_bounds_from_local_mesh_bounds(
      document.world_transform_matrix(entity), local_bounds);
}

std::optional<SceneBounds>
entity_hierarchy_world_bounds(const SceneDocument &document,
                              const AssetRegistry &assets, EntityId entity) {
  if (!document.exists(entity) || !document.is_effectively_visible(entity)) {
    return std::nullopt;
  }

  std::optional<SceneBounds> result;
  for (const EntityId descendant : document.descendants(entity, true)) {
    if (descendant != entity && !document.is_effectively_visible(descendant)) {
      continue;
    }
    if (document.mesh(descendant) != nullptr ||
        document.instance_occurrence(descendant) != nullptr) {
      if (const std::optional<SceneBounds> bounds =
              entity_world_bounds(document, assets, descendant)) {
        include_bounds(result, *bounds);
      }
    }
  }

  if (!result) {
    return entity_world_bounds(document, assets, entity);
  }
  return result;
}

std::optional<SceneBounds> document_world_bounds(const SceneDocument &document,
                                                 const AssetRegistry &assets) {
  return document_world_bounds(document, assets, {});
}

std::optional<SceneBounds> document_world_bounds(
    const SceneDocument &document, const AssetRegistry &assets,
    const std::function<bool(EntityId)> &entity_visible) {
  std::optional<SceneBounds> result;
  for (const EntityId entity : document.entities()) {
    if (entity_visible && !entity_visible(entity)) {
      continue;
    }
    if (const std::optional<SceneBounds> bounds =
            entity_world_bounds(document, assets, entity)) {
      include_bounds(result, *bounds);
    }
  }
  return result;
}

std::optional<SceneBounds> selected_world_bounds(const SceneDocument &document,
                                                 const AssetRegistry &assets) {
  std::optional<SceneBounds> result;
  for (const EntityId entity : document.selected_entities()) {
    if (const std::optional<SceneBounds> bounds =
            entity_world_bounds(document, assets, entity)) {
      include_bounds(result, *bounds);
    }
  }
  return result;
}

std::optional<SceneBounds>
selected_transform_world_bounds(const SceneDocument &document,
                                const AssetRegistry &assets) {
  std::optional<SceneBounds> result;
  for (const EntityId entity : document.selected_entities()) {
    const std::optional<SceneBounds> bounds =
        document.has_children(entity)
            ? entity_hierarchy_world_bounds(document, assets, entity)
            : entity_world_bounds(document, assets, entity);
    if (bounds) {
      include_bounds(result, *bounds);
    }
  }
  return result;
}

} // namespace quader
