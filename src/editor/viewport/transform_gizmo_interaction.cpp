////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/transform_gizmo_interaction.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"

#include <editor/interactions/selection_box.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace quader {
namespace {

bool selection_states_match(const ViewportInteractionSelectionState &left,
                            const ViewportInteractionSelectionState &right) {
  return left.scene_revision == right.scene_revision &&
         left.asset_revision == right.asset_revision &&
         left.pivot_revision == right.pivot_revision &&
         left.summary.mode == right.summary.mode &&
         left.summary.has_selection == right.summary.has_selection &&
         left.summary.content_revision == right.summary.content_revision &&
         left.summary.selection_revision == right.summary.selection_revision;
}

QuaderTransformAxis api_axis_for(quader_editor_core::TransformAxis axis) {
  switch (axis) {
  case quader_editor_core::TransformAxis::X:
    return QuaderTransformAxis::X;
  case quader_editor_core::TransformAxis::Y:
    return QuaderTransformAxis::Y;
  case quader_editor_core::TransformAxis::Z:
    return QuaderTransformAxis::Z;
  case quader_editor_core::TransformAxis::Xy:
    return QuaderTransformAxis::XY;
  case quader_editor_core::TransformAxis::Xz:
    return QuaderTransformAxis::XZ;
  case quader_editor_core::TransformAxis::Yz:
    return QuaderTransformAxis::YZ;
  case quader_editor_core::TransformAxis::All:
    return QuaderTransformAxis::All;
  case quader_editor_core::TransformAxis::None:
    return QuaderTransformAxis::None;
  }
  return QuaderTransformAxis::None;
}

QuaderTransformTool api_transform_tool_for(ViewportToolId tool) {
  switch (viewport_tool_transform_tool(tool)) {
  case ModelingTransformTool::Move:
    return QuaderTransformTool::Move;
  case ModelingTransformTool::Rotate:
    return QuaderTransformTool::Rotate;
  case ModelingTransformTool::Scale:
    return QuaderTransformTool::Scale;
  }
  return QuaderTransformTool::Move;
}

quader_editor_core::TransformGizmoFrameInput
make_gizmo_input(const ViewportInteractionFrame &frame, ViewportToolId tool,
                 QVec3 pivot, quader_editor_core::TransformAxis active_axis,
                 quader_editor_core::TransformAxis hover_axis) {
  quader_editor_core::TransformGizmoFrameInput input;
  input.tool = viewport_tool_interaction_tool(tool);
  input.active_axis = active_axis;
  input.hover_axis = hover_axis;
  input.has_selection = true;
  input.has_pivot = true;
  input.pivot = pivot;
  input.camera = ViewportCameraAdapter::runtime_camera(
      frame.camera, frame.input.viewport_size);
  input.grid_size =
      viewport_grid_world_size_for_preset(frame.input.grid_preset);
  input.gizmo_size_pixels = kOverlayTransformGizmoSizePixels;
  input.pick_radius_pixels = kOverlayTransformGizmoPickRadiusPixels;
  input.gizmo_scale = std::max(
      frame.settings.float_value(ViewportFloatSettingId::GizmoSize), 0.1f);
  input.ring_segments = 64;
  return input;
}

QuaderTransformOperation transform_operation_for(ViewportToolId tool,
                                                 QuaderSelectionMode mode,
                                                 ViewportModifiers modifiers) {
  if (!modifiers.shift ||
      !is_component_selection_mode(to_modeling_selection_mode(mode))) {
    return QuaderTransformOperation::Transform;
  }
  if (tool == ViewportToolId::Move || tool == ViewportToolId::Rotate) {
    return QuaderTransformOperation::Extrude;
  }
  if (tool == ViewportToolId::Scale) {
    return QuaderTransformOperation::Inset;
  }
  return QuaderTransformOperation::Transform;
}

QVec3 projected_onto_plane(QVec3 value, QVec3 normal) {
  return value - normal * dot(value, normal);
}

std::optional<QVec3> intersect_ray_plane(const ViewportRay &ray,
                                         QVec3 plane_point,
                                         QVec3 plane_normal) {
  const float normal_length = length(plane_normal);
  if (normal_length <= 0.000001f) {
    return std::nullopt;
  }
  const QVec3 normal = plane_normal / normal_length;
  const float denominator = dot(normal, ray.direction);
  if (std::abs(denominator) <= 0.000001f) {
    return std::nullopt;
  }
  const float distance = dot(normal, plane_point - ray.origin) / denominator;
  if (!std::isfinite(distance) || distance < 0.0f) {
    return std::nullopt;
  }
  return ray.origin + ray.direction * distance;
}

QVec3 fallback_rotation_plane_direction(const ViewportCameraController &camera,
                                        QVec3 plane_normal) {
  const ViewportCameraBasis camera_basis = ViewportCameraAdapter::basis(camera);
  QVec3 direction = projected_onto_plane(camera_basis.right, plane_normal);
  if (length(direction) <= 0.000001f) {
    direction = projected_onto_plane(camera_basis.up, plane_normal);
  }
  if (length(direction) <= 0.000001f) {
    direction = cross(plane_normal, QVec3{0.0f, 1.0f, 0.0f});
  }
  if (length(direction) <= 0.000001f) {
    direction = cross(plane_normal, QVec3{1.0f, 0.0f, 0.0f});
  }
  const float direction_length = length(direction);
  return direction_length <= 0.000001f ? QVec3{1.0f, 0.0f, 0.0f}
                                       : direction / direction_length;
}

QVec3 rotation_indicator_start_direction_from_mouse(
    const ViewportCameraController &camera, QVec2 local_mouse,
    QVec3 world_pivot, QVec3 plane_normal) {
  const float normal_length = length(plane_normal);
  if (normal_length <= 0.000001f) {
    return {};
  }
  plane_normal = plane_normal / normal_length;

  const ViewportRay ray = camera.ray_from_viewport_point(local_mouse);
  std::optional<QVec3> hit =
      intersect_ray_plane(ray, world_pivot, plane_normal);
  if (!hit) {
    const ViewportCameraBasis camera_basis =
        ViewportCameraAdapter::basis(camera);
    hit = intersect_ray_plane(ray, world_pivot, camera_basis.forward);
  }
  if (hit) {
    const QVec3 direction =
        projected_onto_plane(*hit - world_pivot, plane_normal);
    if (length(direction) > 0.000001f) {
      return normalize(direction);
    }
  }

  return fallback_rotation_plane_direction(camera, plane_normal);
}

std::optional<QVec3> rotation_indicator_mouse_position_from_mouse(
    const ViewportCameraController &camera, QVec2 local_mouse,
    QVec3 world_pivot, QVec3 plane_normal) {
  const ViewportRay ray = camera.ray_from_viewport_point(local_mouse);
  const ViewportCameraBasis camera_basis = ViewportCameraAdapter::basis(camera);
  std::optional<QVec3> hit =
      intersect_ray_plane(ray, world_pivot, camera_basis.forward);
  if (!hit) {
    const float normal_length = length(plane_normal);
    if (normal_length > 0.000001f) {
      hit = intersect_ray_plane(ray, world_pivot, plane_normal / normal_length);
    }
  }
  return hit;
}

double screen_distance_squared(QVec2 a, QVec2 b) {
  const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
  const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
  return dx * dx + dy * dy;
}

} // namespace

bool TransformGizmoInteraction::pending_drag_threshold_reached(
    QVec2 current) const {
  if (!pending_drag_.has_value()) {
    return false;
  }
  const double threshold =
      quader_editor_core::kSelectionBoxDefaultDragThreshold;
  return screen_distance_squared(
             current, pending_drag_->begin_request.screen_position) >=
         threshold * threshold;
}

bool TransformGizmoInteraction::begin_pending_drag(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  QDR_PROFILE_FUNCTION();
  if (!pending_drag_.has_value()) {
    return false;
  }

  const PendingGizmoDrag pending = *pending_drag_;
  QuaderTransformDragBeginRequest request = pending.begin_request;
  request.vertex_snap_radius = 0.0;
  const QuaderApiResult<QuaderTransformDragPreview> begin =
      editor.modeling().begin_transform_drag(request);
  if (!begin.ok) {
    LOG_ERROR("Transform drag begin failed: {}",
              begin.message.empty() ? "Begin failed." : begin.message);
    reset_pending_drag();
    return false;
  }

  dragging_ = begin.value().active;
  active_axis_ =
      dragging_ ? pending.axis : quader_editor_core::TransformAxis::None;
  active_drag_pivot_ = dragging_ ? begin.value().pivot : QVec3{};
  active_world_direction_ =
      dragging_ ? pending.begin_request.world_direction : QVec3{};
  rotation_indicator_start_direction_ =
      dragging_ && viewport_tool_interaction_tool(pending.tool) ==
                       quader_editor_core::ViewportInteractionTool::Rotate
          ? std::optional<QVec3>(rotation_indicator_start_direction_from_mouse(
                frame.camera, pending.begin_request.screen_position,
                pending.begin_request.pivot,
                pending.begin_request.world_direction))
          : std::nullopt;
  reset_pending_drag();
  return dragging_;
}

ViewportBeforeToolInteractionResult
TransformGizmoInteraction::release_pending_click() {
  ViewportBeforeToolInteractionResult result;
  if (pending_drag_.has_value()) {
    result.selection_click_requested = true;
    result.selection_click_local = pending_drag_->click_local;
    result.selection_click_count = pending_drag_->click_count;
  }
  reset_pending_drag();
  return result;
}

ViewportBeforeToolInteractionResult
TransformGizmoInteraction::handle_frame(QuaderEditorApi &editor,
                                        const ViewportInteractionFrame &frame) {
  QDR_PROFILE_FUNCTION();
  ViewportBeforeToolInteractionResult result;

  const ViewportToolId tool = frame.input_router.state().active_tool;
  if (!frame.settings.bool_value(ViewportBoolSettingId::ShowGizmos) ||
      !viewport_tool_supports_transform_gizmo(tool)) {
    clear_frame();
    reset_pending_drag();
    if (dragging_) {
      result.gizmo_consumed = cancel_active_drag(editor);
    }
    return result;
  }

  std::optional<QuaderSelectionSummary> selection;
  QVec3 pivot = {};
  if (dragging_) {
    pivot = active_drag_pivot_;
  } else if (pending_drag_.has_value()) {
    pivot = pending_drag_->begin_request.pivot;
  } else {
    selection = frame.selection_state.summary;
    if (!cached_selection_state_.has_value() ||
        !selection_states_match(*cached_selection_state_,
                                frame.selection_state)) {
      cached_selection_state_ = frame.selection_state;
      cached_selection_pivot_.reset();
      if (selection->has_selection) {
        ++selection_cache_stats_.selection_pivot_recompute_count;
        cached_selection_pivot_ =
            editor.query().selection_transform_pivot();
      }
    } else {
      ++selection_cache_stats_.selection_cache_hit_count;
    }
    if (!selection->has_selection || !cached_selection_pivot_.has_value()) {
      clear_frame();
      reset_pending_drag();
      return result;
    }
    pivot = *cached_selection_pivot_;
  }

  const bool suppress_hover =
      frame.input_result.navigation_active ||
      frame.input_result.camera_changed || frame.input.text_input_active ||
      frame.input.modal_tool_active || frame.input.active_tool_owns_viewport;

  quader_editor_core::TransformGizmoFrameInput gizmo_input = make_gizmo_input(
      frame, tool, pivot,
      dragging_ ? active_axis_
                : (pending_drag_.has_value()
                       ? pending_drag_->axis
                       : quader_editor_core::TransformAxis::None),
      hover_axis_);

  if (!dragging_ && !pending_drag_.has_value() &&
      frame.input.viewport_hovered && !suppress_hover) {
    QDR_PROFILE_SCOPE("Transform gizmo hover pick");
    const quader_editor_core::TransformGizmoPickHit hover =
        quader_editor_core::pick_transform_gizmo(gizmo_input,
                                                 frame.input.mouse_local);
    hover_axis_ =
        hover.hit ? hover.axis : quader_editor_core::TransformAxis::None;
    gizmo_input.hover_axis = hover_axis_;
  } else if (!dragging_ && !pending_drag_.has_value()) {
    hover_axis_ = quader_editor_core::TransformAxis::None;
    gizmo_input.hover_axis = hover_axis_;
  }

  {
    QDR_PROFILE_SCOPE("Transform gizmo build frame");
    current_frame_ =
        quader_editor_core::build_transform_gizmo_frame(gizmo_input);
    if (dragging_ && current_frame_.has_value()) {
      current_frame_->info.rotate_indicator = active_rotate_indicator_;
    }
  }

  // Qt can accumulate a complete press-move-release gesture between app
  // frames. Establish the pending hit before release handling so that a
  // threshold-crossing sample can still become one committed drag.
  if (!dragging_ && !pending_drag_.has_value() && frame.left_pressed &&
      frame.input.viewport_hovered && !suppress_hover) {
    const quader_editor_core::TransformGizmoPickHit hit = [&]() {
      QDR_PROFILE_SCOPE("Transform gizmo click pick");
      return quader_editor_core::pick_transform_gizmo(gizmo_input,
                                                      frame.left_click_local);
    }();
    if (hit.hit && hit.axis != quader_editor_core::TransformAxis::None) {
      const ViewportRay start_ray =
          frame.camera.ray_from_viewport_point(frame.left_click_local);
      const bool duplicate_selected =
          selection.has_value() &&
          !is_component_selection_mode(
              to_modeling_selection_mode(selection->mode)) &&
          (frame.input.modifiers.shift || frame.input.modifiers.alt);
      pending_drag_ = PendingGizmoDrag{
          .begin_request =
              {
                  .tool = api_transform_tool_for(tool),
                  .operation = transform_operation_for(
                      tool, selection->mode, frame.input.modifiers),
                  .axis = api_axis_for(hit.axis),
                  .pivot = pivot,
                  .screen_position = frame.left_click_local,
                  .screen_pivot = current_frame_->info.screen_pivot,
                  .screen_tip = hit.screen_tip,
                  .has_world_direction = true,
                  .world_direction = hit.world_direction,
                  .ray =
                      {
                          .origin = start_ray.origin,
                          .direction = start_ray.direction,
                      },
                  .camera = to_api_viewport_camera(
                      ViewportCameraAdapter::modeling_camera(
                          frame.camera, frame.input.viewport_size)),
                  .grid_size = viewport_grid_world_size_for_preset(
                      frame.input.grid_preset),
                  .vertex_snap_radius = 0.0,
                  .grid_snap_enabled = viewport_grid_snap_active(
                      frame.settings, frame.input.modifiers.primary),
                  .precision = frame.input.modifiers.primary,
                  .duplicate_selected = duplicate_selected,
              },
          .tool = tool,
          .axis = hit.axis,
          .click_local = frame.left_click_local,
          .click_count = std::max(1, frame.left_click_count),
      };
      result.gizmo_consumed = true;
    }
  }

  if (pending_drag_.has_value()) {
    result.gizmo_consumed = true;
    const bool release_requested = frame.left_released || !frame.left_down;
    const bool threshold_reached =
        pending_drag_threshold_reached(frame.input.mouse_local);
    if (!threshold_reached) {
      if (release_requested) {
        return release_pending_click();
      }
      if (!frame.input.viewport_hovered || suppress_hover) {
        reset_pending_drag();
      }
      return result;
    }
    if (!frame.input.viewport_hovered || suppress_hover) {
      if (release_requested) {
        return release_pending_click();
      }
      reset_pending_drag();
      return result;
    }
    if (!begin_pending_drag(editor, frame)) {
      return result;
    }
    result.gizmo_consumed = true;
  }

  if (dragging_) {
    result.gizmo_consumed = true;
    const bool release_requested = frame.left_released || !frame.left_down;
    // The release event carries the newest absolute pointer position. Apply
    // it before committing so a delayed frame catches up instead of retaining
    // the previous sample's transform.
    const ViewportRay drag_ray =
        frame.camera.ray_from_viewport_point(frame.input.mouse_local);
    const bool snap_active = viewport_grid_snap_active(
        frame.settings, frame.input.modifiers.primary);
    const DragUpdateSignature update_signature{
        .screen_position = frame.input.mouse_local,
        .viewport_size = frame.input.viewport_size,
        .grid_preset = frame.input.grid_preset,
        .vertex_snap_radius =
            snap_active ? viewport_vertex_snap_radius(frame.settings) : 0.0f,
        .grid_snap_enabled = snap_active,
        .precision = frame.input.modifiers.primary,
    };
    if (!matches_last_drag_update(update_signature)) {
      const QuaderApiResult<QuaderTransformDragPreview> preview = [&]() {
        QDR_PROFILE_SCOPE("Transform gizmo update drag");
        return editor.modeling().update_transform_drag({
            .screen_position = frame.input.mouse_local,
            .ray =
                {
                    .origin = drag_ray.origin,
                    .direction = drag_ray.direction,
                },
            .camera =
                to_api_viewport_camera(ViewportCameraAdapter::modeling_camera(
                    frame.camera, frame.input.viewport_size)),
            .grid_size =
                viewport_grid_world_size_for_preset(frame.input.grid_preset),
            .vertex_snap_radius = update_signature.vertex_snap_radius,
            .grid_snap_enabled = snap_active,
            .precision = frame.input.modifiers.primary,
        });
      }();
      if (!preview.ok) {
        LOG_ERROR(
            "Transform drag update failed: {}",
            preview.message.empty() ? "Update failed." : preview.message);
      }
      if (preview.ok) {
        last_drag_update_signature_ = update_signature;
      }
      if (preview.ok && preview.value().active) {
        active_drag_pivot_ = preview.value().pivot;
        if (viewport_tool_transform_tool(tool) == ModelingTransformTool::Move) {
          active_drag_pivot_ += preview.value().move_delta;
        }
      }
      if (preview.ok && preview.value().active &&
          viewport_tool_interaction_tool(tool) ==
              quader_editor_core::ViewportInteractionTool::Rotate &&
          current_frame_.has_value() &&
          rotation_indicator_start_direction_.has_value()) {
        const std::optional<QVec3> mouse_position =
            rotation_indicator_mouse_position_from_mouse(
                frame.camera, frame.input.mouse_local,
                current_frame_->info.pivot, active_world_direction_);
        active_rotate_indicator_ = {
            .active = true,
            .axis = active_axis_,
            .world_axis = active_world_direction_,
            .start_direction = *rotation_indicator_start_direction_,
            .has_mouse_position = mouse_position.has_value(),
            .mouse_position = mouse_position.value_or(QVec3{}),
            .angle_degrees = preview.value().rotate_angle_degrees,
        };
        current_frame_->info.rotate_indicator = active_rotate_indicator_;
      }
    }
    if (release_requested) {
      const QuaderApiResult<QuaderTransformDragPreview> commit =
          editor.modeling().commit_transform_drag();
      if (!commit.ok) {
        LOG_ERROR("Transform drag commit failed: {}",
                  commit.message.empty() ? "Commit failed." : commit.message);
      }
      reset_drag_state();
    }
    return result;
  }
  return result;
}

bool TransformGizmoInteraction::matches_last_drag_update(
    const DragUpdateSignature &signature) const {
  if (!last_drag_update_signature_.has_value()) {
    return false;
  }
  const DragUpdateSignature &last = *last_drag_update_signature_;
  return last.screen_position.x == signature.screen_position.x &&
         last.screen_position.y == signature.screen_position.y &&
         last.viewport_size.x == signature.viewport_size.x &&
         last.viewport_size.y == signature.viewport_size.y &&
         last.grid_preset == signature.grid_preset &&
         last.vertex_snap_radius == signature.vertex_snap_radius &&
         last.grid_snap_enabled == signature.grid_snap_enabled &&
         last.precision == signature.precision;
}

bool TransformGizmoInteraction::cancel_active_drag(QuaderEditorApi &editor) {
  if (pending_drag_.has_value()) {
    reset_pending_drag();
    return true;
  }
  if (!dragging_) {
    return false;
  }
  const QuaderApiVoidResult cancel = editor.modeling().cancel_transform_drag();
  if (!cancel.ok) {
    LOG_ERROR("Transform drag cancel failed: {}",
              cancel.message.empty() ? "Cancel failed." : cancel.message);
  }
  reset_drag_state();
  return true;
}

void TransformGizmoInteraction::reset(QuaderEditorApi* editor) {
    if (editor != nullptr && dragging_) {
      (void)cancel_active_drag(*editor);
    } else {
      reset_drag_state();
    }
    reset_pending_drag();
    clear_frame();
    cached_selection_state_.reset();
    cached_selection_pivot_.reset();
}

const quader_editor_core::TransformGizmoFrame* TransformGizmoInteraction::frame() const {
  if (!current_frame_.has_value() || !current_frame_->info.ok) {
    return nullptr;
  }
  return &*current_frame_;
}

void TransformGizmoInteraction::reset_drag_state() {
  dragging_ = false;
  active_axis_ = quader_editor_core::TransformAxis::None;
  active_drag_pivot_ = {};
  active_world_direction_ = {};
  rotation_indicator_start_direction_.reset();
  last_drag_update_signature_.reset();
  active_rotate_indicator_ = {};
}

void TransformGizmoInteraction::reset_pending_drag() { pending_drag_.reset(); }

void TransformGizmoInteraction::clear_frame() {
  current_frame_.reset();
  hover_axis_ = quader_editor_core::TransformAxis::None;
}

} // namespace quader
