////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_interaction_controller.hpp"

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>
#include <optional>
#include <string_view>

namespace quader {
namespace {

bool material_selection_gesture_mode(ViewportSelectionMode mode) {
  return mode == ViewportSelectionMode::Object ||
         mode == ViewportSelectionMode::Face;
}

bool material_face_source_gesture_mode(ViewportSelectionMode mode) {
  return mode == ViewportSelectionMode::Vertex ||
         mode == ViewportSelectionMode::Edge ||
         mode == ViewportSelectionMode::Face ||
         mode == ViewportSelectionMode::Object ||
         mode == ViewportSelectionMode::Group;
}

bool material_interaction_blocked(
    const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool) {
  return !frame.input.viewport_focused || frame.input.text_input_active ||
         frame.input.navigation_active || frame.input_result.navigation_active ||
         frame.input.modal_tool_active ||
         frame.input.active_tool_owns_viewport ||
         frame.input.active_transform_drag ||
         frame.input.operation_preview_active || before_tool.gizmo_consumed ||
         tool_result.handled;
}

QuaderHoverRequest material_face_hover_request(
    const ViewportInteractionFrame &frame) {
  const ViewportRay ray =
      frame.camera.ray_from_viewport_point(frame.right_click_local);
  return {
      .ray = {.origin = ray.origin, .direction = ray.direction},
      .selected_modifier_pressed = false,
      .face_plane_hover = true,
      .vertex_radius = frame.settings.float_value(
          ViewportFloatSettingId::PickVertexRadius),
      .edge_radius = frame.settings.float_value(
          ViewportFloatSettingId::PickEdgeRadius),
  };
}

void log_material_interaction_failure(std::string_view label,
                                      const QuaderApiStatus &result) {
  if (result.ok()) {
    return;
  }
  LOG_ERROR("{} failed: {}", label,
            result.error_message().empty() ? "Material interaction failed."
                                           : result.error_message());
}

} // namespace

ViewportBeforeToolInteractionResult
ViewportInteractionController::handle_before_active_tool(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  QDR_PROFILE_FUNCTION();
  return transform_gizmo_.handle_frame(editor, frame);
}

void ViewportInteractionController::handle_after_active_tool(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool,
    ViewportToolHoverPort *tool_hover) {
  QDR_PROFILE_FUNCTION();
  handle_material_interactions(editor, frame, tool_result, before_tool);
  selection_.handle_frame(editor, frame, tool_result, before_tool, tool_hover);
}

void ViewportInteractionController::handle_material_interactions(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool) {
  const bool sample_face = frame.input.sample_face_material_pressed;
  const bool apply_selection =
      frame.input.apply_active_material_to_selection_pressed &&
      !frame.input.apply_face_material_to_mesh_pressed;
  const bool apply_face_to_mesh =
      frame.input.apply_face_material_to_mesh_pressed;

  if (!sample_face && !apply_selection && !apply_face_to_mesh &&
      !pending_apply_active_material_.has_value()) {
    return;
  }

  // The native double-click sequence includes the first click. Always cancel
  // that delayed single before evaluating whether the double is currently
  // eligible, so a blocked or missed double can never apply later.
  if (apply_face_to_mesh) {
    cancel_pending_material_interaction();
  }

  const ViewportSelectionMode selection_mode =
      frame.input_router.state().selection_mode;
  const bool material_face_source_enabled =
      material_face_source_gesture_mode(selection_mode);
  if (material_interaction_blocked(frame, tool_result, before_tool)) {
    cancel_pending_material_interaction();
    return;
  }
  if (pending_apply_active_material_.has_value() &&
      !material_selection_gesture_mode(selection_mode)) {
    cancel_pending_material_interaction();
  }

  const auto pick_face = [&]() -> std::optional<QuaderHoverTarget> {
    ++material_interaction_work_stats_.face_pick_count;
    const QuaderHoverSummary hover = editor.selection().preview_hover(
        material_face_hover_request(frame));
    if (hover.target.kind != QuaderActiveSelectionKind::Face ||
        hover.target.object_id <= 0 || hover.target.face_id < 0) {
      return std::nullopt;
    }
    return hover.target;
  };

  if (apply_face_to_mesh && frame.input.viewport_hovered &&
      material_face_source_enabled) {
    if (const std::optional<QuaderHoverTarget> target = pick_face()) {
      ++material_interaction_work_stats_.apply_face_to_mesh_request_count;
      log_material_interaction_failure(
          "Apply face material to mesh",
          editor.modeling().apply_face_material_to_mesh(
              static_cast<QuaderModelingObjectId>(target->object_id),
              static_cast<QuaderElementId>(target->face_id)));
    }
  }

  if (sample_face && frame.input.viewport_hovered &&
      material_face_source_enabled) {
    if (const std::optional<QuaderHoverTarget> target = pick_face()) {
      ++material_interaction_work_stats_.sample_request_count;
      const QuaderApiResult<QuaderFaceProjectMaterial> material =
          editor.modeling().project_material_for_face(
              static_cast<QuaderModelingObjectId>(target->object_id),
              static_cast<QuaderElementId>(target->face_id));
      if (!material.ok()) {
        log_material_interaction_failure("Read clicked face material",
                                         material);
      } else {
        log_material_interaction_failure(
            "Set active face material",
            editor.project_assets().set_active_material(
                material.value().id));
      }
    }
  }

  if (apply_selection && frame.input.viewport_hovered &&
      material_selection_gesture_mode(selection_mode)) {
    const double timeout =
        frame.input.apply_active_material_delay_for_double_click
            ? std::max(frame.double_click_interval_seconds, 0.0)
            : 0.0;
    if (timeout <= 0.0) {
      ++material_interaction_work_stats_.apply_selection_command_count;
      log_material_interaction_failure(
          "Apply active material to selection",
          editor.commands().execute(quader_command::kApplyMaterial));
    } else {
      const std::optional<SceneSessionId> scene_session_id =
          editor.scene_documents().active_scene_id();
      const QuaderApiResult<QuaderProjectMaterialInfo> active_material =
          editor.project_assets().active_material();
      if (!scene_session_id.has_value() || !active_material.ok()) {
        cancel_pending_material_interaction();
        return;
      }
      pending_apply_active_material_ = PendingApplyActiveMaterial{
          .deadline_seconds = frame.monotonic_time_seconds + timeout,
          .scene_session_id = scene_session_id->packed(),
          .selection_revision =
              frame.selection_state.summary.selection_revision,
          .active_material_id =
              std::string(active_material.value().id.str()),
      };
    }
    return;
  }

  if (!pending_apply_active_material_.has_value()) {
    return;
  }
  if (frame.monotonic_time_seconds <
      pending_apply_active_material_->deadline_seconds) {
    return;
  }

  const PendingApplyActiveMaterial pending =
      *pending_apply_active_material_;
  cancel_pending_material_interaction();
  if (frame.selection_state.summary.selection_revision !=
      pending.selection_revision) {
    return;
  }
  const std::optional<SceneSessionId> scene_session_id =
      editor.scene_documents().active_scene_id();
  if (!scene_session_id.has_value() ||
      scene_session_id->packed() != pending.scene_session_id) {
    return;
  }
  const QuaderApiResult<QuaderProjectMaterialInfo> active_material =
      editor.project_assets().active_material();
  if (!active_material.ok() ||
      active_material.value().id.str() !=
          std::string_view(pending.active_material_id)) {
    return;
  }
  ++material_interaction_work_stats_.apply_selection_command_count;
  log_material_interaction_failure(
      "Apply active material to selection",
      editor.commands().execute(quader_command::kApplyMaterial));
}

void ViewportInteractionController::cancel_pending_material_interaction() {
  pending_apply_active_material_.reset();
}

QuaderSelectionSummary ViewportInteractionController::pick_context_menu_target(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  return selection_.pick_context_menu_target(editor, frame);
}

bool ViewportInteractionController::cancel_active_drag(
    QuaderEditorApi &editor) {
  return transform_gizmo_.cancel_active_drag(editor);
}

bool ViewportInteractionController::dragging() const {
  return transform_gizmo_.dragging();
}

void ViewportInteractionController::reset(QuaderEditorApi* editor) {
  transform_gizmo_.reset(editor);
  selection_.reset();
  cancel_pending_material_interaction();
}

const quader_editor_core::TransformGizmoFrame *
ViewportInteractionController::transform_gizmo_frame() const {
  return transform_gizmo_.frame();
}

const TransformGizmoSelectionCacheStats &
ViewportInteractionController::transform_gizmo_selection_cache_stats() const {
  return transform_gizmo_.selection_cache_stats();
}

ViewportSelectionMarquee
ViewportInteractionController::selection_marquee() const {
  return selection_.marquee();
}

const ViewportInteractionController::MaterialInteractionWorkStats &
ViewportInteractionController::material_interaction_work_stats() const {
  return material_interaction_work_stats_;
}

void ViewportInteractionController::sync_tool_runtime(ViewportToolId tool) {
  if (tool_runtime_.active_tool() == nullptr ||
      tool_runtime_.active_tool()->viewport_tool() != tool) {
    tool_runtime_.sync_active_tool(tool);
  }
}

bool ViewportInteractionController::active_tool_owns_viewport(
    ViewportToolId tool) {
  sync_tool_runtime(tool);
  return tool_runtime_.active_tool_owns_viewport();
}

bool ViewportInteractionController::active_tool_blocks_viewport_focus_shortcuts(
    ViewportToolId tool) {
  sync_tool_runtime(tool);
  return tool_runtime_.active_tool_blocks_viewport_focus_shortcuts();
}

bool ViewportInteractionController::active_tool_allows_selection_fallback(
    ViewportToolId tool) const {
  return tool_runtime_.tool_allows_selection_fallback(tool);
}

bool ViewportInteractionController::has_active_modal_preview(
    const ViewportToolPreviewPort &preview, bool snap_enabled) const {
  return tool_runtime_.has_active_modal_preview(preview, snap_enabled);
}

ModelingBoxPreview ViewportInteractionController::box_preview(
    const ViewportToolPreviewPort &preview, bool snap_enabled) const {
  return tool_runtime_.box_preview(preview, snap_enabled);
}

const ViewportTool *
ViewportInteractionController::active_tool(ViewportToolId tool) {
  sync_tool_runtime(tool);
  return tool_runtime_.active_tool();
}

bool ViewportInteractionController::set_active_tool(
    ViewportToolState &state, ViewportToolId tool, ViewportInputResult *result) {
  return tool_runtime_.set_active_tool(state, tool, result);
}

void ViewportInteractionController::clear_modal_tool_state() {
  tool_runtime_.clear_modal_tool_state();
}

void ViewportInteractionController::clear_modal_drag_state() {
  tool_runtime_.clear_modal_drag_state();
}

ViewportToolResult ViewportInteractionController::handle_active_tool_event(
    ViewportToolState &state, ViewportToolContext &context,
    const ViewportToolEvent &event) {
  sync_tool_runtime(state.active_tool);
  return tool_runtime_.handle_active_tool_event(state, context, event);
}

ViewportNativeToolInteractionResult
ViewportInteractionController::handle_native_tools(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    PivotToolService *pivot, LinearArrayToolService *linear_array,
    RadialArrayToolService *radial_array,
    ViewportPointPlacementService *point_placement,
    ViewportCameraController &camera,
    const ViewportNativeToolInteractionFrame &frame) {
  return native_tools_.handle_frame(editor, document, assets, pivot,
                                    linear_array, radial_array,
                                    point_placement, camera, frame);
}

void ViewportInteractionController::clear_native_tool_drag_state() {
  native_tools_.clear_drag_state();
}

bool ViewportInteractionController::native_tool_dragging() const {
  return native_tools_.dragging();
}

} // namespace quader
