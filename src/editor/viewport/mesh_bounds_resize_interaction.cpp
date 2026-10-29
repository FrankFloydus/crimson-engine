////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/mesh_bounds_resize_interaction.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "render/render_signature_builder.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <cmath>

namespace quader {

const BoundsResizeFrame *MeshBoundsResizePresentation::prepare(
    QuaderEditorApi &editor, const ViewportCameraController &camera,
    const ViewportInteractionSelectionState &selection,
    QuaderTransformSpace space, std::uint64_t preview_revision,
    float raster_scale, const BoundsResizeFrame *shared) {
  if (editor.mode().workspace_mode() != QuaderWorkspaceMode::Modeling) {
    reset();
    return nullptr;
  }
  RenderSignatureBuilder signature;
  const auto binding = editor.query().document_binding();
  signature.add_value(binding.document_binding_generation)
      .add_value(binding.scene_session_id.slot())
      .add_value(binding.scene_session_id.generation())
      .add_value(selection.scene_revision).add_value(selection.asset_revision)
      .add_value(selection.summary.content_revision)
      .add_value(selection.summary.selection_revision)
      .add_value(selection.summary.sub_selection_revision)
      .add_value(static_cast<std::uint64_t>(selection.summary.mode))
      .add_value(static_cast<std::uint64_t>(space)).add_value(preview_revision)
      .add_value(editor.mode().generation());
  if (signature_ != signature.value()) {
    signature_ = signature.value();
    bounds_.reset();
    if (selection.summary.mode == QuaderSelectionMode::Object &&
        selection.summary.has_selection) {
      ++bounds_query_count_;
      const auto bounds = editor.modeling().selection_resize_bounds(space);
      if (bounds) {
        bounds_ = ViewportBoundsResizeState{
            .local_min = bounds->local_min, .local_max = bounds->local_max,
            .world_origin = bounds->world_origin,
            .world_x = bounds->world_x, .world_y = bounds->world_y,
            .world_z = bounds->world_z, .revision = bounds->revision};
      }
    }
  }
  const auto &frame = shared != nullptr
      ? layout_.project_presentation(bounds_, camera, raster_scale, *shared)
      : layout_.project(bounds_, camera, raster_scale);
  return frame.visible ? &frame : nullptr;
}

void MeshBoundsResizePresentation::reset() {
  signature_.reset();
  bounds_.reset();
  layout_.clear();
}

const BoundsResizeFrame *MeshBoundsResizeInteraction::frame() const {
  return interaction_.frame().visible ? &interaction_.frame() : nullptr;
}

void MeshBoundsResizeInteraction::retire() {
  if (gesture_) release_suppression_.begin(QuaderMouseButton::Left, left_down_);
  gesture_.reset();
  dragging_ = false;
  last_displacement_.reset();
  last_grid_snap_.reset();
  interaction_.end();
}

bool MeshBoundsResizeInteraction::finish(QuaderEditorApi &editor) {
  if (!gesture_) return false;
  if (dragging_ && !editor.modeling().commit_transform_drag().ok()) return true;
  retire();
  return true;
}

bool MeshBoundsResizeInteraction::cancel(QuaderEditorApi &editor) {
  if (!gesture_) return false;
  if (dragging_) (void)editor.modeling().cancel_transform_drag();
  retire();
  return true;
}

void MeshBoundsResizeInteraction::reset(QuaderEditorApi *editor) {
  if (editor != nullptr) (void)cancel(*editor);
  else retire();
  interaction_.clear();
  presentation_.reset();
}

ViewportBeforeToolInteractionResult MeshBoundsResizeInteraction::handle_frame(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  left_down_ = frame.left_down;
  if (release_suppression_.consume(QuaderMouseButton::Left, frame.left_pressed,
                                    frame.left_down, frame.left_released)) {
    return {.gizmo_consumed = true};
  }
  if (editor.mode().workspace_mode() != QuaderWorkspaceMode::Modeling) {
    const bool owned = gesture_.has_value();
    (void)cancel(editor);
    interaction_.clear();
    presentation_.reset();
    return {.gizmo_consumed = owned};
  }
  if (gesture_) {
    if (editor.scene_documents().active_scene_id() != gesture_->scene ||
        editor.mode().generation() != gesture_->mode_generation ||
        editor.query().document_binding().document_binding_generation !=
            gesture_->document_generation ||
        frame.selection_state.summary.selection_revision != gesture_->selection_revision) {
      (void)cancel(editor);
      return {.gizmo_consumed = true};
    }
    if (frame.input_result.cancel_requested) {
      (void)cancel(editor);
      return {.gizmo_consumed = true};
    }
    const bool interrupted = frame.input.pointer_interaction_canceled ||
        frame.input.navigation_active || frame.input_result.navigation_active ||
        frame.input_result.camera_changed || frame.input_result.view_changed;
    if (interrupted) {
      (void)finish(editor);
      return {.gizmo_consumed = true};
    }
    if (!frame.input.viewport_focused) {
      if (frame.left_released || !frame.left_down) (void)finish(editor);
      return {.gizmo_consumed = true};
    }
    const auto *release = viewport_pointer_matching_release(
        frame.input.pointer_released_edges, frame.input.pointer_pressed_edges,
        gesture_->pointer.press);
    const bool released = release != nullptr ||
        (frame.left_released && frame.input.pointer_released_edges.empty());
    const QVec2 local = release != nullptr ? release->local : frame.input.mouse_local;
    if (!dragging_ && viewport_pointer_drag_threshold_reached(gesture_->pointer, local)) {
      const Gesture captured = *gesture_;
      if (captured.settle_tool) {
        const auto settled = editor.tools().activate(ViewportToolId::Select);
        if (!gesture_ || !settled.ok() ||
            editor.scene_documents().active_scene_id() != captured.scene ||
            editor.mode().generation() != captured.mode_generation ||
            editor.query().document_binding().document_binding_generation !=
                captured.document_generation ||
            editor.selection().summary().selection_revision != captured.selection_revision) {
          retire();
          return {.gizmo_consumed = true};
        }
        gesture_->settle_tool = false;
      }
      const auto begun = editor.modeling().begin_transform_drag(captured.request);
      if (!gesture_ || !begun.ok() ||
          editor.query().document_binding().document_binding_generation != captured.document_generation ||
          editor.scene_documents().active_scene_id() != captured.scene) {
        retire();
        return {.gizmo_consumed = true};
      }
      dragging_ = true;
    }
    const bool grid_snap = viewport_grid_snap_active(frame.settings, frame.input.modifiers.primary);
    const auto displacement = interaction_.displacement(frame.camera, local);
    if (dragging_ && displacement &&
        (last_displacement_ != displacement || last_grid_snap_ != grid_snap ||
         last_grid_preset_ != frame.input.grid_preset)) {
      const auto ray = frame.camera.ray_from_viewport_point(local);
      const auto updated = editor.modeling().update_transform_drag({
          .screen_position = local,
          .ray = {.origin = ray.origin, .direction = ray.direction},
          .camera = to_api_viewport_camera(ViewportCameraAdapter::modeling_camera(
              frame.camera, frame.input.viewport_size)),
          .grid_size = viewport_grid_world_size_for_preset(frame.input.grid_preset),
          .grid_snap_enabled = grid_snap,
          .bounds_resize_displacement = *displacement,
      });
      if (!gesture_) return {.gizmo_consumed = true};
      if (updated.ok() || updated.value().active) {
        last_displacement_ = displacement;
        last_grid_snap_ = grid_snap;
        last_grid_preset_ = frame.input.grid_preset;
      }
    }
    if (released || (!frame.left_down && !frame.left_pressed)) (void)finish(editor);
    return {.gizmo_consumed = true, .gizmo_hovered = true};
  }

  const bool allowed = frame.settings.bool_value(ViewportBoolSettingId::ShowGizmos) &&
      frame.selection_state.summary.mode == QuaderSelectionMode::Object &&
      !editor.tools().live_primitive_creation().has_value();
  if (!allowed) {
    interaction_.clear();
    return {};
  }
  const auto *projected = presentation_.prepare(editor, frame.camera,
      frame.selection_state, editor.viewports().transform_space(),
      frame.selection_state.transform_preview_revision,
      frame.viewport_pixel_scale);
  (void)interaction_.project_presentation(presentation_.bounds(), frame.camera,
      frame.viewport_pixel_scale, projected != nullptr ? *projected : BoundsResizeFrame{});
  const bool pointer_allowed = projected != nullptr && frame.input.viewport_hovered &&
      frame.input.viewport_focused && !frame.input.text_input_active &&
      !frame.input.navigation_active && !frame.input_result.navigation_active &&
      !frame.input.pointer_interaction_canceled && !frame.input_result.camera_changed &&
      !frame.input_result.view_changed && !frame.input.active_transform_drag;
  const int hit = pointer_allowed ? interaction_.hit_test(
      frame.left_pressed ? frame.left_click_local : frame.input.mouse_local) : -1;
  interaction_.hover(hit);
  if (hit < 0 || !frame.left_pressed) return {.gizmo_hovered = hit >= 0};
  const auto scene = editor.scene_documents().active_scene_id();
  if (!scene || !presentation_.bounds()) return {.gizmo_consumed = true};
  const auto handle = interaction_.frame().handles[static_cast<std::size_t>(hit)];
  const auto bounds = *presentation_.bounds();
  interaction_.begin(hit, bounds, frame.camera, frame.left_click_local);
  const auto ray = frame.camera.ray_from_viewport_point(frame.left_click_local);
  gesture_ = Gesture{
      .pointer = viewport_pointer_gesture_from_latest_edge(frame.input.pointer_pressed_edges,
          QuaderMouseButton::Left, frame.left_click_local, frame.input.mouse_screen,
          frame.input.modifiers, frame.pointer_drag_threshold_pixels),
      .scene = *scene,
      .mode_generation = editor.mode().generation(),
      .selection_revision = editor.selection().summary().selection_revision,
      .document_generation = editor.query().document_binding().document_binding_generation,
      .settle_tool = frame.input.modal_tool_active || frame.input.active_tool_owns_viewport ||
                     frame.input.operation_preview_active,
      .request = {
          .tool = QuaderTransformTool::Scale,
          .axis = handle.axis == 0 ? QuaderTransformAxis::X
              : handle.axis == 1 ? QuaderTransformAxis::Y : QuaderTransformAxis::Z,
          .space = editor.viewports().transform_space(),
          .orientation = {.x_axis = bounds.world_x, .y_axis = bounds.world_y,
                          .z_axis = bounds.world_z, .origin = bounds.world_origin},
          .pivot = handle.opposite_world_position,
          .screen_position = frame.left_click_local,
          .ray = {.origin = ray.origin, .direction = ray.direction},
          .camera = to_api_viewport_camera(ViewportCameraAdapter::modeling_camera(
              frame.camera, frame.input.viewport_size)),
          .grid_size = viewport_grid_world_size_for_preset(frame.input.grid_preset),
          .grid_snap_enabled = viewport_grid_snap_active(frame.settings, frame.input.modifiers.primary),
          .bounds_resize_reference = QuaderBoundsResizeReference{.moving_point = handle.world_position},
      }};
  return {.gizmo_consumed = true, .gizmo_hovered = true};
}

} // namespace quader
