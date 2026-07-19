////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_camera_controller.hpp"

#include "diagnostics/profile.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_clip_planes.hpp"

#include <editor/interactions/viewport_navigation.hpp>
#include <editor/interactions/viewport_projection.hpp>

#include <cmath>
#include <numbers>
#include <optional>

namespace quader {
namespace {

constexpr float kNavigationZoomStepFactor = 1.18f;
constexpr float kFlyMaxDeltaSeconds = 0.25f;
constexpr float kFlySpeedWheelStep = 1.0f;
constexpr float kMinCameraDistance = 0.05f;
constexpr float kMaxCameraDistance = 500.0f;

float vertical_fov_radians(float fov_degrees) {
  return fov_degrees * std::numbers::pi_v<float> / 180.0f;
}

float perspective_visible_height(const ViewportCameraState &state) {
  return std::max(0.0001f,
                  2.0f * std::max(kMinCameraDistance, state.distance) *
                      std::tan(vertical_fov_radians(state.fov_degrees) * 0.5f));
}

float perspective_distance_for_visible_height(float visible_height,
                                              float fov_degrees) {
  const float half_fov_tangent =
      std::tan(vertical_fov_radians(fov_degrees) * 0.5f);
  if (half_fov_tangent <= 0.000001f) {
    return kMinCameraDistance;
  }
  return std::max(kMinCameraDistance, std::max(0.0001f, visible_height) /
                                          (2.0f * half_fov_tangent));
}

float current_visible_height(const ViewportCameraState &state) {
  return state.projection == ViewportProjection::Orthographic
             ? std::max(0.0001f, state.orthographic_size)
             : perspective_visible_height(state);
}

void sync_perspective_distance_from_orthographic_zoom(
    ViewportCameraState &state) {
  if (state.projection == ViewportProjection::Orthographic) {
    state.distance =
        clamp(perspective_distance_for_visible_height(
                  state.orthographic_size, state.fov_degrees),
              kMinCameraDistance, kMaxCameraDistance);
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
  if (forward_distance > kMinCameraDistance) {
    return forward_distance;
  }
  return std::max(kMinCameraDistance, length(pivot - origin));
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
  const float view_distance =
      std::max(kMinCameraDistance,
               preserved_view_distance.value_or(distance_along_forward_to_pivot(
                   origin, basis.forward, pivot)));
  const float clamped_view_distance =
      clamp(view_distance, kMinCameraDistance, kMaxCameraDistance);

  camera_forward = basis.forward;
  camera_up = basis.up;
  state.target = origin + camera_forward * clamped_view_distance;
  state.distance = clamped_view_distance;
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

ViewportCameraController::ViewportCameraController() {
  const ViewportHostDefaults defaults = viewport_host_defaults();
  state_.orthographic_size =
      static_cast<float>(defaults.default_orthographic_size);
  state_.fov_degrees = static_cast<float>(defaults.default_fov_degrees);
  default_fly_speed_ =
      viewport_sanitize_fly_navigation_speed(
          static_cast<float>(defaults.fly_base_speed));
  fly_speed_ = default_fly_speed_;
  sync_basis_from_angles();
}

void ViewportCameraController::set_viewport_size(float width, float height) {
  viewport_width_ = std::max(1.0f, width);
  viewport_height_ = std::max(1.0f, height);
}

void ViewportCameraController::begin_drag(QVec2 mouse, ViewportDragMode mode) {
  // Every pointer-navigation gesture starts from the point currently centered
  // by the camera. Cursor zoom and fly look can otherwise leave the runtime
  // pivot off-axis while state_.target continues to describe the visible
  // view. Reusing that hidden pivot makes pan scale against the wrong depth
  // and makes the next orbit jump around a point outside the view.
  pivot_ = state_.target;
  drag_mode_ = mode;
  last_mouse_ = mouse;
  clear_orbit_snap_source();
  if (mode == ViewportDragMode::Fly) {
    state_.projection = ViewportProjection::Perspective;
    orthographic_view_locked_ = false;
  }
}

void ViewportCameraController::update_drag(QVec2 mouse) {
  update_drag(mouse, drag_mode_);
}

void ViewportCameraController::update_drag(QVec2 mouse, ViewportDragMode mode) {
  const ViewportDragMode previous_mode = drag_mode_;
  const bool entering_orbit_snap =
      previous_mode != ViewportDragMode::OrbitSnap &&
      mode == ViewportDragMode::OrbitSnap;
  const QVec2 delta = entering_orbit_snap ? QVec2{}
                                          : QVec2{mouse.x - last_mouse_.x,
                                                  mouse.y - last_mouse_.y};
  last_mouse_ = mouse;
  if (previous_mode == ViewportDragMode::OrbitSnap &&
      mode != ViewportDragMode::OrbitSnap) {
    clear_orbit_snap_source();
  }
  drag_mode_ = mode;
  update_drag_delta(delta);
}

void ViewportCameraController::update_drag_delta(QVec2 delta) {
  if (drag_mode_ == ViewportDragMode::Orbit ||
      drag_mode_ == ViewportDragMode::OrbitSnap) {
    QDR_PROFILE_SCOPE("Viewport camera orbit");
    const ViewportHostDefaults defaults = viewport_host_defaults();
    if (orthographic_view_locked_) {
      state_.projection = ViewportProjection::Orthographic;
      clear_orbit_snap_source();
      return;
    }
    quader_editor_core::ViewportOrbitInput input;
    const bool snap_to_orthographic = drag_mode_ == ViewportDragMode::OrbitSnap;
    if (!snap_to_orthographic) {
      clear_orbit_snap_source();
    }
    const float snapped_orthographic_size = current_visible_height(state_);
    sync_perspective_distance_from_orthographic_zoom(state_);
    input.camera_transform =
        snap_to_orthographic && has_orbit_snap_source_
            ? make_runtime_transform(orbit_snap_source_pose_)
            : make_runtime_transform(pose());
    input.pivot = to_runtime(pivot_);
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
    } else {
      state_.projection = ViewportProjection::Perspective;
      orthographic_view_locked_ = false;
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
    clear_orbit_snap_source();
  }
}

void ViewportCameraController::end_drag() {
  drag_mode_ = ViewportDragMode::None;
  // Fly look changes the visible target without rotating its runtime pivot.
  // Make the completed camera pose the sole source for the next gesture.
  pivot_ = state_.target;
  clear_orbit_snap_source();
}

void ViewportCameraController::scroll(float wheel_delta) {
  QDR_PROFILE_SCOPE("Viewport camera zoom");
  quader_editor_core::ViewportZoomInput input;
  input.camera =
      make_runtime_camera(pose(), state_, viewport_width_, viewport_height_);
  input.pivot = to_runtime(pivot_);
  input.steps = wheel_delta;
  apply_runtime_transform(state_, pivot_, camera_forward_, camera_up_,
                          quader_editor_core::viewport_zoom(input));
}

void ViewportCameraController::scroll_to_cursor(float wheel_delta,
                                                QVec2 local_mouse) {
  if (std::abs(wheel_delta) <= 0.000001f) {
    return;
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
  input.steps = wheel_delta;
  apply_runtime_transform(state_, pivot_, camera_forward_, camera_up_,
                          quader_editor_core::viewport_zoom_to_cursor(input));
}

void ViewportCameraController::focus_origin(float distance) {
  clear_orbit_snap_source();
  pivot_ = {};
  state_.target = {};
  state_.distance =
      clamp(distance, kMinCameraDistance, kMaxCameraDistance);
  if (state_.projection == ViewportProjection::Orthographic) {
    state_.orthographic_size = std::max(0.0001f, state_.distance);
  }
}

void ViewportCameraController::focus_bounds(QVec3 min_bounds, QVec3 max_bounds,
                                            float margin) {
  clear_orbit_snap_source();
  quader_editor_core::ViewportFrameBoundsInput input;
  input.camera =
      make_runtime_camera(pose(), state_, viewport_width_, viewport_height_);
  input.min_bounds = to_runtime(min_bounds);
  input.max_bounds = to_runtime(max_bounds);
  input.margin = margin;
  apply_runtime_transform(state_, pivot_, camera_forward_, camera_up_,
                          quader_editor_core::viewport_frame_bounds(input));
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
  pivot_ = state_.target;
  const quader_editor_core::ViewportCameraState camera =
      make_runtime_camera(pose(), state_, viewport_width_, viewport_height_);
  apply_runtime_transform(
      state_, pivot_, camera_forward_, camera_up_,
      quader_editor_core::viewport_view_axis(camera, to_runtime(pivot_),
                                             to_runtime(axis), sign));
  state_.projection = ViewportProjection::Orthographic;
  orthographic_view_locked_ = lock == ViewportAxisLock::Locked;
}

void ViewportCameraController::unlock_orthographic_view() {
  clear_orbit_snap_source();
  orthographic_view_locked_ = false;
  if (state_.projection == ViewportProjection::Orthographic) {
    state_.projection = ViewportProjection::Perspective;
  }
}

void ViewportCameraController::set_view_direction(QVec3 forward_direction) {
  clear_orbit_snap_source();
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
  state_.near_plane = clip_planes.near_plane;
  state_.far_plane = clip_planes.far_plane;
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

float ViewportCameraController::zoomed_distance(float wheel_delta) const {
  const float zoom_factor = std::pow(kNavigationZoomStepFactor, wheel_delta);
  return clamp(state_.distance / zoom_factor, kMinCameraDistance,
               kMaxCameraDistance);
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

void ViewportCameraController::clear_orbit_snap_source() {
  orbit_snap_source_pose_ = {};
  has_orbit_snap_source_ = false;
}

} // namespace quader
