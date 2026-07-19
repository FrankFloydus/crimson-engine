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

/// Precomputes the camera facts shared by every cached bounds visibility test.
struct ViewportInfoVisibilityVolume {
  QVec3 eye = {};
  QVec3 forward = {0.0f, 0.0f, -1.0f};
  QVec3 right = {1.0f, 0.0f, 0.0f};
  QVec3 up = {0.0f, 1.0f, 0.0f};
  float near_plane = 0.0001f;
  float far_plane = 0.0001f;
  float aspect = 1.0f;
  float perspective_half_height_scale = 0.0f;
  float orthographic_half_height = 0.0001f;
  ViewportOverlayProjection projection =
      ViewportOverlayProjection::Perspective;
};

ViewportInfoVisibilityVolume viewport_info_visibility_volume(
    const ViewportOverlayCamera &camera) {
  constexpr float kPi = 3.14159265358979323846f;
  const float fov_radians =
      std::clamp(camera.fov_degrees, 1.0f, 170.0f) * kPi / 180.0f;
  return {
      .eye = camera.eye,
      .forward = viewport_info_normalized_axis(camera.forward,
                                               {0.0f, 0.0f, -1.0f}),
      .right = viewport_info_normalized_axis(camera.right,
                                             {1.0f, 0.0f, 0.0f}),
      .up = viewport_info_normalized_axis(camera.up, {0.0f, 1.0f, 0.0f}),
      .near_plane = std::max(camera.near_plane, 0.0001f),
      .far_plane = std::max(camera.far_plane,
                            std::max(camera.near_plane, 0.0001f)),
      .aspect = std::max(camera.viewport_size.x, 1.0f) /
                std::max(camera.viewport_size.y, 1.0f),
      .perspective_half_height_scale = std::tan(fov_radians * 0.5f),
      .orthographic_half_height =
          std::max(camera.orthographic_size, 0.0001f) * 0.5f,
      .projection = camera.projection,
  };
}

bool viewport_info_point_visible(const ViewportInfoVisibilityVolume &volume,
                                 QVec3 point) {
  const QVec3 delta = point - volume.eye;
  const float depth = dot(delta, volume.forward);
  if (depth < volume.near_plane || depth > volume.far_plane) {
    return false;
  }

  const float half_height =
      volume.projection == ViewportOverlayProjection::Perspective
          ? volume.perspective_half_height_scale * depth
          : volume.orthographic_half_height;
  const float half_width = half_height * volume.aspect;
  const float horizontal = dot(delta, volume.right);
  const float vertical = dot(delta, volume.up);
  return std::abs(horizontal) <= half_width && std::abs(vertical) <= half_height;
}

bool viewport_info_bounds_visible(const ViewportInfoVisibilityVolume &volume,
                                  const SceneBounds &bounds) {
  if (viewport_info_bounds_contains_point(bounds, volume.eye) ||
      viewport_info_point_visible(volume, bounds_center(bounds))) {
    return true;
  }
  for (const QVec3 corner : viewport_info_bounds_corners(bounds)) {
    if (viewport_info_point_visible(volume, corner)) {
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

void append_viewport_info_mesh_stats(ViewportInfoSceneSnapshot &snapshot,
                                     const MeshAsset &mesh,
                                     const QMat4 &world_from_local) {
  if (mesh.geometry().vertices.empty() || mesh.geometry().indices.empty()) {
    return;
  }
  const std::optional<SceneBounds> world_bounds =
      viewport_info_mesh_world_bounds(world_from_local, mesh);
  if (!world_bounds.has_value()) {
    return;
  }
  const std::vector<MeshPrimitiveSpan> spans =
      coalesced_mesh_primitive_spans(mesh);
  if (spans.empty()) {
    return;
  }
  std::uint64_t primitive_count = 0;
  for (const MeshPrimitiveSpan &span : spans) {
    primitive_count += span.index_count / 3u;
  }
  snapshot.renderables.push_back({
      .world_bounds = *world_bounds,
      .primitives = primitive_count,
      .draw_calls = spans.size(),
  });
}

bool viewport_info_local_visibility_allows(
    const ViewportLocalVisibilitySnapshot *local_visibility,
    EntityId entity) {
  return local_visibility == nullptr || local_visibility->allows(entity);
}

} // namespace

ViewportInfoSceneSnapshot build_viewport_info_scene_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  ViewportInfoSceneSnapshot snapshot;
  const std::vector<EntityId> entities = document.entities();
  snapshot.renderables.reserve(entities.size());
  for (const EntityId entity_id : entities) {
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
          snapshot, *mesh, document.world_transform_matrix(entity_id));
    }
  }

  for (const EntityId definition : entities) {
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
            snapshot, *mesh,
            scene_transform_multiply(occurrence_world,
                                     member_from_definition));
      }
    }
  }
  return snapshot;
}

ViewportInfoSceneStats collect_viewport_info_scene_stats(
    const ViewportInfoSceneSnapshot &snapshot,
    const ViewportOverlayCamera &camera) {
  ViewportInfoSceneStats stats;
  const ViewportInfoVisibilityVolume volume =
      viewport_info_visibility_volume(camera);
  for (const ViewportInfoRenderableStats &renderable : snapshot.renderables) {
    if (!viewport_info_bounds_visible(volume, renderable.world_bounds)) {
      continue;
    }
    ++stats.objects;
    stats.primitives += renderable.primitives;
    stats.draw_calls += renderable.draw_calls;
  }
  return stats;
}

ViewportInfoSceneStats
collect_viewport_info_scene_stats(const SceneDocument &document,
                                  const AssetRegistry &assets,
                                  const ViewportOverlayCamera &camera,
                                  const ViewportLocalVisibilitySnapshot *
                                      local_visibility) {
  return collect_viewport_info_scene_stats(
      build_viewport_info_scene_snapshot(document, assets, local_visibility),
      camera);
}

} // namespace quader
