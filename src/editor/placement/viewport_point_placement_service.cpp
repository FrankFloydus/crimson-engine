////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/placement/viewport_point_placement_service.hpp"

#include "assets/asset_registry.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_grid_placement_policy.hpp"
#include "viewport/viewport_pixel_scale.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
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

/// Couples a placement result with its pointer distance for ranking.
struct ScreenCandidate {
  ViewportPointPlacementResult placement;
  float screen_distance_sq = std::numeric_limits<float>::max();
  float ray_distance = std::numeric_limits<float>::max();
  SceneSurfaceParticipantId participant;
  std::uint64_t element_key = 0u;
};

bool candidate_is_better(const ScreenCandidate &candidate,
                         const ScreenCandidate &best,
                         bool use_kind_priority) {
  if (candidate.screen_distance_sq + kDistanceTieEpsilon <
      best.screen_distance_sq) {
    return true;
  }
  if (std::abs(candidate.screen_distance_sq - best.screen_distance_sq) >
      kDistanceTieEpsilon) {
    return false;
  }
  if (use_kind_priority &&
      snap_priority(candidate.placement.snap_kind) !=
          snap_priority(best.placement.snap_kind)) {
    return snap_priority(candidate.placement.snap_kind) <
           snap_priority(best.placement.snap_kind);
  }
  if (std::abs(candidate.ray_distance - best.ray_distance) >
      kDistanceTieEpsilon) {
    return candidate.ray_distance < best.ray_distance;
  }
  if (candidate.participant != best.participant) {
    return scene_surface_participant_id_less(candidate.participant,
                                             best.participant);
  }
  return candidate.element_key < best.element_key;
}

ViewportPointSnapKind point_snap_kind(SceneSurfaceLandmarkKind kind) {
  switch (kind) {
  case SceneSurfaceLandmarkKind::Vertex:
    return ViewportPointSnapKind::Vertex;
  case SceneSurfaceLandmarkKind::EdgeMidpoint:
    return ViewportPointSnapKind::EdgeMidpoint;
  case SceneSurfaceLandmarkKind::FaceCenter:
    return ViewportPointSnapKind::FaceCenter;
  }
  return ViewportPointSnapKind::None;
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

ViewportPointPlacementService::ViewportPointPlacementService(
    SceneSurfaceQuery &surface_query)
    : shared_surface_query_(&surface_query) {}

void ViewportPointPlacementService::invalidate() {
  if (standalone_surface_query_) {
    standalone_surface_query_->invalidate();
  }
  cached_surface_snapshot_ = {};
  cached_surface_revision_ = 0u;
  cached_projection_camera_ = nullptr;
  cached_projection_signature_ = 0;
  projected_landmarks_.clear();
}

void ViewportPointPlacementService::set_viewport_visibility_filter(
    std::function<bool(EntityId)> filter,
    std::function<std::uint64_t()> revision_provider) {
  viewport_visibility_filter_ = std::move(filter);
  viewport_visibility_revision_provider_ = std::move(revision_provider);
  standalone_surface_query_.reset();
  standalone_document_ = nullptr;
  standalone_assets_ = nullptr;
  invalidate();
}

SceneSurfaceQuerySnapshot ViewportPointPlacementService::surface_snapshot(
    const SceneDocument &document, const AssetRegistry &assets) {
  SceneSurfaceQuery *query = shared_surface_query_;
  if (query == nullptr) {
    if (!standalone_surface_query_ || standalone_document_ != &document ||
        standalone_assets_ != &assets) {
      standalone_surface_query_ = std::make_unique<SceneSurfaceQuery>(
          document, assets, viewport_visibility_filter_,
          viewport_visibility_revision_provider_);
      standalone_document_ = &document;
      standalone_assets_ = &assets;
      cached_surface_snapshot_ = {};
      cached_surface_revision_ = 0u;
      cached_projection_camera_ = nullptr;
      cached_projection_signature_ = 0u;
      projected_landmarks_.clear();
    }
    query = standalone_surface_query_.get();
  }
  SceneSurfaceQuerySnapshot snapshot = query->snapshot();
  if (snapshot.revision() != cached_surface_revision_) {
    cached_surface_snapshot_ = snapshot;
    cached_surface_revision_ = snapshot.revision();
    cached_projection_camera_ = nullptr;
    cached_projection_signature_ = 0u;
    projected_landmarks_.clear();
  }
  return snapshot;
}

void ViewportPointPlacementService::refresh_projection_cache(
    const ViewportCameraController &camera,
    const SceneSurfaceQuerySnapshot &surface_snapshot) {
  const std::uint64_t signature = camera_projection_signature(camera);
  if (cached_projection_camera_ == &camera &&
      cached_projection_signature_ == signature &&
      cached_surface_revision_ == surface_snapshot.revision() &&
      projected_landmarks_.size() == surface_snapshot.landmarks().size()) {
    return;
  }

  projected_landmarks_.clear();
  projected_landmarks_.reserve(surface_snapshot.landmarks().size());
  for (const SceneSurfaceLandmark &landmark : surface_snapshot.landmarks()) {
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
  const bool landmark_candidates_enabled =
      request.policy.vertex || request.policy.edge_midpoint ||
      request.policy.face_center;
  const bool geometry_candidates_enabled =
      landmark_candidates_enabled || request.policy.surface ||
      request.policy.prefer_surface_normal_for_features;
  SceneSurfaceQuerySnapshot surfaces;
  if (geometry_candidates_enabled) {
    surfaces = surface_snapshot(document, assets);
  }
  if (landmark_candidates_enabled) {
    refresh_projection_cache(camera, surfaces);
  }

  ViewportRay ray = camera.ray_from_viewport_point(request.mouse_local);
  ray.direction = normalize_or_zero(ray.direction);
  const QVec3 grid_normal =
      normalized_or(plane_normal(request.active_grid), {0.0f, 1.0f, 0.0f});
  const float radius =
      std::max(request.policy.snap_radius_pixels, 0.0f) *
      viewport_sanitized_pixel_scale(request.viewport_pixel_scale);
  const float radius_sq = radius * radius;

  std::optional<SceneSurfaceHit> render_surface;
  std::optional<SceneSurfaceHit> closest_surface;
  if (geometry_candidates_enabled && surfaces.valid()) {
    render_surface = surfaces.nearest_hit(
        {
            .world_origin = ray.origin,
            .world_direction = ray.direction,
            .eligibility = SceneSurfaceEligibility::RenderVisible,
        },
        raycast_scratch_);
    SceneSurfaceRaycastRequest selectable_request{
        .world_origin = ray.origin,
        .world_direction = ray.direction,
        .eligibility = SceneSurfaceEligibility::SelectableUnlocked,
    };
    if (render_surface.has_value()) {
      selectable_request.max_distance =
          render_surface->distance + kDistanceTieEpsilon;
    }
    closest_surface =
        surfaces.nearest_hit(selectable_request, raycast_scratch_);
  }

  std::array<std::optional<ScreenCandidate>, 3> best_by_landmark_kind;
  std::optional<ScreenCandidate> best_by_distance;
  if (landmark_candidates_enabled) {
    const auto landmarks = surfaces.landmarks();
    for (std::size_t landmark_index = 0;
         landmark_index < landmarks.size(); ++landmark_index) {
      const SceneSurfaceLandmark &landmark = landmarks[landmark_index];
      const ViewportPointSnapKind kind = point_snap_kind(landmark.kind);
      if (!policy_enables(kind, request.policy) || !landmark.selectable ||
          landmark.effectively_locked) {
        continue;
      }
      const std::optional<std::size_t> slot = landmark_slot(kind);
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

      const float ray_distance =
          dot(landmark.world_position - ray.origin, ray.direction);
      if (!std::isfinite(ray_distance) || ray_distance < 0.0f ||
          (render_surface.has_value() &&
           ray_distance > render_surface->distance + kDistanceTieEpsilon)) {
        continue;
      }

      QVec3 normal = landmark.world_normal;
      if (request.policy.prefer_surface_normal_for_features &&
          (kind == ViewportPointSnapKind::Vertex ||
           kind == ViewportPointSnapKind::EdgeMidpoint) &&
          render_surface.has_value() &&
          render_surface->participant == landmark.participant) {
        normal = render_surface->world_normal;
      }
      const ScreenCandidate candidate{
          .placement = make_placement(
              landmark.world_position, normal, kind,
              landmark.participant.display_entity, grid_normal),
          .screen_distance_sq = distance_sq,
          .ray_distance = ray_distance,
          .participant = landmark.participant,
          .element_key = landmark.element_key,
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
    const ViewportPointPlacementPlane surface_grid =
        surface_grid_plane(closest_surface->world_position,
                           closest_surface->world_normal);
    const QVec3 surface_position =
        snap_point_to_grid(closest_surface->world_position, surface_grid,
                           request.grid_size)
            .value_or(closest_surface->world_position);
    return make_placement(surface_position, closest_surface->world_normal,
                           ViewportPointSnapKind::Surface,
                           closest_surface->participant.display_entity,
                           grid_normal);
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

SceneSurfaceQueryStats
ViewportPointPlacementService::surface_query_stats() const noexcept {
  if (shared_surface_query_ != nullptr) {
    return shared_surface_query_->stats();
  }
  return standalone_surface_query_ ? standalone_surface_query_->stats()
                                   : SceneSurfaceQueryStats{};
}

} // namespace quader
