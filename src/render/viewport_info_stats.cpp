////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/viewport_info_stats.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/mesh_wire_geometry.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_mesh_occurrence_projection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <unordered_map>
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

std::optional<SceneBounds> viewport_info_world_bounds(
    const QMat4 &world_from_local, const SceneBounds &local_bounds) {
  std::optional<SceneBounds> world_bounds;
  for (const QVec3 local_corner : viewport_info_bounds_corners(local_bounds)) {
    const QVec3 world_corner = transform_point(world_from_local, local_corner);
    if (!world_bounds.has_value()) {
      world_bounds = SceneBounds{.min = world_corner, .max = world_corner};
    } else {
      include_point(*world_bounds, world_corner);
    }
  }
  return world_bounds;
}

/// Accumulates user-facing topology counts and renderer-facing submission
/// counts for one mesh payload.
struct ViewportInfoMeshStats {
  std::uint64_t vertices = 0;
  std::uint64_t edges = 0;
  std::uint64_t faces = 0;
  std::uint64_t triangles = 0;
  std::uint64_t primitives = 0;
  std::uint64_t draw_calls = 0;
};

ViewportInfoMeshStats build_viewport_info_mesh_stats(const MeshAsset &mesh) {
  ViewportInfoMeshStats stats;
  const std::vector<MeshPrimitiveSpan> spans =
      coalesced_mesh_primitive_spans(mesh);
  for (const MeshPrimitiveSpan &span : spans) {
    stats.primitives += span.index_count / 3u;
  }
  stats.draw_calls = spans.size();
  const MeshTopologyCounts topology = mesh_topology_counts(mesh);
  stats.vertices = topology.vertices;
  stats.edges = topology.edges;
  stats.faces = topology.faces;
  stats.triangles = topology.triangles;
  return stats;
}

void append_viewport_info_mesh_stats(ViewportInfoSceneSnapshot &snapshot,
                                     const MeshAsset &mesh,
                                     const QMat4 &world_from_local,
                                     EntityId source_entity,
                                     EntityId instance_definition,
                                     EntityId instance_occurrence,
                                     std::unordered_map<const MeshAsset *,
                                                        ViewportInfoMeshStats>
                                         &mesh_stats_cache) {
  if (mesh.geometry().vertices.empty() || mesh.geometry().indices.empty()) {
    return;
  }
  const std::optional<SceneBounds> local_bounds = local_mesh_bounds(mesh);
  if (!local_bounds.has_value()) {
    return;
  }
  const std::optional<SceneBounds> world_bounds =
      viewport_info_world_bounds(world_from_local, *local_bounds);
  if (!world_bounds.has_value()) {
    return;
  }
  const auto [cached, inserted] = mesh_stats_cache.try_emplace(&mesh);
  if (inserted) {
    cached->second = build_viewport_info_mesh_stats(mesh);
  }
  const ViewportInfoMeshStats &mesh_stats = cached->second;
  if (mesh_stats.draw_calls == 0u) {
    return;
  }
  snapshot.renderables.push_back({
      .world_bounds = *world_bounds,
      .local_bounds = *local_bounds,
      .source_entity = source_entity,
      .instance_definition = instance_definition,
      .instance_occurrence = instance_occurrence,
      .vertices = mesh_stats.vertices,
      .edges = mesh_stats.edges,
      .faces = mesh_stats.faces,
      .triangles = mesh_stats.triangles,
      .primitives = mesh_stats.primitives,
      .draw_calls = mesh_stats.draw_calls,
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
  const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      projection = document.mesh_occurrence_projection();
  snapshot.renderables.reserve(projection->records().size());
  std::unordered_map<const MeshAsset *, ViewportInfoMeshStats>
      mesh_stats_cache;
  mesh_stats_cache.reserve(projection->records().size());
  const auto append_projected = [&](std::size_t index) {
    const SceneMeshOccurrenceProjectionRecord *placement =
        projection->record(index);
    if (placement == nullptr ||
        !viewport_info_local_visibility_allows(
            local_visibility, placement->display_entity)) {
      return;
    }
    const MeshComponent *mesh_component =
        document.mesh(placement->geometry_entity);
    const MeshAsset *mesh = mesh_component != nullptr
                                ? assets.mesh(mesh_component->mesh)
                                : nullptr;
    if (mesh == nullptr) {
      return;
    }
    append_viewport_info_mesh_stats(
        snapshot, *mesh, placement->world_from_geometry,
        placement->geometry_entity, placement->definition_entity,
        placement->formal_instance() ? placement->display_entity
                                     : kInvalidEntityId,
        mesh_stats_cache);
  };
  for (const std::size_t index : projection->ordinary_placement_indices()) {
    append_projected(index);
  }
  for (const std::size_t index : projection->formal_placement_indices()) {
    append_projected(index);
  }
  return snapshot;
}

bool try_refresh_viewport_info_scene_snapshot_transforms(
    ViewportInfoSceneSnapshot &snapshot, const SceneDocument &document) {
  const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      projection = document.mesh_occurrence_projection();
  for (ViewportInfoRenderableStats &renderable : snapshot.renderables) {
    if (renderable.source_entity == kInvalidEntityId ||
        !document.exists(renderable.source_entity)) {
      return false;
    }
    const SceneMeshOccurrenceProjectionRecord *placement = projection->find({
        .display_entity = renderable.instance_occurrence != kInvalidEntityId
                              ? renderable.instance_occurrence
                              : renderable.source_entity,
        .definition_entity = renderable.instance_definition,
        .geometry_entity = renderable.source_entity,
    });
    if (placement == nullptr) {
      return false;
    }

    const std::optional<SceneBounds> world_bounds = viewport_info_world_bounds(
        placement->world_from_geometry, renderable.local_bounds);
    if (!world_bounds.has_value()) {
      return false;
    }
    renderable.world_bounds = *world_bounds;
  }
  return true;
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
    stats.vertices += renderable.vertices;
    stats.edges += renderable.edges;
    stats.faces += renderable.faces;
    stats.triangles += renderable.triangles;
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
