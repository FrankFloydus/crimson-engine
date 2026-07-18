////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_native_tool_interaction.hpp"

#include "api/quader_editor_api.hpp"
#include "assets/asset_registry.hpp"
#include "core/logger.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/interactions/tool_drag_math.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_grid_placement_policy.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace quader {
namespace {

float screen_distance_squared(QVec2 left, QVec2 right) {
  const QVec2 delta = left - right;
  return delta.x * delta.x + delta.y * delta.y;
}

float distance_to_segment_squared(QVec2 point, QVec2 start, QVec2 end) {
  const QVec2 segment = end - start;
  const float length_sq = segment.x * segment.x + segment.y * segment.y;
  if (length_sq <= 0.000001f) {
    return screen_distance_squared(point, start);
  }
  const QVec2 relative = point - start;
  const float t =
      std::clamp((relative.x * segment.x + relative.y * segment.y) / length_sq,
                 0.0f, 1.0f);
  return screen_distance_squared(point, start + segment * t);
}

std::optional<QVec3> intersect_ray_plane(const ViewportRay &ray,
                                         QVec3 plane_point,
                                         QVec3 plane_normal) {
  const float denominator = dot(plane_normal, ray.direction);
  if (std::abs(denominator) <= 0.000001f) {
    return std::nullopt;
  }
  const float t = dot(plane_point - ray.origin, plane_normal) / denominator;
  return std::isfinite(t) ? std::optional<QVec3>(ray.origin + ray.direction * t)
                          : std::nullopt;
}

} // namespace

ViewportNativeToolInteractionResult
ViewportNativeToolInteraction::handle_frame(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    PivotToolService *pivot, LinearArrayToolService *linear_array,
    RadialArrayToolService *radial_array,
    ViewportPointPlacementService *point_placement,
    ViewportCameraController &camera,
    const ViewportNativeToolInteractionFrame &frame) {
  ViewportNativeToolInteractionResult output;
  const ViewportInputFrame &input = frame.input;
  const ViewportInputResult &input_result = frame.input_result;
  const auto record_change = [&](const CommandResult &result) {
    if (result.changed) {
      output.invalidate_hover = true;
      output.mark_overlay_dirty = true;
    }
  };
  const auto record_commit = [&](std::string_view label,
                                 const QuaderApiVoidResult &result) {
    if (!result.ok) {
      LOG_ERROR("{} failed: {}", label,
                result.message.empty() ? "Command failed." : result.message);
    }
    output.sync_active_tool = true;
    output.invalidate_hover = true;
    output.mark_overlay_dirty = true;
  };

  if (radial_array != nullptr && radial_array->active()) {
    output.radial_array_consumed = true;
    if (input.radial_array_copy_delta != 0) {
      record_change(radial_array->adjust_copies(input.radial_array_copy_delta));
    }
    if (frame.radial_array_wheel_remainder != nullptr) {
      reconcile_bounded_wheel_remainder(
          radial_array->copies() <= kRadialArrayMinimumCopies,
          radial_array->copies() >= radial_array->maximum_copies(),
          *frame.radial_array_wheel_remainder);
    }

    if (input.modal_tool_finalize_pressed && radial_array->center_locked()) {
      record_commit("Radial Array apply",
                    editor.tools().commit_active_tool_preview());
    } else if (!radial_array->center_locked()) {
      const bool can_place = point_placement != nullptr &&
                             input.viewport_hovered &&
                             !input_result.navigation_active &&
                             !frame.navigation_gizmo_consumed &&
                             !input.text_input_active &&
                             !frame.suppress_hover_work;
      if (!can_place) {
        (void)radial_array->clear_center_candidate();
      } else {
        output.claim_interaction_pane = true;
        const float grid_size =
            viewport_grid_world_size_for_preset(input.grid_preset);
        const auto resolve_center = [&](QVec2 mouse_local) {
          return point_placement->resolve(
              document, assets, camera,
              ViewportPointPlacementRequest{
                  .mouse_local = mouse_local,
                  .active_grid = viewport_active_grid_placement_plane(camera),
                  .grid_size = grid_size,
                  .policy = ViewportPointPlacementPolicy::radial_array(),
              });
        };
        const ViewportPointPlacementResult hover =
            resolve_center(input.mouse_local);
        if (hover.valid) {
          const CommandResult changed = radial_array->update_center_candidate(
              hover.world_position, hover.world_normal, grid_size);
          output.mark_overlay_dirty =
              output.mark_overlay_dirty || changed.changed;
        } else {
          (void)radial_array->clear_center_candidate();
        }

        if (frame.left_pressed) {
          const ViewportPointPlacementResult clicked =
              resolve_center(frame.left_click_local);
          if (clicked.valid) {
            (void)radial_array->update_center_candidate(
                clicked.world_position, clicked.world_normal, grid_size);
            const CommandResult locked = radial_array->lock_center();
            if (locked.changed) {
              output.claim_interaction_pane = true;
              output.invalidate_hover = true;
              output.mark_overlay_dirty = true;
            }
          }
        }
      }
    }
  }

  if (linear_array != nullptr && linear_array->active()) {
    output.linear_array_consumed = true;
    if (input.linear_array_copy_delta != 0) {
      record_change(linear_array->adjust_count(input.linear_array_copy_delta));
    }
    if (frame.linear_array_wheel_remainder != nullptr) {
      reconcile_bounded_wheel_remainder(
          linear_array->count() <= kLinearArrayMinimumCount,
          linear_array->count() >= linear_array->maximum_count(),
          *frame.linear_array_wheel_remainder);
    }
    if (input.tool_constraint_x_pressed) {
      (void)linear_array->set_axis_constraint(LinearArrayAxisConstraint::X);
    }
    if (input.tool_constraint_y_pressed) {
      (void)linear_array->set_axis_constraint(LinearArrayAxisConstraint::Y);
    }
    if (input.tool_constraint_z_pressed) {
      (void)linear_array->set_axis_constraint(LinearArrayAxisConstraint::Z);
    }
    if (input.modal_tool_finalize_pressed) {
      record_commit("Linear Array apply",
                    editor.tools().commit_active_tool_preview());
      linear_array_handle_dragging_ = false;
    } else {
      const LinearArrayHandleState handle = linear_array->handle_state();
      const bool can_interact = handle.visible && input.viewport_hovered &&
                                !input_result.navigation_active &&
                                !frame.navigation_gizmo_consumed &&
                                !input.text_input_active;
      if (can_interact && frame.left_pressed &&
          linear_array_handle_hit(camera, handle, frame.left_click_local)) {
        linear_array_handle_dragging_ = true;
        linear_array->begin_endpoint_drag();
        output.claim_interaction_pane = true;
      }
      if (linear_array_handle_dragging_) {
        const LinearArrayHandleState drag_handle = linear_array->handle_state();
        if (frame.left_down && drag_handle.visible) {
          (void)linear_array->set_endpoint(
              linear_array_drag_point(camera, input.mouse_local, drag_handle),
              frame.grid_snap_enabled,
              viewport_grid_world_size_for_preset(input.grid_preset));
          output.invalidate_hover = true;
          output.mark_overlay_dirty = true;
        }
        if (frame.left_released || !frame.left_down || !drag_handle.visible) {
          linear_array->end_endpoint_drag();
          linear_array_handle_dragging_ = false;
        }
      }
    }
  }

  if (pivot != nullptr) {
    const bool blocked_by_navigation = input_result.fly_navigation_active ||
                                       input_result.navigation_active ||
                                       frame.navigation_gizmo_consumed;
    if (input.quick_pivot_released) {
      pivot->end_quick_edit();
      pivot_handle_dragging_ = false;
      output.pivot_consumed = true;
    }
    if (blocked_by_navigation && pivot->quick_edit_active()) {
      pivot->clear_quick_override();
      pivot_handle_dragging_ = false;
      output.pivot_consumed = true;
    }
    if (!blocked_by_navigation && input.quick_pivot_down &&
        !pivot->quick_edit_active()) {
      if (const std::optional<QVec3> initial =
              editor.query().selection_transform_pivot()) {
        pivot->begin_quick(*initial);
        output.pivot_consumed = true;
      }
    }
    if (pivot->persistent_active() && input.modal_tool_finalize_pressed) {
      record_commit("Pivot apply",
                    editor.tools().commit_active_tool_preview());
      pivot_handle_dragging_ = false;
      output.pivot_consumed = true;
    }

    const PivotHandleState handle = pivot->handle_state();
    const bool can_interact = handle.visible && input.viewport_hovered &&
                              !input_result.navigation_active &&
                              !frame.navigation_gizmo_consumed &&
                              !input.text_input_active;
    if (can_interact && frame.left_pressed &&
        pivot_handle_hit(camera, handle, frame.left_click_local)) {
      pivot_handle_dragging_ = true;
      output.claim_interaction_pane = true;
      output.pivot_consumed = true;
    }
    if (pivot_handle_dragging_) {
      output.pivot_consumed = true;
      if (frame.left_down && handle.visible && point_placement != nullptr) {
        const PivotSnapSettings snap_settings =
            handle.persistent ? pivot->snap_settings()
                              : PivotSnapSettings{.vertex = true,
                                                  .edge_midpoint = false,
                                                  .face_center = false,
                                                  .grid = true};
        const QVec3 position = resolved_pivot_drag_position(
            camera, input.mouse_local, handle.world_position, document, assets,
            *point_placement, snap_settings,
            viewport_grid_world_size_for_preset(input.grid_preset));
        if (handle.persistent) {
          pivot->set_persistent_preview_position(position);
        } else {
          pivot->set_quick_preview_position(position);
        }
        output.invalidate_hover = true;
        output.mark_overlay_dirty = true;
      }
      if (frame.left_released || !frame.left_down || !handle.visible) {
        pivot_handle_dragging_ = false;
      }
    }

    const PivotHandleState after = pivot->handle_state();
    output.pivot_blocks_gizmo = after.visible && after.editing;
  }
  return output;
}

void ViewportNativeToolInteraction::clear_drag_state() {
  pivot_handle_dragging_ = false;
  linear_array_handle_dragging_ = false;
}

bool ViewportNativeToolInteraction::dragging() const {
  return pivot_handle_dragging_ || linear_array_handle_dragging_;
}

std::optional<int> ViewportNativeToolInteraction::hit_drawn_plane_captured_point(
    const std::array<QVec3, 3> &points, int captured_point_count,
    QVec2 mouse_local, const ViewportCameraController &camera) {
  constexpr float kPointHitRadiusPixels = 10.0f;
  const int count = std::min(captured_point_count, 2);
  for (int index = count - 1; index >= 0; --index) {
    const ViewportScreenPoint screen =
        camera.project_world_point(points[static_cast<std::size_t>(index)]);
    if (screen.visible &&
        screen_distance_squared(mouse_local, screen.position) <=
            kPointHitRadiusPixels * kPointHitRadiusPixels) {
      return index;
    }
  }
  return std::nullopt;
}

bool ViewportNativeToolInteraction::hit_drawn_plane_line(
    const std::array<QVec3, 3> &points, int captured_point_count, bool has_line,
    QVec2 mouse_local, const ViewportCameraController &camera) {
  constexpr float kLineHitRadiusPixels = 10.0f;
  if (!has_line || captured_point_count < 2) {
    return false;
  }
  const ViewportScreenPoint start = camera.project_world_point(points[0]);
  const ViewportScreenPoint end = camera.project_world_point(points[1]);
  return start.visible && end.visible &&
         distance_to_segment_squared(mouse_local, start.position,
                                     end.position) <=
             kLineHitRadiusPixels * kLineHitRadiusPixels;
}

ModelingBoxToolEvent ViewportNativeToolInteraction::modeling_box_event(
    const ViewportCameraController &camera, const ViewportInputFrame &input,
    float grid_size, bool snap_enabled) {
  const ViewportRay ray = camera.ray_from_viewport_point(input.mouse_local);
  return {
      .screen_position = input.mouse_local,
      .ray = {.origin = ray.origin, .direction = ray.direction},
      .grid_size = grid_size,
      .has_camera = true,
      .camera =
          ViewportCameraAdapter::modeling_camera(camera, input.viewport_size),
      .snap_enabled = snap_enabled,
  };
}

QVec3 ViewportNativeToolInteraction::resolved_pivot_drag_position(
    const ViewportCameraController &camera, QVec2 mouse_local,
    QVec3 current_position, const SceneDocument &document,
    const AssetRegistry &assets, ViewportPointPlacementService &placement,
    PivotSnapSettings settings, float grid_size) {
  const ViewportPointPlacementResult resolved = placement.resolve(
      document, assets, camera,
      ViewportPointPlacementRequest{
          .mouse_local = mouse_local,
          .active_grid = {},
          .grid_size = grid_size,
          .policy = ViewportPointPlacementPolicy::pivot(settings),
          .fallback_plane = ViewportPointPlacementPlane{
              .origin = current_position,
          },
      });
  return resolved.valid ? resolved.world_position : current_position;
}

bool ViewportNativeToolInteraction::pivot_handle_hit(
    const ViewportCameraController &camera, const PivotHandleState &handle,
    QVec2 mouse_local) {
  if (!handle.visible) {
    return false;
  }
  const ViewportScreenPoint screen =
      camera.project_world_point(handle.world_position);
  constexpr float kPickRadiusPixels = 16.0f;
  return screen.visible &&
         screen_distance_squared(screen.position, mouse_local) <=
             kPickRadiusPixels * kPickRadiusPixels;
}

bool ViewportNativeToolInteraction::linear_array_handle_hit(
    const ViewportCameraController &camera,
    const LinearArrayHandleState &handle, QVec2 mouse_local) {
  if (!handle.visible) {
    return false;
  }
  const ViewportScreenPoint screen =
      camera.project_world_point(handle.endpoint_world);
  constexpr float kPickRadiusPixels = 16.0f;
  return screen.visible &&
         screen_distance_squared(screen.position, mouse_local) <=
             kPickRadiusPixels * kPickRadiusPixels;
}

QVec3 ViewportNativeToolInteraction::linear_array_drag_point(
    const ViewportCameraController &camera, QVec2 mouse_local,
    const LinearArrayHandleState &handle) {
  const ViewportRay ray = camera.ray_from_viewport_point(mouse_local);
  QVec3 axis = {};
  switch (handle.constraint) {
  case LinearArrayAxisConstraint::X:
    axis = {1.0f, 0.0f, 0.0f};
    break;
  case LinearArrayAxisConstraint::Y:
    axis = {0.0f, 1.0f, 0.0f};
    break;
  case LinearArrayAxisConstraint::Z:
    axis = {0.0f, 0.0f, 1.0f};
    break;
  case LinearArrayAxisConstraint::Free:
    break;
  }
  if (length_squared(axis) > 0.5f) {
    const quader_editor_core::DragDistanceResult distance =
        quader_editor_core::axis_distance_from_ray(
            axis, handle.source_world,
            quader_poly::Ray{.origin = ray.origin, .direction = ray.direction},
            0.000001);
    return distance.ok ? handle.source_world +
                             axis * static_cast<float>(distance.distance)
                       : handle.endpoint_world;
  }
  const ViewportCameraPose pose = camera.pose();
  const QVec3 plane_normal = normalize_or_zero(pose.target - pose.eye);
  return intersect_ray_plane(ray, handle.endpoint_world, plane_normal)
      .value_or(handle.endpoint_world);
}

} // namespace quader
