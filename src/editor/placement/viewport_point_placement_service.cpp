////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/placement/viewport_point_placement_service.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_grid_placement_policy.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kDirectionEpsilonSquared = 0.0000000001f;
constexpr float kIntersectionEpsilon = 0.000001f;
constexpr float kDistanceTieEpsilon = 0.001f;

void append_signature_float(std::uint64_t &signature, float value) {
  signature ^= static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value));
  signature *= 1099511628211ull;
}

void append_signature_vector(std::uint64_t &signature, QVec3 value) {
  append_signature_float(signature, value.x);
  append_signature_float(signature, value.y);
  append_signature_float(signature, value.z);
}

std::uint64_t
camera_projection_signature(const ViewportCameraController &camera) {
  std::uint64_t signature = 1469598103934665603ull;
  const ViewportCameraState &state = camera.state();
  append_signature_vector(signature, state.target);
  append_signature_float(signature, state.yaw_radians);
  append_signature_float(signature, state.pitch_radians);
  append_signature_float(signature, state.distance);
  signature ^= static_cast<std::uint64_t>(state.projection);
  signature *= 1099511628211ull;
  append_signature_float(signature, state.orthographic_size);
  append_signature_float(signature, state.fov_degrees);
  append_signature_float(signature, state.near_plane);
  append_signature_float(signature, state.far_plane);
  const ViewportCameraPose pose = camera.pose();
  append_signature_vector(signature, pose.eye);
  append_signature_vector(signature, pose.target);
  append_signature_vector(signature, pose.up);
  const QVec2 viewport_size = camera.viewport_size();
  append_signature_float(signature, viewport_size.x);
  append_signature_float(signature, viewport_size.y);
  return signature;
}

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

QVec3 normalized_or(QVec3 value, QVec3 fallback) {
  const QVec3 normalized = normalize_or_zero(value);
  return length_squared(normalized) > kDirectionEpsilonSquared
             ? normalized
             : fallback;
}

QVec3 plane_normal(const ViewportPointPlacementPlane &plane) {
  return normalize_or_zero(cross(plane.u_axis, plane.v_axis));
}

bool valid_plane(const ViewportPointPlacementPlane &plane) {
  return finite_vector(plane.origin) && finite_vector(plane.u_axis) &&
         finite_vector(plane.v_axis) &&
         length_squared(cross(plane.u_axis, plane.v_axis)) >
             kDirectionEpsilonSquared;
}

std::optional<QVec3> intersect_ray_plane(
    const ViewportRay &ray, const ViewportPointPlacementPlane &plane) {
  if (!valid_plane(plane)) {
    return std::nullopt;
  }
  const QVec3 normal = plane_normal(plane);
  const float denominator = dot(normal, ray.direction);
  if (!std::isfinite(denominator) ||
      std::abs(denominator) <= kIntersectionEpsilon) {
    return std::nullopt;
  }
  const float distance = dot(plane.origin - ray.origin, normal) / denominator;
  if (!std::isfinite(distance) || distance < 0.0f) {
    return std::nullopt;
  }
  const QVec3 point = ray.origin + ray.direction * distance;
  return finite_vector(point) ? std::optional<QVec3>(point) : std::nullopt;
}

std::optional<QVec3>
snap_point_to_grid(QVec3 point, const ViewportPointPlacementPlane &plane,
                   float grid_size) {
  if (!valid_plane(plane) || !finite_vector(point) ||
      !std::isfinite(grid_size)) {
    return std::nullopt;
  }
  const QVec3 u = normalize_or_zero(plane.u_axis);
  const QVec3 v = normalize_or_zero(plane.v_axis);
  if (length_squared(u) <= kDirectionEpsilonSquared ||
      length_squared(v) <= kDirectionEpsilonSquared) {
    return std::nullopt;
  }
  const float spacing = std::max(std::abs(grid_size), 0.0001f);
  const QVec3 relative = point - plane.origin;
  const float u_distance = std::round(dot(relative, u) / spacing) * spacing;
  const float v_distance = std::round(dot(relative, v) / spacing) * spacing;
  const QVec3 snapped = plane.origin + u * u_distance + v * v_distance;
  return finite_vector(snapped) ? std::optional<QVec3>(snapped)
                                : std::nullopt;
}

std::optional<QVec3>
snap_ray_to_grid(const ViewportRay &ray,
                 const ViewportPointPlacementPlane &plane, float grid_size) {
  const std::optional<QVec3> intersection = intersect_ray_plane(ray, plane);
  return intersection.has_value()
             ? snap_point_to_grid(*intersection, plane, grid_size)
             : std::nullopt;
}

ViewportPointPlacementPlane surface_grid_plane(QVec3 point, QVec3 normal) {
  const QVec3 safe_normal = normalize_or_zero(normal);
  QVec3 preferred = {1.0f, 0.0f, 0.0f};
  if (std::abs(dot(safe_normal, preferred)) > 0.9f) {
    preferred = {0.0f, 0.0f, 1.0f};
  }
  const QVec3 u = normalize_or_zero(
      preferred - safe_normal * dot(preferred, safe_normal));
  const QVec3 v = normalize_or_zero(cross(safe_normal, u));

  // Match Box placement: projecting the world origin onto the hovered plane
  // gives coplanar triangles one stable lattice instead of anchoring a new
  // grid at every raw ray hit.
  return {
      .origin = safe_normal * dot(point, safe_normal),
      .u_axis = u,
      .v_axis = v,
  };
}

float screen_distance_squared(QVec2 left, QVec2 right) {
  const float dx = left.x - right.x;
  const float dy = left.y - right.y;
  return dx * dx + dy * dy;
}

int snap_priority(ViewportPointSnapKind kind) {
  switch (kind) {
  case ViewportPointSnapKind::Vertex:
    return 0;
  case ViewportPointSnapKind::EdgeMidpoint:
    return 1;
  case ViewportPointSnapKind::FaceCenter:
    return 2;
  case ViewportPointSnapKind::Surface:
    return 3;
  case ViewportPointSnapKind::Grid:
    return 4;
  case ViewportPointSnapKind::None:
    return 5;
  }
  return 5;
}

std::optional<std::size_t> landmark_slot(ViewportPointSnapKind kind) {
  switch (kind) {
  case ViewportPointSnapKind::Vertex:
    return 0u;
  case ViewportPointSnapKind::EdgeMidpoint:
    return 1u;
  case ViewportPointSnapKind::FaceCenter:
    return 2u;
  case ViewportPointSnapKind::None:
  case ViewportPointSnapKind::Surface:
  case ViewportPointSnapKind::Grid:
    break;
  }
  return std::nullopt;
}

bool policy_enables(ViewportPointSnapKind kind,
                    const ViewportPointPlacementPolicy &policy) {
  switch (kind) {
  case ViewportPointSnapKind::Vertex:
    return policy.vertex;
  case ViewportPointSnapKind::EdgeMidpoint:
    return policy.edge_midpoint;
  case ViewportPointSnapKind::FaceCenter:
    return policy.face_center;
  case ViewportPointSnapKind::Surface:
    return policy.surface;
  case ViewportPointSnapKind::Grid:
    return policy.grid;
  case ViewportPointSnapKind::None:
    return false;
  }
  return false;
}

std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) {
  const std::uint32_t low = std::min(a, b);
  const std::uint32_t high = std::max(a, b);
  return (static_cast<std::uint64_t>(low) << 32u) |
         static_cast<std::uint64_t>(high);
}

/// Accumulates incident triangle normals for one unique mesh edge.
struct EdgeNormalAccumulator {
  std::uint32_t a = 0;
  std::uint32_t b = 0;
  QVec3 world_normal_sum = {};
};

/// Describes the nearest data returned by one ray/triangle intersection.
struct RayTriangleHit {
  float distance = std::numeric_limits<float>::max();
  QVec3 world_position = {};
};

std::optional<RayTriangleHit> intersect_ray_triangle(const ViewportRay &ray,
                                                     QVec3 a, QVec3 b,
                                                     QVec3 c) {
  const QVec3 edge_ab = b - a;
  const QVec3 edge_ac = c - a;
  const QVec3 p = cross(ray.direction, edge_ac);
  const float determinant = dot(edge_ab, p);
  if (!std::isfinite(determinant) ||
      std::abs(determinant) <= kIntersectionEpsilon) {
    return std::nullopt;
  }
  const float inverse_determinant = 1.0f / determinant;
  const QVec3 from_a = ray.origin - a;
  const float u = dot(from_a, p) * inverse_determinant;
  if (!std::isfinite(u) || u < 0.0f || u > 1.0f) {
    return std::nullopt;
  }
  const QVec3 q = cross(from_a, edge_ab);
  const float v = dot(ray.direction, q) * inverse_determinant;
  if (!std::isfinite(v) || v < 0.0f || u + v > 1.0f) {
    return std::nullopt;
  }
  const float distance = dot(edge_ac, q) * inverse_determinant;
  if (!std::isfinite(distance) || distance < kIntersectionEpsilon) {
    return std::nullopt;
  }
  return RayTriangleHit{
      .distance = distance,
      .world_position = ray.origin + ray.direction * distance,
  };
}

/// Stores the closest ray hit and surface normal for one entity.
struct EntitySurfaceHit {
  float distance = std::numeric_limits<float>::max();
  QVec3 world_position = {};
  QVec3 world_normal = {};
};

/// Couples a placement result with its pointer distance for ranking.
struct ScreenCandidate {
  ViewportPointPlacementResult placement;
  float screen_distance_sq = std::numeric_limits<float>::max();
};

bool candidate_is_better(const ScreenCandidate &candidate,
                         const ScreenCandidate &best,
                         bool use_kind_priority) {
  if (candidate.screen_distance_sq + kDistanceTieEpsilon <
      best.screen_distance_sq) {
    return true;
  }
  return use_kind_priority &&
         std::abs(candidate.screen_distance_sq - best.screen_distance_sq) <=
             kDistanceTieEpsilon &&
         snap_priority(candidate.placement.snap_kind) <
             snap_priority(best.placement.snap_kind);
}

ViewportPointPlacementResult make_placement(QVec3 position, QVec3 normal,
                                             ViewportPointSnapKind kind,
                                             EntityId entity,
                                             QVec3 fallback_normal) {
  return {
      .valid = finite_vector(position),
      .world_position = position,
      .world_normal = normalized_or(normal, fallback_normal),
      .snap_kind = kind,
      .entity = entity,
  };
}

} // namespace

ViewportPointPlacementPolicy
ViewportPointPlacementPolicy::pivot(PivotSnapSettings settings) {
  return {
      .vertex = settings.vertex,
      .edge_midpoint = settings.edge_midpoint,
      .face_center = settings.face_center,
      .surface = false,
      .grid = settings.grid,
      .include_effectively_locked_meshes = true,
      .prefer_surface_normal_for_features = false,
      .strict_kind_priority = false,
      .grid_requires_snap_radius = true,
      .snap_radius_pixels = 14.0f,
  };
}

ViewportPointPlacementPolicy ViewportPointPlacementPolicy::radial_array() {
  return {
      .vertex = true,
      .edge_midpoint = true,
      .face_center = true,
      .surface = true,
      .grid = true,
      .include_effectively_locked_meshes = false,
      .prefer_surface_normal_for_features = true,
      .strict_kind_priority = true,
      .grid_requires_snap_radius = false,
      .snap_radius_pixels = 14.0f,
  };
}

ViewportPointPlacementPlane viewport_active_grid_placement_plane(
    const ViewportCameraController &camera) {
  switch (ViewportGridPlacementPolicy::plane(camera, camera.pose())) {
  case ViewportGridPlane::YZ:
    return {
        .origin = {},
        .u_axis = {0.0f, 0.0f, -1.0f},
        .v_axis = {0.0f, 1.0f, 0.0f},
    };
  case ViewportGridPlane::XY:
    return {
        .origin = {},
        .u_axis = {1.0f, 0.0f, 0.0f},
        .v_axis = {0.0f, 1.0f, 0.0f},
    };
  case ViewportGridPlane::XZ:
    return {};
  }
  return {};
}

void ViewportPointPlacementService::invalidate() {
  cached_document_ = nullptr;
  cached_assets_ = nullptr;
  cached_scene_revision_ = 0;
  cached_asset_revision_ = 0;
  cache_valid_ = false;
  landmarks_.clear();
  triangles_.clear();
  cached_projection_camera_ = nullptr;
  cached_projection_signature_ = 0;
  projected_landmarks_.clear();
}

void ViewportPointPlacementService::refresh_cache(
    const SceneDocument &document, const AssetRegistry &assets) {
  if (cache_valid_ && cached_document_ == &document &&
      cached_assets_ == &assets &&
      cached_scene_revision_ == document.revision() &&
      cached_asset_revision_ == assets.revision()) {
    return;
  }

  landmarks_.clear();
  triangles_.clear();
  cached_projection_camera_ = nullptr;
  cached_projection_signature_ = 0;
  projected_landmarks_.clear();

  for (const EntityId entity : document.entities()) {
    if (!document.is_effectively_visible(entity)) {
      continue;
    }
    const VisibilityComponent *visibility = document.visibility(entity);
    if (visibility != nullptr && !visibility->selectable) {
      continue;
    }
    const MeshComponent *component = document.mesh(entity);
    if (component == nullptr) {
      continue;
    }
    const MeshAsset *mesh = assets.mesh(component->mesh);
    if (mesh == nullptr || mesh->geometry().vertices.empty()) {
      continue;
    }
    const MeshGeometryPayload &geometry = mesh->geometry();

    const bool effectively_locked = document.is_effectively_locked(entity);
    const QMat4 world = document.world_transform_matrix(entity);
    std::vector<QVec3> world_vertices;
    world_vertices.reserve(geometry.vertices.size());
    for (const MeshVertex &vertex : geometry.vertices) {
      world_vertices.push_back(transform_point(world, vertex.position));
    }

    std::vector<QVec3> vertex_normal_sums(geometry.vertices.size());
    std::vector<EdgeNormalAccumulator> edge_accumulators;
    std::unordered_map<std::uint64_t, std::size_t> edge_indices;
    edge_indices.reserve(geometry.indices.size());

    for (std::size_t index = 0; index + 2u < geometry.indices.size();
         index += 3u) {
      const std::uint32_t ia = geometry.indices[index];
      const std::uint32_t ib = geometry.indices[index + 1u];
      const std::uint32_t ic = geometry.indices[index + 2u];
      if (ia >= world_vertices.size() || ib >= world_vertices.size() ||
          ic >= world_vertices.size()) {
        continue;
      }
      const QVec3 a = world_vertices[ia];
      const QVec3 b = world_vertices[ib];
      const QVec3 c = world_vertices[ic];
      const QVec3 area_normal = cross(b - a, c - a);
      if (!finite_vector(area_normal) ||
          length_squared(area_normal) <= kDirectionEpsilonSquared) {
        continue;
      }
      const QVec3 normal = normalize_or_zero(area_normal);
      triangles_.push_back(CachedTriangle{
          .a = a,
          .b = b,
          .c = c,
          .world_normal = normal,
          .entity = entity,
          .effectively_locked = effectively_locked,
      });
      landmarks_.push_back(CachedLandmark{
          .world_position = (a + b + c) / 3.0f,
          .incident_world_normal = normal,
          .kind = ViewportPointSnapKind::FaceCenter,
          .entity = entity,
          .effectively_locked = effectively_locked,
      });
      vertex_normal_sums[ia] += area_normal;
      vertex_normal_sums[ib] += area_normal;
      vertex_normal_sums[ic] += area_normal;

      const std::array<std::pair<std::uint32_t, std::uint32_t>, 3> edges{{
          {ia, ib},
          {ib, ic},
          {ic, ia},
      }};
      for (const auto &[left, right] : edges) {
        const std::uint64_t key = edge_key(left, right);
        const auto found = edge_indices.find(key);
        if (found == edge_indices.end()) {
          edge_indices.emplace(key, edge_accumulators.size());
          edge_accumulators.push_back(EdgeNormalAccumulator{
              .a = left,
              .b = right,
              .world_normal_sum = area_normal,
          });
        } else {
          edge_accumulators[found->second].world_normal_sum += area_normal;
        }
      }
    }

    for (std::size_t index = 0; index < world_vertices.size(); ++index) {
      landmarks_.push_back(CachedLandmark{
          .world_position = world_vertices[index],
          .incident_world_normal =
              normalize_or_zero(vertex_normal_sums[index]),
          .kind = ViewportPointSnapKind::Vertex,
          .entity = entity,
          .effectively_locked = effectively_locked,
      });
    }
    for (const EdgeNormalAccumulator &edge : edge_accumulators) {
      landmarks_.push_back(CachedLandmark{
          .world_position =
              (world_vertices[edge.a] + world_vertices[edge.b]) * 0.5f,
          .incident_world_normal =
              normalize_or_zero(edge.world_normal_sum),
          .kind = ViewportPointSnapKind::EdgeMidpoint,
          .entity = entity,
          .effectively_locked = effectively_locked,
      });
    }
  }

  cached_document_ = &document;
  cached_assets_ = &assets;
  cached_scene_revision_ = document.revision();
  cached_asset_revision_ = assets.revision();
  cache_valid_ = true;
}

void ViewportPointPlacementService::refresh_projection_cache(
    const ViewportCameraController &camera) {
  const std::uint64_t signature = camera_projection_signature(camera);
  if (cached_projection_camera_ == &camera &&
      cached_projection_signature_ == signature &&
      projected_landmarks_.size() == landmarks_.size()) {
    return;
  }

  projected_landmarks_.clear();
  projected_landmarks_.reserve(landmarks_.size());
  for (const CachedLandmark &landmark : landmarks_) {
    const ViewportScreenPoint screen =
        camera.project_world_point(landmark.world_position);
    projected_landmarks_.push_back({
        .screen_position = screen.position,
        .visible = screen.visible,
    });
  }
  cached_projection_camera_ = &camera;
  cached_projection_signature_ = signature;
}

ViewportPointPlacementResult ViewportPointPlacementService::resolve(
    const SceneDocument &document, const AssetRegistry &assets,
    const ViewportCameraController &camera,
    const ViewportPointPlacementRequest &request) {
  refresh_cache(document, assets);
  refresh_projection_cache(camera);

  const ViewportRay ray = camera.ray_from_viewport_point(request.mouse_local);
  const QVec3 grid_normal =
      normalized_or(plane_normal(request.active_grid), {0.0f, 1.0f, 0.0f});
  const float radius = std::max(request.policy.snap_radius_pixels, 0.0f);
  const float radius_sq = radius * radius;

  std::unordered_map<EntityId, EntitySurfaceHit> surface_hits_by_entity;
  std::optional<std::pair<EntityId, EntitySurfaceHit>> closest_surface;
  if (request.policy.surface ||
      request.policy.prefer_surface_normal_for_features) {
    for (const CachedTriangle &triangle : triangles_) {
      if (triangle.effectively_locked &&
          !request.policy.include_effectively_locked_meshes) {
        continue;
      }
      const std::optional<RayTriangleHit> hit =
          intersect_ray_triangle(ray, triangle.a, triangle.b, triangle.c);
      if (!hit.has_value()) {
        continue;
      }
      EntitySurfaceHit &entity_hit = surface_hits_by_entity[triangle.entity];
      if (hit->distance < entity_hit.distance) {
        entity_hit = {
            .distance = hit->distance,
            .world_position = hit->world_position,
            .world_normal = triangle.world_normal,
        };
      }
      if (!closest_surface.has_value() ||
          hit->distance < closest_surface->second.distance) {
        closest_surface = std::pair<EntityId, EntitySurfaceHit>{
            triangle.entity,
            EntitySurfaceHit{
                .distance = hit->distance,
                .world_position = hit->world_position,
                .world_normal = triangle.world_normal,
            },
        };
      }
    }
  }

  std::array<std::optional<ScreenCandidate>, 3> best_by_landmark_kind;
  std::optional<ScreenCandidate> best_by_distance;
  for (std::size_t landmark_index = 0;
       landmark_index < landmarks_.size(); ++landmark_index) {
    const CachedLandmark &landmark = landmarks_[landmark_index];
    if (!policy_enables(landmark.kind, request.policy) ||
        (landmark.effectively_locked &&
         !request.policy.include_effectively_locked_meshes)) {
      continue;
    }
    const std::optional<std::size_t> slot = landmark_slot(landmark.kind);
    if (!slot.has_value()) {
      continue;
    }
    const CachedProjection &screen = projected_landmarks_[landmark_index];
    if (!screen.visible) {
      continue;
    }
    const float distance_sq =
        screen_distance_squared(screen.screen_position, request.mouse_local);
    if (!std::isfinite(distance_sq) || distance_sq > radius_sq) {
      continue;
    }

    QVec3 normal = landmark.incident_world_normal;
    if (request.policy.prefer_surface_normal_for_features &&
        (landmark.kind == ViewportPointSnapKind::Vertex ||
         landmark.kind == ViewportPointSnapKind::EdgeMidpoint)) {
      const auto surface = surface_hits_by_entity.find(landmark.entity);
      if (surface != surface_hits_by_entity.end()) {
        normal = surface->second.world_normal;
      }
    }
    const ScreenCandidate candidate{
        .placement = make_placement(
            landmark.world_position, normal, landmark.kind, landmark.entity,
            grid_normal),
        .screen_distance_sq = distance_sq,
    };
    std::optional<ScreenCandidate> &kind_best =
        best_by_landmark_kind[*slot];
    if (!kind_best.has_value() ||
        candidate_is_better(candidate, *kind_best, false)) {
      kind_best = candidate;
    }
    if (!best_by_distance.has_value() ||
        candidate_is_better(candidate, *best_by_distance, true)) {
      best_by_distance = candidate;
    }
  }

  std::optional<ScreenCandidate> grid_candidate;
  if (request.policy.grid) {
    if (const std::optional<QVec3> snapped =
            snap_ray_to_grid(ray, request.active_grid, request.grid_size)) {
      const ViewportScreenPoint screen = camera.project_world_point(*snapped);
      const float distance_sq =
          screen.visible
              ? screen_distance_squared(screen.position, request.mouse_local)
              : std::numeric_limits<float>::max();
      if (!request.policy.grid_requires_snap_radius ||
          (screen.visible && distance_sq <= radius_sq)) {
        grid_candidate = ScreenCandidate{
            .placement = make_placement(*snapped, grid_normal,
                                        ViewportPointSnapKind::Grid,
                                        kInvalidEntityId, grid_normal),
            .screen_distance_sq = distance_sq,
        };
      }
    }
  }

  const auto surface_placement = [&]() {
    const EntitySurfaceHit &surface = closest_surface->second;
    const ViewportPointPlacementPlane surface_grid =
        surface_grid_plane(surface.world_position, surface.world_normal);
    const QVec3 surface_position =
        snap_point_to_grid(surface.world_position, surface_grid,
                           request.grid_size)
            .value_or(surface.world_position);
    return make_placement(surface_position, surface.world_normal,
                          ViewportPointSnapKind::Surface,
                          closest_surface->first, grid_normal);
  };

  if (request.policy.strict_kind_priority) {
    for (const std::optional<ScreenCandidate> &candidate :
         best_by_landmark_kind) {
      if (candidate.has_value()) {
        return candidate->placement;
      }
    }
    if (request.policy.surface && closest_surface.has_value()) {
      return surface_placement();
    }
    if (grid_candidate.has_value()) {
      return grid_candidate->placement;
    }
  } else {
    if (grid_candidate.has_value() &&
        (!best_by_distance.has_value() ||
         candidate_is_better(*grid_candidate, *best_by_distance, true))) {
      best_by_distance = grid_candidate;
    }
    if (best_by_distance.has_value()) {
      return best_by_distance->placement;
    }
    if (request.policy.surface && closest_surface.has_value()) {
      return surface_placement();
    }
  }

  if (request.fallback_plane.has_value()) {
    const QVec3 fallback_normal = normalized_or(
        plane_normal(*request.fallback_plane), grid_normal);
    const QVec3 fallback_position =
        intersect_ray_plane(ray, *request.fallback_plane)
            .value_or(request.fallback_plane->origin);
    return make_placement(fallback_position, fallback_normal,
                          ViewportPointSnapKind::None, kInvalidEntityId,
                          grid_normal);
  }
  return {};
}

} // namespace quader
