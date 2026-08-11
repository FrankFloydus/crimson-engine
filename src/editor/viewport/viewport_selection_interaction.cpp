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
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/selection/box_selection_rules.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>
#include <limits>
#include <optional>

namespace quader {
namespace {

quader_editor_core::SelectionEditOperation
selection_box_operation(QuaderSelectionEdit edit) {
  switch (edit) {
  case QuaderSelectionEdit::Add:
    return quader_editor_core::SelectionEditOperation::Add;
  case QuaderSelectionEdit::Remove:
    return quader_editor_core::SelectionEditOperation::Remove;
  case QuaderSelectionEdit::Toggle:
    return quader_editor_core::SelectionEditOperation::Toggle;
  case QuaderSelectionEdit::Replace:
    return quader_editor_core::SelectionEditOperation::Replace;
  }
  return quader_editor_core::SelectionEditOperation::Replace;
}

QuaderSelectionEdit selection_edit_from_modifiers(ViewportModifiers modifiers) {
  if (modifiers.primary) {
    return QuaderSelectionEdit::Remove;
  }
  if (modifiers.shift) {
    return QuaderSelectionEdit::Add;
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
    bool preserve_existing_component_selection_on_hit = false) {
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
      .face_plane_hover = modifiers.alt,
      .vertex_radius =
          settings.float_value(ViewportFloatSettingId::PickVertexRadius),
      .edge_radius =
          settings.float_value(ViewportFloatSettingId::PickEdgeRadius),
  };
}

QuaderBoxSelectRequest
make_box_select_request(const ViewportCameraController &camera,
                        QVec2 viewport_size, QVec2 start, QVec2 end,
                        QuaderSelectionEdit edit) {
  return {
      .camera = to_api_viewport_camera(
          ViewportCameraAdapter::modeling_camera(camera, viewport_size)),
      .start = start,
      .end = end,
      .edit = edit,
      .rule = QuaderBoxSelectionRule::DragDirection,
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
                                      const QuaderSelectionSummary &summary) {
  return frame.left_click_count >= 2 &&
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
    const ViewportInteractionFrame &frame, EntityId entity,
    bool preserve_selection_on_selected_hit) {
  QDR_PROFILE_FUNCTION();
  const QuaderSelectionEdit edit =
      selection_edit_from_modifiers(frame.input.modifiers);
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

QuaderSelectionSummary
pick_selection(QuaderEditorApi &editor, ViewportInputRouter &input_router,
               const ViewportInteractionFrame &frame, QVec2 local_mouse,
               int click_count, bool preserve_selection_on_miss = false,
               bool preserve_selection_on_selected_hit = false,
               bool preserve_existing_component_selection_on_hit = false) {
  QDR_PROFILE_FUNCTION();
  if (const std::optional<EntityId> billboard_entity =
          pick_scene_billboard_entity(frame, local_mouse)) {
    return select_billboard_entity(editor, input_router, frame,
                                   *billboard_entity,
                                   preserve_selection_on_selected_hit);
  }
  const QuaderApiResult<QuaderSelectionSummary> result =
      editor.selection().pick_select(make_pick_select_request(
          frame.camera, frame.settings, local_mouse, frame.input.modifiers,
          click_count, preserve_selection_on_miss,
          preserve_selection_on_selected_hit,
          preserve_existing_component_selection_on_hit));
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

QuaderSelectionSummary
box_selection(QuaderEditorApi &editor, ViewportInputRouter &input_router,
              const ViewportInteractionFrame &frame, QVec2 start, QVec2 end,
              QuaderSelectionEdit edit,
              const QuaderSelectionBoxBaseline *baseline = nullptr) {
  QDR_PROFILE_FUNCTION();
  const QuaderBoxSelectRequest request = make_box_select_request(
      frame.camera, frame.input.viewport_size, start, end, edit);
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
  box_edit_ = QuaderSelectionEdit::Replace;
  box_hovered_before_selection_.reset();
  box_baseline_.reset();
  committed_marquee_.reset();
  hover_suppressed_target_.reset();
  paint_selection_active_ = false;
  viewport_hover_state_initialized_ = false;
  viewport_hovered_last_frame_ = false;
}

ViewportSelectionMarquee ViewportSelectionInteraction::marquee() const {
  if (!box_state_.active || !box_state_.dragging) {
    return committed_marquee_.value_or(ViewportSelectionMarquee{});
  }
  return box_marquee(box_state_.start, box_state_.end);
}

void ViewportSelectionInteraction::handle_frame(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool,
    ViewportToolHoverPort *tool_hover) {
  QDR_PROFILE_FUNCTION();
  committed_marquee_.reset();
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

  if (before_tool.selection_click_requested && can_pick) {
    QDR_PROFILE_SCOPE("Viewport selection deferred gizmo click");
    const QuaderHoverTarget hovered_before_selection =
        editor.selection().hover_summary().target;
    const QuaderSelectionSummary summary = pick_selection(
        editor, frame.input_router, frame, before_tool.selection_click_local,
        std::max(1, before_tool.selection_click_count),
        true, true, true);
    paint_selection_active_ =
        is_component_selection_mode(to_modeling_selection_mode(summary.mode));
    suppress_hover_after_selection(before_tool.selection_click_local,
                                   hovered_before_selection);
    refresh_hover_work_after_selection(before_tool.selection_click_local,
                                       false);
  }

  if (!box_state_.active && can_pick && frame.input.modifiers.alt &&
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

  if (!box_state_.active && can_use_selection_fallback && frame.left_pressed) {
    QDR_PROFILE_SCOPE("Viewport selection begin box");
    box_edit_ = selection_edit_from_modifiers(frame.input.modifiers);
    box_hovered_before_selection_ = editor.selection().hover_summary().target;
    box_baseline_ = editor.selection().capture_box_selection_baseline();
    quader_editor_core::begin_selection_box(box_state_, frame.left_click_local,
                                            selection_box_operation(box_edit_));
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
            box_state_, frame.input.mouse_local,
            selection_box_operation(box_edit_));
    const QuaderHoverTarget hovered_before_selection =
        box_hovered_before_selection_.value_or(
            editor.selection().hover_summary().target);
    if (commit.dragged) {
      committed_marquee_ = box_marquee(commit.start, commit.end);
      (void)box_selection(editor, frame.input_router, frame, commit.start,
                          commit.end, box_edit_,
                          box_baseline_ ? &*box_baseline_ : nullptr);
      suppress_hover_after_selection(commit.end, hovered_before_selection);
      refresh_hover_work_after_selection(commit.end, false);
    } else if (can_pick) {
      const QuaderSelectionSummary summary = pick_selection(
          editor, frame.input_router, frame, frame.left_click_local,
          std::max(1, frame.left_click_count));
      paint_selection_active_ =
          is_component_selection_mode(to_modeling_selection_mode(summary.mode));
      if (plain_instance_edit_double_click(frame, summary)) {
        if (const std::optional<EntityId> occurrence =
                active_selected_instance(editor)) {
          const QuaderApiVoidResult entered =
              editor.modeling().enter_instance_edit(*occurrence);
          if (!entered.ok()) {
            LOG_ERROR("Instance edit entry failed: {}",
                      entered.error_message().empty()
                          ? "Instance edit entry failed."
                          : entered.error_message());
          }
        }
      }
      suppress_hover_after_selection(frame.left_click_local,
                                     hovered_before_selection);
      refresh_hover_work_after_selection(frame.left_click_local, false);
    }
    box_edit_ = QuaderSelectionEdit::Replace;
    box_hovered_before_selection_.reset();
    box_baseline_.reset();
  }

  if (frame.left_released || !frame.left_down) {
    if (box_state_.active) {
      quader_editor_core::clear_selection_box(box_state_);
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
