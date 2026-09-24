////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/tools/uv_peel_tool_service.hpp"
#include "core/release_features.hpp"

#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/path_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"

#include <cassert>
#include <optional>
#include <utility>

namespace quader {
namespace {

class CurveViewportInteractionAdapter final
    : public ViewportCurveInteractionPort {
public:
  explicit CurveViewportInteractionAdapter(CurveToolService &curve)
      : curve_(curve) {}

  [[nodiscard]] bool active() const override { return curve_.active(); }
  [[nodiscard]] bool controls_editable() const override {
    return curve_.controls_editable();
  }
  [[nodiscard]] bool dragging() const override { return curve_.dragging(); }
  [[nodiscard]] bool point_construction_dragging() const override {
    return curve_.point_construction_dragging();
  }
  [[nodiscard]] const CurveToolPreviewState &preview_state() const override {
    return curve_.preview_state();
  }
  [[nodiscard]] const CurveControlSelection &
  control_selection() const override {
    return curve_.control_selection();
  }
  [[nodiscard]] std::uint64_t
  control_selection_revision() const override {
    return curve_.control_selection_revision();
  }
  [[nodiscard]] std::optional<QVec3>
  selected_controls_world_pivot() const override {
    return curve_.selected_controls_world_pivot();
  }
  CommandResult refresh() override { return curve_.refresh(); }
  CommandResult begin_selected_control_drag() override {
    return curve_.begin_selected_control_drag();
  }
  CommandResult update_drag(QVec3 world_position) override {
    return curve_.update_drag(world_position);
  }
  CommandResult commit_drag() override { return curve_.commit_drag(); }
  CommandResult cancel_drag() override { return curve_.cancel_drag(); }

private:
  CurveToolService &curve_;
};

} // namespace

NativeViewportToolCoordinator::NativeViewportToolCoordinator(
    PivotToolService &pivot, LinearArrayToolService &linear_array,
    RadialArrayToolService &radial_array, PathArrayToolService &path_array,
    PrimitiveCreationToolService &primitives,
    LightCreationToolService &light_creation,
    EditorSelectionService &selection, ModelingSessionService &modeling,
    CurveToolService &curve, UvPeelToolService &uv_peel,
    ExtrudeToolService &extrude)
    : pivot_(pivot), linear_array_(linear_array), radial_array_(radial_array),
      path_array_(path_array), primitives_(primitives),
      light_creation_(light_creation), selection_(selection),
      modeling_(modeling), curve_(curve), uv_peel_(uv_peel), extrude_(extrude),
      curve_interaction_(
          std::make_unique<CurveViewportInteractionAdapter>(curve_)) {
  // Registration order preserves commit/cancel precedence. active_priority
  // preserves the former presentation order after a failed cancellation.
  (void)runtime_.register_native_lifecycle({
      .tool = ViewportToolId::Curve,
      .active_priority = 0,
      .unavailable_message =
          "Select one visible, unlocked mesh or group in object mode.",
      .commit_label = "Apply Curve",
      .can_activate = [&] { return curve_.can_activate(); },
      .active = [&] { return curve_.active(); },
      .pending = [&] { return curve_.active(); },
      .pending_history_preview = [&] {
        return curve_.dragging() ||
               (curve_.drawing() && curve_.point_count() != 0u);
      },
      .blocks_modeling_content_mutation = [&] { return curve_.active(); },
      .blocks_document_mutation =
          [&](EntityId entity) { return curve_.owns_editing_entity(entity); },
      .activate = [&](double) { return curve_.activate(); },
      .commit = [&] {
        CommandResult applied = curve_.apply();
        if (!applied.ok) {
          return applied;
        }

        // A finalized Curve is an ordinary selected scene object. End the
        // creation lifecycle so CurveToolService retains it only through the
        // passive selected-Curve editing path, then make Select authoritative
        // for every viewport and Tool Settings observer.
        CommandResult exited = curve_.cancel();
        if (!exited.ok) {
          exited.changed = applied.changed || exited.changed;
          return exited;
        }

        ModelingCommandResult selected =
            modeling_.activate_viewport_tool(ModelingViewportTool::Select);
        return CommandResult{
            .ok = selected.ok,
            .changed =
                applied.changed || exited.changed || selected.changed,
            .message = !selected.message.empty()
                           ? std::move(selected.message)
                           : (!exited.message.empty()
                                  ? std::move(exited.message)
                                  : std::move(applied.message)),
        };
      },
      .cancel = [&] { return curve_.cancel(); },
      .reset = [&] { (void)curve_.cancel(); },
  });
  (void)runtime_.register_native_lifecycle({
      .tool = ViewportToolId::CreatePrimitive,
      .active_priority = 4,
      .unavailable_message =
          "Select one visible, unlocked mesh or group in object mode.",
      .commit_label = "Apply Primitive Tool",
      .can_activate = [] { return true; },
      .active = [&] { return primitives_.active(); },
      .pending = [&] { return primitives_.active(); },
      .pending_history_preview = [&] { return primitives_.active(); },
      .blocks_modeling_content_mutation = [&] { return primitives_.active(); },
      .blocks_document_mutation = [&](EntityId entity) {
        const std::optional<int> object_id =
            modeling_.modeling_object_for_entity(entity);
        return object_id.has_value() &&
               primitives_.owns_created_object(*object_id);
      },
      .activate = [&](double) { return primitives_.activate(); },
      .commit = [&] { return primitives_.apply(); },
      .cancel = [&] { return primitives_.cancel(); },
      .reset = [&] { (void)primitives_.cancel(); },
      .local_history = ViewportNativeToolHistoryAdapter{
          .label = "Create Primitive",
          .can_undo = [&] { return primitives_.can_undo(); },
          .can_redo = [&] { return primitives_.can_redo(); },
          .undo = [&] { return primitives_.undo(); },
          .redo = [&] { return primitives_.redo(); },
      },
  });
  (void)runtime_.register_native_lifecycle({
      .tool = ViewportToolId::LinearArray,
      .active_priority = 1,
      .unavailable_message =
          "Select one or more visible, unlocked meshes in object mode.",
      .commit_label = "Apply Linear Array",
      .can_activate = [&] { return linear_array_.can_activate(); },
      .active = [&] { return linear_array_.active(); },
      .pending = [&] { return linear_array_.has_session(); },
      .pending_history_preview = [&] { return linear_array_.has_session(); },
      .activate = [&](double grid_size) {
        return linear_array_.activate(grid_size);
      },
      .commit = [&] { return linear_array_.apply(); },
      .cancel = [&] { return linear_array_.cancel(); },
      .reset = [&] { (void)linear_array_.cancel(); },
  });
  (void)runtime_.register_native_lifecycle({
      .tool = ViewportToolId::RadialArray,
      .active_priority = 2,
      .unavailable_message =
          "Select one or more visible, unlocked meshes in object mode.",
      .commit_label = "Apply Radial Array",
      .can_activate = [&] { return radial_array_.can_activate(); },
      .active = [&] { return radial_array_.active(); },
      .pending = [&] { return radial_array_.has_session(); },
      .pending_history_preview = [&] { return radial_array_.has_session(); },
      .activate = [&](double grid_size) {
        return radial_array_.activate(grid_size);
      },
      .commit = [&] { return radial_array_.apply(); },
      .cancel = [&] { return radial_array_.cancel(); },
      .reset = [&] { (void)radial_array_.cancel(); },
  });
  (void)runtime_.register_native_lifecycle({
      .tool = ViewportToolId::PathArray,
      .active_priority = 3,
      .unavailable_message =
          "Select exactly one visible, unlocked, nondegenerate Curve.",
      .commit_label = "Path Array",
      .commit_domain = ViewportNativeToolCommitDomain::Document,
      .can_activate = [&] { return path_array_.can_activate(); },
      .active = [&] { return path_array_.active(); },
      .pending = [&] { return path_array_.has_session(); },
      .pending_history_preview = [&] { return path_array_.has_session(); },
      .blocks_modeling_content_mutation =
          [&] { return path_array_.blocks_modeling_content_mutation(); },
      .blocks_document_mutation =
          [&](EntityId entity) {
            return path_array_.blocks_document_mutation(entity);
          },
      .activate = [&](double) { return path_array_.activate(); },
      .commit = [&] { return path_array_.apply(); },
      .cancel = [&] { return path_array_.cancel(); },
      .reset = [&] { (void)path_array_.cancel(); },
  });
  (void)runtime_.register_native_lifecycle({
      .tool = ViewportToolId::Pivot,
      .active_priority = 5,
      .unavailable_message =
          "Select one visible, unlocked mesh or group in object mode.",
      .commit_label = "Apply Pivot",
      .commit_domain = ViewportNativeToolCommitDomain::Document,
      .can_activate = [&] { return pivot_.can_activate_persistent(selection_); },
      .active = [&] { return pivot_.persistent_active(); },
      .pending = [&] { return pivot_.persistent_active(); },
      .pending_history_preview = [&] { return pivot_.persistent_active(); },
      .activate = [&](double) { return pivot_.activate_persistent(selection_); },
      .commit = [&] {
        CommandResult applied = pivot_.apply_persistent();
        if (!applied.ok) {
          return applied;
        }
        ModelingCommandResult activated =
            modeling_.activate_viewport_tool(ModelingViewportTool::Move);
        return CommandResult{
            .ok = activated.ok,
            .changed = applied.changed || activated.changed,
            .message = std::move(activated.message),
        };
      },
      .cancel = [&] { return pivot_.cancel_persistent(); },
      .reset = [&] {
        (void)pivot_.cancel_persistent();
        pivot_.clear_quick_override();
      },
  });
  (void)runtime_.register_native_lifecycle({
      .tool = ViewportToolId::UvPeel,
      .active_priority = 6,
      .commit_label = "UV Peel",
      .can_activate = [] {
        return release_has_feature(ReleaseFeature::UvEditing);
      },
      .active = [&] { return uv_peel_.active(); },
      .pending = [&] { return uv_peel_.active(); },
      .pending_history_preview = [&] { return uv_peel_.active(); },
      .blocks_modeling_content_mutation = [&] { return uv_peel_.active(); },
      .blocks_document_mutation = [&](EntityId) { return uv_peel_.active(); },
      .activate = [&](double) { return uv_peel_.activate(); },
      .commit = [&] { return uv_peel_.apply(); },
      .cancel = [&] { return uv_peel_.cancel(); },
      .reset = [&] { (void)uv_peel_.cancel(); },
  });
  (void)runtime_.register_native_lifecycle({
      .tool = ViewportToolId::Extrude,
      .active_priority = 7,
      .commit_label = "Extrude",
      .can_activate = [] { return true; },
      .active = [&] { return extrude_.active(); },
      .pending = [&] { return extrude_.active(); },
      .pending_history_preview = [&] { return extrude_.preview_active(); },
      .blocks_modeling_content_mutation = [&] { return extrude_.preview_active(); },
      .blocks_document_mutation = [&](EntityId) { return extrude_.preview_active(); },
      .activate = [&](double) { return extrude_.activate_preview(); },
      .commit = [&] { return extrude_.commit_preview(); },
      .cancel = [&] { return extrude_.cancel(); },
      .reset = [&] { (void)extrude_.cancel(); },
  });
  assert(runtime_.native_lifecycle_catalog_complete());
}

NativeViewportToolCoordinator::~NativeViewportToolCoordinator() = default;

bool NativeViewportToolCoordinator::handles(std::string_view tool_id) const {
  return runtime_.handles_native_lifecycle(tool_id);
}

bool NativeViewportToolCoordinator::can_activate(
    std::string_view tool_id) const {
  if (!release_allows_tool(tool_id)) {
    return false;
  }
  if (extrude_.preview_active() && tool_id != kExtrudeToolId) {
    return false;
  }
  return runtime_.can_activate_native_lifecycle(tool_id);
}

bool NativeViewportToolCoordinator::has_pending_tool() const {
  return light_creation_.active() || runtime_.has_pending_native_lifecycle();
}

bool NativeViewportToolCoordinator::has_pending_history_preview() const {
  return runtime_.has_pending_native_history_preview();
}

bool NativeViewportToolCoordinator::blocks_modeling_content_mutation() const {
  return light_creation_.active() ||
         runtime_.native_lifecycle_blocks_modeling_content_mutation();
}

bool NativeViewportToolCoordinator::blocks_document_mutation(
    EntityId entity) const {
  return runtime_.native_lifecycle_blocks_document_mutation(entity);
}

std::string_view NativeViewportToolCoordinator::active_tool_id() const {
  if (light_creation_.active()) {
    return light_creation_.active_tool_id();
  }
  return runtime_.active_native_lifecycle_id();
}

CommandResult NativeViewportToolCoordinator::refresh() {
  const CommandResult linear = linear_array_.refresh();
  const CommandResult radial = radial_array_.refresh();
  const CommandResult path = path_array_.refresh();
  const CommandResult curve = curve_.refresh();
  const CommandResult uv_peel = uv_peel_.refresh();
  const CommandResult extrude = extrude_.refresh();

  if (linear.changed || path.changed ||
      (curve.changed && !curve_.dragging())) {
    interaction_.clear_drag_state();
  }

  const auto first_failure = [&]() -> const CommandResult * {
    for (const CommandResult *candidate : {&linear, &radial, &path, &curve,
                                           &uv_peel, &extrude}) {
      if (!candidate->ok) {
        return candidate;
      }
    }
    return nullptr;
  }();
  return {
      .ok = first_failure == nullptr,
      .changed =
          linear.changed || radial.changed || path.changed || curve.changed ||
          uv_peel.changed || extrude.changed,
      .message = first_failure != nullptr ? first_failure->message
                                          : std::string{},
  };
}

NativeViewportToolPresentationSnapshot
NativeViewportToolCoordinator::presentation_snapshot() const {
  ExtrudeHandleState extrusion = extrude_.handle_state();
  extrusion.new_hovered = extrude_interaction_.hovered_part() ==
                          ExtrudeHandlePart::NewExtrusion;
  return {
      .pivot_handle = pivot_.handle_state(),
      .linear_array_handle = linear_array_.handle_state(),
      .radial_array_center = radial_array_.center_state(),
      .extrusion_handle = extrusion,
      .curve_preview = &curve_.preview_state(),
      .curve_controls_editable = curve_.controls_editable(),
      .curve_dragging = curve_.dragging(),
      .path_array_source_picking = path_array_.active(),
      .selection_pivot_revision = pivot_.selection_pivot_revision(),
  };
}

ViewportNativeToolInteractionResult
NativeViewportToolCoordinator::handle_pointer_frame(
    SceneDocument &document, AssetRegistry &assets,
    ViewportPointPlacementService *point_placement,
    ViewportCameraController &camera,
    const ViewportNativeToolInteractionFrame &frame) {
  if (extrude_.active() || extrude_interaction_.dragging() ||
      extrude_interaction_.suppressing_release()) {
    return extrude_interaction_.handle_frame(extrude_, camera, frame, &modeling_);
  }
  if (uv_peel_.active()) {
    if (frame.input_result.cancel_requested) {
      const CommandResult canceled = cancel_active();
      return {.consumed = true,
              .sync_active_tool = canceled.ok,
              .invalidate_hover = canceled.changed,
              .mark_overlay_dirty = canceled.changed,
              .command_attempted = true,
              .command_result = canceled};
    }
    if (frame.input.modal_tool_finalize_pressed &&
        !frame.input.navigation_active && !frame.input_result.navigation_active &&
        !frame.input.text_input_active && frame.input.viewport_focused &&
        uv_peel_.preview_valid()) {
      const CommandResult applied = commit_active();
      return {.consumed = true,
              .sync_active_tool = applied.ok,
              .invalidate_hover = applied.changed,
              .mark_overlay_dirty = applied.changed,
              .command_attempted = true,
              .command_result = applied};
    }
    // Edge picking and marquee selection own ordinary pointer frames. Gizmo
    // availability comes from the UV Peel descriptor without consuming picks.
    return {};
  }
  return interaction_.handle_frame(*this, document, assets, &pivot_,
                                   &linear_array_, &radial_array_, &path_array_,
                                   &selection_, point_placement, camera, frame,
                                   &curve_);
}

void NativeViewportToolCoordinator::clear_pointer_drag_state() {
  interaction_.clear_drag_state();
  extrude_interaction_.clear_drag_state();
  (void)extrude_.cancel_drag();
  (void)curve_.set_preset_hover(std::nullopt);
}

bool NativeViewportToolCoordinator::pointer_dragging() const {
  return interaction_.dragging() || extrude_interaction_.dragging();
}

ViewportPointerCursor NativeViewportToolCoordinator::pointer_cursor() const {
  if (extrude_.active()) {
    return extrude_interaction_.pointer_cursor();
  }
  if (light_creation_.active()) {
    return ViewportPointerCursor::PrecisionCross;
  }
  return interaction_.pointer_cursor();
}

bool NativeViewportToolCoordinator::curve_control_gizmo_start_allowed(
    ViewportToolId active_tool, bool selection_fallback_allowed,
    bool active_tool_owns_viewport, bool modal_tool_active) const {
  return curve_interaction_->allows_control_gizmo_start({
      .native_lifecycle_active =
          runtime_.native_lifecycle_active(viewport_tool_id(active_tool)),
      .selection_fallback_allowed = selection_fallback_allowed,
      .active_tool_owns_viewport = active_tool_owns_viewport,
      .modal_tool_active = modal_tool_active,
  });
}

CommandResult NativeViewportToolCoordinator::activate(
    std::string_view tool_id, double grid_world_size) {
  if (!release_allows_tool(tool_id)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (extrude_.preview_active() && tool_id != kExtrudeToolId) {
    return command_invalid_result(
        "Apply or cancel the extrusion before changing tools.");
  }
  if (tool_id == kExtrudeToolId && extrude_.active()) {
    return extrude_.activate_preview();
  }
  return runtime_.activate_native_lifecycle(
      tool_id, grid_world_size,
      [&] { return cancel_competing_state_for_tool_activation(); });
}

CommandResult NativeViewportToolCoordinator::activate_extrude(
    modeling::FaceExtrudeMode mode) {
  if (!release_has_feature(ReleaseFeature::ExtrudeTool)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (extrude_.active()) {
    return extrude_.activate_preview(mode);
  }
  const CommandResult canceled = cancel_competing_state_for_tool_activation();
  if (!canceled.ok) {
    return canceled;
  }
  CommandResult activated = extrude_.activate_preview(mode);
  activated.changed = activated.changed || canceled.changed;
  return activated;
}

CommandResult NativeViewportToolCoordinator::begin_light_creation(
    LightCreationKind kind) {
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (light_creation_.active_kind() == kind) {
    return command_unchanged_result();
  }
  const CommandResult canceled =
      cancel_competing_state_for_tool_activation();
  if (!canceled.ok) {
    return canceled;
  }
  CommandResult begun = light_creation_.begin(kind);
  begun.changed = begun.changed || canceled.changed;
  return begun;
}

CommandResult NativeViewportToolCoordinator::complete_active_light_creation(
    LightCreationOperation operation) {
  return light_creation_.complete(operation);
}

CommandResult
NativeViewportToolCoordinator::cancel_competing_state_for_tool_activation() {
  if (extrude_.preview_active()) {
    return command_invalid_result(
        "Apply or cancel the extrusion before changing tools.");
  }
  return cancel_transient_state();
}

CommandResult NativeViewportToolCoordinator::cancel_transient_state() {
  const ModelingCommandResult modeling_cancel =
      modeling_.cancel_competing_state_for_native_tool_activation();
  if (!modeling_cancel.ok) {
    return {
        .ok = false,
        .changed = modeling_cancel.changed,
        .message = modeling_cancel.message,
    };
  }

  const CommandResult native_cancel = runtime_.cancel_native_lifecycles();
  if (!native_cancel.ok) {
    return {
        .ok = false,
        .changed = modeling_cancel.changed || native_cancel.changed,
        .message = native_cancel.message,
    };
  }
  const CommandResult light_cancel = light_creation_.cancel();
  if (!light_cancel.ok) {
    return {
        .ok = false,
        .changed = modeling_cancel.changed || native_cancel.changed ||
                   light_cancel.changed,
        .message = light_cancel.message,
    };
  }
  interaction_.clear_drag_state();
  extrude_interaction_.clear_drag_state();
  return {
      .ok = true,
      .changed = modeling_cancel.changed || native_cancel.changed ||
                 light_cancel.changed,
  };
}

CommandResult NativeViewportToolCoordinator::commit_active() {
  const CommandResult result = runtime_.commit_active_native_lifecycle();
  if (result.ok) {
    interaction_.clear_drag_state();
    extrude_interaction_.clear_drag_state();
  }
  return result;
}

CommandResult NativeViewportToolCoordinator::undo_active() {
  return runtime_.undo_active_native_lifecycle();
}

CommandResult NativeViewportToolCoordinator::redo_active() {
  return runtime_.redo_active_native_lifecycle();
}

CommandResult NativeViewportToolCoordinator::cancel_active() {
  if (extrude_.dragging()) {
    const CommandResult result = extrude_.cancel_drag();
    if (result.ok) {
      extrude_interaction_.clear_drag_state();
    }
    return result;
  }
  const CommandResult native = runtime_.cancel_native_lifecycles();
  const CommandResult light = light_creation_.cancel();
  const CommandResult result{
      .ok = native.ok && light.ok,
      .changed = native.changed || light.changed,
      .message = !native.ok ? native.message
                            : (!light.ok ? light.message : std::string{}),
  };
  if (result.ok) {
    interaction_.clear_drag_state();
    extrude_interaction_.clear_drag_state();
  }
  return result;
}

CommandResult NativeViewportToolCoordinator::accept_active_primitive(
    std::string name, MaterialReference material, bool snap_enabled) {
  if (!primitives_.active()) {
    return command_invalid_result("No primitive creation tool is active.");
  }
  return primitives_.accept_footprint(modeling_.box_preview(snap_enabled),
                                      std::move(name), material);
}

void NativeViewportToolCoordinator::reset_transient_state() {
  clear_pointer_drag_state();
  runtime_.reset_native_lifecycles();
  light_creation_.reset();
}

} // namespace quader
