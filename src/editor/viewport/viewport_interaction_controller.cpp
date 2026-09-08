////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_interaction_controller.hpp"

#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/viewport/primitive_creation_api_adapter.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "editor/viewport/viewport_path_array_source_picking.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>
#include <optional>
#include <string_view>

namespace quader {
namespace {

bool material_face_source_gesture_mode(ViewportSelectionMode mode) {
  return mode == ViewportSelectionMode::Vertex ||
         mode == ViewportSelectionMode::Edge ||
         mode == ViewportSelectionMode::Face ||
         mode == ViewportSelectionMode::Object ||
         mode == ViewportSelectionMode::Group;
}

bool material_input_blocked(const ViewportInteractionFrame &frame) {
  return !frame.input.viewport_focused || frame.input.text_input_active ||
         frame.input.navigation_active || frame.input_result.navigation_active ||
         frame.input.modal_tool_active ||
         frame.input.active_tool_owns_viewport ||
         frame.input.active_transform_drag ||
         frame.input.operation_preview_active;
}

bool material_interaction_blocked(
    const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool) {
  return material_input_blocked(frame) || before_tool.gizmo_consumed ||
         tool_result.handled;
}

QuaderHoverRequest material_face_hover_request(
    const ViewportInteractionFrame &frame, QVec2 local_mouse) {
  const ViewportRay ray =
      frame.camera.ray_from_viewport_point(local_mouse);
  return {
      .ray = {.origin = ray.origin, .direction = ray.direction},
      .selected_modifier_pressed = false,
      .face_plane_hover = true,
      .vertex_radius = frame.settings.float_value(
          ViewportFloatSettingId::PickVertexRadius),
      .edge_radius = frame.settings.float_value(
          ViewportFloatSettingId::PickEdgeRadius),
      .select_backfaces = frame.settings.bool_value(
          ViewportBoolSettingId::BackfaceSelection),
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

struct PointerButtonFrameState final {
  bool pressed = false;
  bool down = false;
  bool released = false;
};

bool input_chord_uses_button(const QuaderInputChord &chord,
                             QuaderMouseButton button) {
  return chord.device == QuaderInputDevice::Mouse &&
         chord.mouse_button == button;
}

bool input_chords_use_button(std::span<const QuaderInputChord> chords,
                             QuaderMouseButton button) {
  return std::ranges::any_of(chords, [button](const auto &chord) {
    return input_chord_uses_button(chord, button);
  });
}

PointerButtonFrameState pointer_button_state(
    const ViewportInteractionFrame &frame, QuaderMouseButton button) {
  PointerButtonFrameState state{
      .pressed = viewport_pointer_latest_edge(
                     frame.input.pointer_pressed_edges, button) != nullptr,
      .down = input_chords_use_button(frame.input.active_input_chords, button),
      .released = viewport_pointer_latest_edge(
                      frame.input.pointer_released_edges, button) != nullptr,
  };
  switch (button) {
  case QuaderMouseButton::Left:
    state.pressed = state.pressed || frame.left_pressed;
    state.down = state.down || frame.left_down;
    state.released = state.released || frame.left_released;
    break;
  case QuaderMouseButton::Middle:
    state.pressed = state.pressed || frame.input.middle_pressed;
    state.down = state.down || frame.input.middle_down;
    state.released = state.released || frame.input.middle_released;
    break;
  case QuaderMouseButton::Right:
    state.pressed = state.pressed || frame.right_pressed ||
                    frame.input.right_pressed;
    state.down = state.down || frame.right_down || frame.input.right_down;
    state.released = state.released || frame.right_released ||
                     frame.input.right_released;
    break;
  default:
    break;
  }
  return state;
}

QVec2 pointer_press_fallback_local(const ViewportInteractionFrame &frame,
                                   QuaderMouseButton button) {
  if (button == QuaderMouseButton::Left) {
    return frame.left_click_local;
  }
  if (button == QuaderMouseButton::Right) {
    return frame.right_click_local;
  }
  return frame.input.mouse_local;
}

const ViewportPointerButtonEdge *exact_pointer_release_after(
    std::span<const ViewportPointerButtonEdge> edges,
    const ViewportPointerButtonEdge &press) {
  const ViewportPointerButtonEdge *matching = nullptr;
  for (const ViewportPointerButtonEdge &edge : edges) {
    if (edge.button != press.button ||
        (press.sequence != 0u && edge.sequence != 0u &&
         edge.sequence <= press.sequence)) {
      continue;
    }
    if (matching == nullptr || matching->sequence == 0u ||
        (edge.sequence != 0u && edge.sequence < matching->sequence)) {
      matching = &edge;
    }
  }
  return matching;
}

const ViewportSemanticPointerEdge *texture_transfer_edge(
    const ViewportInputFrame &input, ViewportSemanticPointerEdgeKind kind) {
  const auto found = std::find_if(
      input.transfer_face_texture_edges.rbegin(),
      input.transfer_face_texture_edges.rend(),
      [kind](const auto &edge) { return edge.kind == kind; });
  return found == input.transfer_face_texture_edges.rend() ? nullptr
                                                           : &*found;
}

bool texture_transfer_trigger_matches(
    const ViewportSemanticPointerEdge &trigger,
    const ViewportPointerGesture &gesture,
    const PointerButtonFrameState &button_state,
    const ViewportPointerButtonEdge *exact_release) {
  if (trigger.semantic_chord.device != QuaderInputDevice::Mouse ||
      trigger.semantic_chord.mouse_button != gesture.press.button) {
    return false;
  }
  if (!trigger.pointer_edge.has_value()) {
    // Sequence-zero synthetic callers still prove the same semantic button
    // through the canonical frame state. Production always carries an edge.
    return gesture.press.sequence == 0u &&
           (trigger.semantic_chord.gesture == QuaderInputGesture::Click
                ? button_state.released
                : button_state.pressed);
  }
  if (trigger.pointer_edge->button != gesture.press.button) {
    return false;
  }
  const std::uint64_t press_sequence = gesture.press.sequence;
  const std::uint64_t edge_sequence = trigger.pointer_edge->sequence;
  if (trigger.semantic_chord.gesture == QuaderInputGesture::Click) {
    return exact_release != nullptr &&
           (exact_release->sequence == edge_sequence ||
            exact_release->sequence == 0u || edge_sequence == 0u);
  }
  return press_sequence == 0u || edge_sequence == 0u ||
         edge_sequence >= press_sequence;
}

} // namespace

ViewportBeforeToolInteractionResult
ViewportInteractionController::handle_before_active_tool(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  return handle_before_active_tool_impl(editor, frame, nullptr, nullptr);
}

ViewportBeforeToolInteractionResult
ViewportInteractionController::handle_before_active_tool(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    NativeViewportToolCoordinator &native_tools) {
  return handle_before_active_tool_impl(
      editor, frame, &native_tools.curve_interaction(), &native_tools);
}

ViewportBeforeToolInteractionResult
ViewportInteractionController::handle_before_active_tool_impl(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    ViewportCurveInteractionPort *curve,
    NativeViewportToolCoordinator *native_tools) {
  QDR_PROFILE_FUNCTION();
  interaction_cursor_ = ViewportPointerCursor::Default;
  if (!point_light_range_.pointer_active()) {
    if (const auto transfer =
            reserve_face_texture_transfer(editor, frame, native_tools)) {
      return *transfer;
    }
  }
  ViewportInteractionFrame prepared = frame;
  const std::optional<ViewportInteractionSelectionState> resolved_selection =
      selection_.prepare_frame(editor, frame);
  if (resolved_selection.has_value()) {
    prepared.selection_state = *resolved_selection;
  }
  if (frame.input_result.cancel_requested) {
    (void)selection_.cancel_lasso_drag();
    (void)point_light_range_.cancel(editor);
    (void)transform_gizmo_.cancel_active_drag(editor, curve);
    return {.gizmo_consumed = true};
  }
  if (frame.input_router.state().active_tool == ViewportToolId::LassoSelect ||
      selection_.lasso_pointer_active()) {
    return {.selection_after_pending_pick = resolved_selection};
  }
  if (!transform_gizmo_.pointer_interaction_active() &&
      point_light_range_.handle_frame(editor, prepared)) {
    interaction_cursor_ = point_light_range_.pointer_cursor();
    selection_.cancel_pending_click();
    return {.selection_after_pending_pick = resolved_selection,
            .gizmo_consumed = true};
  }
  ViewportBeforeToolInteractionResult result =
      transform_gizmo_.handle_frame(editor, prepared, curve);
  interaction_cursor_ = result.gizmo_consumed ||
                                transform_gizmo_.pointer_interaction_active() ||
                                point_light_range_.pointer_cursor() ==
                                    ViewportPointerCursor::Default
                            ? transform_gizmo_.pointer_cursor()
                            : point_light_range_.pointer_cursor();
  result.selection_after_pending_pick = resolved_selection;
  return result;
}

std::optional<ViewportBeforeToolInteractionResult>
ViewportInteractionController::reserve_face_texture_transfer(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    NativeViewportToolCoordinator *native_tools) {
  if (transfer_release_suppression_.active()) {
    const QuaderMouseButton suppressed_button =
        transfer_release_suppression_.button();
    const PointerButtonFrameState suppressed =
        pointer_button_state(frame, suppressed_button);
    if (transfer_release_suppression_.consume(
            suppressed_button, suppressed.pressed, suppressed.down,
            suppressed.released)) {
      return ViewportBeforeToolInteractionResult{
          .gizmo_consumed = true, .transfer_face_texture_consumed = true};
    }
  }
  const ViewportSemanticPointerEdge *trigger = texture_transfer_edge(
      frame.input, ViewportSemanticPointerEdgeKind::Trigger);
  const ViewportSemanticPointerEdge *reserve = texture_transfer_edge(
      frame.input, ViewportSemanticPointerEdgeKind::ReservePress);
  if (transfer_face_texture_pointer_.has_value()) {
    // Consume the release even if focus, scene, or mode changes. No click was
    // started for selection or a gizmo, so none may be synthesized on release.
    const bool source_context_matches =
        !material_input_blocked(frame) && !frame.input_result.cancel_requested &&
        !frame.input_result.camera_changed &&
        frame.input_router.state().selection_mode == ViewportSelectionMode::Face &&
        frame.selection_state.summary.selection_revision ==
            transfer_face_texture_pointer_->selection_revision &&
        editor.scene_documents().active_scene_id().value_or(SceneSessionId{}) ==
            transfer_face_texture_pointer_->scene_session_id;
    const QuaderMouseButton button =
        transfer_face_texture_pointer_->pointer.press.button;
    const PointerButtonFrameState button_state =
        pointer_button_state(frame, button);
    const ViewportPointerButtonEdge *release = exact_pointer_release_after(
        frame.input.pointer_released_edges,
        transfer_face_texture_pointer_->pointer.press);
    const bool matching_release = release != nullptr ||
                                  (transfer_face_texture_pointer_->pointer.press
                                               .sequence == 0u &&
                                   button_state.released);
    const QVec2 release_local =
        release != nullptr ? release->local : frame.input.mouse_local;
    const bool trigger_matches =
        trigger != nullptr &&
        texture_transfer_trigger_matches(
            *trigger, transfer_face_texture_pointer_->pointer, button_state,
            release);
    const bool legacy_release_trigger =
        trigger == nullptr && frame.input.transfer_face_texture_pressed &&
        matching_release;

    // A second physical press can be Qt's DoubleClick edge. Replace the
    // retained press only after the semantic trigger proved it belongs to the
    // same Settings-resolved button.
    if (trigger_matches && trigger->pointer_edge.has_value() &&
        trigger->semantic_chord.gesture != QuaderInputGesture::Click) {
      transfer_face_texture_pointer_->pointer.press = *trigger->pointer_edge;
      transfer_face_texture_pointer_->semantic_chord =
          trigger->semantic_chord;
    }

    if (button_state.released || !button_state.down ||
        !source_context_matches) {
      transfer_release_suppression_.begin(button, button_state.down);
      transfer_face_texture_pointer_.reset();
    }
    return ViewportBeforeToolInteractionResult{
        .gizmo_consumed = true,
        .transfer_face_texture_consumed = true,
        .transfer_face_texture_requested =
            source_context_matches &&
            (legacy_release_trigger ||
             (trigger_matches &&
              (trigger->semantic_chord.gesture != QuaderInputGesture::Click ||
               matching_release))),
        .transfer_face_texture_local =
            trigger_matches && trigger->pointer_edge.has_value()
                ? trigger->pointer_edge->local
                : release_local,
    };
  }
  if ((!frame.input.transfer_face_texture_pressed &&
       !frame.input.transfer_face_texture_pointer_reserved &&
       trigger == nullptr && reserve == nullptr) ||
      frame.input_router.state().selection_mode != ViewportSelectionMode::Face ||
      material_input_blocked(frame) || frame.input_result.cancel_requested ||
      frame.input_result.camera_changed ||
      transform_gizmo_.pointer_interaction_active() ||
      selection_.pointer_interaction_active() ||
      (native_tools != nullptr && native_tools->pointer_dragging())) {
    return std::nullopt;
  }

  const ViewportSemanticPointerEdge *pointer_action =
      trigger != nullptr ? trigger : reserve;
  if (pointer_action != nullptr) {
    const QuaderInputChord &semantic = pointer_action->semantic_chord;
    if (semantic.device != QuaderInputDevice::Mouse ||
        semantic.mouse_button == QuaderMouseButton::None) {
      return ViewportBeforeToolInteractionResult{
          .gizmo_consumed = true, .transfer_face_texture_consumed = true};
    }
    // A Click can execute only through the exact press retained by this
    // owner. A stale/orphan semantic release is consumed without mutation.
    if (pointer_action->kind == ViewportSemanticPointerEdgeKind::Trigger &&
        semantic.gesture == QuaderInputGesture::Click) {
      return ViewportBeforeToolInteractionResult{
          .gizmo_consumed = true, .transfer_face_texture_consumed = true};
    }

    selection_.cancel_pending_click();
    const PointerButtonFrameState button_state =
        pointer_button_state(frame, semantic.mouse_button);
    const ViewportPointerButtonEdge accepted_edge =
        pointer_action->pointer_edge.value_or(ViewportPointerButtonEdge{
            .button = semantic.mouse_button,
            .local = pointer_press_fallback_local(frame,
                                                  semantic.mouse_button),
        });
    transfer_face_texture_pointer_ = FaceTextureTransferPointer{
        .pointer = {
            .press = accepted_edge,
            .press_screen = frame.input.mouse_screen,
            .modifiers = frame.input.modifiers,
            .drag_threshold_pixels = frame.pointer_drag_threshold_pixels,
        },
        .semantic_chord = semantic,
        .scene_session_id =
            editor.scene_documents().active_scene_id().value_or(SceneSessionId{}),
        .selection_revision = frame.selection_state.summary.selection_revision,
    };
    if (!button_state.down && !button_state.pressed) {
      transfer_face_texture_pointer_.reset();
    }
    return ViewportBeforeToolInteractionResult{
        .gizmo_consumed = true,
        .transfer_face_texture_consumed = true,
        .transfer_face_texture_requested =
            pointer_action->kind == ViewportSemanticPointerEdgeKind::Trigger,
        .transfer_face_texture_local = accepted_edge.local,
    };
  }

  // Reserve before gizmo picking and keep the source selection even when the
  // target misses or the modeling owner rejects the selected source faces.
  selection_.cancel_pending_click();
  QuaderMouseButton legacy_button = QuaderMouseButton::None;
  bool legacy_fresh_press = false;
  if (!frame.input.pointer_pressed_edges.empty()) {
    legacy_button = frame.input.pointer_pressed_edges.back().button;
  } else if (frame.left_pressed || frame.left_down) {
    legacy_button = QuaderMouseButton::Left;
  } else if (frame.input.middle_pressed || frame.input.middle_down) {
    legacy_button = QuaderMouseButton::Middle;
  } else if (frame.right_pressed || frame.right_down) {
    legacy_button = QuaderMouseButton::Right;
  }
  if (legacy_button != QuaderMouseButton::None) {
    const PointerButtonFrameState legacy_state =
        pointer_button_state(frame, legacy_button);
    legacy_fresh_press = legacy_state.pressed;
    transfer_face_texture_pointer_ = FaceTextureTransferPointer{
        .pointer = viewport_pointer_gesture_from_latest_edge(
            frame.input.pointer_pressed_edges, legacy_button,
            pointer_press_fallback_local(frame, legacy_button),
            frame.input.mouse_screen,
            frame.input.modifiers, frame.pointer_drag_threshold_pixels),
        .semantic_chord = {
            .device = QuaderInputDevice::Mouse,
            .mouse_button = legacy_button,
            .gesture = frame.input.transfer_face_texture_pointer_reserved
                           ? QuaderInputGesture::Click
                           : QuaderInputGesture::Press,
        },
        .scene_session_id =
            editor.scene_documents().active_scene_id().value_or(SceneSessionId{}),
        .selection_revision = frame.selection_state.summary.selection_revision,
    };
    if (!legacy_state.down && !legacy_state.pressed) {
      transfer_face_texture_pointer_.reset();
    }
  }
  return ViewportBeforeToolInteractionResult{
      .gizmo_consumed = true,
      .transfer_face_texture_consumed = true,
      .transfer_face_texture_requested =
          frame.input.transfer_face_texture_pressed &&
          (legacy_fresh_press || frame.input.pointer_released_edges.empty()),
      .transfer_face_texture_local =
          legacy_button != QuaderMouseButton::None
              ? pointer_press_fallback_local(frame, legacy_button)
              : frame.input.mouse_local,
  };
}

void ViewportInteractionController::handle_after_active_tool(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool,
    ViewportToolHoverPort *tool_hover,
    ViewportRuntimeBindingToken runtime_binding) {
  handle_after_active_tool_impl(editor, frame, tool_result, before_tool,
                                tool_hover, nullptr, runtime_binding);
}

void ViewportInteractionController::handle_after_active_tool_impl(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool,
    ViewportToolHoverPort *tool_hover,
    ViewportCurveInteractionPort *curve,
    ViewportRuntimeBindingToken runtime_binding) {
  QDR_PROFILE_FUNCTION();
  if (!runtime_binding.current()) {
    return;
  }
  ViewportInteractionFrame prepared = frame;
  if (before_tool.selection_after_pending_pick.has_value()) {
    prepared.selection_state = *before_tool.selection_after_pending_pick;
  }
  if (frame.input_result.cancel_requested) {
    (void)transform_gizmo_.cancel_active_drag(editor, curve);
    if (!runtime_binding.current()) {
      return;
    }
  }
  handle_material_interactions(editor, prepared, tool_result, before_tool,
                               runtime_binding);
  if (!runtime_binding.current()) {
    return;
  }
  selection_.handle_frame(editor, prepared, tool_result, before_tool, tool_hover,
                          curve);
}

void ViewportInteractionController::handle_after_active_tool(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool,
    NativeViewportToolCoordinator &native_tools,
    ViewportToolHoverPort *tool_hover,
    ViewportRuntimeBindingToken runtime_binding) {
  handle_after_active_tool_impl(editor, frame, tool_result, before_tool,
                                tool_hover,
                                &native_tools.curve_interaction(),
                                runtime_binding);
}

void ViewportInteractionController::handle_material_interactions(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool,
    ViewportRuntimeBindingToken runtime_binding) {
  if (!runtime_binding.current()) {
    return;
  }
  if (frame.input.transfer_face_texture_pressed) {
    pending_apply_active_material_.reset();
    if (!before_tool.transfer_face_texture_requested ||
        material_input_blocked(frame) || frame.input_result.cancel_requested ||
        frame.input_result.camera_changed || tool_result.handled ||
        !frame.input.viewport_hovered ||
        frame.input_router.state().selection_mode != ViewportSelectionMode::Face) {
      return;
    }
    const QVec2 local_mouse = before_tool.transfer_face_texture_local;
    // Routing a held gesture to its original pane can retain logical hover;
    // a release outside that pane still must not edit through an outside ray.
    if (!(local_mouse.x >= 0.0f && local_mouse.y >= 0.0f &&
          local_mouse.x < frame.input.viewport_size.x &&
          local_mouse.y < frame.input.viewport_size.y)) {
      return;
    }
    if (const std::optional<QuaderHoverTarget> target =
            pick_material_face_target(editor, frame, local_mouse)) {
      ++material_interaction_work_stats_.transfer_face_texture_request_count;
      const QuaderApiVoidResult transferred =
          editor.modeling().transfer_face_texture_to_face(
              static_cast<QuaderModelingObjectId>(target->object_id),
              static_cast<QuaderElementId>(target->face_id));
      if (!runtime_binding.current()) {
        return;
      }
      log_material_interaction_failure("Continue texture from selected face",
                                       transferred);
    }
    return;
  }

  const bool sample_face = frame.input.sample_face_material_pressed;
  const bool apply_target =
      frame.input.apply_active_material_to_selection_pressed &&
      !frame.input.apply_face_material_to_mesh_pressed;
  const bool apply_face_to_mesh =
      frame.input.apply_face_material_to_mesh_pressed;

  if (!sample_face && !apply_target && !apply_face_to_mesh &&
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
      pending_apply_active_material_->selection_mode != selection_mode) {
    cancel_pending_material_interaction();
  }

  if (apply_face_to_mesh && frame.input.viewport_hovered &&
      material_face_source_enabled) {
    if (const std::optional<QuaderHoverTarget> target =
            pick_material_face_target(editor, frame)) {
      ++material_interaction_work_stats_.apply_face_to_mesh_request_count;
      const QuaderApiVoidResult applied =
          editor.modeling().apply_face_material_to_mesh(
              static_cast<QuaderModelingObjectId>(target->object_id),
              static_cast<QuaderElementId>(target->face_id));
      if (!runtime_binding.current()) {
        return;
      }
      log_material_interaction_failure("Apply face material to mesh", applied);
    }
  }

  if (sample_face && frame.input.viewport_hovered &&
      material_face_source_enabled) {
    if (const std::optional<QuaderHoverTarget> target =
            pick_material_face_target(editor, frame)) {
      ++material_interaction_work_stats_.sample_request_count;
      const QuaderApiResult<QuaderFaceProjectMaterial> material =
          editor.modeling().project_material_for_face(
              static_cast<QuaderModelingObjectId>(target->object_id),
              static_cast<QuaderElementId>(target->face_id));
      if (!runtime_binding.current()) {
        return;
      }
      if (!material.ok()) {
        log_material_interaction_failure("Read clicked face material",
                                         material);
      } else {
        const QuaderApiVoidResult activated =
            editor.project_assets().set_active_material(material.value().id);
        if (!runtime_binding.current()) {
          return;
        }
        log_material_interaction_failure("Set active face material",
                                         activated);
      }
    }
  }

  if (apply_target && frame.input.viewport_hovered &&
      material_face_source_enabled) {
    const std::optional<QuaderHoverTarget> target =
        pick_material_face_target(editor, frame);
    if (!target.has_value()) {
      cancel_pending_material_interaction();
      return;
    }
    const double timeout =
        frame.input.apply_active_material_delay_for_double_click
            ? std::max(frame.double_click_interval_seconds, 0.0)
            : 0.0;
    if (timeout <= 0.0) {
      ++material_interaction_work_stats_.apply_target_request_count;
      const QuaderApiVoidResult applied =
          selection_mode == ViewportSelectionMode::Face
              ? editor.modeling().apply_active_material_to_face(
                    static_cast<QuaderModelingObjectId>(target->object_id),
                    static_cast<QuaderElementId>(target->face_id))
              : editor.modeling().apply_active_material_to_mesh(
                    static_cast<QuaderModelingObjectId>(target->object_id),
                    static_cast<QuaderElementId>(target->face_id));
      if (!runtime_binding.current()) {
        return;
      }
      log_material_interaction_failure("Apply active material under mouse",
                                       applied);
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
          .pointer = viewport_pointer_gesture_from_latest_edge(
              frame.input.pointer_released_edges, QuaderMouseButton::Left,
              frame.input.mouse_local, frame.input.mouse_screen,
              frame.input.modifiers, 4.0f,
              frame.monotonic_time_seconds + timeout),
          .scene_session_id = scene_session_id->packed(),
          .selection_mode = selection_mode,
          .modeling_object_id = target->object_id,
          .face_id = target->face_id,
          .active_material_id =
              std::string(active_material.value().id.str()),
      };
    }
    return;
  }

  if (!pending_apply_active_material_.has_value()) {
    return;
  }
  if (!viewport_pointer_deadline_reached(
          pending_apply_active_material_->pointer,
          frame.monotonic_time_seconds)) {
    return;
  }

  const PendingApplyActiveMaterial pending =
      *pending_apply_active_material_;
  cancel_pending_material_interaction();
  if (selection_mode != pending.selection_mode) {
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
  ++material_interaction_work_stats_.apply_target_request_count;
  const QuaderApiVoidResult applied =
      pending.selection_mode == ViewportSelectionMode::Face
          ? editor.modeling().apply_active_material_to_face(
                static_cast<QuaderModelingObjectId>(
                    pending.modeling_object_id),
                static_cast<QuaderElementId>(pending.face_id))
          : editor.modeling().apply_active_material_to_mesh(
                static_cast<QuaderModelingObjectId>(
                    pending.modeling_object_id),
                static_cast<QuaderElementId>(pending.face_id));
  if (!runtime_binding.current()) {
    return;
  }
  log_material_interaction_failure("Apply active material under mouse",
                                   applied);
}

void ViewportInteractionController::cancel_pending_material_interaction() {
  pending_apply_active_material_.reset();
  if (transfer_face_texture_pointer_.has_value()) {
    transfer_release_suppression_.begin(
        transfer_face_texture_pointer_->pointer.press.button, true);
  }
  transfer_face_texture_pointer_.reset();
}

ViewportContextMenuPointerResult
ViewportInteractionController::handle_context_menu_pointer(
    const ViewportInteractionFrame &frame, bool modal_tool_owns_viewport) {
  ViewportContextMenuPointerResult result;
  if (context_menu_release_suppression_.consume(
          QuaderMouseButton::Right, frame.right_pressed, frame.right_down,
          frame.right_released)) {
    return result;
  }

  const bool texture_transfer_owns_right =
      (transfer_face_texture_pointer_.has_value() &&
       transfer_face_texture_pointer_->pointer.press.button ==
           QuaderMouseButton::Right) ||
      std::ranges::any_of(
          frame.input.transfer_face_texture_edges, [](const auto &edge) {
            return edge.semantic_chord.device == QuaderInputDevice::Mouse &&
                   edge.semantic_chord.mouse_button ==
                       QuaderMouseButton::Right;
          });
  if (texture_transfer_owns_right) {
    cancel_context_menu_pointer();
    return result;
  }

  const bool blocked = modal_tool_owns_viewport ||
                       !frame.input.viewport_focused ||
                       frame.input.text_input_active ||
                       frame.input.pointer_interaction_canceled;
  if (blocked) {
    cancel_context_menu_pointer();
    return result;
  }

  if (!context_menu_pointer_.has_value() && frame.right_pressed &&
      frame.input.viewport_hovered && !frame.input.modifiers.primary &&
      !frame.input.modifiers.shift && !frame.input.modifiers.alt) {
    context_menu_pointer_ = viewport_pointer_gesture_from_latest_edge(
        frame.input.pointer_pressed_edges, QuaderMouseButton::Right,
        frame.right_click_local, frame.input.mouse_screen,
        frame.input.modifiers, frame.pointer_drag_threshold_pixels);
  }

  if (!context_menu_pointer_.has_value()) {
    return result;
  }
  if (frame.right_down && !frame.right_pressed) {
    context_menu_pointer_->accumulated_delta =
        context_menu_pointer_->accumulated_delta + frame.input.mouse_delta;
  }
  if (!frame.right_released) {
    if (!frame.right_down && !frame.right_pressed) {
      context_menu_pointer_.reset();
    }
    return result;
  }

  const ViewportPointerGesture gesture = *context_menu_pointer_;
  const ViewportPointerButtonEdge *release = exact_pointer_release_after(
      frame.input.pointer_released_edges, gesture.press);
  const bool matching_release =
      release != nullptr || gesture.press.sequence == 0u;
  QVec2 release_screen = frame.input.mouse_screen;
  QVec2 release_local =
      release != nullptr ? release->local : frame.input.mouse_local;
  QVec2 accumulated_drag = gesture.accumulated_delta;
  if (frame.pointer_capture_recentered) {
    accumulated_drag = accumulated_drag + frame.input.mouse_delta;
    release_screen = gesture.press_screen + accumulated_drag;
    release_local = gesture.press.local + accumulated_drag;
  }
  context_menu_pointer_.reset();

  result = {
      .completed = matching_release,
      .release_inside_viewport =
          release_local.x >= 0.0f && release_local.y >= 0.0f &&
          release_local.x <= frame.input.viewport_size.x &&
          release_local.y <= frame.input.viewport_size.y,
      .press_screen = gesture.press_screen,
      .release_screen = release_screen,
      .release_local = release_local,
      .accumulated_drag_delta = accumulated_drag,
      .drag_threshold_pixels = gesture.drag_threshold_pixels,
  };
  return result;
}

void ViewportInteractionController::cancel_context_menu_pointer() {
  if (context_menu_pointer_.has_value()) {
    context_menu_release_suppression_.begin(QuaderMouseButton::Right, true);
  }
  context_menu_pointer_.reset();
}

void ViewportInteractionController::cancel_pending_selection_click() {
  selection_.cancel_pending_click();
}

QuaderSelectionSummary ViewportInteractionController::pick_context_menu_target(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  return selection_.pick_context_menu_target(editor, frame);
}

const PrimitiveCreationControlsFrame &
ViewportInteractionController::primitive_creation_controls(
    const std::optional<QuaderPrimitiveCreationState> &cube,
    const ViewportCameraController &camera, float raster_scale) {
  return tool_runtime_.primitive_creation_interaction().project(
      ViewportPrimitiveCreationApiAdapter::from_api(cube), camera, raster_scale);
}

void ViewportInteractionController::set_primitive_creation_decrement(bool decrement) {
  tool_runtime_.primitive_creation_interaction().set_subdivision_decrement(decrement);
}

bool ViewportInteractionController::cancel_active_drag(
    QuaderEditorApi &editor) {
  interaction_cursor_ = ViewportPointerCursor::Default;
  if (selection_.cancel_lasso_drag()) return true;
  if (point_light_range_.cancel(editor)) return true;
  if (primitive_creation_pointer_active()) {
    const auto cube = editor.tools().live_primitive_creation();
    const auto &interaction = tool_runtime_.primitive_creation_interaction();
    if (interaction.active() < 6 &&
        (!cube || cube->object_id == interaction.object_id()) &&
        !editor.tools().cancel_primitive_resize().ok()) return false;
    tool_runtime_.clear_modal_tool_state();
    return true;
  }
  return transform_gizmo_.cancel_active_drag(editor);
}

bool ViewportInteractionController::cancel_active_drag(
    QuaderEditorApi &editor, NativeViewportToolCoordinator &native_tools) {
  interaction_cursor_ = ViewportPointerCursor::Default;
  if (selection_.cancel_lasso_drag()) return true;
  if (primitive_creation_pointer_active()) return cancel_active_drag(editor);
  if (point_light_range_.cancel(editor)) return true;
  return transform_gizmo_.cancel_active_drag(
      editor, &native_tools.curve_interaction());
}

bool ViewportInteractionController::dragging() const {
  return selection_.lasso_pointer_active() ||
         point_light_range_.dragging() || transform_gizmo_.dragging() ||
         (tool_runtime_.primitive_creation_interaction().active() >= 0 &&
          tool_runtime_.primitive_creation_interaction().active() < 6);
}

bool ViewportInteractionController::transform_gizmo_pointer_active() const {
  return transform_gizmo_.pointer_interaction_active();
}

void ViewportInteractionController::reset(QuaderEditorApi *editor) {
  interaction_cursor_ = ViewportPointerCursor::Default;
  point_light_range_.reset(editor);
  transform_gizmo_.reset(editor);
  selection_.reset();
  cancel_pending_material_interaction();
  cancel_context_menu_pointer();
}

void ViewportInteractionController::reset(
    QuaderEditorApi *editor, NativeViewportToolCoordinator &native_tools) {
  interaction_cursor_ = ViewportPointerCursor::Default;
  point_light_range_.reset(editor);
  transform_gizmo_.reset(editor, &native_tools.curve_interaction());
  selection_.reset();
  cancel_pending_material_interaction();
  cancel_context_menu_pointer();
  native_tools.clear_pointer_drag_state();
}

std::optional<QuaderHoverTarget>
ViewportInteractionController::pick_material_face_target(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  return pick_material_face_target(editor, frame, frame.right_click_local);
}

std::optional<QuaderHoverTarget>
ViewportInteractionController::pick_material_face_target(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    QVec2 local_mouse) {
  ++material_interaction_work_stats_.face_pick_count;
  const QuaderHoverSummary hover =
      editor.selection().preview_hover(material_face_hover_request(frame, local_mouse));
  if (hover.target.kind != QuaderActiveSelectionKind::Face ||
      hover.target.object_id <= 0 || hover.target.face_id < 0) {
    return std::nullopt;
  }
  return hover.target;
}

const quader_editor_core::TransformGizmoFrame *
ViewportInteractionController::transform_gizmo_frame() const {
  return transform_gizmo_.frame();
}

const std::vector<quader_editor_core::SelectionOverlayPoint> &
ViewportInteractionController::transform_gizmo_snap_overlay_points() const {
  return transform_gizmo_.snap_overlay_points();
}

const TransformGizmoSelectionCacheStats &
ViewportInteractionController::transform_gizmo_selection_cache_stats() const {
  return transform_gizmo_.selection_cache_stats();
}

const ViewportSelectionMarquee &
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

bool ViewportInteractionController::active_tool_uses_editor_native_lifecycle(
    ViewportToolId tool) const {
  return tool_runtime_.tool_uses_editor_native_lifecycle(tool);
}

ViewportPointerCursor
ViewportInteractionController::active_tool_pointer_cursor(
    ViewportToolId tool) const {
  return interaction_cursor_ != ViewportPointerCursor::Default
             ? interaction_cursor_ : tool_runtime_.tool_pointer_cursor(tool);
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
  interaction_cursor_ = ViewportPointerCursor::Default;
  return tool_runtime_.set_active_tool(state, tool, result);
}

void ViewportInteractionController::clear_modal_tool_state() {
  interaction_cursor_ = ViewportPointerCursor::Default;
  tool_runtime_.clear_modal_tool_state();
}

void ViewportInteractionController::clear_modal_drag_state() {
  interaction_cursor_ = ViewportPointerCursor::Default;
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
    NativeViewportToolCoordinator &native_tools,
    ViewportPointPlacementService *point_placement,
    ViewportCameraController &camera,
    const ViewportNativeToolInteractionFrame &frame) {
  ViewportNativeToolInteractionFrame resolved_frame = frame;
  if (resolved_frame.input.quick_pivot_down &&
      !resolved_frame.selection_transform_pivot.has_value()) {
    resolved_frame.selection_transform_pivot =
        editor.query().selection_transform_pivot();
  }
  return native_tools.handle_pointer_frame(document, assets, point_placement,
                                           camera, resolved_frame);
}

ViewportToolPointerLifecycleResult
ViewportInteractionController::handle_tool_pointer_lifecycle(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    NativeViewportToolCoordinator &native_tools,
    ViewportPointPlacementService *point_placement,
    ViewportCameraController &camera,
    const ViewportToolPointerLifecycleFrame &frame) {
  interaction_cursor_ = ViewportPointerCursor::Default;
  ViewportToolPointerLifecycleResult output;
  ViewportInteractionFrame interaction = frame.interaction;
  const bool light_range_prepass = point_light_range_.pointer_active();
  if (light_range_prepass) {
    interaction.selection_state = {
        .summary = editor.selection().summary(),
        .scene_revision = document.revision(),
        .asset_revision = assets.revision(),
        .pivot_revision = native_tools.presentation_snapshot()
                              .selection_pivot_revision,
    };
    output.before_tool = handle_before_active_tool_impl(
        editor, interaction, &native_tools.curve_interaction(), &native_tools);
  }
  std::optional<QuaderSelectionSummary> curve_prepass_selection;
  ViewportCurveInteractionPort *curve = &native_tools.curve_interaction();

  const bool curve_gizmo_pointer_active =
      transform_gizmo_.pointer_interaction_active();
  const bool curve_gizmo_start_allowed =
      native_tools.curve_control_gizmo_start_allowed(
          interaction.input_router.state().active_tool,
          interaction.active_tool_allows_selection_fallback,
          interaction.input.active_tool_owns_viewport,
          interaction.input.modal_tool_active);
  output.curve_gizmo_prepass =
      !light_range_prepass && curve != nullptr && curve->controls_editable() &&
      !frame.cancel_consumed &&
      (!frame.native_tool.navigation_gizmo_consumed ||
       curve_gizmo_pointer_active) &&
      (curve_gizmo_pointer_active ||
       (curve_gizmo_start_allowed && !native_tools.pointer_dragging() &&
        interaction.input.curve_conversion_edges.empty() &&
        !curve->point_construction_dragging() &&
        curve->has_selected_controls()));

  if (output.curve_gizmo_prepass) {
    curve_prepass_selection = editor.selection().summary();
    interaction.selection_state = {
        .summary = *curve_prepass_selection,
        .scene_revision = document.revision(),
        .asset_revision = assets.revision(),
        .pivot_revision = native_tools.presentation_snapshot()
                              .selection_pivot_revision,
    };
    output.before_tool =
        handle_before_active_tool_impl(editor, interaction, curve,
                                       &native_tools);
  }

  ViewportNativeToolInteractionFrame native_frame = frame.native_tool;
  native_frame.handles_visible =
      interaction.settings.bool_value(ViewportBoolSettingId::ShowGizmos);
  native_frame.scene_surface_revision =
      interaction.scene_surfaces ? interaction.scene_surfaces->revision() : 0;
  native_frame.viewport_pixel_scale = interaction.viewport_pixel_scale;
  native_frame.select_backfaces = interaction.settings.bool_value(
      ViewportBoolSettingId::BackfaceSelection);
  const ViewportCurveControlPickingAdapter curve_control_picker(
      camera, interaction.scene_surfaces, interaction.viewport_pixel_scale);
  const ViewportPathArraySourcePickingAdapter path_array_source_picker(
      camera, interaction.scene_billboard_pick_candidates,
      interaction.scene_curve_pick_candidates, interaction.scene_surfaces,
      interaction.viewport_pixel_scale);
  native_frame.curve_control_picker = &curve_control_picker;
  native_frame.path_array_source_picker = &path_array_source_picker;
  native_frame.transform_gizmo_consumed =
      (light_range_prepass || output.curve_gizmo_prepass) && output.before_tool.gizmo_consumed;
  output.native_tool = handle_native_tools(
      editor, document, assets, native_tools, point_placement, camera,
      native_frame);

  output.interaction_selection = {
      .summary = curve_prepass_selection.has_value()
                     ? *curve_prepass_selection
                     : editor.selection().summary(),
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .pivot_revision = native_tools.presentation_snapshot()
                            .selection_pivot_revision,
  };
  interaction.selection_state = output.interaction_selection;

  if (!light_range_prepass && !output.curve_gizmo_prepass) {
    output.before_tool =
        frame.cancel_consumed ||
                frame.native_tool.navigation_gizmo_consumed ||
                output.native_tool.blocks_transform_gizmo
            ? ViewportBeforeToolInteractionResult{.gizmo_consumed = true}
            : handle_before_active_tool_impl(editor, interaction, curve,
                                             &native_tools);
  }
  if (native_tools.pointer_cursor() != ViewportPointerCursor::Default &&
      (!output.before_tool.gizmo_consumed ||
       (!light_range_prepass && !output.curve_gizmo_prepass &&
        output.native_tool.blocks_transform_gizmo))) {
    interaction_cursor_ = native_tools.pointer_cursor();
  }
  return output;
}

} // namespace quader
