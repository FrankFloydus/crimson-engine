////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/viewport_info_stats.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace quader {
namespace {

std::array<QVec3, 8> viewport_info_bounds_corners(const SceneBounds &bounds) {
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

bool viewport_info_bounds_contains_point(const SceneBounds &bounds,
                                         QVec3 point) {
  return point.x >= bounds.min.x && point.x <= bounds.max.x &&
         point.y >= bounds.min.y && point.y <= bounds.max.y &&
         point.z >= bounds.min.z && point.z <= bounds.max.z;
}

QVec3 viewport_info_normalized_axis(QVec3 axis, QVec3 fallback) {
  const QVec3 normalized = normalize(axis);
  return length_squared(normalized) > 0.0f ? normalized : fallback;
}

bool viewport_info_point_visible(const ViewportOverlayCamera &camera,
                                 QVec3 point) {
  const QVec3 forward =
      viewport_info_normalized_axis(camera.forward, {0.0f, 0.0f, -1.0f});
  const QVec3 right =
      viewport_info_normalized_axis(camera.right, {1.0f, 0.0f, 0.0f});
  const QVec3 up = viewport_info_normalized_axis(camera.up, {0.0f, 1.0f, 0.0f});
  const QVec3 delta = point - camera.eye;
  const float depth = dot(delta, forward);
  const float near_plane = std::max(camera.near_plane, 0.0001f);
  const float far_plane = std::max(camera.far_plane, near_plane);
  if (depth < near_plane || depth > far_plane) {
    return false;
  }

  const float aspect =
      std::max(camera.viewport_size.x, 1.0f) /
      std::max(camera.viewport_size.y, 1.0f);
  float half_height = std::max(camera.orthographic_size, 0.0001f) * 0.5f;
  if (camera.projection == ViewportOverlayProjection::Perspective) {
    constexpr float kPi = 3.14159265358979323846f;
    const float fov_radians =
        std::clamp(camera.fov_degrees, 1.0f, 170.0f) * kPi / 180.0f;
    half_height = std::tan(fov_radians * 0.5f) * depth;
  }
  const float half_width = half_height * aspect;
  const float horizontal = dot(delta, right);
  const float vertical = dot(delta, up);
  return std::abs(horizontal) <= half_width && std::abs(vertical) <= half_height;
}

bool viewport_info_bounds_visible(const ViewportOverlayCamera &camera,
                                  const SceneBounds &bounds) {
  if (viewport_info_bounds_contains_point(bounds, camera.eye) ||
      viewport_info_point_visible(camera, bounds_center(bounds))) {
    return true;
  }
  for (const QVec3 corner : viewport_info_bounds_corners(bounds)) {
    if (viewport_info_point_visible(camera, corner)) {
      return true;
    }
  }
  return false;
}

std::optional<SceneBounds> viewport_info_mesh_world_bounds(
    const QMat4 &world_from_local, const MeshAsset &mesh) {
  const std::optional<SceneBounds> local_bounds = local_mesh_bounds(mesh);
  if (!local_bounds.has_value()) {
    return std::nullopt;
  }

  std::optional<SceneBounds> world_bounds;
  for (const QVec3 local_corner : viewport_info_bounds_corners(*local_bounds)) {
    const QVec3 world_corner = transform_point(world_from_local, local_corner);
    if (!world_bounds.has_value()) {
      world_bounds = SceneBounds{.min = world_corner, .max = world_corner};
    } else {
      include_point(*world_bounds, world_corner);
    }
  }
  return world_bounds;
}

void append_viewport_info_mesh_stats(ViewportInfoSceneStats &stats,
                                     const MeshAsset &mesh,
                                     const QMat4 &world_from_local,
                                     const ViewportOverlayCamera &camera) {
  if (mesh.geometry().vertices.empty() || mesh.geometry().indices.empty()) {
    return;
  }
  const std::optional<SceneBounds> world_bounds =
      viewport_info_mesh_world_bounds(world_from_local, mesh);
  if (!world_bounds.has_value() ||
      !viewport_info_bounds_visible(camera, *world_bounds)) {
    return;
  }
  const std::vector<MeshPrimitiveSpan> spans =
      coalesced_mesh_primitive_spans(mesh);
  if (spans.empty()) {
    return;
  }
  ++stats.objects;
  stats.draw_calls += spans.size();
  for (const MeshPrimitiveSpan &span : spans) {
    stats.primitives += span.index_count / 3u;
  }
}

bool viewport_info_local_visibility_allows(
    const ViewportLocalVisibilitySnapshot *local_visibility,
    EntityId entity) {
  return local_visibility == nullptr || local_visibility->allows(entity);
}

} // namespace

ViewportInfoSceneStats
collect_viewport_info_scene_stats(const SceneDocument &document,
                                  const AssetRegistry &assets,
                                  const ViewportOverlayCamera &camera,
                                  const ViewportLocalVisibilitySnapshot *
                                      local_visibility) {
  ViewportInfoSceneStats stats;
  for (const EntityId entity_id : document.entities()) {
    if (!document.is_effectively_visible(entity_id) ||
        !viewport_info_local_visibility_allows(local_visibility, entity_id) ||
        document.is_instance_definition_entity(entity_id) ||
        document.is_instance_definition_member(entity_id) ||
        document.instance_occurrence(entity_id) != nullptr) {
      continue;
    }

    const MeshComponent *mesh_component = document.mesh(entity_id);
    if (mesh_component == nullptr) {
      continue;
    }

    const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
    if (mesh != nullptr) {
      append_viewport_info_mesh_stats(
          stats, *mesh, document.world_transform_matrix(entity_id), camera);
    }
  }

  for (const EntityId definition : document.entities()) {
    if (!document.is_instance_definition_entity(definition)) {
      continue;
    }
    const std::optional<QMat4> definition_inverse =
        try_scene_matrix_inverse(document.world_transform_matrix(definition));
    if (!definition_inverse.has_value()) {
      continue;
    }
    for (const EntityId occurrence :
         document.instance_occurrences_for_definition(definition)) {
      if (!document.is_effectively_visible(occurrence) ||
          !viewport_info_local_visibility_allows(local_visibility,
                                                 occurrence)) {
        continue;
      }
      const QMat4 occurrence_world =
          document.world_transform_matrix(occurrence);
      for (const EntityId member : document.descendants(definition, false)) {
        if (document.containing_instance_definition(member) != definition ||
            !scene_hierarchy_segment_is_visible(document, member,
                                                definition)) {
          continue;
        }
        const MeshComponent *mesh_component = document.mesh(member);
        if (mesh_component == nullptr) {
          continue;
        }
        const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
        if (mesh == nullptr) {
          continue;
        }
        const QMat4 member_from_definition = scene_transform_multiply(
            *definition_inverse, document.world_transform_matrix(member));
        append_viewport_info_mesh_stats(
            stats, *mesh,
            scene_transform_multiply(occurrence_world,
                                     member_from_definition),
            camera);
      }
    }
  }
  return stats;
}

} // namespace quader
