////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/point_light_range_interaction.hpp"

#include "api/quader_editor_api.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_pixel_scale.hpp"

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

std::optional<float> plane_coordinate(const ViewportCameraController &camera,
                                      QVec2 local, QVec3 center,
                                      QVec3 normal) {
  const auto ray = camera.ray_from_viewport_point(local);
  const float denominator = dot(ray.direction, normal);
  if (std::abs(denominator) < 0.000001f) return std::nullopt;
  const float distance = dot(center - ray.origin, normal) / denominator;
  if (!std::isfinite(distance) || distance < 0.0f) return std::nullopt;
  const float coordinate = length(ray.origin + ray.direction * distance - center);
  return std::isfinite(coordinate) ? std::optional<float>(coordinate) : std::nullopt;
}

} // namespace

std::optional<ViewportSceneBillboardPickCandidate>
PointLightRangeInteraction::pick_handle(const ViewportInteractionFrame &frame,
                                       QVec2 local) {
  if (frame.scene_billboard_pick_candidates.empty()) {
    pick_cache_valid_ = false;
    return std::nullopt;
  }
  const auto pose = frame.camera.pose();
  const auto &state = frame.camera.state();
  const auto viewport = frame.camera.viewport_size();
  const std::array<float, 17> camera{
      pose.eye.x, pose.eye.y, pose.eye.z, pose.target.x, pose.target.y,
      pose.target.z, pose.up.x, pose.up.y, pose.up.z,
      static_cast<float>(state.projection), state.orthographic_size,
      state.fov_degrees, state.near_plane, state.far_plane,
      viewport.x, viewport.y, frame.viewport_pixel_scale};
  if (pick_cache_valid_ && frame.scene_billboard_pick_revision != 0 &&
      pick_revision_ == frame.scene_billboard_pick_revision &&
      pick_camera_ == camera && pick_local_.x == local.x &&
      pick_local_.y == local.y &&
      pick_candidate_count_ == frame.scene_billboard_pick_candidates.size()) {
    return picked_handle_;
  }
  ++hover_pick_count_;
  pick_cache_valid_ = true;
  pick_revision_ = frame.scene_billboard_pick_revision;
  pick_camera_ = camera;
  pick_local_ = local;
  pick_candidate_count_ = frame.scene_billboard_pick_candidates.size();
  picked_handle_.reset();
  const QVec3 normal = normalize(pose.target - pose.eye);
  const QVec3 right = normalize(cross(normal, pose.up));
  const float radius = 10.0f * viewport_sanitized_pixel_scale(frame.viewport_pixel_scale);
  double nearest = static_cast<double>(radius * radius);
  for (const auto &candidate : frame.scene_billboard_pick_candidates) {
    if (!candidate.point_light_range || !candidate.selectable ||
        !candidate.effectively_selectable || !std::isfinite(*candidate.point_light_range)) continue;
    const auto tip = frame.camera.project_world_point(
        candidate.position + right * std::max(*candidate.point_light_range, 0.001f));
    if (!tip.visible) continue;
    const double distance = viewport_pointer_distance_squared(tip.position, local);
    if (distance <= nearest) {
      nearest = distance;
      picked_handle_ = candidate;
    }
  }
  return picked_handle_;
}

bool PointLightRangeInteraction::handle_frame(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  left_down_ = frame.left_down;
  hovered_ = false;
  if (release_suppression_.consume(QuaderMouseButton::Left, frame.left_pressed,
                                    frame.left_down, frame.left_released)) {
    return true;
  }
  if (frame.input_result.cancel_requested) return cancel(editor);

  if (gesture_) {
    if (editor.scene_documents().active_scene_id() != gesture_->scene) {
      return cancel(editor);
    }
    const bool target_present = std::any_of(
        frame.scene_billboard_pick_candidates.begin(),
        frame.scene_billboard_pick_candidates.end(), [this](const auto &candidate) {
          return candidate.entity == gesture_->entity && candidate.point_light_range &&
                 candidate.selectable && candidate.effectively_selectable;
        });
    if (!target_present) {
      // Selection, visibility or locking may already have settled the edit in
      // its owner. Finish that original reference without requiring a pickable
      // handle; a deleted/replaced target must never publish an old draft.
      const auto target = editor.query().entity(gesture_->entity);
      if (!target || !target->has_light ||
          target->light.type != QuaderLightType::Point) {
        return cancel(editor);
      }
      return finish(editor);
    }
    if (frame.selection_state.summary.selection_revision != gesture_->selection_revision) {
      return finish(editor);
    }
    if (!frame.input.viewport_focused && !frame.input.pointer_interaction_canceled &&
        frame.left_down && !frame.left_released) {
      // Another pane gaining focus is presentation-only while this pane still
      // owns the physical gesture. Do not use its coordinates for an update.
      return true;
    }
  }
  const bool allowed = frame.settings.bool_value(ViewportBoolSettingId::ShowGizmos) &&
      frame.settings.bool_value(ViewportBoolSettingId::ShowOverlays) &&
      editor.preferences().overlays_visible() &&
      frame.input.viewport_focused && !frame.input.text_input_active &&
      !frame.input.modal_tool_active && !frame.input.active_tool_owns_viewport &&
      !frame.input.navigation_active && !frame.input_result.navigation_active &&
      !frame.input_result.camera_changed && !frame.input_result.view_changed &&
      !frame.input.pointer_interaction_canceled && !frame.input_result.cancel_requested;
  if (!allowed) return finish(editor);

  if (!gesture_) {
    if (!frame.input.viewport_hovered) return false;
    const auto pose = frame.camera.pose();
    const QVec3 normal = normalize(pose.target - pose.eye);
    const auto picked = pick_handle(frame, frame.left_pressed
                                              ? frame.left_click_local
                                              : frame.input.mouse_local);
    hovered_ = picked.has_value();
    if (!frame.left_pressed) return false;
    const auto scene = editor.scene_documents().active_scene_id();
    if (!picked || !scene) return false;
    const auto coordinate = plane_coordinate(frame.camera, frame.left_click_local,
                                              picked->position, normal);
    if (!coordinate) return false;
    gesture_ = Gesture{
        .pointer = viewport_pointer_gesture_from_latest_edge(
            frame.input.pointer_pressed_edges, QuaderMouseButton::Left,
            frame.left_click_local, frame.input.mouse_screen,
            frame.input.modifiers, frame.pointer_drag_threshold_pixels),
        .scene = *scene, .entity = picked->entity,
        .selection_revision = frame.selection_state.summary.selection_revision,
        .center = picked->position, .normal = normal,
        .start_coordinate = *coordinate,
        .range = std::max(*picked->point_light_range, 0.001f)};
  }

  const auto *release = viewport_pointer_edge_after(frame.input.pointer_released_edges,
                                                    gesture_->pointer.press);
  const bool released = release || (frame.left_released && frame.input.pointer_released_edges.empty());
  const QVec2 local = release ? release->local : frame.input.mouse_local;
  if (!edit_ && viewport_pointer_drag_threshold_reached(gesture_->pointer, local)) {
    auto begun = editor.scene().begin_point_light_range_edit(gesture_->entity);
    if (!begun.ok()) return cancel(editor);
    edit_ = begun.value();
  }
  if (edit_) {
    const auto coordinate = plane_coordinate(frame.camera, local, gesture_->center,
                                              gesture_->normal);
    if (coordinate) {
      const float range = std::max(0.001f, gesture_->range + (*coordinate - gesture_->start_coordinate));
      if (!editor.scene().update_point_light_range_edit(*edit_, range).ok()) return cancel(editor);
    }
  }
  if (released) {
    (void)finish(editor);
  } else if (!frame.left_down && !frame.left_pressed) {
    (void)finish(editor);
  }
  return true;
}

bool PointLightRangeInteraction::finish(QuaderEditorApi &editor) {
  hovered_ = false;
  if (!gesture_) return false;
  if (edit_) {
    const auto result = editor.scene().finish_point_light_range_edit(*edit_);
    if (!result.ok()) {
      (void)editor.ui().notify({.level = QuaderNotificationLevel::Error,
                                .message = result.error_message()});
      return true;
    }
  }
  release_suppression_.begin(QuaderMouseButton::Left, left_down_);
  edit_.reset();
  gesture_.reset();
  return true;
}

bool PointLightRangeInteraction::cancel(QuaderEditorApi &editor) {
  hovered_ = false;
  if (!gesture_) return false;
  if (edit_) (void)editor.scene().finish_point_light_range_edit(*edit_, true);
  release_suppression_.begin(QuaderMouseButton::Left, left_down_);
  edit_.reset();
  gesture_.reset();
  return true;
}

void PointLightRangeInteraction::reset(QuaderEditorApi *editor) {
  hovered_ = false;
  pick_cache_valid_ = false;
  if (editor) (void)cancel(*editor);
  else {
    if (gesture_) release_suppression_.begin(QuaderMouseButton::Left, left_down_);
    edit_.reset();
    gesture_.reset();
  }
}

} // namespace quader
