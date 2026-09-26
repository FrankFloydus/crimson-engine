////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_interaction_controller.hpp"

#include "api/quader_editor_api.hpp"
#include "api/quader_operation_input_context.hpp"
#include "core/logger.hpp"
#include "core/release_features.hpp"
#include "diagnostics/profile.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/viewport/primitive_creation_api_adapter.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "editor/viewport/viewport_path_array_source_picking.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_pixel_scale.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace quader {


namespace {

CommandResult place_active_light_at_world_position(
    QuaderEditorApi &editor, LightCreationKind kind,
    QVec3 world_position) {
  if (!std::isfinite(world_position.x) || !std::isfinite(world_position.y) ||
      !std::isfinite(world_position.z)) {
    return command_invalid_result("Light world position is not finite.");
  }

  CommandResult result;
  if (kind == LightCreationKind::Point) {
    QuaderCreatePointLightArgs args;
    args.transform.position = world_position;
    args.select_created = true;
    const QuaderApiResult<QuaderEntityRef> created =
        editor.scene().create_point_light(args);
    result = {
        .ok = created.ok(),
        .changed = created.changed(),
        .message = created.error_message(),
    };
  } else {
    QuaderCreateDirectionalLightArgs args;
    args.transform.position = world_position;
    args.select_created = true;
    const QuaderApiResult<QuaderEntityRef> created =
        editor.scene().create_directional_light(args);
    result = {
        .ok = created.ok(),
        .changed = created.changed(),
        .message = created.error_message(),
    };
  }
  return result;
}

bool material_face_source_gesture_mode(ViewportSelectionMode mode) {
  return mode == ViewportSelectionMode::Vertex ||
         mode == ViewportSelectionMode::Edge ||
         mode == ViewportSelectionMode::Face ||
         mode == ViewportSelectionMode::Object ||
         mode == ViewportSelectionMode::Group;
}

bool material_input_blocked(const ViewportInteractionFrame &frame) {
  return (!release_has_feature(ReleaseFeature::Materials) &&
          !release_has_feature(ReleaseFeature::UvEditing)) ||
         !frame.input.viewport_focused || frame.input.text_input_active ||
         frame.input.pointer_interaction_canceled ||
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
  return !release_has_feature(ReleaseFeature::Materials) ||
         material_input_blocked(frame) ||
         (before_tool.gizmo_consumed && !before_tool.material_transfer_consumed) ||
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

const ViewportSemanticPointerEdge *material_transfer_edge(
    const ViewportInputFrame &input, ViewportSemanticPointerEdgeKind kind) {
  const auto found = std::find_if(
      input.material_transfer_edges.rbegin(),
      input.material_transfer_edges.rend(),
      [kind](const auto &edge) { return edge.kind == kind; });
  return found == input.material_transfer_edges.rend() ? nullptr
                                                           : &*found;
}

ViewportInputActionKind material_pointer_action(
    const ViewportSemanticPointerEdge &edge) {
  return edge.action == ViewportInputActionKind::None
             ? ViewportInputActionKind::TransferFaceTexture
             : edge.action;
}

bool release_allows_material_pointer_action(ViewportInputActionKind action) {
  return release_has_feature(
      action == ViewportInputActionKind::None ||
              action == ViewportInputActionKind::TransferFaceTexture
          ? ReleaseFeature::UvEditing
          : ReleaseFeature::Materials);
}

bool same_pointer_modifiers(const QuaderInputChord &left,
                            const QuaderInputChord &right) {
  return left.mouse_button == right.mouse_button &&
         left.primary == right.primary && left.shift == right.shift &&
         left.alt == right.alt;
}

bool material_transfer_trigger_matches(
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

void ViewportInteractionController::populate_operation_input_context(
    QuaderOperationInputContext &context,
    const ViewportInteractionFrame &frame) const {
  context.orthographic =
      frame.camera.state().projection == ViewportProjection::Orthographic;
  context.orbit_available = !frame.camera.orthographic_view_locked();
  context.shift = frame.input.modifiers.shift;
  context.control = frame.input.modifiers.primary;
  context.alt = frame.input.modifiers.alt;
  context.orbit_snap_active = frame.input_router.orbit_snap_navigating();
  context.held_vertex_snap = frame.input.transform_vertex_snap_down;
  context.quick_pivot = frame.input.quick_pivot_down;
  context.grid_snap_available = viewport_snapping_active(frame.settings);
  context.grid_snap_enabled = frame.settings.bool_value(ViewportBoolSettingId::GridSnap);
  context.angle_snap_available = viewport_snapping_active(frame.settings);
  context.angle_snap_enabled = frame.settings.bool_value(ViewportBoolSettingId::RotationSnap);
  if (frame.input_router.fly_navigating()) {
    context.navigation = QuaderInputNavigation::Fly;
  } else if (frame.input_router.orbit_navigating()) {
    context.navigation = QuaderInputNavigation::Orbit;
  } else if (frame.input_router.pan_navigating()) {
    context.navigation = QuaderInputNavigation::Pan;
  } else {
    // A wheel tick is an instantaneous action, not a held navigation mode.
    // Keep the idle/tool hints instead of replacing them for one frame.
    context.navigation = QuaderInputNavigation::None;
  }
  if (context.navigation != QuaderInputNavigation::None) return;
  tool_runtime_.populate_operation_input_context(context);
  transform_gizmo_.populate_operation_input_context(context, frame);
  selection_.populate_operation_input_context(context, frame);
  context.point_light_range_hovered = point_light_range_.hovered();
  context.point_light_range_dragging = point_light_range_.dragging();
  context.pointer_dragging = context.pointer_dragging ||
                             dragging();
  const auto &primitive = tool_runtime_.primitive_creation_interaction();
  const auto &controls = primitive.frame();
  const int control = controls.active >= 0 ? controls.active : controls.hovered;
  if (controls.visible && control >= 6 && control <= 8 &&
      controls.controls[static_cast<std::size_t>(control)].visible &&
      controls.controls[static_cast<std::size_t>(control)].enabled) {
    using Control = QuaderInputPrimitiveControl;
    switch (primitive.presented_shape()) {
    case ViewportPrimitiveCreationShape::Box:
      context.primitive_control = control == 6 ? Control::SubdivisionsX
                                : control == 7 ? Control::SubdivisionsY
                                               : Control::SubdivisionsZ;
      break;
    case ViewportPrimitiveCreationShape::Cylinder:
    case ViewportPrimitiveCreationShape::Cone:
      context.primitive_control = Control::Sides;
      break;
    case ViewportPrimitiveCreationShape::Sphere:
      context.primitive_control = primitive.presented_sphere_type() == ViewportSphereType::Quad
          ? Control::SubdivisionLevel
          : control == 6 ? Control::Segments : Control::Rings;
      break;
    case ViewportPrimitiveCreationShape::Torus:
      context.primitive_control = control == 6 ? Control::MajorSegments
                                              : Control::MinorSegments;
      break;
    }
  }
  if (material_input_blocked(frame) || !frame.input.viewport_hovered) return;
  const auto count = frame.selection_state.summary.selected_face_count;
  using Action = ViewportInputActionKind;
  using Transfer = QuaderInputMaterialTransfer;
  const auto material_action = material_transfer_pointer_
      ? material_transfer_pointer_->action : accepted_material_hint_action_;
  switch (material_action) {
  case Action::ApplySelectedFaceMaterialToFace:
    if (count == 1) context.material_transfer = Transfer::SelectedFaceToFace;
    break;
  case Action::ApplySelectedFaceMaterialToMesh:
    if (count == 1) context.material_transfer = Transfer::SelectedFaceToMesh;
    break;
  case Action::ApplyFaceMaterialToSelection:
    if (count > 0) context.material_transfer = Transfer::FaceToSelection;
    break;
  case Action::ApplyFaceMaterialToMesh:
    context.material_transfer = Transfer::FaceToMesh;
    break;
  case Action::TransferFaceTexture:
    if (count == 1) context.material_transfer = Transfer::ContinueTexture;
    break;
  default:
    break;
  }
}

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
  if (!release_has_feature(ReleaseFeature::Curves)) {
    curve = nullptr;
  }
  if (!point_light_range_.pointer_active()) {
    if (const auto transfer =
            reserve_material_transfer(editor, frame, native_tools)) {
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
  const bool selection_pointer_available =
      frame.active_tool_allows_selection_fallback ||
      (release_has_feature(ReleaseFeature::Curves) &&
       frame.input.curve_controls_editable);
  const bool selection_pointer_blocked =
      !frame.input.viewport_hovered ||
      frame.input.text_input_active || frame.input.navigation_active ||
      frame.input_result.navigation_active ||
      frame.input.pointer_interaction_canceled ||
      frame.input.operation_preview_active || frame.input.active_transform_drag ||
      frame.input_result.cancel_requested || frame.input_result.camera_changed ||
      frame.input_result.view_changed ||
      (!(release_has_feature(ReleaseFeature::Curves) &&
         frame.input.curve_controls_editable) &&
       (frame.input.modal_tool_active || frame.input.active_tool_owns_viewport));
  if (selection_pointer_available && !selection_pointer_blocked) {
    const std::optional<quader_editor_core::SelectionEditOperation> edit =
        selection_.pointer_edit_operation(prepared);
    if (edit.has_value()) {
      interaction_cursor_ = ViewportPointerCursor::SelectionDefault;
    }
    if (edit == quader_editor_core::SelectionEditOperation::Add) {
      interaction_cursor_ = ViewportPointerCursor::SelectionAdd;
    } else if (edit == quader_editor_core::SelectionEditOperation::Remove) {
      interaction_cursor_ = ViewportPointerCursor::SelectionSubtract;
    }
  }
  if ((release_has_feature(ReleaseFeature::LassoSelection) &&
       frame.input_router.state().active_tool == ViewportToolId::LassoSelect) ||
      selection_.lasso_pointer_active()) {
    return {.selection_after_pending_pick = resolved_selection};
  }
  bool range_hovered = false;
  if (release_has_feature(ReleaseFeature::Lights) &&
      !transform_gizmo_.pointer_interaction_active()) {
    const bool range_consumed = point_light_range_.handle_frame(editor, prepared);
    range_hovered = point_light_range_.hovered();
    if (range_consumed) {
      interaction_cursor_ = point_light_range_.pointer_cursor();
      selection_.cancel_pending_click();
      return {.selection_after_pending_pick = resolved_selection,
              .gizmo_consumed = true,
              .gizmo_hovered = range_hovered};
    }
  }
  ViewportBeforeToolInteractionResult result =
      transform_gizmo_.handle_frame(editor, prepared, curve);
  const auto *gizmo_frame = transform_gizmo_.frame();
  result.gizmo_hovered =
      range_hovered ||
      (gizmo_frame != nullptr &&
       gizmo_frame->info.hover_axis != quader_editor_core::TransformAxis::None);
  const ViewportPointerCursor direct_manipulation_cursor =
      result.gizmo_consumed ||
              transform_gizmo_.pointer_interaction_active() ||
              point_light_range_.pointer_cursor() ==
                  ViewportPointerCursor::Default
          ? transform_gizmo_.pointer_cursor()
          : point_light_range_.pointer_cursor();
  if (result.gizmo_hovered || result.gizmo_consumed ||
      transform_gizmo_.pointer_interaction_active() ||
      direct_manipulation_cursor != ViewportPointerCursor::Default) {
    interaction_cursor_ = direct_manipulation_cursor;
  }
  result.selection_after_pending_pick = resolved_selection;
  return result;
}

std::optional<ViewportBeforeToolInteractionResult>
ViewportInteractionController::reserve_material_transfer(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    NativeViewportToolCoordinator *native_tools) {
  if (!release_has_feature(ReleaseFeature::Materials) &&
      !release_has_feature(ReleaseFeature::UvEditing)) {
    return std::nullopt;
  }
  const auto retire_released_pointer = [&](std::uint64_t before_sequence = 0u) {
    if (!material_transfer_pointer_.has_value()) {
      return;
    }
    const auto *release = exact_pointer_release_after(
        frame.input.pointer_released_edges, material_transfer_pointer_->pointer.press);
    if (release != nullptr &&
        (before_sequence == 0u ||
         (release->sequence != 0u && release->sequence < before_sequence))) {
      material_transfer_pointer_.reset();
      transfer_release_suppression_.clear();
    }
  };
  if (frame.input.material_transfer_edges.size() < 2u) {
    if (!frame.input.material_transfer_edges.empty()) {
      const auto &edge = frame.input.material_transfer_edges.front();
      if (edge.pointer_edge.has_value() && edge.pointer_edge->sequence != 0u &&
          (edge.kind == ViewportSemanticPointerEdgeKind::ReservePress ||
           edge.semantic_chord.gesture != QuaderInputGesture::Click)) {
        retire_released_pointer(edge.pointer_edge->sequence);
      }
    }
    auto result = reserve_material_pointer_edge(editor, frame, native_tools);
    retire_released_pointer();
    return result;
  }

  // A release and the following press can arrive in one host sample. Apply
  // ownership in physical order so the later press cannot steal that release.
  ViewportInputFrame input = frame.input;
  ViewportBeforeToolInteractionResult result;
  for (const auto &edge : frame.input.material_transfer_edges) {
    if (!release_allows_material_pointer_action(material_pointer_action(edge))) {
      continue;
    }
    const bool press = edge.kind == ViewportSemanticPointerEdgeKind::ReservePress ||
                       edge.semantic_chord.gesture != QuaderInputGesture::Click;
    const auto physical = edge.pointer_edge.value_or(ViewportPointerButtonEdge{
        .button = edge.semantic_chord.mouse_button,
        .local = frame.input.mouse_local,
    });
    if (press && physical.sequence != 0u) {
      retire_released_pointer(physical.sequence);
    }
    input.material_transfer_edges = {edge};
    input.material_transfer_pressed =
        edge.kind == ViewportSemanticPointerEdgeKind::Trigger;
    input.material_transfer_pointer_reserved = !input.material_transfer_pressed;
    input.material_transfer_action = material_pointer_action(edge);
    input.pointer_pressed_edges = press ? std::vector{physical}
                                       : std::vector<ViewportPointerButtonEdge>{};
    input.pointer_released_edges = press ? std::vector<ViewportPointerButtonEdge>{}
                                        : std::vector{physical};
    input.active_input_chords.clear();
    if (press) {
      input.active_input_chords.push_back(edge.semantic_chord);
    }
    input.middle_pressed = input.middle_down =
        press && physical.button == QuaderMouseButton::Middle;
    input.middle_released = !press && physical.button == QuaderMouseButton::Middle;
    input.right_pressed = input.right_down =
        press && physical.button == QuaderMouseButton::Right;
    input.right_released = !press && physical.button == QuaderMouseButton::Right;
    input.mouse_local = physical.local;
    ViewportInteractionFrame event{
        .input_router = frame.input_router,
        .camera = frame.camera,
        .settings = frame.settings,
        .input = input,
        .input_result = frame.input_result,
    };
    event.left_click_local = event.right_click_local = physical.local;
    event.left_pressed = event.left_down =
        press && physical.button == QuaderMouseButton::Left;
    event.left_released = !press && physical.button == QuaderMouseButton::Left;
    event.right_pressed = event.right_down = input.right_pressed;
    event.right_released = input.right_released;
    event.pointer_drag_threshold_pixels = frame.pointer_drag_threshold_pixels;
    event.selection_state = frame.selection_state;
    const auto accepted = reserve_material_pointer_edge(editor, event, native_tools);
    if (!accepted.has_value()) {
      continue;
    }
    result.gizmo_consumed = result.gizmo_consumed || accepted->gizmo_consumed;
    result.material_transfer_consumed = true;
    if (accepted->material_transfer_requested) {
      result.material_transfer_requested = true;
      result.material_transfer_local = accepted->material_transfer_local;
      auto request = edge;
      request.pointer_edge = physical;
      request.pointer_edge->local = accepted->material_transfer_local;
      result.material_transfer_requests.push_back(request);
    }
  }
  retire_released_pointer();
  return result.material_transfer_consumed
             ? std::optional{std::move(result)}
             : std::nullopt;
}

std::optional<ViewportBeforeToolInteractionResult>
ViewportInteractionController::reserve_material_pointer_edge(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    NativeViewportToolCoordinator *native_tools) {
  const ViewportSemanticPointerEdge *trigger = material_transfer_edge(
      frame.input, ViewportSemanticPointerEdgeKind::Trigger);
  const ViewportSemanticPointerEdge *reserve = material_transfer_edge(
      frame.input, ViewportSemanticPointerEdgeKind::ReservePress);
  ViewportInputActionKind action = material_transfer_pointer_.has_value()
                                       ? material_transfer_pointer_->action
                                       : frame.input.material_transfer_action;
  if (trigger != nullptr) {
    action = material_pointer_action(*trigger);
  } else if (reserve != nullptr) {
    action = material_pointer_action(*reserve);
  }
  if (!release_allows_material_pointer_action(action)) {
    return std::nullopt;
  }
  if (transfer_release_suppression_.active()) {
    const QuaderMouseButton suppressed_button =
        transfer_release_suppression_.button();
    const PointerButtonFrameState suppressed =
        pointer_button_state(frame, suppressed_button);
    if (transfer_release_suppression_.consume(
            suppressed_button, suppressed.pressed, suppressed.down,
            suppressed.released)) {
      return ViewportBeforeToolInteractionResult{
          .gizmo_consumed = true, .material_transfer_consumed = true};
    }
  }
  if (material_transfer_pointer_.has_value()) {
    // Consume the release even if focus, scene, or mode changes. No click was
    // started for selection or a gizmo, so none may be synthesized on release.
    const bool source_context_matches =
        !material_input_blocked(frame) && !frame.input_result.cancel_requested &&
        !frame.input_result.camera_changed &&
        frame.input_router.state().selection_mode == ViewportSelectionMode::Face &&
        frame.selection_state.summary.selection_revision ==
            material_transfer_pointer_->selection_revision &&
        editor.commands().keybindings_revision() ==
            material_transfer_pointer_->keybindings_revision &&
        editor.scene_documents().active_scene_id().value_or(SceneSessionId{}) ==
            material_transfer_pointer_->scene_session_id;
    const QuaderMouseButton button =
        material_transfer_pointer_->pointer.press.button;
    const PointerButtonFrameState button_state =
        pointer_button_state(frame, button);
    const ViewportPointerButtonEdge *release = exact_pointer_release_after(
        frame.input.pointer_released_edges,
        material_transfer_pointer_->pointer.press);
    const bool matching_release = release != nullptr ||
                                  (material_transfer_pointer_->pointer.press
                                               .sequence == 0u &&
                                   button_state.released);
    const QVec2 release_local =
        release != nullptr ? release->local : frame.input.mouse_local;
    const bool trigger_matches =
        trigger != nullptr &&
        same_pointer_modifiers(trigger->semantic_chord,
                               material_transfer_pointer_->semantic_chord) &&
        (material_pointer_action(*trigger) ==
             material_transfer_pointer_->action ||
         (trigger->semantic_chord.gesture == QuaderInputGesture::DoubleClick &&
          material_transfer_pointer_->action ==
              ViewportInputActionKind::ApplySelectedFaceMaterialToFace &&
          material_pointer_action(*trigger) ==
              ViewportInputActionKind::ApplySelectedFaceMaterialToMesh)) &&
        material_transfer_trigger_matches(
            *trigger, material_transfer_pointer_->pointer, button_state,
            release);
    const bool legacy_release_trigger =
        trigger == nullptr && frame.input.material_transfer_pressed &&
        matching_release;

    // A second physical press can be Qt's DoubleClick edge. Replace the
    // retained press only after the semantic trigger proved it belongs to the
    // same Settings-resolved button.
    if (trigger_matches && trigger->pointer_edge.has_value() &&
        trigger->semantic_chord.gesture != QuaderInputGesture::Click) {
      material_transfer_pointer_->pointer.press = *trigger->pointer_edge;
      material_transfer_pointer_->semantic_chord =
          trigger->semantic_chord;
      material_transfer_pointer_->action = material_pointer_action(*trigger);
    }

    if (button_state.released || !button_state.down ||
        !source_context_matches) {
      transfer_release_suppression_.begin(button, button_state.down);
      material_transfer_pointer_.reset();
    }
    return ViewportBeforeToolInteractionResult{
        .gizmo_consumed = true,
        .material_transfer_consumed = true,
        .material_transfer_requested =
            source_context_matches &&
            (legacy_release_trigger ||
             (trigger_matches &&
              (trigger->semantic_chord.gesture != QuaderInputGesture::Click ||
               matching_release))),
        .material_transfer_local =
            trigger_matches && trigger->pointer_edge.has_value()
                ? trigger->pointer_edge->local
                : release_local,
    };
  }
  if ((!frame.input.material_transfer_pressed &&
       !frame.input.material_transfer_pointer_reserved &&
       trigger == nullptr && reserve == nullptr) ||
      frame.input_router.state().selection_mode != ViewportSelectionMode::Face ||
      material_input_blocked(frame) || frame.input_result.cancel_requested ||
      frame.input_result.camera_changed ||
      transform_gizmo_.pointer_interaction_active() ||
      selection_.pointer_interaction_active() ||
      (native_tools != nullptr && native_tools->pointer_dragging())) {
    return std::nullopt;
  }

  const bool coalesced_click =
      trigger != nullptr && reserve != nullptr &&
      trigger->semantic_chord.gesture == QuaderInputGesture::Click &&
      same_pointer_modifiers(trigger->semantic_chord, reserve->semantic_chord) &&
      material_pointer_action(*trigger) == material_pointer_action(*reserve);
  const ViewportSemanticPointerEdge *pointer_action =
      coalesced_click ? reserve : (trigger != nullptr ? trigger : reserve);
  if (pointer_action != nullptr) {
    const QuaderInputChord &semantic = pointer_action->semantic_chord;
    if (semantic.device != QuaderInputDevice::Mouse ||
        semantic.mouse_button == QuaderMouseButton::None) {
      return ViewportBeforeToolInteractionResult{
          .gizmo_consumed = true, .material_transfer_consumed = true};
    }
    // A Click can execute only through the exact press retained by this
    // owner. A stale/orphan semantic release is consumed without mutation.
    if (pointer_action->kind == ViewportSemanticPointerEdgeKind::Trigger &&
        semantic.gesture == QuaderInputGesture::Click) {
      return ViewportBeforeToolInteractionResult{
          .gizmo_consumed = true, .material_transfer_consumed = true};
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
    material_transfer_pointer_ = MaterialTransferPointer{
        .pointer = {
            .press = accepted_edge,
            .press_screen = frame.input.mouse_screen,
            .modifiers = frame.input.modifiers,
            .drag_threshold_pixels = frame.pointer_drag_threshold_pixels,
        },
        .semantic_chord = semantic,
        .action = material_pointer_action(*pointer_action),
        .scene_session_id =
            editor.scene_documents().active_scene_id().value_or(SceneSessionId{}),
        .selection_revision = frame.selection_state.summary.selection_revision,
        .keybindings_revision = editor.commands().keybindings_revision(),
    };
    const bool coalesced_trigger_matches =
        coalesced_click && material_transfer_trigger_matches(
                               *trigger, material_transfer_pointer_->pointer,
                               button_state,
                               exact_pointer_release_after(
                                   frame.input.pointer_released_edges,
                                   accepted_edge));
    if (!button_state.down &&
        (!button_state.pressed || coalesced_trigger_matches)) {
      material_transfer_pointer_.reset();
    }
    return ViewportBeforeToolInteractionResult{
        .gizmo_consumed = true,
        .material_transfer_consumed = true,
        .material_transfer_requested =
            pointer_action->kind == ViewportSemanticPointerEdgeKind::Trigger ||
            coalesced_trigger_matches,
        .material_transfer_local =
            coalesced_trigger_matches && trigger->pointer_edge.has_value()
                ? trigger->pointer_edge->local
                : accepted_edge.local,
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
    material_transfer_pointer_ = MaterialTransferPointer{
        .pointer = viewport_pointer_gesture_from_latest_edge(
            frame.input.pointer_pressed_edges, legacy_button,
            pointer_press_fallback_local(frame, legacy_button),
            frame.input.mouse_screen,
            frame.input.modifiers, frame.pointer_drag_threshold_pixels),
        .semantic_chord = {
            .device = QuaderInputDevice::Mouse,
            .mouse_button = legacy_button,
            .gesture = frame.input.material_transfer_pointer_reserved
                           ? QuaderInputGesture::Click
                           : QuaderInputGesture::Press,
        },
        .action = frame.input.material_transfer_action,
        .scene_session_id =
            editor.scene_documents().active_scene_id().value_or(SceneSessionId{}),
        .selection_revision = frame.selection_state.summary.selection_revision,
        .keybindings_revision = editor.commands().keybindings_revision(),
    };
    if (!legacy_state.down && !legacy_state.pressed) {
      material_transfer_pointer_.reset();
    }
  }
  return ViewportBeforeToolInteractionResult{
      .gizmo_consumed = true,
      .material_transfer_consumed = true,
      .material_transfer_requested =
          frame.input.material_transfer_pressed &&
          (legacy_fresh_press || frame.input.pointer_released_edges.empty()),
      .material_transfer_local =
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
  accepted_material_hint_action_ = ViewportInputActionKind::None;
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
  if (!runtime_binding.current() ||
      (!release_has_feature(ReleaseFeature::Materials) &&
       !release_has_feature(ReleaseFeature::UvEditing))) {
    return;
  }
  const bool batched = !before_tool.material_transfer_requests.empty();
  if (batched) {
    ViewportInputFrame input = frame.input;
    input.apply_face_material_to_mesh_pressed = false;
    ViewportBeforeToolInteractionResult accepted = before_tool;
    accepted.material_transfer_requests.clear();
    for (const auto &request : before_tool.material_transfer_requests) {
      if (!release_allows_material_pointer_action(
              material_pointer_action(request))) {
        continue;
      }
      input.material_transfer_pressed = true;
      input.material_transfer_edges = {request};
      input.material_transfer_action = material_pointer_action(request);
      accepted.material_transfer_local = request.pointer_edge->local;
      ViewportInteractionFrame event{
          .input_router = frame.input_router,
          .camera = frame.camera,
          .settings = frame.settings,
          .input = input,
          .input_result = frame.input_result,
      };
      event.double_click_interval_seconds = frame.double_click_interval_seconds;
      event.monotonic_time_seconds = frame.monotonic_time_seconds;
      event.scene_surfaces = frame.scene_surfaces;
      event.selection_state = frame.selection_state;
      handle_material_interactions(editor, event, tool_result, accepted, runtime_binding);
      if (!runtime_binding.current()) {
        return;
      }
    }
  }
  if (!batched && frame.input.material_transfer_pressed) {
    if (!before_tool.material_transfer_requested ||
        !release_allows_material_pointer_action(
            frame.input.material_transfer_action) ||
        material_input_blocked(frame) || frame.input_result.cancel_requested ||
        frame.input_result.camera_changed || tool_result.handled ||
        !frame.input.viewport_hovered ||
        frame.input_router.state().selection_mode != ViewportSelectionMode::Face) {
      return;
    }
    const QVec2 local_mouse = before_tool.material_transfer_local;
    // Routing a held gesture to its original pane can retain logical hover;
    // a release outside that pane still must not edit through an outside ray.
    if (!(local_mouse.x >= 0.0f && local_mouse.y >= 0.0f &&
          local_mouse.x < frame.input.viewport_size.x &&
          local_mouse.y < frame.input.viewport_size.y)) {
      return;
    }
    if (const std::optional<QuaderHoverTarget> target =
            pick_material_face_target(editor, frame, local_mouse)) {
      const auto action = frame.input.material_transfer_action;
      const auto object = static_cast<QuaderModelingObjectId>(target->object_id);
      const auto face = static_cast<QuaderElementId>(target->face_id);
      QuaderApiVoidResult transferred;
      if (action == ViewportInputActionKind::TransferFaceTexture) {
        ++material_interaction_work_stats_.transfer_face_texture_request_count;
        transferred = editor.modeling().transfer_face_texture_to_face(object, face);
      } else {
        ++material_interaction_work_stats_.copy_face_material_request_count;
        switch (action) {
        case ViewportInputActionKind::ApplySelectedFaceMaterialToFace:
          transferred = editor.modeling().apply_selected_face_material_to_face(
              object, face);
          break;
        case ViewportInputActionKind::ApplySelectedFaceMaterialToMesh:
          transferred = editor.modeling().apply_selected_face_material_to_mesh(
              object, face);
          break;
        case ViewportInputActionKind::ApplyFaceMaterialToSelection:
          transferred = editor.modeling().apply_face_material_to_selected_faces(
              object, face);
          break;
        default:
          return;
        }
      }
      if (!runtime_binding.current()) {
        return;
      }
      if (transferred.ok()) accepted_material_hint_action_ = action;
      log_material_interaction_failure(
          action == ViewportInputActionKind::TransferFaceTexture
              ? "Continue texture from selected face"
              : "Copy face material",
          transferred);
    }
    if (!frame.input.apply_face_material_to_mesh_pressed) {
      return;
    }
  }

  if (!frame.input.apply_face_material_to_mesh_pressed) {
    return;
  }
  if (material_interaction_blocked(frame, tool_result, before_tool) ||
      !frame.input.viewport_hovered ||
      !material_face_source_gesture_mode(
          frame.input_router.state().selection_mode)) {
    return;
  }
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
    if (applied.ok()) accepted_material_hint_action_ = ViewportInputActionKind::ApplyFaceMaterialToMesh;
    log_material_interaction_failure("Apply face material to mesh", applied);
  }
}

void ViewportInteractionController::cancel_pending_material_interaction() {
  accepted_material_hint_action_ = ViewportInputActionKind::None;
  if (material_transfer_pointer_.has_value()) {
    transfer_release_suppression_.begin(
        material_transfer_pointer_->pointer.press.button, true);
  }
  material_transfer_pointer_.reset();
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

  const bool material_transfer_owns_right =
      (material_transfer_pointer_.has_value() &&
       material_transfer_pointer_->pointer.press.button ==
           QuaderMouseButton::Right) ||
      std::ranges::any_of(
          frame.input.material_transfer_edges, [](const auto &edge) {
            return release_allows_material_pointer_action(
                       material_pointer_action(edge)) &&
                   edge.semantic_chord.device == QuaderInputDevice::Mouse &&
                   edge.semantic_chord.mouse_button ==
                       QuaderMouseButton::Right;
          });
  if (material_transfer_owns_right) {
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
  const bool fly_navigation_active = frame.input.fly_navigation_active ||
                                     frame.input_result.fly_navigation_active;
  const QVec3 fly_direction = frame.input.fly_local_direction;
  const bool fly_movement_requested =
      std::abs(fly_direction.x) > 0.000001f ||
      std::abs(fly_direction.y) > 0.000001f ||
      std::abs(fly_direction.z) > 0.000001f;
  if ((fly_navigation_active && fly_movement_requested) ||
      frame.input_result.fly_speed_changed) {
    // Keep navigation ownership through the release, even after movement stops.
    cancel_context_menu_pointer();
    return result;
  }
  if (frame.right_down && !frame.right_pressed) {
    context_menu_pointer_->accumulated_delta =
        context_menu_pointer_->accumulated_delta + frame.input.mouse_delta;
    if (!viewport_pointer_within_drag_threshold(
            *context_menu_pointer_,
            context_menu_pointer_->press.local +
                context_menu_pointer_->accumulated_delta)) {
      // Returning the pointer to its press position cannot undo a drag.
      cancel_context_menu_pointer();
      return result;
    }
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
  if (release_has_feature(ReleaseFeature::Pivot) &&
      resolved_frame.input.quick_pivot_down &&
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
  const bool light_range_prepass =
      release_has_feature(ReleaseFeature::Lights) &&
      point_light_range_.pointer_active();
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
  ViewportCurveInteractionPort *curve =
      release_has_feature(ReleaseFeature::Curves)
          ? &native_tools.curve_interaction()
          : nullptr;

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
  native_frame.left_click_count = interaction.left_click_count;
  native_frame.handles_visible =
      interaction.settings.bool_value(ViewportBoolSettingId::ShowGizmos);
  native_frame.scene_surface_revision =
      interaction.scene_surfaces ? interaction.scene_surfaces->revision() : 0;
  native_frame.viewport_pixel_scale = interaction.viewport_pixel_scale;
  native_frame.scene_vertex_snap_radius = viewport_scaled_world_pick_radius(
      interaction.settings.float_value(ViewportFloatSettingId::PickVertexRadius),
      interaction.viewport_pixel_scale, interaction.input.device_pixel_ratio);
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
  if (output.native_tool.light_creation_world_position.has_value()) {
    output.native_tool.command_attempted = true;
    const std::optional<LightCreationOperation> light_creation =
        native_tools.active_light_creation();
    if (!light_creation.has_value()) {
      output.native_tool.command_result =
          command_invalid_result("No light creation tool is active.");
      return output;
    }
    output.native_tool.command_result = place_active_light_at_world_position(
        editor, light_creation->kind,
        *output.native_tool.light_creation_world_position);
    if (!frame.runtime_binding.current()) {
      return output;
    }
    if (output.native_tool.command_result.ok &&
        output.native_tool.command_result.changed) {
      const CommandResult completed =
          native_tools.complete_active_light_creation(*light_creation);
      if (!completed.ok) {
        output.native_tool.command_result.ok = false;
        output.native_tool.command_result.message = completed.message;
        return output;
      }
      output.native_tool.sync_active_tool = true;
      output.native_tool.invalidate_hover = true;
      output.native_tool.mark_overlay_dirty = true;
    }
  }

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
  if (output.native_tool.selection_click_requested) {
    output.before_tool.selection_click_requested = true;
    output.before_tool.selection_click_local =
        output.native_tool.selection_click_local;
    output.before_tool.selection_click_modifiers =
        output.native_tool.selection_click_modifiers;
    output.before_tool.selection_click_count =
        output.native_tool.selection_click_count;
    output.before_tool.selection_cycle_depth =
        output.native_tool.selection_cycle_depth;
    output.before_tool.selection_cycle_depth_modifiers =
        output.native_tool.selection_cycle_depth_modifiers;
  }
  return output;
}

} // namespace quader
