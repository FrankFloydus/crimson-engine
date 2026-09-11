////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_camera_controller.hpp"

#include "core/camera_pose_validation.hpp"
#include "diagnostics/profile.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_clip_planes.hpp"

#include <editor/interactions/viewport_navigation.hpp>
#include <editor/interactions/viewport_projection.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>

namespace quader {
namespace {

constexpr float kFlyMaxDeltaSeconds = 0.25f;
constexpr float kFlySpeedWheelStep = 1.0f;
constexpr float kMinCameraDistance = static_cast<float>(
    quader_editor_core::kNavigationMinCameraDistance);
constexpr float kMaxCameraDistance = static_cast<float>(
    quader_editor_core::kNavigationMaxCameraDistance);
constexpr float kMinOrthographicZoomSize = static_cast<float>(
    quader_editor_core::kNavigationMinOrthographicSize);
constexpr float kMaxOrthographicZoomSize = static_cast<float>(
    quader_editor_core::kNavigationMaxOrthographicZoomOutSize);
constexpr float kMinimumRenderablePerspectiveDistance =
    kViewportCameraMinimumNearPlane * 2.0f;
constexpr float kSevereDepthRepairRatio = 16.0f;

float vertical_fov_radians(float fov_degrees) {
  return fov_degrees * std::numbers::pi_v<float> / 180.0f;
}

float perspective_visible_height(const ViewportCameraState &state) {
  const float distance = std::isfinite(state.distance) && state.distance > 0.0f
                             ? state.distance
                             : kMinCameraDistance;
  return std::max(
      0.0001f,
      2.0f * distance *
          std::tan(vertical_fov_radians(state.fov_degrees) * 0.5f));
}

float perspective_distance_for_visible_height(float visible_height,
                                              float fov_degrees) {
  const float half_fov_tangent =
      std::tan(vertical_fov_radians(fov_degrees) * 0.5f);
  if (half_fov_tangent <= 0.000001f) {
    return kMinCameraDistance;
  }
  return std::max(0.0001f, visible_height) / (2.0f * half_fov_tangent);
}

float current_visible_height(const ViewportCameraState &state) {
  return state.projection == ViewportProjection::Orthographic
             ? std::max(0.0001f, state.orthographic_size)
             : perspective_visible_height(state);
}

void sync_perspective_distance_from_orthographic_zoom(
    ViewportCameraState &state) {
  if (state.projection == ViewportProjection::Orthographic) {
    state.distance = perspective_distance_for_visible_height(
        state.orthographic_size, state.fov_degrees);
  }
}

QVec3 forward_from_angles(float yaw_radians, float pitch_radians) {
  const float cp = std::cos(pitch_radians);
  return normalize({
      std::sin(yaw_radians) * cp,
      std::sin(pitch_radians),
      -std::cos(yaw_radians) * cp,
  });
}

QVec3 fallback_up_for_forward(QVec3 forward) {
  if (std::abs(forward.y) > 0.999f) {
    return forward.y < 0.0f ? QVec3{0.0f, 0.0f, 1.0f}
                            : QVec3{0.0f, 0.0f, -1.0f};
  }
  return {0.0f, 1.0f, 0.0f};
}

[[nodiscard]] std::uint32_t
canonical_persistence_float_bits(float value) noexcept {
  if (value == 0.0f) {
    value = 0.0f;
  }
  return std::bit_cast<std::uint32_t>(value);
}

void append_persistence_float(std::uint64_t &hash, float value) noexcept {
  const std::uint32_t bits = canonical_persistence_float_bits(value);
  for (unsigned int shift = 0u; shift < 32u; shift += 8u) {
    hash ^= static_cast<std::uint8_t>((bits >> shift) & 0xffu);
    hash *= 1099511628211ull;
  }
}

/**
 * Represents a Camera Basis value used by the viewport input, layout, and camera layer.
 */
struct CameraBasis {
    QVec3 forward;
    QVec3 up;
};

CameraBasis make_camera_basis(QVec3 forward_direction, QVec3 up_direction,
                              QVec3 fallback_forward = {0.0f, 0.0f, -1.0f}) {
  QVec3 forward = normalize(forward_direction);
  if (length(forward) <= 0.000001f) {
    forward = normalize(fallback_forward);
  }
  if (length(forward) <= 0.000001f) {
    forward = {0.0f, 0.0f, -1.0f};
  }

  QVec3 candidate_up = normalize(up_direction);
  if (length(candidate_up) <= 0.000001f ||
      std::abs(dot(candidate_up, forward)) > 0.999f) {
    candidate_up = fallback_up_for_forward(forward);
  }

  QVec3 right = normalize(cross(forward, candidate_up));
  if (length(right) <= 0.000001f) {
    right = {1.0f, 0.0f, 0.0f};
  }

  QVec3 up = normalize(cross(right, forward));
  if (length(up) <= 0.000001f) {
    up = fallback_up_for_forward(forward);
  }

  return {.forward = forward, .up = up};
}

void sync_angles_for_forward(ViewportCameraState &state, QVec3 forward) {
  state.pitch_radians = std::asin(clamp(forward.y, -1.0f, 1.0f));
  state.yaw_radians = std::atan2(forward.x, -forward.z);
}

quader::QVec2 to_runtime(QVec2 value) { return {value.x, value.y}; }

quader::QVec3 to_runtime(QVec3 value) { return {value.x, value.y, value.z}; }

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

quader_editor_core::ViewAxis to_runtime(ViewportViewAxis axis) {
  switch (axis) {
  case ViewportViewAxis::X:
    return quader_editor_core::ViewAxis::X;
  case ViewportViewAxis::Y:
    return quader_editor_core::ViewAxis::Y;
  case ViewportViewAxis::Z:
    return quader_editor_core::ViewAxis::Z;
  }
  return quader_editor_core::ViewAxis::Z;
}

quader_poly::Transform3 make_runtime_transform(const ViewportCameraPose &pose) {
  QVec3 forward = normalize(pose.target - pose.eye);
  if (length(forward) <= 0.000001f) {
    forward = {0.0f, 0.0f, -1.0f};
  }

  QVec3 right = normalize(cross(forward, pose.up));
  if (length(right) <= 0.000001f) {
    right = {1.0f, 0.0f, 0.0f};
  }

  QVec3 up = normalize(cross(right, forward));
  if (length(up) <= 0.000001f) {
    up = {0.0f, 1.0f, 0.0f};
  }

  return {
      .x_axis = to_runtime(right),
      .y_axis = to_runtime(up),
      .z_axis = to_runtime(forward * -1.0f),
      .origin = to_runtime(pose.eye),
  };
}

ViewportCameraPose
pose_from_runtime_transform(const quader_poly::Transform3 &transform,
                            float distance) {
  const QVec3 origin = from_runtime(transform.origin);
  QVec3 forward = normalize(from_runtime(transform.z_axis) * -1.0f);
  if (length(forward) <= 0.000001f) {
    forward = {0.0f, 0.0f, -1.0f};
  }

  QVec3 up = normalize(from_runtime(transform.y_axis));
  if (length(up) <= 0.000001f) {
    up = {0.0f, 1.0f, 0.0f};
  }

  return {
      .eye = origin,
      .target = origin + forward * std::max(kMinCameraDistance, distance),
      .up = up,
  };
}

quader_editor_core::ViewportCameraState
make_runtime_camera(const ViewportCameraPose &pose,
                    const ViewportCameraState &state, float viewport_width,
                    float viewport_height) {
  quader_editor_core::ViewportCameraState camera;
  camera.projection = state.projection == ViewportProjection::Orthographic
                          ? quader_editor_core::CameraProjection::Orthographic
                          : quader_editor_core::CameraProjection::Perspective;
  camera.transform = make_runtime_transform(pose);
  camera.viewport_size = {std::max(viewport_width, 1.0f),
                          std::max(viewport_height, 1.0f)};
  camera.orthographic_size = std::max(0.0001f, state.orthographic_size);
  camera.fov_degrees = state.fov_degrees;
  return camera;
}

float distance_along_forward_to_pivot(QVec3 origin, QVec3 forward,
                                      QVec3 pivot) {
  const float forward_distance = dot(pivot - origin, forward);
  if (std::isfinite(forward_distance) && forward_distance > 0.0f) {
    return forward_distance;
  }
  const float radial_distance = length(pivot - origin);
  return std::isfinite(radial_distance) && radial_distance > 0.0f
             ? radial_distance
             : kMinCameraDistance;
}

void apply_runtime_transform(
    ViewportCameraState &state, QVec3 &pivot, QVec3 &camera_forward,
    QVec3 &camera_up,
    const quader_editor_core::ViewportNavigationResult &result,
    std::optional<float> preserved_view_distance = std::nullopt) {
  if (!result.ok) {
    return;
  }

  const QVec3 origin = from_runtime(result.camera_transform.origin);
  QVec3 forward =
      normalize(from_runtime(result.camera_transform.z_axis) * -1.0f);
  if (length(forward) <= 0.000001f) {
    forward = forward_from_angles(state.yaw_radians, state.pitch_radians);
  }
  const CameraBasis basis = make_camera_basis(
      forward, from_runtime(result.camera_transform.y_axis), camera_forward);

  pivot = from_runtime(result.pivot);
  const float requested_view_distance = preserved_view_distance.value_or(
      distance_along_forward_to_pivot(origin, basis.forward, pivot));
  const float view_distance = std::isfinite(requested_view_distance) &&
                                      requested_view_distance > 0.0f
                                  ? requested_view_distance
                                  : kMinCameraDistance;

  camera_forward = basis.forward;
  camera_up = basis.up;
  state.target = origin + camera_forward * view_distance;
  state.distance = view_distance;
  // ViewportCameraState::target is the visible center of the camera and is
  // the controller's canonical navigation pivot. Runtime operations may
  // return an off-axis cursor target or retain the pre-fly-look pivot; never
  // let either become a second, hidden center of rotation.
  pivot = state.target;
  if (result.has_camera_size) {
    state.orthographic_size =
        static_cast<float>(std::max(0.0001, result.camera_size));
  }
  sync_angles_for_forward(state, camera_forward);
}

QVec3 fallback_forward_for_axis(ViewportViewAxis axis, int sign) {
  const float value = sign < 0 ? 1.0f : -1.0f;
  switch (axis) {
  case ViewportViewAxis::X:
    return {value, 0.0f, 0.0f};
  case ViewportViewAxis::Y:
    return {0.0f, value, 0.0f};
  case ViewportViewAxis::Z:
    return {0.0f, 0.0f, value};
  }
  return {0.0f, 0.0f, -1.0f};
}

} // namespace

bool viewport_camera_pose_is_valid(
    const ViewportCameraPose &pose) noexcept {
  return camera_pose_basis_is_valid(pose.eye, pose.target, pose.up);
}

std::uint64_t viewport_camera_pose_persistence_signature(
    const ViewportCameraPose &pose) noexcept {
  std::uint64_t hash = 14695981039346656037ull;
  const QVec3 values[] = {pose.eye, pose.target, pose.up};
  for (const QVec3 value : values) {
    append_persistence_float(hash, value.x);
    append_persistence_float(hash, value.y);
    append_persistence_float(hash, value.z);
  }
  return hash;
}

ViewportCameraController::ViewportCameraController() {
  const ViewportHostDefaults defaults = viewport_host_defaults();
  state_.orthographic_size =
      static_cast<float>(defaults.default_orthographic_size);
  state_.fov_degrees = static_cast<float>(defaults.default_fov_degrees);
  default_fly_speed_ =
      viewport_sanitize_fly_navigation_speed(
          static_cast<float>(defaults.fly_base_speed));
  fly_speed_ = default_fly_speed_;
  configured_near_plane_ = state_.near_plane;
  configured_far_plane_ = state_.far_plane;
  sync_basis_from_angles();
  establish_zoom_envelope_from_current_view();
  apply_clip_plane_constraints();
}

void ViewportCameraController::set_viewport_size(float width, float height) {
  viewport_width_ = std::max(1.0f, width);
  viewport_height_ = std::max(1.0f, height);
}

void ViewportCameraController::begin_drag(
    QVec2 mouse, ViewportDragMode mode,
    std::optional<QVec3> navigation_anchor,
    bool navigation_anchor_repair_only) {
  // Latch the scene depth sampled at gesture start. The first accepted motion
  // rebases the canonical target without changing eye or orientation, so a
  // click/cancel remains state-neutral. Off-axis orbit anchors stay separate
  // and gesture-local.
  pivot_ = state_.target;
  pending_navigation_anchor_.reset();
  pending_navigation_anchor_repair_only_ = false;
  orbit_anchor_.reset();
  if (navigation_anchor.has_value() &&
      (mode == ViewportDragMode::Orbit ||
       mode == ViewportDragMode::OrbitSnap || mode == ViewportDragMode::Pan)) {
    pending_navigation_anchor_ = navigation_anchor;
    pending_navigation_anchor_repair_only_ =
        navigation_anchor_repair_only;
  }
  drag_mode_ = mode;
  last_mouse_ = mouse;
  clear_orbit_snap_source();
  if (mode == ViewportDragMode::Fly) {
    const bool converting_from_orthographic =
        state_.projection == ViewportProjection::Orthographic;
    sync_perspective_distance_from_orthographic_zoom(state_);
    state_.projection = ViewportProjection::Perspective;
    orthographic_view_locked_ = false;
    orbit_snap_orthographic_projection_ = false;
    if (converting_from_orthographic) {
      ensure_perspective_target_within_clip_range();
      establish_zoom_envelope_from_current_view();
    }
    apply_clip_plane_constraints();
  }
}

void ViewportCameraController::update_drag(QVec2 mouse) {
  update_drag(mouse, drag_mode_);
}

void ViewportCameraController::update_drag(QVec2 mouse, ViewportDragMode mode) {
  const QVec2 delta{mouse.x - last_mouse_.x, mouse.y - last_mouse_.y};
  last_mouse_ = mouse;
  update_drag_delta(delta, mode);
}

void ViewportCameraController::update_drag_delta(QVec2 delta,
                                                 ViewportDragMode mode) {
  const ViewportDragMode previous_mode = drag_mode_;
  if (orthographic_view_locked_ &&
      (mode == ViewportDragMode::Orbit ||
       mode == ViewportDragMode::OrbitSnap)) {
    // Locked orthographic panes reject orbit as a whole. In particular, do
    // not accept a pending scene-depth anchor before discovering the lock,
    // because that would move the camera even though no orbit is allowed.
    drag_mode_ = mode;
    state_.projection = ViewportProjection::Orthographic;
    orbit_snap_orthographic_projection_ = false;
    pending_navigation_anchor_.reset();
    pending_navigation_anchor_repair_only_ = false;
    orbit_anchor_.reset();
    clear_orbit_snap_source();
    return;
  }
  const bool entering_orbit_snap =
      previous_mode != ViewportDragMode::OrbitSnap &&
      mode == ViewportDragMode::OrbitSnap;
  const bool input_has_motion = std::abs(delta.x) > 0.000001f ||
                                std::abs(delta.y) > 0.000001f;
  if (!input_has_motion && previous_mode == mode) {
    return;
  }
  if (entering_orbit_snap) {
    if (input_has_motion && pending_navigation_anchor_.has_value()) {
      orbit_anchor_ = accept_pending_navigation_anchor();
    }
    delta = {};
  }
  if (previous_mode == ViewportDragMode::OrbitSnap &&
      mode != ViewportDragMode::OrbitSnap) {
    clear_orbit_snap_source();
  }
  drag_mode_ = mode;
  update_drag_delta(delta);
}

void ViewportCameraController::update_drag_delta(QVec2 delta) {
  const bool pointer_navigation =
      drag_mode_ == ViewportDragMode::Orbit ||
      drag_mode_ == ViewportDragMode::OrbitSnap ||
      drag_mode_ == ViewportDragMode::Pan;
  const bool has_motion = std::abs(delta.x) > 0.000001f ||
                          std::abs(delta.y) > 0.000001f;
  if (orthographic_view_locked_ &&
      (drag_mode_ == ViewportDragMode::Orbit ||
       drag_mode_ == ViewportDragMode::OrbitSnap)) {
    state_.projection = ViewportProjection::Orthographic;
    orbit_snap_orthographic_projection_ = false;
    pending_navigation_anchor_.reset();
    pending_navigation_anchor_repair_only_ = false;
    orbit_anchor_.reset();
    clear_orbit_snap_source();
    return;
  }
  if (pointer_navigation && !has_motion &&
      pending_navigation_anchor_.has_value()) {
    return;
  }
  if (pointer_navigation && has_motion &&
      pending_navigation_anchor_.has_value()) {
    const std::optional<QVec3> accepted_anchor =
        accept_pending_navigation_anchor();
    if (accepted_anchor.has_value() &&
        (drag_mode_ == ViewportDragMode::Orbit ||
         drag_mode_ == ViewportDragMode::OrbitSnap)) {
      orbit_anchor_ = accepted_anchor;
    }
  }

  if (drag_mode_ == ViewportDragMode::Orbit ||
      drag_mode_ == ViewportDragMode::OrbitSnap) {
    QDR_PROFILE_SCOPE("Viewport camera orbit");
    const ViewportHostDefaults defaults = viewport_host_defaults();
    quader_editor_core::ViewportOrbitInput input;
    const bool snap_to_orthographic = drag_mode_ == ViewportDragMode::OrbitSnap;
    if (!snap_to_orthographic) {
      clear_orbit_snap_source();
    }
    const bool converting_from_orthographic =
        state_.projection == ViewportProjection::Orthographic;
    const float snapped_orthographic_size = current_visible_height(state_);
    sync_perspective_distance_from_orthographic_zoom(state_);
    const ViewportCameraPose orbit_input_pose =
        snap_to_orthographic && has_orbit_snap_source_
            ? orbit_snap_source_pose_
            : pose();
    input.camera_transform = make_runtime_transform(orbit_input_pose);
    input.pivot = to_runtime(orbit_input_pose.target);
    if (orbit_anchor_.has_value()) {
      input.has_orbit_anchor = true;
      input.orbit_anchor = to_runtime(*orbit_anchor_);
    }
    input.screen_delta = to_runtime(delta);
    input.sensitivity = defaults.orbit_radians_per_pixel;
    input.snap_to_orthogonal = snap_to_orthographic;
    const quader_editor_core::ViewportNavigationResult orbit_result =
        quader_editor_core::viewport_orbit(input);
    apply_runtime_transform(state_, pivot_, camera_forward_, camera_up_,
                            orbit_result);
    if (snap_to_orthographic && orbit_result.ok) {
      orbit_snap_source_pose_ = pose_from_runtime_transform(
          orbit_result.unsnapped_camera_transform, state_.distance);
      has_orbit_snap_source_ = true;
    } else {
      clear_orbit_snap_source();
    }
    if (snap_to_orthographic && orbit_result.orthogonal_snap) {
      state_.projection = ViewportProjection::Orthographic;
      state_.orthographic_size = snapped_orthographic_size;
      orthographic_view_locked_ = false;
      orbit_snap_orthographic_projection_ = true;
    } else {
      state_.projection = ViewportProjection::Perspective;
      orthographic_view_locked_ = false;
      orbit_snap_orthographic_projection_ = false;
    }
    if (converting_from_orthographic &&
        state_.projection == ViewportProjection::Perspective) {
      ensure_perspective_target_within_clip_range();
    }
    if (converting_from_orthographic ||
        (snap_to_orthographic && orbit_result.orthogonal_snap)) {
      establish_zoom_envelope_from_current_view();
    }
  } else if (drag_mode_ == ViewportDragMode::Pan) {
    QDR_PROFILE_SCOPE("Viewport camera pan");
    quader_editor_core::ViewportPanInput input;
    input.camera =
        make_runtime_camera(pose(), state_, viewport_width_, viewport_height_);
    input.pivot = to_runtime(pivot_);
    input.screen_delta = to_runtime(delta);
    apply_runtime_transform(state_, pivot_, camera_forward_, camera_up_,
                            quader_editor_core::viewport_pan(input));
  } else if (drag_mode_ == ViewportDragMode::Fly) {
    QDR_PROFILE_SCOPE("Viewport camera fly look");
    const ViewportHostDefaults defaults = viewport_host_defaults();
    quader_editor_core::ViewportFlyLookInput input;
    input.camera_transform = make_runtime_transform(pose());
    input.pivot = to_runtime(pivot_);
    input.screen_delta = to_runtime(delta);
    input.sensitivity = defaults.fly_look_radians_per_pixel;
    apply_runtime_transform(state_, pivot_, camera_forward_, camera_up_,
                            quader_editor_core::viewport_fly_look(input),
                            state_.distance);
    state_.projection = ViewportProjection::Perspective;
    orthographic_view_locked_ = false;
    orbit_snap_orthographic_projection_ = false;
    clear_orbit_snap_source();
  }
  apply_clip_plane_constraints();
}

void ViewportCameraController::end_drag() {
  drag_mode_ = ViewportDragMode::None;
  // Fly look changes the visible target without rotating its runtime pivot.
  // Make the completed camera pose the sole source for the next gesture.
  pivot_ = state_.target;
  pending_navigation_anchor_.reset();
  pending_navigation_anchor_repair_only_ = false;
  orbit_anchor_.reset();
  clear_orbit_snap_source();
}

void ViewportCameraController::scroll(
    float wheel_delta, std::optional<QVec3> navigation_anchor) {
  if (std::abs(wheel_delta) <= 0.000001f) {
    return;
  }
  if (navigation_anchor.has_value()) {
    (void)repair_navigation_depth(*navigation_anchor);
  }
  QDR_PROFILE_SCOPE("Viewport camera zoom");
  quader_editor_core::ViewportZoomInput input;
  input.camera =
      make_runtime_camera(pose(), state_, viewport_width_, viewport_height_);
  input.pivot = to_runtime(pivot_);
  input.limits.minimum_perspective_distance =
      minimum_perspective_zoom_distance_;
  input.limits.maximum_perspective_distance =
      maximum_perspective_zoom_distance_;
  input.limits.minimum_orthographic_size =
      minimum_orthographic_zoom_size_;
  input.limits.maximum_orthographic_size =
      maximum_orthographic_zoom_size_;
  input.steps = wheel_delta;
  apply_runtime_transform(state_, pivot_, camera_forward_, camera_up_,
                          quader_editor_core::viewport_zoom(input));
  extend_zoom_envelope_to_current_view();
  apply_clip_plane_constraints();
}

void ViewportCameraController::scroll_to_cursor(float wheel_delta,
                                                QVec2 local_mouse,
                                                std::optional<QVec3>
                                                    navigation_anchor) {
  if (std::abs(wheel_delta) <= 0.000001f) {
    return;
  }
  if (navigation_anchor.has_value()) {
    (void)repair_navigation_depth(*navigation_anchor);
  }

  QDR_PROFILE_SCOPE("Viewport camera zoom to cursor");
  const ViewportRay ray = ray_from_viewport_point(local_mouse);
  quader_editor_core::ViewportZoomToCursorInput input;
  input.camera =
      make_runtime_camera(pose(), state_, viewport_width_, viewport_height_);
  input.pivot = to_runtime(pivot_);
  input.ray_origin = to_runtime(ray.origin);
  input.ray_direction = to_runtime(ray.direction);
  input.has_cursor_target = false;
  input.limits.minimum_perspective_distance =
      minimum_perspective_zoom_distance_;
  input.limits.maximum_perspective_distance =
      maximum_perspective_zoom_distance_;
  input.limits.minimum_orthographic_size =
      minimum_orthographic_zoom_size_;
  input.limits.maximum_orthographic_size =
      maximum_orthographic_zoom_size_;
  input.steps = wheel_delta;
  apply_runtime_transform(state_, pivot_, camera_forward_, camera_up_,
                          quader_editor_core::viewport_zoom_to_cursor(input));
  extend_zoom_envelope_to_current_view();
  apply_clip_plane_constraints();
}

void ViewportCameraController::focus_origin(float distance) {
  clear_orbit_snap_source();
  pending_navigation_anchor_.reset();
  pending_navigation_anchor_repair_only_ = false;
  orbit_anchor_.reset();
  transient_far_plane_floor_ = 0.0f;
  pivot_ = {};
  state_.target = {};
  state_.distance =
      clamp(distance, kMinCameraDistance, kMaxCameraDistance);
  if (state_.projection == ViewportProjection::Orthographic) {
    state_.orthographic_size = std::max(0.0001f, state_.distance);
  }
  establish_zoom_envelope_from_current_view();
  apply_clip_plane_constraints();
}

void ViewportCameraController::focus_bounds(QVec3 min_bounds, QVec3 max_bounds,
                                             float margin) {
  clear_orbit_snap_source();
  pending_navigation_anchor_.reset();
  pending_navigation_anchor_repair_only_ = false;
  orbit_anchor_.reset();
  quader_editor_core::ViewportFrameBoundsInput input;
  input.camera =
      make_runtime_camera(pose(), state_, viewport_width_, viewport_height_);
  input.min_bounds = to_runtime(min_bounds);
  input.max_bounds = to_runtime(max_bounds);
  input.margin = margin;
  const quader_editor_core::ViewportNavigationResult result =
      quader_editor_core::viewport_frame_bounds(input);
  apply_runtime_transform(state_, pivot_, camera_forward_, camera_up_, result);
  if (!result.ok) {
    return;
  }
  establish_zoom_envelope_from_current_view();

  // Framing is allowed to place the eye beyond the interactive wheel cap, but
  // the clip range used to remain at its old value. Expand it to include every
  // framed corner so a successful Focus Bounds cannot produce a blank or
  // partially clipped view for a large document.
  const ViewportCameraPose framed_pose = pose();
  const QVec3 framed_forward =
      normalize(framed_pose.target - framed_pose.eye);
  double maximum_depth = 0.0;
  for (const float x : {min_bounds.x, max_bounds.x}) {
    for (const float y : {min_bounds.y, max_bounds.y}) {
      for (const float z : {min_bounds.z, max_bounds.z}) {
        const QVec3 offset = QVec3{x, y, z} - framed_pose.eye;
        const double depth =
            static_cast<double>(offset.x) * framed_forward.x +
            static_cast<double>(offset.y) * framed_forward.y +
            static_cast<double>(offset.z) * framed_forward.z;
        if (std::isfinite(depth)) {
          maximum_depth = std::max(maximum_depth, depth);
        }
      }
    }
  }
  const double padding = std::max(1.0, maximum_depth * 0.01);
  const double float_limit =
      static_cast<double>(std::numeric_limits<float>::max());
  transient_far_plane_floor_ = maximum_depth > 0.0
                                   ? static_cast<float>(std::min(
                                         float_limit, maximum_depth + padding))
                                   : 0.0f;
  apply_clip_plane_constraints();
}

bool ViewportCameraController::focus_optional_bounds(
    const std::optional<ViewportFocusBounds> &bounds,
    ViewportEmptyFocusBehavior empty_behavior, float margin) {
  if (bounds.has_value()) {
    focus_bounds(bounds->min, bounds->max, margin);
    return true;
  }
  if (empty_behavior == ViewportEmptyFocusBehavior::FocusOrigin) {
    focus_origin();
    return true;
  }
  return false;
}

void ViewportCameraController::view_axis(ViewportViewAxis axis, int sign,
                                          ViewportAxisLock lock) {
  clear_orbit_snap_source();
  orbit_snap_orthographic_projection_ = false;
  pending_navigation_anchor_.reset();
  pending_navigation_anchor_repair_only_ = false;
  orbit_anchor_.reset();
  pivot_ = state_.target;
  const quader_editor_core::ViewportCameraState camera =
      make_runtime_camera(pose(), state_, viewport_width_, viewport_height_);
  apply_runtime_transform(
      state_, pivot_, camera_forward_, camera_up_,
      quader_editor_core::viewport_view_axis(camera, to_runtime(pivot_),
                                             to_runtime(axis), sign));
  state_.projection = ViewportProjection::Orthographic;
  orthographic_view_locked_ = lock == ViewportAxisLock::Locked;
  establish_zoom_envelope_from_current_view();
  apply_clip_plane_constraints();
}

void ViewportCameraController::unlock_orthographic_view() {
  clear_orbit_snap_source();
  orthographic_view_locked_ = false;
  orbit_snap_orthographic_projection_ = false;
  if (state_.projection == ViewportProjection::Orthographic) {
    sync_perspective_distance_from_orthographic_zoom(state_);
    state_.projection = ViewportProjection::Perspective;
    ensure_perspective_target_within_clip_range();
    establish_zoom_envelope_from_current_view();
    apply_clip_plane_constraints();
  }
}

void ViewportCameraController::set_view_direction(QVec3 forward_direction) {
  clear_orbit_snap_source();
  orbit_snap_orthographic_projection_ = false;
  pending_navigation_anchor_.reset();
  pending_navigation_anchor_repair_only_ = false;
  orbit_anchor_.reset();
  const QVec3 direction = normalize(forward_direction);
  if (length(direction) <= 0.000001f) {
    return;
  }

  set_camera_basis(direction, fallback_up_for_forward(direction));
  pivot_ = state_.target;
}

void ViewportCameraController::fly_move(QVec3 local_direction,
                                        float delta_seconds) {
  if (length(local_direction) <= 0.000001f) {
    return;
  }

  const float clamped_delta = clamp(delta_seconds, 0.0f, kFlyMaxDeltaSeconds);
  const float distance = fly_speed_ * clamped_delta;
  if (distance <= 0.000001f) {
    return;
  }

  QDR_PROFILE_SCOPE("Viewport camera WASD fly move");
  quader_editor_core::ViewportFlyMoveInput input;
  input.camera_transform = make_runtime_transform(pose());
  input.pivot = to_runtime(pivot_);
  input.local_direction = to_runtime(local_direction);
  input.distance = distance;
  apply_runtime_transform(state_, pivot_, camera_forward_, camera_up_,
                          quader_editor_core::viewport_fly_move(input),
                          state_.distance);
  apply_clip_plane_constraints();
}

void ViewportCameraController::adjust_fly_speed(float wheel_steps) {
  if (std::abs(wheel_steps) <= 0.000001f) {
    return;
  }
  fly_speed_ =
      clamp(fly_speed_ + wheel_steps * kFlySpeedWheelStep,
            kMinimumViewportFlyNavigationSpeed,
            kMaximumViewportFlyNavigationSpeed);
}

bool ViewportCameraController::set_default_fly_speed(float speed) {
  const float sanitized = viewport_sanitize_fly_navigation_speed(speed);
  if (std::abs(default_fly_speed_ - sanitized) <= 0.0001f) {
    return false;
  }
  const bool current_was_default = fly_speed_is_default();
  default_fly_speed_ = sanitized;
  if (current_was_default) {
    fly_speed_ = default_fly_speed_;
  }
  return true;
}

void ViewportCameraController::reset_fly_speed() {
  fly_speed_ = default_fly_speed_;
}

void ViewportCameraController::set_clip_planes(float near_plane,
                                               float far_plane) {
  const ViewportCameraClipPlanes clip_planes =
      sanitize_viewport_camera_clip_planes(near_plane, far_plane);
  const bool authored_far_plane_changed =
      clip_planes.far_plane != configured_far_plane_;
  configured_near_plane_ = clip_planes.near_plane;
  configured_far_plane_ = clip_planes.far_plane;
  if (authored_far_plane_changed) {
    // Automatic framing/projection expansions are transient guarantees for
    // the current view, not hidden edits to scene settings. Reapplying the
    // same authored values each frame preserves them; an actual far-clip
    // settings change deliberately starts a new clip policy.
    transient_far_plane_floor_ = 0.0f;
  }
  apply_clip_plane_constraints();
}

bool ViewportCameraController::restore_perspective_pose(
    const ViewportCameraPose &camera_pose) {
  if (!viewport_camera_pose_is_valid(camera_pose)) {
    return false;
  }
  const double forward_x = static_cast<double>(camera_pose.target.x) -
                           static_cast<double>(camera_pose.eye.x);
  const double forward_y = static_cast<double>(camera_pose.target.y) -
                           static_cast<double>(camera_pose.eye.y);
  const double forward_z = static_cast<double>(camera_pose.target.z) -
                           static_cast<double>(camera_pose.eye.z);
  const double distance_double =
      std::sqrt(forward_x * forward_x + forward_y * forward_y +
                forward_z * forward_z);
  // The shared projection contract cannot represent a near plane below
  // kViewportCameraMinimumNearPlane. Preserve the authored target and camera
  // basis, but move the eye back for the narrow set of otherwise-valid poses
  // whose target would land on or behind that plane.
  const float distance = std::max(
      static_cast<float>(distance_double),
      kMinimumRenderablePerspectiveDistance);
  const QVec3 forward_direction{
      static_cast<float>(forward_x / distance_double),
      static_cast<float>(forward_y / distance_double),
      static_cast<float>(forward_z / distance_double),
  };
  const double up_x = camera_pose.up.x;
  const double up_y = camera_pose.up.y;
  const double up_z = camera_pose.up.z;
  const double up_length =
      std::sqrt(up_x * up_x + up_y * up_y + up_z * up_z);
  const QVec3 up_direction{
      static_cast<float>(up_x / up_length),
      static_cast<float>(up_y / up_length),
      static_cast<float>(up_z / up_length),
  };

  state_.target = camera_pose.target;
  state_.distance = distance;
  state_.projection = ViewportProjection::Perspective;
  orbit_snap_orthographic_projection_ = false;
  transient_far_plane_floor_ = 0.0f;
  pivot_ = state_.target;
  pending_navigation_anchor_.reset();
  pending_navigation_anchor_repair_only_ = false;
  orbit_anchor_.reset();
  set_camera_basis(forward_direction, up_direction);
  drag_mode_ = ViewportDragMode::None;
  orthographic_view_locked_ = false;
  clear_orbit_snap_source();
  ensure_perspective_target_within_clip_range();
  establish_zoom_envelope_from_current_view();
  apply_clip_plane_constraints();
  return true;
}

bool ViewportCameraController::fly_speed_is_default() const {
  return std::abs(fly_speed_ - default_fly_speed_) <= 0.0001f;
}

ViewportCameraPose ViewportCameraController::pose() const {
    const QVec3 fwd = forward();
    const QVec3 eye = state_.target - fwd * state_.distance;
    return {eye, state_.target, up()};
}

ViewportRay
ViewportCameraController::ray_from_viewport_point(QVec2 local_mouse) const {
  const quader_poly::Ray ray =
      quader_editor_core::ray_from_viewport_screen_point(
          make_runtime_camera(pose(), state_, viewport_width_,
                              viewport_height_),
          to_runtime(local_mouse));
  return {
      .origin = from_runtime(ray.origin),
      .direction = normalize(from_runtime(ray.direction)),
  };
}

ViewportScreenPoint
ViewportCameraController::project_world_point(QVec3 world_point) const {
  const quader_editor_core::ViewportCameraState camera =
      make_runtime_camera(pose(), state_, viewport_width_, viewport_height_);
  quader::QVec2 screen{};
  const bool projected = quader_editor_core::project_viewport_point(
      camera, to_runtime(world_point), screen);
  const ViewportCameraPose camera_pose = pose();
  const QVec3 fwd = normalize(camera_pose.target - camera_pose.eye);
  const float depth = dot(world_point - camera_pose.eye, fwd);
  return {
      .position = {screen.x, screen.y},
      .depth = depth,
      .visible =
          projected && depth > state_.near_plane && depth < state_.far_plane,
  };
}

std::optional<ViewportScreenSegment>
ViewportCameraController::project_visible_world_segment(QVec3 start,
                                                        QVec3 end) const {
  return project_visible_viewport_segment(
      make_runtime_camera(pose(), state_, viewport_width_, viewport_height_),
      start, end, state_.near_plane, state_.far_plane);
}

QVec3 ViewportCameraController::forward() const { return camera_forward_; }

QVec3 ViewportCameraController::right() const {
  QVec3 value = cross(camera_forward_, camera_up_);
  if (length(value) <= 0.000001f) {
    value = {1.0f, 0.0f, 0.0f};
  }
    return normalize(value);
}

QVec3 ViewportCameraController::up() const {
  QVec3 value = normalize(camera_up_);
  if (length(value) <= 0.000001f) {
    value = {0.0f, 1.0f, 0.0f};
  }
    return value;
}

void ViewportCameraController::set_camera_basis(QVec3 forward_direction,
                                                QVec3 up_direction) {
  const CameraBasis basis =
      make_camera_basis(forward_direction, up_direction, camera_forward_);
  camera_forward_ = basis.forward;
  camera_up_ = basis.up;
  sync_angles_from_basis();
}

void ViewportCameraController::sync_basis_from_angles() {
  set_camera_basis(
      forward_from_angles(state_.yaw_radians, state_.pitch_radians),
      fallback_up_for_forward(
          forward_from_angles(state_.yaw_radians, state_.pitch_radians)));
}

void ViewportCameraController::sync_angles_from_basis() {
  sync_angles_for_forward(state_, camera_forward_);
}

void ViewportCameraController::establish_zoom_envelope_from_current_view() {
  const float perspective_distance =
      std::isfinite(state_.distance) && state_.distance > 0.0f
          ? state_.distance
          : kMinCameraDistance;
  minimum_perspective_zoom_distance_ =
      std::min(kMinCameraDistance, perspective_distance);
  maximum_perspective_zoom_distance_ =
      std::max(kMaxCameraDistance, perspective_distance);

  const float orthographic_size =
      std::isfinite(state_.orthographic_size) &&
              state_.orthographic_size > 0.0f
          ? state_.orthographic_size
          : kMinOrthographicZoomSize;
  minimum_orthographic_zoom_size_ =
      std::min(kMinOrthographicZoomSize, orthographic_size);
  maximum_orthographic_zoom_size_ =
      std::max(kMaxOrthographicZoomSize, orthographic_size);
}

void ViewportCameraController::extend_zoom_envelope_to_current_view() {
  if (std::isfinite(state_.distance) && state_.distance > 0.0f) {
    minimum_perspective_zoom_distance_ =
        std::min(minimum_perspective_zoom_distance_, state_.distance);
    maximum_perspective_zoom_distance_ =
        std::max(maximum_perspective_zoom_distance_, state_.distance);
  }
  if (std::isfinite(state_.orthographic_size) &&
      state_.orthographic_size > 0.0f) {
    minimum_orthographic_zoom_size_ =
        std::min(minimum_orthographic_zoom_size_, state_.orthographic_size);
    maximum_orthographic_zoom_size_ =
        std::max(maximum_orthographic_zoom_size_, state_.orthographic_size);
  }
}

void ViewportCameraController::ensure_perspective_target_within_clip_range() {
  if (state_.projection != ViewportProjection::Perspective ||
      !std::isfinite(state_.distance) || state_.distance <= 0.0f) {
    return;
  }
  const double distance = state_.distance;
  const double padding = std::max(1.0, distance * 0.01);
  const double float_limit =
      static_cast<double>(std::numeric_limits<float>::max());
  transient_far_plane_floor_ = std::max(
      transient_far_plane_floor_,
      static_cast<float>(std::min(float_limit, distance + padding)));
}

void ViewportCameraController::apply_clip_plane_constraints() {
  float effective_near_plane = configured_near_plane_;
  if (state_.projection == ViewportProjection::Perspective &&
      std::isfinite(state_.distance) && state_.distance > 0.0f &&
      state_.distance <= effective_near_plane) {
    // Orthographic views can legitimately have a visible scale below the
    // interactive perspective distance floor. When orbit unlocks such a view,
    // keep its target in front of the near plane without changing eye/target.
    effective_near_plane = std::max(kViewportCameraMinimumNearPlane,
                                    state_.distance * 0.5f);
  }
  const ViewportCameraClipPlanes effective_clips =
      sanitize_viewport_camera_clip_planes(
          effective_near_plane,
          std::max(configured_far_plane_, transient_far_plane_floor_));
  state_.near_plane = effective_clips.near_plane;
  state_.far_plane = effective_clips.far_plane;
}

std::optional<QVec3> ViewportCameraController::rebase_navigation_depth(
    QVec3 navigation_anchor) {
  const ViewportCameraPose current_pose = pose();
  const float anchor_depth =
      dot(navigation_anchor - current_pose.eye, camera_forward_);
  if (!std::isfinite(anchor_depth) || anchor_depth <= kMinCameraDistance) {
    return std::nullopt;
  }
  // A gesture may begin over geometry behind the camera's current target.
  // Keep a valid nearby target authoritative so that a fresh orbit cannot
  // inflate its radius to the depth of that farther surface. Closer anchors
  // still repair stale outward targets and preserve scene-scale navigation.
  if (std::isfinite(state_.distance) && state_.distance > 0.0f &&
      anchor_depth > state_.distance) {
    return std::nullopt;
  }

  const ViewportScreenPoint projected = project_world_point(navigation_anchor);
  if (!projected.visible || !std::isfinite(projected.position.x) ||
      !std::isfinite(projected.position.y)) {
    return std::nullopt;
  }

  state_.target = current_pose.eye + camera_forward_ * anchor_depth;
  state_.distance = anchor_depth;
  pivot_ = state_.target;
  establish_zoom_envelope_from_current_view();
  const bool anchor_is_on_screen =
      projected.position.x >= 0.0f && projected.position.y >= 0.0f &&
      projected.position.x <= viewport_width_ &&
      projected.position.y <= viewport_height_;
  return anchor_is_on_screen ? navigation_anchor : state_.target;
}

std::optional<QVec3> ViewportCameraController::repair_navigation_depth(
    QVec3 navigation_anchor) {
  const ViewportCameraPose current_pose = pose();
  const float anchor_depth =
      dot(navigation_anchor - current_pose.eye, camera_forward_);
  if (!std::isfinite(anchor_depth) || anchor_depth <= kMinCameraDistance) {
    return std::nullopt;
  }
  // The legacy defect only poisoned cameras by ratcheting their stored target
  // far behind nearby visible content. A legitimately close view of a scene
  // with distant bounds must not be pulled outward to that distant depth.
  if (state_.distance / anchor_depth >= kSevereDepthRepairRatio) {
    return rebase_navigation_depth(navigation_anchor);
  }
  return std::nullopt;
}

std::optional<QVec3>
ViewportCameraController::accept_pending_navigation_anchor() {
  if (!pending_navigation_anchor_.has_value()) {
    return std::nullopt;
  }
  const QVec3 navigation_anchor = *pending_navigation_anchor_;
  const bool repair_only = pending_navigation_anchor_repair_only_;
  pending_navigation_anchor_.reset();
  pending_navigation_anchor_repair_only_ = false;
  return repair_only ? repair_navigation_depth(navigation_anchor)
                     : rebase_navigation_depth(navigation_anchor);
}

void ViewportCameraController::clear_orbit_snap_source() {
  orbit_snap_source_pose_ = {};
  has_orbit_snap_source_ = false;
}

} // namespace quader
