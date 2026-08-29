////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_curve_picking.hpp"

#include "assets/curve_asset_payload.hpp"
#include "diagnostics/profile.hpp"
#include "editor/interactions/selection_box.hpp"
#include "math/glm_bridge.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace quader {
namespace {

constexpr std::array kCurveControlPickOrder{
    CurveControlKind::Anchor, CurveControlKind::IncomingHandle,
    CurveControlKind::OutgoingHandle};

template <typename Visitor>
void visit_projected_curve_controls(const ViewportCameraController &camera,
                                    const CurveToolPreviewState &preview,
                                    bool anchors_only, Visitor &&visitor) {
  if (!preview.active || !preview.document) {
    return;
  }
  const auto points = preview.document->points();
  for (const CurveControlKind kind : kCurveControlPickOrder) {
    if (anchors_only && kind != CurveControlKind::Anchor) {
      break;
    }
    for (std::size_t index = 0; index < points.size(); ++index) {
      if (!curve_control_is_visible(preview, index, kind)) {
        continue;
      }
      const QVec3 world_position = transform_point(
          preview.world_transform,
          curve_control_local_position(points[index], kind));
      const ViewportScreenPoint screen =
          camera.project_world_point(world_position);
      if (!screen.visible) {
        continue;
      }
      visitor(CurveControlTarget{.point = points[index].id, .kind = kind},
              world_position, screen);
    }
  }
}

double screen_distance_squared(QVec2 left, QVec2 right) {
  const double dx = static_cast<double>(left.x) - right.x;
  const double dy = static_cast<double>(left.y) - right.y;
  return dx * dx + dy * dy;
}

constexpr double kCurveDepthEpsilon = 1.0e-5;
constexpr double kCurvePixelDistanceEpsilon = 1.0e-5;
constexpr double kCurveAnchorLocalPickEpsilon = 1.0e-6;
constexpr double kCurveAnchorGlobalPickEpsilon = 1.0e-8;
constexpr double kMeshComponentSurfaceOcclusionTolerance = 0.001;

bool finite_world_point(QVec3 point) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::isfinite(point.z);
}

bool curve_control_pick_hit_precedes(
    const ViewportCurveControlPickHit &candidate,
    const ViewportCurveControlPickHit &best) {
  if (std::abs(candidate.distance_pixels - best.distance_pixels) >
      kCurvePixelDistanceEpsilon) {
    return candidate.distance_pixels < best.distance_pixels;
  }
  if (std::abs(candidate.ray_distance - best.ray_distance) >
      kCurveDepthEpsilon) {
    return candidate.ray_distance < best.ray_distance;
  }
  if (candidate.target.kind != best.target.kind) {
    return static_cast<int>(candidate.target.kind) <
           static_cast<int>(best.target.kind);
  }
  return candidate.target.point < best.target.point;
}

bool curve_anchor_snap_target_precedes(
    const ViewportCurveAnchorSnapTarget &candidate,
    const ViewportCurveAnchorSnapTarget &best) {
  if (std::abs(candidate.ray_distance - best.ray_distance) >
      kCurveAnchorGlobalPickEpsilon) {
    return candidate.ray_distance < best.ray_distance;
  }
  if (std::abs(candidate.pick_distance - best.pick_distance) >
      kCurveAnchorGlobalPickEpsilon) {
    return candidate.pick_distance < best.pick_distance;
  }
  if (candidate.group != best.group) {
    return candidate.group < best.group;
  }
  return candidate.point < best.point;
}

bool curve_anchor_entity_target_precedes(
    const ViewportCurveAnchorSnapTarget &candidate,
    const ViewportCurveAnchorSnapTarget &best) {
  if (std::abs(candidate.pick_distance - best.pick_distance) >
      kCurveAnchorLocalPickEpsilon) {
    return candidate.pick_distance < best.pick_distance;
  }
  if (candidate.ray_distance != best.ray_distance) {
    return candidate.ray_distance < best.ray_distance;
  }
  return candidate.point < best.point;
}

bool ray_intersects_expanded_curve_anchor_bounds(
    const ViewportRay &ray, QVec3 bounds_min, QVec3 bounds_max,
    double padding) {
  double minimum_distance = 0.0;
  double maximum_distance = std::numeric_limits<double>::infinity();
  const auto update_axis = [&](double origin, double direction,
                               double minimum, double maximum) {
    minimum -= padding;
    maximum += padding;
    if (std::abs(direction) <= 1.0e-12) {
      return origin >= minimum && origin <= maximum;
    }
    double first = (minimum - origin) / direction;
    double second = (maximum - origin) / direction;
    if (first > second) {
      std::swap(first, second);
    }
    minimum_distance = std::max(minimum_distance, first);
    maximum_distance = std::min(maximum_distance, second);
    return maximum_distance >= minimum_distance;
  };
  return update_axis(ray.origin.x, ray.direction.x, bounds_min.x,
                     bounds_max.x) &&
         update_axis(ray.origin.y, ray.direction.y, bounds_min.y,
                     bounds_max.y) &&
         update_axis(ray.origin.z, ray.direction.z, bounds_min.z,
                     bounds_max.z);
}

void append_curve_anchor_snap_point(
    ViewportCurveAnchorSnapCandidate &group, modeling::CurvePointId point,
    QVec3 world_position) {
  if (group.anchors.empty()) {
    group.world_bounds_min = world_position;
    group.world_bounds_max = world_position;
  } else {
    group.world_bounds_min = {
        std::min(group.world_bounds_min.x, world_position.x),
        std::min(group.world_bounds_min.y, world_position.y),
        std::min(group.world_bounds_min.z, world_position.z),
    };
    group.world_bounds_max = {
        std::max(group.world_bounds_max.x, world_position.x),
        std::max(group.world_bounds_max.y, world_position.y),
        std::max(group.world_bounds_max.z, world_position.z),
    };
  }
  group.anchors.push_back({.point = point, .world_position = world_position});
}

} // namespace

std::optional<ViewportCurveControlPickHit> pick_viewport_curve_control(
    const ViewportCameraController &camera,
    const CurveToolPreviewState &preview, QVec2 local_mouse,
    bool anchors_only, double radius_pixels,
    std::optional<double> maximum_ray_distance) {
  if (!std::isfinite(radius_pixels) || radius_pixels <= 0.0 ||
      !std::isfinite(local_mouse.x) || !std::isfinite(local_mouse.y) ||
      (maximum_ray_distance.has_value() &&
       (!std::isfinite(*maximum_ray_distance) ||
        *maximum_ray_distance < 0.0))) {
    return std::nullopt;
  }
  const QVec2 viewport_size = camera.viewport_size();
  if (local_mouse.x < 0.0f || local_mouse.y < 0.0f ||
      local_mouse.x > viewport_size.x || local_mouse.y > viewport_size.y) {
    return std::nullopt;
  }

  const ViewportRay ray = camera.ray_from_viewport_point(local_mouse);
  const double radius_squared = radius_pixels * radius_pixels;
  std::optional<ViewportCurveControlPickHit> closest;
  visit_projected_curve_controls(
      camera, preview, anchors_only,
      [&](CurveControlTarget target, QVec3 world_position,
          const ViewportScreenPoint &screen) {
        if (!finite_world_point(world_position) ||
            !std::isfinite(screen.position.x) ||
            !std::isfinite(screen.position.y)) {
          return;
        }
        const double distance_squared =
            screen_distance_squared(screen.position, local_mouse);
        if (!std::isfinite(distance_squared) ||
            distance_squared > radius_squared) {
          return;
        }
        const double ray_distance = static_cast<double>(
            dot(world_position - ray.origin, ray.direction));
        if (!std::isfinite(ray_distance) || ray_distance < 0.0 ||
            (maximum_ray_distance.has_value() &&
             ray_distance > *maximum_ray_distance +
                                kMeshComponentSurfaceOcclusionTolerance)) {
          return;
        }
        ViewportCurveControlPickHit hit{
            .target = target,
            .world_position = world_position,
            .ray_distance = ray_distance,
            .distance_pixels = std::sqrt(distance_squared),
        };
        if (!closest.has_value() ||
            curve_control_pick_hit_precedes(hit, *closest)) {
          closest = hit;
        }
      });
  return closest;
}

std::optional<ViewportCurveControlPickHit>
pick_unoccluded_viewport_curve_control(
    const ViewportCameraController &camera,
    const CurveToolPreviewState &preview, QVec2 local_mouse,
    const std::function<std::optional<double>(const ViewportRay &)>
        &mesh_surface_distance,
    bool anchors_only, double radius_pixels) {
  auto hit = pick_viewport_curve_control(camera, preview, local_mouse,
                                         anchors_only, radius_pixels);
  if (!hit.has_value() || !mesh_surface_distance) {
    return hit;
  }
  const ViewportRay ray = camera.ray_from_viewport_point(local_mouse);
  const std::optional<double> mesh_distance = mesh_surface_distance(ray);
  if (!mesh_distance.has_value() || !std::isfinite(*mesh_distance) ||
      *mesh_distance < 0.0 ||
      hit->ray_distance <=
          *mesh_distance + kMeshComponentSurfaceOcclusionTolerance) {
    return hit;
  }
  return pick_viewport_curve_control(camera, preview, local_mouse,
                                     anchors_only, radius_pixels,
                                     mesh_distance);
}

std::vector<CurveControlTarget> box_pick_viewport_curve_controls(
    const ViewportCameraController &camera,
    const CurveToolPreviewState &preview, QVec2 start, QVec2 end) {
  std::vector<CurveControlTarget> hits;
  if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
      !std::isfinite(end.x) || !std::isfinite(end.y)) {
    return hits;
  }
  const quader_editor_core::ScreenRect rect =
      quader_editor_core::normalized_screen_rect(start, end);
  visit_projected_curve_controls(
      camera, preview, false,
      [&](CurveControlTarget target, QVec3,
          const ViewportScreenPoint &screen) {
        if (quader_editor_core::screen_rect_contains(rect, screen.position)) {
          hits.push_back(target);
        }
      });
  return hits;
}

std::vector<ViewportCurveAnchorSnapCandidate>
collect_viewport_curve_anchor_snap_candidates(
    std::span<const ViewportSceneCurvePickCandidate> candidates,
    const ViewportCurveAnchorSnapExclusion &is_excluded) {
  std::vector<ViewportCurveAnchorSnapCandidate> groups;
  groups.reserve(candidates.size());
  for (const auto &candidate : candidates) {
    if (candidate.entity == kInvalidEntityId || candidate.document == nullptr) {
      continue;
    }
    ViewportCurveAnchorSnapCandidate group;
    group.group = ViewportCurveAnchorSnapGroupIdentity::scene(candidate.entity);
    group.anchors.reserve(candidate.document->point_count());
    for (const modeling::CurveControlPoint &point : candidate.document->points()) {
      if (!point.id.valid() ||
          (is_excluded && is_excluded(candidate.entity, point.id))) {
        continue;
      }
      const QVec3 world_position = transform_point(
          candidate.world_transform,
          {point.position.x, point.position.y, point.position.z});
      if (!finite_world_point(world_position)) {
        continue;
      }
      append_curve_anchor_snap_point(group, point.id, world_position);
    }
    if (!group.anchors.empty()) {
      groups.push_back(std::move(group));
    }
  }
  std::sort(groups.begin(), groups.end(),
            [](const ViewportCurveAnchorSnapCandidate &left,
               const ViewportCurveAnchorSnapCandidate &right) {
              return left.group < right.group;
            });
  return groups;
}

std::optional<ViewportCurveAnchorSnapCandidate>
collect_viewport_curve_draft_anchor_snap_candidate(
    const CurveToolPreviewState &preview,
    const ViewportCurveDraftAnchorSnapExclusion &is_excluded) {
  if (!preview.active || !preview.drawing || !preview.document.has_value()) {
    return std::nullopt;
  }
  ViewportCurveAnchorSnapCandidate group;
  group.group = ViewportCurveAnchorSnapGroupIdentity::active_draft();
  group.anchors.reserve(preview.document->point_count());
  for (const modeling::CurveControlPoint &point : preview.document->points()) {
    if (!point.id.valid() || (is_excluded && is_excluded(point.id))) {
      continue;
    }
    const QVec3 world_position = transform_point(
        preview.world_transform,
        {point.position.x, point.position.y, point.position.z});
    if (finite_world_point(world_position)) {
      append_curve_anchor_snap_point(group, point.id, world_position);
    }
  }
  return group.anchors.empty()
             ? std::nullopt
             : std::optional<ViewportCurveAnchorSnapCandidate>{
                   std::move(group)};
}

std::optional<ViewportCurveAnchorSnapTarget>
resolve_viewport_curve_anchor_snap_target(
    const ViewportRay &normalized_world_ray,
    std::span<const ViewportCurveAnchorSnapCandidate> candidates,
    double world_radius,
    const ViewportCurveAnchorSnapExclusion &is_excluded,
    std::optional<double> maximum_ray_distance,
    ViewportCurveAnchorSnapWorkCounters *work_counters) {
  if (!finite_world_point(normalized_world_ray.origin) ||
      !finite_world_point(normalized_world_ray.direction) ||
      !std::isfinite(world_radius) || world_radius <= 0.0 ||
      (maximum_ray_distance.has_value() &&
       (!std::isfinite(*maximum_ray_distance) ||
        *maximum_ray_distance < 0.0))) {
    return std::nullopt;
  }
  const double direction_length_squared =
      static_cast<double>(length_squared(normalized_world_ray.direction));
  if (std::abs(direction_length_squared - 1.0) > 1.0e-4) {
    return std::nullopt;
  }

  const double radius_squared = world_radius * world_radius;
  std::optional<ViewportCurveAnchorSnapTarget> best;
  for (const auto &group : candidates) {
    if (work_counters != nullptr) {
      ++work_counters->entity_bounds_tested;
    }
    if (!group.group.valid() || group.anchors.empty() ||
        !finite_world_point(group.world_bounds_min) ||
        !finite_world_point(group.world_bounds_max) ||
        !ray_intersects_expanded_curve_anchor_bounds(
            normalized_world_ray, group.world_bounds_min,
            group.world_bounds_max, world_radius)) {
      continue;
    }
    std::optional<ViewportCurveAnchorSnapTarget> entity_best;
    for (const auto &candidate : group.anchors) {
      if (work_counters != nullptr) {
        ++work_counters->anchors_tested;
      }
      if (!candidate.point.valid() ||
          !finite_world_point(candidate.world_position) ||
          (group.group.kind ==
               ViewportCurveAnchorSnapGroupKind::SceneEntity &&
           is_excluded &&
           is_excluded(group.group.scene_entity, candidate.point))) {
        continue;
      }
      const QVec3 from_origin =
          candidate.world_position - normalized_world_ray.origin;
      const double ray_distance = static_cast<double>(
          dot(from_origin, normalized_world_ray.direction));
      if (!std::isfinite(ray_distance) || ray_distance < 0.0 ||
          (maximum_ray_distance.has_value() &&
           ray_distance > *maximum_ray_distance +
                              kMeshComponentSurfaceOcclusionTolerance)) {
        continue;
      }
      const QVec3 closest =
          normalized_world_ray.origin +
          normalized_world_ray.direction * static_cast<float>(ray_distance);
      const QVec3 offset = candidate.world_position - closest;
      const double distance_squared =
          static_cast<double>(length_squared(offset));
      if (!std::isfinite(distance_squared) ||
          distance_squared > radius_squared) {
        continue;
      }
      ViewportCurveAnchorSnapTarget target;
      target.group = group.group;
      target.entity = group.group.scene_entity;
      target.point = candidate.point;
      target.world_position = candidate.world_position;
      target.ray_distance = ray_distance;
      target.pick_distance = std::sqrt(distance_squared);
      if (!entity_best.has_value() ||
          curve_anchor_entity_target_precedes(target, *entity_best)) {
        entity_best = target;
      }
    }
    if (entity_best.has_value() &&
        (!best.has_value() ||
         curve_anchor_snap_target_precedes(*entity_best, *best))) {
      best = entity_best;
    }
  }
  return best;
}

std::optional<ViewportCurvePickHit> pick_viewport_curve(
    const ViewportCameraController &camera, QVec2 local_mouse,
    std::span<const ViewportSceneCurvePickCandidate> candidates,
    double radius_pixels, std::optional<double> maximum_ray_distance) {
  if (!std::isfinite(radius_pixels) || radius_pixels <= 0.0 ||
      !std::isfinite(local_mouse.x) || !std::isfinite(local_mouse.y) ||
      (maximum_ray_distance.has_value() &&
       (!std::isfinite(*maximum_ray_distance) || *maximum_ray_distance < 0.0))) {
    return std::nullopt;
  }
  const QVec2 viewport_size = camera.viewport_size();
  if (local_mouse.x < 0.0f || local_mouse.y < 0.0f ||
      local_mouse.x > viewport_size.x || local_mouse.y > viewport_size.y) {
    return std::nullopt;
  }
  QDR_PROFILE_SCOPE("Viewport pick sampled curve");
  const ViewportRay ray = camera.ray_from_viewport_point(local_mouse);
  const double radius_squared = radius_pixels * radius_pixels;
  std::optional<ViewportCurvePickHit> best;
  for (const auto &candidate : candidates) {
    if (candidate.entity == kInvalidEntityId || candidate.sample_payload == nullptr) {
      continue;
    }
    const auto &samples = candidate.sample_payload->samples;
    for (std::size_t index = 1; index < samples.size(); ++index) {
      const auto &a = samples[index - 1u].position;
      const auto &b = samples[index].position;
      const QVec3 start = transform_point(candidate.world_transform, {a.x, a.y, a.z});
      const QVec3 end = transform_point(candidate.world_transform, {b.x, b.y, b.z});
      if (start.x == end.x && start.y == end.y && start.z == end.z) {
        continue;
      }
      const auto projected = camera.project_visible_world_segment(start, end);
      if (!projected.has_value()) {
        continue;
      }
      const double dx = static_cast<double>(projected->end.x) - projected->start.x;
      const double dy = static_cast<double>(projected->end.y) - projected->start.y;
      const double length_squared = dx * dx + dy * dy;
      const double parameter = length_squared > 1.0e-18
          ? std::clamp(((local_mouse.x - projected->start.x) * dx +
                        (local_mouse.y - projected->start.y) * dy) / length_squared,
                       0.0, 1.0)
          : (projected->start_depth <= projected->end_depth ? 0.0 : 1.0);
      const double distance_x = local_mouse.x - std::lerp(
          static_cast<double>(projected->start.x), projected->end.x, parameter);
      const double distance_y = local_mouse.y - std::lerp(
          static_cast<double>(projected->start.y), projected->end.y, parameter);
      const double distance_squared = distance_x * distance_x + distance_y * distance_y;
      if (distance_squared > radius_squared) {
        continue;
      }
      const double world_parameter = camera.state().projection == ViewportProjection::Orthographic
          ? parameter
          : parameter * projected->start_depth /
                std::lerp(projected->end_depth, projected->start_depth, parameter);
      const double original_parameter = std::lerp(
          projected->start_parameter, projected->end_parameter, world_parameter);
      const QVec3 position{
          static_cast<float>(std::lerp(static_cast<double>(start.x), end.x, original_parameter)),
          static_cast<float>(std::lerp(static_cast<double>(start.y), end.y, original_parameter)),
          static_cast<float>(std::lerp(static_cast<double>(start.z), end.z, original_parameter)),
      };
      const double distance = (static_cast<double>(position.x) - ray.origin.x) * ray.direction.x +
                              (static_cast<double>(position.y) - ray.origin.y) * ray.direction.y +
                              (static_cast<double>(position.z) - ray.origin.z) * ray.direction.z;
      if (!std::isfinite(distance) || distance < 0.0 ||
          (maximum_ray_distance.has_value() &&
           distance > *maximum_ray_distance + kCurveDepthEpsilon)) {
        continue;
      }
      const double pixels = std::sqrt(distance_squared);
      // Prefer the closest stroke; intersections and subpixel ties prefer the
      // nearest visible depth. Entity identity breaks exact ties deterministically.
      if (!best.has_value() || pixels < best->distance_pixels - 0.25 ||
          (std::abs(pixels - best->distance_pixels) <= 0.25 &&
           (distance < best->ray_distance - kCurveDepthEpsilon ||
            (std::abs(distance - best->ray_distance) <= kCurveDepthEpsilon &&
             candidate.entity < best->entity)))) {
        best = ViewportCurvePickHit{candidate.entity, position, distance, pixels};
      }
    }
  }
  return best;
}

} // namespace quader
