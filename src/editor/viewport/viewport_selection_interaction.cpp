////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_selection_interaction.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/interactions/selection_edit.hpp"
#include "editor/interactions/transform_gizmo.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/selection/box_selection_rules.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <span>

namespace quader {
namespace {

// The plane-cut API and implementation remain available; its viewport gesture
// is intentionally dormant while Alt+LMB continues the selected face texture.
constexpr bool kFacePlaneCutGestureEnabled = false;

QuaderSelectionEdit selection_edit_to_api(
    quader_editor_core::SelectionEditOperation operation) {
  switch (operation) {
  case quader_editor_core::SelectionEditOperation::Add:
    return QuaderSelectionEdit::Add;
  case quader_editor_core::SelectionEditOperation::Remove:
    return QuaderSelectionEdit::Remove;
  case quader_editor_core::SelectionEditOperation::Toggle:
    return QuaderSelectionEdit::Toggle;
  case quader_editor_core::SelectionEditOperation::Replace:
    return QuaderSelectionEdit::Replace;
  }
  return QuaderSelectionEdit::Replace;
}

ViewportSelectionMarquee box_marquee(QVec2 start, QVec2 end) {
  return {
      .active = true,
      .start = start,
      .end = end,
      .rule = resolve_box_selection_rule(
          ModelingBoxSelectionRule::DragDirection, start, end),
  };
}

bool hover_target_valid(const QuaderHoverTarget &target) {
  return target.kind != QuaderActiveSelectionKind::None && target.object_id > 0;
}

bool hover_targets_match(const QuaderHoverTarget &left,
                         const QuaderHoverTarget &right) {
  if (left.kind != right.kind || left.object_id != right.object_id) {
    return false;
  }
  switch (left.kind) {
  case QuaderActiveSelectionKind::Object:
    return true;
  case QuaderActiveSelectionKind::Vertex:
    return left.vertex_id == right.vertex_id;
  case QuaderActiveSelectionKind::Edge:
    return (left.edge_a == right.edge_a && left.edge_b == right.edge_b) ||
           (left.edge_a == right.edge_b && left.edge_b == right.edge_a);
  case QuaderActiveSelectionKind::Face:
    return left.face_id == right.face_id;
  case QuaderActiveSelectionKind::None:
  case QuaderActiveSelectionKind::Light:
    break;
  }
  return false;
}

QuaderPickSelectRequest make_pick_select_request(
    const ViewportCameraController &camera, const ViewportSettings &settings,
    QVec2 local_mouse, ViewportModifiers modifiers, int click_count,
    bool preserve_selection_on_miss, bool preserve_selection_on_selected_hit,
    bool preserve_existing_component_selection_on_hit = false,
    bool cycle_depth = false) {
  const ViewportRay world_ray = camera.ray_from_viewport_point(local_mouse);
  return {
      .ray =
          {
              .origin = world_ray.origin,
              .direction = world_ray.direction,
          },
      .click_count = click_count,
      .control_pressed = modifiers.primary,
      .shift_pressed = modifiers.shift,
      .command_pressed = modifiers.primary,
      .alt_pressed = modifiers.alt,
      .preserve_selection_on_miss = preserve_selection_on_miss,
      .preserve_selection_on_selected_hit = preserve_selection_on_selected_hit,
      .preserve_existing_component_selection_on_hit =
          preserve_existing_component_selection_on_hit,
      .vertex_radius =
          settings.float_value(ViewportFloatSettingId::PickVertexRadius),
      .edge_radius =
          settings.float_value(ViewportFloatSettingId::PickEdgeRadius),
      .select_backfaces =
          settings.bool_value(ViewportBoolSettingId::BackfaceSelection),
      .cycle_depth = cycle_depth,
      .cycle_origin_tolerance =
          camera.state().projection == ViewportProjection::Orthographic
              ? quader_editor_core::kSelectionBoxDefaultDragThreshold *
                    quader_editor_core::viewport_world_units_per_pixel(
                        ViewportCameraAdapter::runtime_camera(
                            camera, camera.viewport_size()),
                        world_ray.origin)
              : 0.0,
  };
}

QuaderHoverRequest make_hover_request(const ViewportCameraController &camera,
                                      const ViewportSettings &settings,
                                      QVec2 local_mouse,
                                      ViewportModifiers modifiers) {
  const ViewportRay world_ray = camera.ray_from_viewport_point(local_mouse);
  return {
      .ray =
          {
              .origin = world_ray.origin,
              .direction = world_ray.direction,
      },
      .selected_modifier_pressed = modifiers.primary,
      .face_plane_hover = kFacePlaneCutGestureEnabled && modifiers.alt,
      .vertex_radius =
          settings.float_value(ViewportFloatSettingId::PickVertexRadius),
      .edge_radius =
          settings.float_value(ViewportFloatSettingId::PickEdgeRadius),
      .select_backfaces =
          settings.bool_value(ViewportBoolSettingId::BackfaceSelection),
  };
}

QuaderBoxSelectRequest
make_box_select_request(const ViewportCameraController &camera,
                        const ViewportSettings &settings,
                        QVec2 viewport_size, QVec2 start, QVec2 end,
                        quader_editor_core::SelectionEditOperation edit) {
  return {
      .camera = to_api_viewport_camera(
          ViewportCameraAdapter::modeling_camera(camera, viewport_size)),
      .start = start,
      .end = end,
      .edit = selection_edit_to_api(edit),
      .rule = QuaderBoxSelectionRule::DragDirection,
      .select_backfaces =
          settings.bool_value(ViewportBoolSettingId::BackfaceSelection),
  };
}

bool entity_is_selected(const QuaderEditorApi &editor, EntityId entity) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      editor.query().entity(entity);
  return snapshot.has_value() && snapshot->selected;
}

std::optional<EntityId>
active_selected_instance(const QuaderEditorApi &editor) {
  for (const QuaderEntitySnapshot &entity : editor.query().entities()) {
    if (entity.active && entity.selected &&
        entity.kind == QuaderEntityKind::Instance &&
        entity.has_instance_occurrence) {
      return entity.id;
    }
  }
  return std::nullopt;
}

bool plain_instance_edit_double_click(const ViewportInteractionFrame &frame,
                                      const QuaderSelectionSummary &summary,
                                      int click_count) {
  return click_count >= 2 &&
         summary.mode == QuaderSelectionMode::Object &&
         !frame.input.modifiers.primary && !frame.input.modifiers.shift &&
         !frame.input.modifiers.alt && !frame.input.text_input_active &&
         !frame.input.navigation_active &&
         !frame.input.operation_preview_active &&
         !frame.input.modal_tool_active &&
         !frame.input.active_tool_owns_viewport &&
         !frame.input_result.consumed &&
         !frame.input_result.navigation_active &&
         !frame.input_result.camera_changed;
}

void enter_picked_instance(QuaderEditorApi &editor,
                           const ViewportInteractionFrame &frame,
                           const QuaderSelectionSummary &summary,
                           int click_count, bool cycle_depth) {
  if (cycle_depth ||
      !plain_instance_edit_double_click(frame, summary, click_count)) {
    return;
  }
  if (const std::optional<EntityId> occurrence = active_selected_instance(editor)) {
    const QuaderApiVoidResult entered =
        editor.modeling().enter_instance_edit(*occurrence);
    if (!entered.ok()) {
      LOG_ERROR("Instance edit entry failed: {}",
                entered.error_message().empty() ? "Instance edit entry failed."
                                                : entered.error_message());
    }
  }
}

bool depth_cycle_context_available(const ViewportInteractionFrame &frame) {
  return frame.camera.state().projection == ViewportProjection::Orthographic &&
         frame.input_router.state().selection_mode ==
             ViewportSelectionMode::Object &&
         frame.input.viewport_focused && !frame.input.text_input_active &&
         !frame.input.navigation_active &&
         !frame.input.operation_preview_active &&
         !frame.input.active_transform_drag && !frame.input.modal_tool_active &&
         !frame.input.active_tool_owns_viewport &&
         !frame.input.keybinding_input_consumed &&
         !frame.input_result.consumed &&
         !frame.input_result.navigation_active &&
         !frame.input_result.camera_changed;
}

std::optional<EntityId> pick_scene_billboard_entity(
    const ViewportInteractionFrame &frame, QVec2 local_mouse) {
  EntityId best_entity = kInvalidEntityId;
  float best_depth = std::numeric_limits<float>::max();
  for (const ViewportSceneBillboardPickCandidate &candidate :
       frame.scene_billboard_pick_candidates) {
    if (candidate.entity == kInvalidEntityId) {
      continue;
    }
    const ViewportScreenPoint projected =
        frame.camera.project_world_point(candidate.position);
    if (!projected.visible || projected.depth < 0.0f ||
        projected.depth >= best_depth) {
      continue;
    }
    const float half_size = std::max(candidate.size_pixels, 1.0f) * 0.5f;
    if (local_mouse.x < projected.position.x - half_size ||
        local_mouse.x > projected.position.x + half_size ||
        local_mouse.y < projected.position.y - half_size ||
        local_mouse.y > projected.position.y + half_size) {
      continue;
    }
    best_entity = candidate.entity;
    best_depth = projected.depth;
  }
  if (best_entity == kInvalidEntityId) {
    return std::nullopt;
  }
  return best_entity;
}

QuaderSelectionSummary select_billboard_entity(
    QuaderEditorApi &editor, ViewportInputRouter &input_router,
    EntityId entity, ViewportModifiers modifiers,
    bool preserve_selection_on_selected_hit) {
  QDR_PROFILE_FUNCTION();
  const quader_editor_core::SelectionEditOperation operation =
      quader_editor_core::selection_edit_operation_from_modifiers(
          modifiers.primary, modifiers.shift);
  const QuaderSelectionEdit edit = selection_edit_to_api(operation);
  const bool preserves_selected_hit =
      preserve_selection_on_selected_hit &&
      edit == QuaderSelectionEdit::Replace && entity_is_selected(editor, entity);
  if (!preserves_selected_hit) {
    const QuaderApiVoidResult result =
        editor.selection().select_object(entity, edit);
    if (!result.ok) {
      LOG_ERROR("Viewport billboard selection failed: {}",
                result.message.empty() ? "Selection failed." : result.message);
    }
  }
  const QuaderSelectionSummary summary = editor.selection().summary();
  input_router.set_selection_mode(
      app_selection_mode_to_viewport(to_modeling_selection_mode(summary.mode)));
  return summary;
}

QuaderSelectionSummary apply_pick_request(
    QuaderEditorApi &editor, ViewportInputRouter &input_router,
    const QuaderPickSelectRequest &request,
    const QuaderSelectionBoxBaseline *baseline = nullptr) {
  const QuaderApiResult<QuaderSelectionSummary> result =
      baseline != nullptr && baseline->valid()
          ? editor.selection().pick_select(request, *baseline)
          : editor.selection().pick_select(request);
  if (!result.ok) {
    LOG_ERROR("Viewport selection failed: {}",
              result.message.empty() ? "Selection failed." : result.message);
  }
  const QuaderSelectionSummary summary =
      result.ok ? result.value() : editor.selection().summary();
  input_router.set_selection_mode(
      app_selection_mode_to_viewport(to_modeling_selection_mode(summary.mode)));
  return summary;
}

std::optional<EntityId> pick_scene_curve_entity(
    const ViewportInteractionFrame &frame, QVec2 local_mouse) {
  const auto mode = frame.input_router.state().selection_mode;
  if (mode != ViewportSelectionMode::Object && mode != ViewportSelectionMode::Group) {
    return std::nullopt;
  }
  auto hit = pick_viewport_curve(frame.camera, local_mouse,
                                 frame.scene_curve_pick_candidates);
  if (hit.has_value() && frame.scene_mesh_surface_distance) {
    const auto mesh_distance = frame.scene_mesh_surface_distance(
        frame.camera.ray_from_viewport_point(local_mouse));
    if (mesh_distance.has_value() && hit->ray_distance > *mesh_distance + 1.0e-5) {
      hit = pick_viewport_curve(frame.camera, local_mouse,
          frame.scene_curve_pick_candidates, 8.0, mesh_distance);
    }
  }
  return hit.has_value() ? std::optional<EntityId>{hit->entity} : std::nullopt;
}

QuaderSelectionSummary apply_curve_pick_request(
    QuaderEditorApi &editor, ViewportInputRouter &input_router,
    EntityId entity, const QuaderPickSelectRequest &request) {
  const auto result = editor.selection().pick_scene_entity(entity, request);
  if (!result.ok()) {
    LOG_ERROR("Viewport curve selection failed: {}", result.error_message());
  }
  const auto summary = result.ok() ? result.value() : editor.selection().summary();
  input_router.set_selection_mode(
      app_selection_mode_to_viewport(to_modeling_selection_mode(summary.mode)));
  return summary;
}

QuaderSelectionSummary
pick_selection(QuaderEditorApi &editor, ViewportInputRouter &input_router,
               const ViewportInteractionFrame &frame, QVec2 local_mouse,
               int click_count, bool preserve_selection_on_miss = false,
               bool preserve_selection_on_selected_hit = false,
               bool preserve_existing_component_selection_on_hit = false,
               bool cycle_depth = false,
               ViewportModifiers cycle_modifiers = {},
               const QuaderSelectionBoxBaseline *baseline = nullptr) {
  QDR_PROFILE_FUNCTION();
  if (!cycle_depth) {
    if (const std::optional<EntityId> billboard_entity =
            pick_scene_billboard_entity(frame, local_mouse)) {
      return select_billboard_entity(editor, input_router, *billboard_entity,
                                     frame.input.modifiers,
                                     preserve_selection_on_selected_hit);
    }
  }
  const QuaderPickSelectRequest request = make_pick_select_request(
          frame.camera, frame.settings, local_mouse,
          cycle_depth ? cycle_modifiers : frame.input.modifiers,
          cycle_depth ? 1 : click_count, preserve_selection_on_miss,
          preserve_selection_on_selected_hit,
          preserve_existing_component_selection_on_hit, cycle_depth);
  if (!cycle_depth) {
    if (const auto curve_entity = pick_scene_curve_entity(frame, local_mouse)) {
      return apply_curve_pick_request(editor, input_router, *curve_entity, request);
    }
  }
  return apply_pick_request(editor, input_router, request, baseline);
}

QuaderSelectionSummary
box_selection(QuaderEditorApi &editor, ViewportInputRouter &input_router,
               const ViewportInteractionFrame &frame, QVec2 start, QVec2 end,
               quader_editor_core::SelectionEditOperation edit,
              const QuaderSelectionBoxBaseline *baseline = nullptr) {
  QDR_PROFILE_FUNCTION();
  const QuaderBoxSelectRequest request = make_box_select_request(
      frame.camera, frame.settings, frame.input.viewport_size, start, end,
      edit);
  const QuaderApiResult<QuaderSelectionSummary> result =
      baseline != nullptr && baseline->valid()
          ? editor.selection().box_select(request, *baseline)
          : editor.selection().box_select(request);
  if (!result.ok) {
    LOG_ERROR("Viewport box selection failed: {}", result.message.empty()
                                                       ? "Box selection failed."
                                                       : result.message);
  }
  const QuaderSelectionSummary summary =
      result.ok ? result.value() : editor.selection().summary();
  input_router.set_selection_mode(
      app_selection_mode_to_viewport(to_modeling_selection_mode(summary.mode)));
  return summary;
}

} // namespace

void ViewportSelectionInteraction::reset() {
  hover_clear_issued_ = false;
  quader_editor_core::clear_selection_box(box_state_);
  box_edit_ = quader_editor_core::SelectionEditOperation::Replace;
  box_cycle_depth_ = false;
  box_cycle_modifiers_ = {};
  cancel_pending_click();
  pending_pick_feedback_.reset();
  box_hovered_before_selection_.reset();
  box_baseline_.reset();
  curve_box_baseline_.reset();
  curve_control_box_active_ = false;
  committed_marquee_.reset();
  hover_suppressed_target_.reset();
  paint_selection_active_ = false;
  viewport_hover_state_initialized_ = false;
  viewport_hovered_last_frame_ = false;
}

bool ViewportSelectionInteraction::DepthClickBaseline::matches_context(
    const ViewportInteractionFrame &frame, SceneSessionId current_scene) const {
  const QuaderSelectionSummary &current = frame.selection_state.summary;
  if (camera != &frame.camera || scene_session_id != current_scene ||
      tool != frame.input_router.state().active_tool ||
      viewport_size.x != frame.input.viewport_size.x ||
      viewport_size.y != frame.input.viewport_size.y ||
      content_revision != current.content_revision ||
      selection_revision != current.selection_revision ||
      scene_revision != frame.selection_state.scene_revision ||
      asset_revision != frame.selection_state.asset_revision) {
    return false;
  }
  const ViewportRay current_ray = frame.camera.ray_from_viewport_point(local);
  const auto same_vector = [](QVec3 left, QVec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
  };
  return same_vector(ray.origin, current_ray.origin) &&
         same_vector(ray.direction, current_ray.direction);
}

bool ViewportSelectionInteraction::DepthClickBaseline::matches_frame(
    const ViewportInteractionFrame &frame, SceneSessionId current_scene) const {
  const QVec2 delta = frame.left_click_local - local;
  constexpr float kClickTolerance =
      quader_editor_core::kSelectionBoxDefaultDragThreshold;
  return delta.x * delta.x + delta.y * delta.y <=
             kClickTolerance * kClickTolerance &&
         matches_context(frame, current_scene);
}

void ViewportSelectionInteraction::cancel_pending_click() {
  if (depth_click_baseline_.has_value() &&
      depth_click_baseline_->delay_single_click && box_state_.active &&
      !box_state_.dragging) {
    quader_editor_core::clear_selection_box(box_state_);
    box_edit_ = quader_editor_core::SelectionEditOperation::Replace;
    box_cycle_depth_ = false;
    box_cycle_modifiers_ = {};
    box_hovered_before_selection_.reset();
    box_baseline_.reset();
  }
  pending_first_pick_.reset();
  depth_click_baseline_.reset();
}

std::optional<ViewportInteractionSelectionState>
ViewportSelectionInteraction::prepare_frame(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  pending_pick_feedback_.reset();
  if (!frame.active_tool_allows_selection_fallback ||
      !frame.input.viewport_hovered || !depth_cycle_context_available(frame) ||
      frame.input_result.cancel_requested) {
    cancel_pending_click();
    return std::nullopt;
  }
  if (!frame.left_pressed && !pending_first_pick_.has_value() &&
      !frame.input.cycle_selection_depth_pressed) {
    return std::nullopt;
  }
  const SceneSessionId scene_session_id =
      editor.scene_documents().active_scene_id().value_or(SceneSessionId{});
  // Recognition wins over timeout even when the event is consumed by a late
  // frame. Never publish the ordinary pick before a recognized depth action.
  if (frame.input.cycle_selection_depth_pressed ||
      (frame.left_pressed && frame.left_click_count > 1)) {
    pending_first_pick_.reset();
  }
  if (pending_first_pick_.has_value() &&
      !pending_click_matches_context(editor, frame)) {
    cancel_pending_click();
  }
  std::optional<ViewportInteractionSelectionState> resolved_selection;
  if (pending_first_pick_.has_value() && frame.left_pressed) {
    // Distinct single clicks must retain their ordering (notably Shift adds).
    // The controller forwards this fresh summary to gizmo and after-tool work.
    const QuaderSelectionSummary summary = commit_pending_pick(editor, frame);
    const QuaderRevisionStamp revisions = editor.query().revisions();
    resolved_selection = ViewportInteractionSelectionState{
        .summary = summary,
        .scene_revision = revisions.scene_revision,
        .asset_revision = revisions.asset_revision,
        .pivot_revision = frame.selection_state.pivot_revision,
    };
  }
  if (!frame.left_pressed) {
    // A keyboard binding is a standalone depth action, not the second half of
    // an earlier mouse click. Do not apply that earlier click's selection.
    if (frame.input.cycle_selection_depth_pressed && !frame.left_down) {
      depth_click_baseline_.reset();
    }
    return resolved_selection;
  }

  const bool double_click = frame.input.cycle_selection_depth_pressed;
  const ViewportInteractionSelectionState &selection_state =
      resolved_selection.has_value() ? *resolved_selection
                                     : frame.selection_state;
  const QuaderSelectionSummary &selection = selection_state.summary;
  // Native double-click recognition already owns timing. Frame processing may
  // be delayed beyond that interval, so never expire a recognized pair here.
  const bool keep_first_click =
      double_click && frame.left_click_count > 1 &&
      depth_click_baseline_.has_value() &&
      depth_click_baseline_->matches_frame(frame, scene_session_id);
  if (!keep_first_click) {
    const ViewportRay ray =
        frame.camera.ray_from_viewport_point(frame.left_click_local);
    depth_click_baseline_ = DepthClickBaseline{
        .selection = editor.selection().capture_box_selection_baseline(),
        .camera = &frame.camera,
        .ray = {.origin = ray.origin, .direction = ray.direction},
        .local = frame.left_click_local,
        .viewport_size = frame.input.viewport_size,
        .scene_session_id = scene_session_id,
        .tool = frame.input_router.state().active_tool,
        .content_revision = selection.content_revision,
        .selection_revision = selection.selection_revision,
        .scene_revision = selection_state.scene_revision,
        .asset_revision = selection_state.asset_revision,
        .keybindings_revision = editor.commands().keybindings_revision(),
        .delay_single_click = frame.input.selection_click_delay_for_double_click,
        .click_modifiers = frame.input.selection_click_delay_for_double_click
                               ? frame.input.selection_click_modifiers
                               : frame.input.modifiers,
        .deadline_seconds = frame.monotonic_time_seconds +
                            std::max(frame.double_click_interval_seconds, 0.0),
    };
  }
  depth_click_baseline_->double_click_pending = double_click;
  return resolved_selection;
}

bool ViewportSelectionInteraction::pending_click_matches_context(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) const {
  if (!depth_click_baseline_.has_value() ||
      !depth_click_baseline_->matches_context(
          frame, editor.scene_documents().active_scene_id().value_or(
                     SceneSessionId{})) ||
      depth_click_baseline_->keybindings_revision !=
          editor.commands().keybindings_revision()) {
    return false;
  }
  const QuaderPickSelectRequest &request = pending_first_pick_->request;
  return request.select_backfaces ==
             frame.settings.bool_value(ViewportBoolSettingId::BackfaceSelection) &&
         request.vertex_radius ==
             frame.settings.float_value(ViewportFloatSettingId::PickVertexRadius) &&
         request.edge_radius ==
             frame.settings.float_value(ViewportFloatSettingId::PickEdgeRadius);
}

QuaderSelectionSummary ViewportSelectionInteraction::commit_pending_pick(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  const PendingFirstPick pending = *pending_first_pick_;
  cancel_pending_click();
  pending_pick_feedback_ = PendingPickFeedback{
      .local = pending.local,
      .hovered_before_selection = pending.hovered_before_selection,
  };
  if (!pending.billboard_entity.has_value() && pending.curve_entity.has_value()) {
    return apply_curve_pick_request(editor, frame.input_router,
                                    *pending.curve_entity, pending.request);
  }
  return pending.billboard_entity.has_value()
             ? select_billboard_entity(
                   editor, frame.input_router, *pending.billboard_entity,
                   pending.modifiers,
                   pending.request.preserve_selection_on_selected_hit)
             : apply_pick_request(editor, frame.input_router, pending.request);
}

std::optional<QuaderSelectionSummary>
ViewportSelectionInteraction::commit_pick_selection(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    QVec2 local_mouse, int click_count, bool preserve_selection_on_miss,
    bool preserve_selection_on_selected_hit,
    bool preserve_existing_component_selection_on_hit, bool cycle_depth,
    ViewportModifiers cycle_modifiers,
    QuaderHoverTarget hovered_before_selection) {
  if (depth_click_baseline_.has_value() &&
      !depth_click_baseline_->matches_frame(
          frame, editor.scene_documents().active_scene_id().value_or(
                     SceneSessionId{}))) {
    cancel_pending_click();
  }
  if (!cycle_depth && click_count == 1 && depth_cycle_context_available(frame) &&
      depth_click_baseline_.has_value() &&
      (depth_click_baseline_->delay_single_click ||
       frame.input.selection_click_delay_for_double_click) &&
      frame.double_click_interval_seconds > 0.0) {
    const ViewportModifiers modifiers =
        frame.input.selection_click_delay_for_double_click
            ? frame.input.selection_click_modifiers
            : depth_click_baseline_->click_modifiers;
    pending_first_pick_ = PendingFirstPick{
        .request = make_pick_select_request(
            frame.camera, frame.settings, local_mouse, modifiers, click_count,
            preserve_selection_on_miss, preserve_selection_on_selected_hit,
            preserve_existing_component_selection_on_hit),
        .billboard_entity = pick_scene_billboard_entity(frame, local_mouse),
        .curve_entity = pick_scene_curve_entity(frame, local_mouse),
        .modifiers = modifiers,
        .local = local_mouse,
        .hovered_before_selection = hovered_before_selection,
    };
    return std::nullopt;
  }
  const bool apply_first_click_baseline =
      cycle_depth && (cycle_modifiers.primary || cycle_modifiers.shift) &&
      depth_click_baseline_.has_value() &&
      depth_click_baseline_->double_click_pending;
  const QuaderSelectionSummary summary = pick_selection(
      editor, frame.input_router, frame, local_mouse, click_count,
      preserve_selection_on_miss, preserve_selection_on_selected_hit,
      preserve_existing_component_selection_on_hit, cycle_depth, cycle_modifiers,
      apply_first_click_baseline ? &depth_click_baseline_->selection : nullptr);
  if (cycle_depth) {
    cancel_pending_click();
  } else if (depth_click_baseline_.has_value()) {
    // A single with no paired depth binding is still immediate. Its own
    // revision is allowed; unrelated changes invalidate the saved baseline.
    depth_click_baseline_->selection_revision = summary.selection_revision;
    depth_click_baseline_->content_revision = summary.content_revision;
    const QuaderRevisionStamp revisions = editor.query().revisions();
    depth_click_baseline_->scene_revision = revisions.scene_revision;
    depth_click_baseline_->asset_revision = revisions.asset_revision;
  }
  return summary;
}

ViewportSelectionMarquee ViewportSelectionInteraction::marquee() const {
  if (!box_state_.active || !box_state_.dragging) {
    return committed_marquee_.value_or(ViewportSelectionMarquee{});
  }
  return box_marquee(box_state_.start, box_state_.end);
}

void ViewportSelectionInteraction::clear_curve_control_box() {
  quader_editor_core::clear_selection_box(box_state_);
  curve_box_baseline_.reset();
  curve_control_box_active_ = false;
  box_edit_ = quader_editor_core::SelectionEditOperation::Replace;
}

void ViewportSelectionInteraction::handle_frame(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool,
    ViewportToolHoverPort *tool_hover, CurveToolService *curve) {
  QDR_PROFILE_FUNCTION();
  committed_marquee_.reset();
  const bool curve_controls_editable =
      curve != nullptr && curve->controls_editable();
  if (curve_control_box_active_ && !curve_controls_editable) {
    clear_curve_control_box();
  }
  const bool paint_selection_was_active = paint_selection_active_;
  const bool gizmo_consumed = before_tool.gizmo_consumed;
  const bool selection_fallback_active =
      frame.active_tool_allows_selection_fallback;
  const bool suppress_hover_work =
      frame.input_result.navigation_active ||
      frame.input_result.camera_changed || frame.input.text_input_active ||
      frame.input.modal_tool_active || frame.input.active_tool_owns_viewport ||
      gizmo_consumed;
  const bool can_use_selection_fallback =
      selection_fallback_active && !frame.input.text_input_active &&
      !frame.input.modal_tool_active &&
      !frame.input.active_tool_owns_viewport &&
      !frame.input_result.navigation_active &&
      !frame.input_result.camera_changed && !gizmo_consumed &&
      !tool_result.handled;
  const bool can_pick =
      can_use_selection_fallback && frame.input.viewport_hovered;
  const bool can_cycle_depth =
      can_pick && depth_cycle_context_available(frame);
  if (tool_result.handled ||
      (before_tool.gizmo_consumed && frame.left_released) ||
      !depth_cycle_context_available(frame) ||
      frame.input_result.cancel_requested) {
    cancel_pending_click();
  }
  // Some synthetic callers enter after-tool work directly; recognized depth
  // must cancel the deferred ordinary pick here as well as in prepare_frame.
  if (frame.input.cycle_selection_depth_pressed ||
      ((frame.left_pressed || frame.left_released) &&
       frame.left_click_count > 1)) {
    pending_first_pick_.reset();
  }
  // The gizmo can take over a pending selection click when the pointer crosses
  // its pick boundary between clicks. Its deferred request owns this release.
  if (before_tool.selection_click_requested ||
      (box_cycle_depth_ && !can_cycle_depth)) {
    if (curve_control_box_active_) {
      clear_curve_control_box();
    } else {
      quader_editor_core::clear_selection_box(box_state_);
    }
    box_edit_ = quader_editor_core::SelectionEditOperation::Replace;
    box_cycle_depth_ = false;
    box_cycle_modifiers_ = {};
    box_hovered_before_selection_.reset();
    box_baseline_.reset();
  }
  if (box_state_.active &&
      (frame.input.pointer_interaction_canceled ||
       frame.input_result.cancel_requested)) {
    clear_curve_control_box();
    box_cycle_depth_ = false;
    box_cycle_modifiers_ = {};
    box_hovered_before_selection_.reset();
    box_baseline_.reset();
  }
  // The first release and second double-click press can share a frame while
  // the ordinary first click still owns the pending selection box.
  if (box_state_.active && !box_state_.dragging &&
      (frame.left_pressed || frame.left_released) &&
      frame.input.cycle_selection_depth_pressed && can_cycle_depth) {
    box_cycle_depth_ = true;
    box_cycle_modifiers_ = frame.input.cycle_selection_depth_modifiers;
  }

  auto claim_hover_work = [&]() {
    return tool_hover == nullptr ||
           tool_hover->claim(ModelingBoxDragState::Idle);
  };

  auto refresh_hover_work_after_selection =
      [&](QVec2 local_mouse, bool consume_stationary_key) {
    if (tool_hover == nullptr) {
      return;
    }
    // Refresh the host-owned work-key inputs after the selection mutation.
    tool_hover->invalidate();
    if (!consume_stationary_key ||
        local_mouse.x != frame.input.mouse_local.x ||
        local_mouse.y != frame.input.mouse_local.y) {
      return;
    }
    // Held paint cannot run the release-time hover suppression comparison.
    // Consume its post-pick revision explicitly so the stationary pointer
    // does not paint again on the next frame.
    (void)tool_hover->claim(ModelingBoxDragState::Idle);
  };

  const bool pointer_left_viewport =
      viewport_hover_state_initialized_ && viewport_hovered_last_frame_ &&
      !frame.input.viewport_hovered;
  const bool pointer_entered_viewport =
      viewport_hover_state_initialized_ && !viewport_hovered_last_frame_ &&
      frame.input.viewport_hovered;
  viewport_hover_state_initialized_ = true;
  viewport_hovered_last_frame_ = frame.input.viewport_hovered;
  if (pointer_left_viewport) {
    hover_suppressed_target_.reset();
  }
  if (pointer_entered_viewport && tool_hover != nullptr &&
      frame.input_router.state().active_tool != ViewportToolId::Knife) {
    // Leaving clears retained hover below. Re-arm the unchanged spatial key
    // only on re-entry. Knife re-arms its owner key when its retained hover is
    // cleared on leave, so it must not receive a second invalidation here.
    tool_hover->invalidate();
  }

  auto clear_hover = [&]() {
    const QuaderApiVoidResult clear_hover_result =
        editor.selection().clear_hover();
    if (!clear_hover_result.ok) {
      LOG_ERROR("Viewport hover clear failed: {}",
                clear_hover_result.message.empty()
                    ? "Hover clear failed."
                    : clear_hover_result.message);
    }
    hover_clear_issued_ = true;
  };

  auto suppress_hover_after_selection =
      [&](QVec2 local_mouse, QuaderHoverTarget hovered_before_selection) {
        if (hover_target_valid(hovered_before_selection)) {
          hover_suppressed_target_ = hovered_before_selection;
        } else {
          const QuaderHoverSummary hover = editor.selection().preview_hover(
              make_hover_request(frame.camera, frame.settings, local_mouse,
                                 frame.input.modifiers));
          if (hover_target_valid(hover.target)) {
            hover_suppressed_target_ = hover.target;
          } else {
            hover_suppressed_target_.reset();
          }
        }
        clear_hover();
      };

  auto finish_pending_pick_feedback = [&]() {
    if (!pending_pick_feedback_.has_value()) {
      return;
    }
    const PendingPickFeedback feedback = *pending_pick_feedback_;
    pending_pick_feedback_.reset();
    suppress_hover_after_selection(feedback.local,
                                   feedback.hovered_before_selection);
    refresh_hover_work_after_selection(feedback.local, false);
  };
  finish_pending_pick_feedback();

  if (before_tool.selection_click_requested && can_pick &&
      (!before_tool.selection_cycle_depth || can_cycle_depth)) {
    QDR_PROFILE_SCOPE("Viewport selection deferred gizmo click");
    const QuaderHoverTarget hovered_before_selection =
        editor.selection().hover_summary().target;
    const std::optional<QuaderSelectionSummary> summary = commit_pick_selection(
        editor, frame, before_tool.selection_click_local,
        std::max(1, before_tool.selection_click_count),
        true, !before_tool.selection_cycle_depth,
        !before_tool.selection_cycle_depth, before_tool.selection_cycle_depth,
        before_tool.selection_cycle_depth_modifiers, hovered_before_selection);
    if (summary.has_value()) {
      paint_selection_active_ =
          is_component_selection_mode(to_modeling_selection_mode(summary->mode));
      enter_picked_instance(editor, frame, *summary,
                            before_tool.selection_click_count,
                            before_tool.selection_cycle_depth);
      suppress_hover_after_selection(before_tool.selection_click_local,
                                     hovered_before_selection);
      refresh_hover_work_after_selection(before_tool.selection_click_local,
                                         false);
    }
  }

  // A rebound keyboard gesture has no mouse press/release to carry its intent.
  // Mouse double-clicks use the normal click-versus-drag lifecycle below.
  if (frame.input.cycle_selection_depth_pressed && !frame.left_pressed &&
      !frame.left_down && !box_state_.active &&
      !before_tool.selection_click_requested && can_cycle_depth) {
    const QuaderHoverTarget hovered_before_selection =
        editor.selection().hover_summary().target;
    (void)commit_pick_selection(
        editor, frame, frame.input.mouse_local, 1, true, false, false, true,
        frame.input.cycle_selection_depth_modifiers);
    suppress_hover_after_selection(frame.input.mouse_local,
                                   hovered_before_selection);
    refresh_hover_work_after_selection(frame.input.mouse_local, false);
  }

  if (kFacePlaneCutGestureEnabled && !box_state_.active && can_pick &&
      frame.input.modifiers.alt &&
      !frame.input.modifiers.primary && !frame.input.modifiers.shift &&
      frame.right_pressed) {
    QDR_PROFILE_SCOPE("Viewport face plane cut");
    const QuaderApiResult<QuaderHoverSummary> hover = editor.selection().hover(
        make_hover_request(frame.camera, frame.settings,
                           frame.right_click_local, frame.input.modifiers));
    if (!hover.ok) {
      LOG_ERROR("Viewport face-plane hover failed: {}",
                hover.message.empty() ? "Hover failed." : hover.message);
    } else if (hover.value().target.kind == QuaderActiveSelectionKind::Face) {
      const QuaderApiVoidResult cut =
          editor.modeling().cut_selected_meshes_by_hovered_face_plane();
      if (!cut.ok) {
        LOG_ERROR("Viewport face-plane cut failed: {}",
                  cut.message.empty() ? "Face-plane cut failed."
                                      : cut.message);
      }
    }
    hover_clear_issued_ = false;
    hover_suppressed_target_.reset();
    return;
  }

  const bool curve_control_gesture_available =
      curve_controls_editable &&
      (curve->active() || can_use_selection_fallback) &&
      !frame.input.pointer_interaction_canceled &&
      !frame.input_result.cancel_requested &&
      !frame.input_result.navigation_active &&
      !frame.input_result.camera_changed && !frame.input_result.view_changed &&
      !frame.input.text_input_active && !frame.input.modifiers.alt &&
      frame.input.curve_conversion_edges.empty() && !curve->dragging() &&
      !before_tool.gizmo_consumed;
  if (!box_state_.active &&
      (can_use_selection_fallback || curve_control_gesture_available) &&
      frame.left_pressed && !before_tool.selection_click_requested) {
    QDR_PROFILE_SCOPE("Viewport selection begin box");
    box_edit_ = quader_editor_core::selection_edit_operation_from_modifiers(
        frame.input.modifiers.primary, frame.input.modifiers.shift);
    box_cycle_depth_ = frame.input.cycle_selection_depth_pressed;
    box_cycle_modifiers_ = frame.input.cycle_selection_depth_modifiers;
    if (box_cycle_depth_ && !can_cycle_depth) {
      box_cycle_depth_ = false;
      return;
    }
    box_hovered_before_selection_ = editor.selection().hover_summary().target;
    box_baseline_ = !box_cycle_depth_ && depth_click_baseline_.has_value()
                        ? depth_click_baseline_->selection
                        : editor.selection().capture_box_selection_baseline();
    if (curve_control_gesture_available) {
      curve_box_baseline_ = curve->control_selection();
      curve_control_box_active_ = true;
    }
    quader_editor_core::begin_selection_box(box_state_, frame.left_click_local,
                                            box_edit_);
  }

  if (box_state_.active && frame.left_down) {
    QDR_PROFILE_SCOPE("Viewport selection update box");
    quader_editor_core::update_selection_box(box_state_,
                                             frame.input.mouse_local);
    if (box_state_.dragging) {
      // Mouse motion only presents the marquee. Resolving and publishing a
      // selection here would synchronously rebuild SDK, scene, UI, and overlay
      // state for every changing hit set; the final rectangle commits below.
      if (!hover_clear_issued_) {
        clear_hover();
      }
    }
  }

  if (box_state_.active && (frame.left_released || !frame.left_down)) {
    QDR_PROFILE_SCOPE("Viewport selection finish box");
    const quader_editor_core::SelectionBoxCommit commit =
        quader_editor_core::finish_selection_box(
            box_state_, frame.input.mouse_local, box_edit_);
    const QuaderHoverTarget hovered_before_selection =
        box_hovered_before_selection_.value_or(
            editor.selection().hover_summary().target);
    bool curve_control_consumed = false;
    if (curve_control_box_active_ && curve != nullptr) {
      const CurveControlSelection curve_baseline =
          curve_box_baseline_.value_or(curve->control_selection());
      std::vector<CurveControlTarget> curve_hits;
      std::optional<CurveControlTarget> direct_curve_hit;
      if (commit.dragged) {
        curve_hits = box_pick_viewport_curve_controls(
            frame.camera, curve->preview_state(), commit.start, commit.end);
        curve_control_consumed = true;
      } else {
        const auto control_hit = pick_unoccluded_viewport_curve_control(
            frame.camera, curve->preview_state(), commit.start,
            frame.scene_mesh_surface_distance);
        if (control_hit.has_value()) {
          direct_curve_hit = control_hit->target;
          curve_hits.push_back(*direct_curve_hit);
          curve_control_consumed = true;
        }
      }
      (void)curve->edit_control_selection(
          std::span<const CurveControlTarget>{curve_hits.data(),
                                              curve_hits.size()},
          box_edit_, &curve_baseline, direct_curve_hit);
      (void)curve->set_hover(std::nullopt);
      if (curve_control_consumed) {
        cancel_pending_click();
        if (!hover_clear_issued_) {
          clear_hover();
        }
      }
    }
    curve_box_baseline_.reset();
    curve_control_box_active_ = false;
    if (commit.dragged && curve_control_consumed) {
      committed_marquee_ = box_marquee(commit.start, commit.end);
    } else if (commit.dragged) {
      cancel_pending_click();
      committed_marquee_ = box_marquee(commit.start, commit.end);
      (void)box_selection(editor, frame.input_router, frame, commit.start,
                          commit.end, box_edit_,
                          box_baseline_ ? &*box_baseline_ : nullptr);
      suppress_hover_after_selection(commit.end, hovered_before_selection);
      refresh_hover_work_after_selection(commit.end, false);
    } else if (!curve_control_consumed && can_pick) {
      const std::optional<QuaderSelectionSummary> summary = commit_pick_selection(
          editor, frame, frame.left_click_local,
          std::max(1, frame.left_click_count), box_cycle_depth_, false, false,
          box_cycle_depth_, box_cycle_modifiers_, hovered_before_selection);
      if (summary.has_value()) {
        paint_selection_active_ = is_component_selection_mode(
            to_modeling_selection_mode(summary->mode));
        enter_picked_instance(editor, frame, *summary, frame.left_click_count,
                              box_cycle_depth_);
        suppress_hover_after_selection(frame.left_click_local,
                                       hovered_before_selection);
        refresh_hover_work_after_selection(frame.left_click_local, false);
      }
    }
    box_edit_ = quader_editor_core::SelectionEditOperation::Replace;
    box_cycle_depth_ = false;
    box_cycle_modifiers_ = {};
    box_hovered_before_selection_.reset();
    box_baseline_.reset();
  }

  if (pending_first_pick_.has_value() && !frame.left_down) {
    if (!can_pick || !pending_click_matches_context(editor, frame)) {
      cancel_pending_click();
    } else if (frame.monotonic_time_seconds >=
               depth_click_baseline_->deadline_seconds) {
      (void)commit_pending_pick(editor, frame);
      finish_pending_pick_feedback();
    }
  }

  if (frame.left_released || !frame.left_down) {
    if (box_state_.active) {
      clear_curve_control_box();
    }
    paint_selection_active_ = false;
    if (paint_selection_was_active && frame.input.viewport_hovered &&
        tool_hover != nullptr) {
      // The held-paint key was consumed after its last mutation. Re-arm the
      // release frame so suppression policy, including the primary-modifier
      // hover exception, runs once before the pointer becomes stationary.
      tool_hover->invalidate();
    }
  }

  if (!box_state_.active && can_pick && paint_selection_active_ &&
      frame.left_down && claim_hover_work()) {
    QDR_PROFILE_SCOPE("Viewport paint selection");
    ++work_stats_.paint_pick_count;
    const QuaderHoverTarget hovered_before_selection =
        editor.selection().hover_summary().target;
    const QuaderSelectionSummary summary = pick_selection(
        editor, frame.input_router, frame, frame.input.mouse_local, 0);
    paint_selection_active_ =
        is_component_selection_mode(to_modeling_selection_mode(summary.mode));
    suppress_hover_after_selection(frame.input.mouse_local,
                                   hovered_before_selection);
    refresh_hover_work_after_selection(frame.input.mouse_local, true);
  }

  const bool box_dragging = box_state_.active && box_state_.dragging;
  const bool group_selection_mode =
      frame.input_router.state().selection_mode == ViewportSelectionMode::Group;
  if (group_selection_mode && !box_dragging && !frame.left_down) {
    hover_suppressed_target_.reset();
    if (!hover_clear_issued_) {
      clear_hover();
    }
    return;
  }

  const bool can_hover = selection_fallback_active &&
                         frame.input.viewport_hovered && !suppress_hover_work &&
                         !box_dragging && !frame.left_down &&
                         !tool_result.handled;
  if (can_hover && !claim_hover_work()) {
    return;
  }

  bool hover_suppressed_this_frame = false;
  if (can_hover && hover_suppressed_target_.has_value()) {
    const QuaderHoverSummary hover = editor.selection().preview_hover(
        make_hover_request(frame.camera, frame.settings,
                           frame.input.mouse_local, frame.input.modifiers));
    if (hover_target_valid(hover.target) &&
        hover_targets_match(*hover_suppressed_target_, hover.target)) {
      if (!frame.input.modifiers.primary) {
        clear_hover();
        hover_suppressed_this_frame = true;
      }
    } else {
      hover_suppressed_target_.reset();
    }
  }

  if (can_hover && !hover_suppressed_this_frame) {
    QDR_PROFILE_SCOPE("Viewport hover selection");
    ++work_stats_.hover_pick_count;
    const QuaderApiResult<QuaderHoverSummary> hover = editor.selection().hover(
        make_hover_request(frame.camera, frame.settings,
                           frame.input.mouse_local, frame.input.modifiers));
    if (!hover.ok) {
      LOG_ERROR("Viewport hover failed: {}",
                hover.message.empty() ? "Hover failed." : hover.message);
    }
    hover_clear_issued_ = false;
    return;
  }

  const bool knife_authored_hover_this_frame =
      tool_result.handled &&
      frame.input_router.state().active_tool == ViewportToolId::Knife &&
      frame.input.viewport_hovered && !frame.input.text_input_active &&
      !frame.input_result.navigation_active &&
      !frame.input_result.camera_changed && !gizmo_consumed;
  if (knife_authored_hover_this_frame) {
    // Knife owns component hover while its point resolver is eligible to run.
    // Re-arm clearing so a later suppressed frame or pointer leave cannot retain
    // the last Knife target.
    hover_clear_issued_ = false;
    return;
  }

  const bool should_clear_hover =
      !frame.input.viewport_hovered || !selection_fallback_active ||
      frame.input.text_input_active || frame.input.modal_tool_active ||
      frame.input.active_tool_owns_viewport || tool_result.handled;
  if (!hover_clear_issued_ && should_clear_hover) {
    hover_suppressed_target_.reset();
    if (tool_hover != nullptr &&
        frame.input_router.state().active_tool == ViewportToolId::Knife) {
      const ModelingCommandResult clear_knife = tool_hover->clear_knife_hover();
      if (!clear_knife.ok) {
        LOG_ERROR("Knife hover clear failed: {}",
                  clear_knife.message.empty() ? "Knife hover clear failed."
                                              : clear_knife.message);
      } else if (clear_knife.changed) {
        tool_hover->invalidate();
      }
    }
    clear_hover();
  }
}

QuaderSelectionSummary ViewportSelectionInteraction::pick_context_menu_target(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  cancel_pending_click();
  const QuaderSelectionSummary summary =
      pick_selection(editor, frame.input_router, frame, frame.input.mouse_local,
                     1, true, true, true);
  paint_selection_active_ =
      is_component_selection_mode(to_modeling_selection_mode(summary.mode));
  hover_clear_issued_ = false;
  hover_suppressed_target_.reset();
  return summary;
}

} // namespace quader
