////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/scene_bounds.hpp"

#include "math/glm_bridge.hpp"
#include "scene/scene_mesh_occurrence_projection.hpp"

#include <quader/modeling/curves/curve_document.hpp>

#include <algorithm>
#include <array>
#include <cmath>

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

std::optional<SceneBounds> transformed_mesh_geometry_bounds(
    const MeshAsset &mesh, const QMat4 &world_from_local,
    SceneBounds *local_bounds) {
  const MeshGeometryPayload &geometry = mesh.geometry();
  if (geometry.vertices.empty()) {
    return std::nullopt;
  }

  const glm::mat4 world_matrix = to_glm(world_from_local);
  const auto world_point = [&world_matrix](QVec3 local_point) {
    const glm::vec4 transformed =
        world_matrix * glm::vec4{to_glm(local_point), 1.0f};
    return from_glm(glm::vec3{transformed} / transformed.w);
  };
  const QVec3 first_local = geometry.vertices.front().position;
  const QVec3 first_world = world_point(first_local);
  if (local_bounds != nullptr) {
    *local_bounds = SceneBounds{.min = first_local, .max = first_local};
  }
  SceneBounds world_bounds{.min = first_world, .max = first_world};
  for (std::size_t index = 1u; index < geometry.vertices.size(); ++index) {
    const MeshVertex &vertex = geometry.vertices[index];
    if (local_bounds != nullptr) {
      include_point(*local_bounds, vertex.position);
    }
    include_point(world_bounds, world_point(vertex.position));
  }
  return world_bounds;
}

std::optional<SceneBounds> instance_occurrence_world_bounds(
    const SceneDocument &document, const AssetRegistry &assets,
    const SceneMeshOccurrenceProjectionSnapshot &mesh_projection,
    EntityId occurrence) {
  std::optional<SceneBounds> result;
  for (const SceneMeshOccurrenceProjectionRecord &placement :
       mesh_projection.records_for_display(occurrence)) {
    if (!placement.formal_instance()) {
      continue;
    }
    const MeshComponent *component = document.mesh(placement.geometry_entity);
    const MeshAsset *mesh =
        component != nullptr ? assets.mesh(component->mesh) : nullptr;
    if (mesh == nullptr) {
      continue;
    }
    const std::optional<SceneBounds> member_bounds = mesh_world_bounds(
        *mesh, placement.world_from_geometry);
    if (member_bounds.has_value()) {
      include_bounds(result, *member_bounds);
    }
  }
  return result;
}

void expand_bounds(SceneBounds &bounds, QVec3 extent) {
  bounds.min = bounds.min - extent;
  bounds.max = bounds.max + extent;
}

std::optional<SceneBounds> entity_world_bounds_with_projection(
    const SceneDocument &document, const AssetRegistry &assets,
    const SceneMeshOccurrenceProjectionSnapshot &mesh_projection,
    EntityId entity) {
  if (!document.is_effectively_visible(entity) ||
      document.transform(entity) == nullptr) {
    return std::nullopt;
  }

  if (document.instance_occurrence(entity) != nullptr) {
    if (const std::optional<SceneBounds> instance_bounds =
            instance_occurrence_world_bounds(document, assets,
                                             mesh_projection, entity)) {
      return instance_bounds;
    }
  }

  const MeshComponent *mesh_component = document.mesh(entity);
  if (mesh_component != nullptr) {
    if (const MeshAsset *mesh = assets.mesh(mesh_component->mesh)) {
      if (const std::optional<SceneBounds> bounds = mesh_world_bounds(
              *mesh, document.world_transform_matrix(entity))) {
        return bounds;
      }
    }
  }

  if (const CurveComponent *component = document.curve(entity)) {
    if (const CurveAsset *curve = assets.curve(component->curve)) {
      if (const std::optional<SceneBounds> bounds = curve_world_bounds(
              *curve, *component,
              document.world_transform_matrix(entity))) {
        return bounds;
      }
    }
  }

  const QVec3 world_position =
      transform_point(document.world_transform_matrix(entity), {});
  return SceneBounds{.min = world_position, .max = world_position};
}

std::optional<SceneBounds> entity_hierarchy_world_bounds_with_projection(
    const SceneDocument &document, const AssetRegistry &assets,
    const SceneMeshOccurrenceProjectionSnapshot &mesh_projection,
    EntityId entity) {
  if (!document.exists(entity) || !document.is_effectively_visible(entity)) {
    return std::nullopt;
  }

  std::optional<SceneBounds> result;
  for (const EntityId descendant : document.descendants(entity, true)) {
    if (descendant != entity && !document.is_effectively_visible(descendant)) {
      continue;
    }
    if (document.mesh(descendant) != nullptr ||
        document.curve(descendant) != nullptr ||
        document.instance_occurrence(descendant) != nullptr) {
      if (const std::optional<SceneBounds> bounds =
              entity_world_bounds_with_projection(
                  document, assets, mesh_projection, descendant)) {
        include_bounds(result, *bounds);
      }
    }
  }

  if (!result) {
    return entity_world_bounds_with_projection(document, assets,
                                               mesh_projection, entity);
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

std::optional<SceneBounds> local_curve_bounds(
    const CurveAsset &curve, const CurveComponent &component) {
  if (!curve.document) {
    return std::nullopt;
  }
  const modeling::CurveBounds bounds = curve.document->bounds();
  if (!bounds.valid) {
    return std::nullopt;
  }
  SceneBounds result{.min = {bounds.min.x, bounds.min.y, bounds.min.z},
                     .max = {bounds.max.x, bounds.max.y, bounds.max.z}};
  if (component.solidify && curve_component_is_valid(component)) {
    const QVec3 radius{component.solidify_radius,
                       component.solidify_radius,
                       component.solidify_radius};
    expand_bounds(result, radius);
  }
  return result;
}

std::optional<SceneBounds> curve_world_bounds(
    const CurveAsset &curve, const CurveComponent &component,
    const QMat4 &world_from_local) {
  if (!curve.document) {
    return std::nullopt;
  }
  const auto &m = world_from_local.values;
  modeling::CurveDocument transformed = *curve.document;
  const auto applied = transformed.transform(modeling::Transform3{
      .x_axis = {m[0], m[1], m[2]},
      .y_axis = {m[4], m[5], m[6]},
      .z_axis = {m[8], m[9], m[10]},
      .origin = {m[12], m[13], m[14]},
  });
  if (!applied.ok()) {
    return std::nullopt;
  }
  const modeling::CurveBounds bounds = transformed.bounds();
  if (!bounds.valid) {
    return std::nullopt;
  }
  SceneBounds result{.min = {bounds.min.x, bounds.min.y, bounds.min.z},
                     .max = {bounds.max.x, bounds.max.y, bounds.max.z}};
  if (component.solidify && curve_component_is_valid(component)) {
    // The image of a local radius sphere under the entity's complete linear
    // transform bounds every possible tube cross-section. Each world-axis
    // extent is the norm of the corresponding matrix row times the radius.
    const double radius = component.solidify_radius;
    const QVec3 extent{
        static_cast<float>(radius * std::sqrt(
            static_cast<double>(m[0]) * m[0] +
            static_cast<double>(m[4]) * m[4] +
            static_cast<double>(m[8]) * m[8])),
        static_cast<float>(radius * std::sqrt(
            static_cast<double>(m[1]) * m[1] +
            static_cast<double>(m[5]) * m[5] +
            static_cast<double>(m[9]) * m[9])),
        static_cast<float>(radius * std::sqrt(
            static_cast<double>(m[2]) * m[2] +
            static_cast<double>(m[6]) * m[6] +
            static_cast<double>(m[10]) * m[10])),
    };
    if (std::isfinite(extent.x) && std::isfinite(extent.y) &&
        std::isfinite(extent.z)) {
      expand_bounds(result, extent);
    }
  }
  return result;
}

std::optional<SceneBounds> mesh_world_bounds(
    const MeshAsset &mesh, const QMat4 &world_from_local) {
  return transformed_mesh_geometry_bounds(mesh, world_from_local, nullptr);
}

std::optional<MeshLocalWorldBounds> mesh_local_world_bounds(
    const MeshAsset &mesh, const QMat4 &world_from_local) {
  SceneBounds local_bounds;
  const std::optional<SceneBounds> world_bounds =
      transformed_mesh_geometry_bounds(mesh, world_from_local,
                                       &local_bounds);
  if (!world_bounds) {
    return std::nullopt;
  }
  return MeshLocalWorldBounds{.local = local_bounds,
                              .world = *world_bounds};
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
  const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      mesh_projection = document.mesh_occurrence_projection();
  return entity_world_bounds_with_projection(document, assets,
                                             *mesh_projection, entity);
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
  const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      mesh_projection = document.mesh_occurrence_projection();
  return entity_hierarchy_world_bounds_with_projection(
      document, assets, *mesh_projection, entity);
}

std::optional<SceneBounds> document_world_bounds(const SceneDocument &document,
                                                 const AssetRegistry &assets) {
  return document_world_bounds(document, assets, {});
}

std::optional<SceneBounds> document_world_bounds(
    const SceneDocument &document, const AssetRegistry &assets,
    const std::function<bool(EntityId)> &entity_visible) {
  const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      mesh_projection = document.mesh_occurrence_projection();
  std::optional<SceneBounds> result;
  for (const EntityId entity : document.entities()) {
    if (entity_visible && !entity_visible(entity)) {
      continue;
    }
    if (const std::optional<SceneBounds> bounds =
            entity_world_bounds_with_projection(document, assets,
                                                *mesh_projection, entity)) {
      include_bounds(result, *bounds);
    }
  }
  return result;
}

std::optional<SceneBounds> selected_world_bounds(const SceneDocument &document,
                                                 const AssetRegistry &assets) {
  const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      mesh_projection = document.mesh_occurrence_projection();
  std::optional<SceneBounds> result;
  for (const EntityId entity : document.selected_entities()) {
    if (const std::optional<SceneBounds> bounds =
            entity_world_bounds_with_projection(document, assets,
                                                *mesh_projection, entity)) {
      include_bounds(result, *bounds);
    }
  }
  return result;
}

std::optional<SceneBounds>
selected_transform_world_bounds(const SceneDocument &document,
                                const AssetRegistry &assets) {
  const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      mesh_projection = document.mesh_occurrence_projection();
  std::optional<SceneBounds> result;
  for (const EntityId entity : document.selected_entities()) {
    const std::optional<SceneBounds> bounds =
        document.has_children(entity)
            ? entity_hierarchy_world_bounds_with_projection(
                  document, assets, *mesh_projection, entity)
            : entity_world_bounds_with_projection(document, assets,
                                                  *mesh_projection, entity);
    if (bounds) {
      include_bounds(result, *bounds);
    }
  }
  return result;
}

} // namespace quader
