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
#include "editor/interactions/selection_edit.hpp"
#include "editor/interactions/selection_lasso.hpp"
#include "editor/interactions/transform_gizmo.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/selection/box_selection_rules.hpp"
#include "editor/selection/quader_selection_api_runtime_access.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "editor/viewport/viewport_scene_hit_policy.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_pixel_scale.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace quader {
namespace {

// The plane-cut API and implementation remain available; its viewport gesture
// is intentionally dormant while Alt+LMB continues the selected face texture.
constexpr bool kFacePlaneCutGestureEnabled = false;

ModelingSelectionEdit selection_edit_to_modeling(
    quader_editor_core::SelectionEditOperation operation) {
  switch (operation) {
  case quader_editor_core::SelectionEditOperation::Add:
    return ModelingSelectionEdit::Add;
  case quader_editor_core::SelectionEditOperation::Remove:
    return ModelingSelectionEdit::Remove;
  case quader_editor_core::SelectionEditOperation::Toggle:
    return ModelingSelectionEdit::Toggle;
  case quader_editor_core::SelectionEditOperation::Replace:
    return ModelingSelectionEdit::Replace;
  }
  return ModelingSelectionEdit::Replace;
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

ModelingPickSelectRequest make_pick_select_request(
    const ViewportInteractionFrame &frame, QVec2 local_mouse,
    ViewportModifiers modifiers, int click_count,
    bool preserve_selection_on_miss, bool preserve_selection_on_selected_hit,
    bool preserve_existing_component_selection_on_hit = false,
    bool cycle_depth = false) {
  const ViewportCameraController &camera = frame.camera;
  const ViewportSettings &settings = frame.settings;
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
      .vertex_radius = viewport_scaled_world_pick_radius(
          settings.float_value(ViewportFloatSettingId::PickVertexRadius),
          frame.viewport_pixel_scale, frame.input.device_pixel_ratio),
      .edge_radius = viewport_scaled_world_pick_radius(
          settings.float_value(ViewportFloatSettingId::PickEdgeRadius),
          frame.viewport_pixel_scale, frame.input.device_pixel_ratio),
      .select_backfaces =
          settings.bool_value(ViewportBoolSettingId::BackfaceSelection),
      .cycle_depth = cycle_depth,
      .cycle_origin_tolerance =
          camera.state().projection == ViewportProjection::Orthographic
              ? static_cast<double>(frame.pointer_drag_threshold_pixels) *
                    quader_editor_core::viewport_world_units_per_pixel(
                        ViewportCameraAdapter::runtime_camera(
                            camera, camera.viewport_size()),
                        world_ray.origin)
              : 0.0,
  };
}

QuaderHoverRequest make_hover_request(const ViewportInteractionFrame &frame,
                                      QVec2 local_mouse,
                                      ViewportModifiers modifiers) {
  const ViewportCameraController &camera = frame.camera;
  const ViewportSettings &settings = frame.settings;
  const ViewportRay world_ray = camera.ray_from_viewport_point(local_mouse);
  return {
      .ray =
          {
              .origin = world_ray.origin,
              .direction = world_ray.direction,
      },
      .selected_modifier_pressed = modifiers.primary,
      .face_plane_hover = kFacePlaneCutGestureEnabled && modifiers.alt,
      .vertex_radius = viewport_scaled_world_pick_radius(
          settings.float_value(ViewportFloatSettingId::PickVertexRadius),
          frame.viewport_pixel_scale, frame.input.device_pixel_ratio),
      .edge_radius = viewport_scaled_world_pick_radius(
          settings.float_value(ViewportFloatSettingId::PickEdgeRadius),
          frame.viewport_pixel_scale, frame.input.device_pixel_ratio),
      .select_backfaces =
          settings.bool_value(ViewportBoolSettingId::BackfaceSelection),
  };
}

const ViewportPointerButtonEdge *lasso_release_edge(
    const ViewportInputFrame &input, const ViewportPointerButtonEdge &press) {
  const ViewportPointerButtonEdge *release = nullptr;
  for (const auto &edge : input.pointer_released_edges) {
    if (edge.button != press.button ||
        (press.sequence != 0u && edge.sequence != 0u &&
         edge.sequence <= press.sequence)) {
      continue;
    }
    const bool followed_new_press = std::any_of(
        input.pointer_pressed_edges.begin(), input.pointer_pressed_edges.end(),
        [&](const auto &new_press) {
          return new_press.button == press.button &&
                 new_press.sequence > press.sequence &&
                 new_press.sequence < edge.sequence;
        });
    if (!followed_new_press &&
        (release == nullptr || edge.sequence < release->sequence)) {
      release = &edge;
    }
  }
  return release;
}

ModelingBoxSelectRequest
make_box_select_request(const ViewportCameraController &camera,
                        const ViewportSettings &settings,
                        QVec2 viewport_size, QVec2 start, QVec2 end,
                        quader_editor_core::SelectionEditOperation edit) {
  return {
      .camera = ViewportCameraAdapter::modeling_camera(camera, viewport_size),
      .start = start,
      .end = end,
      .edit = selection_edit_to_modeling(edit),
      .rule = ModelingBoxSelectionRule::DragDirection,
      .select_backfaces =
          settings.bool_value(ViewportBoolSettingId::BackfaceSelection),
  };
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
         (!frame.input.active_transform_drag ||
          frame.input_router.state().active_tool == ViewportToolId::LassoSelect) &&
         !frame.input.modal_tool_active &&
         !frame.input.active_tool_owns_viewport &&
         !frame.input.keybinding_input_consumed &&
         !frame.input_result.consumed &&
         !frame.input_result.navigation_active &&
         !frame.input_result.camera_changed;
}

EditorSelectionService &selection_owner(QuaderEditorApi &editor) {
  return QuaderSelectionApiRuntimeAccess::service(editor.selection());
}

const EditorSelectionService &selection_owner(const QuaderEditorApi &editor) {
  return QuaderSelectionApiRuntimeAccess::service(editor.selection());
}

std::vector<ViewportSceneHitCandidate> collect_selection_point_hits(
    const EditorSelectionService &selection,
    const ViewportInteractionFrame &frame, QVec2 local_mouse,
    const ModelingPickSelectRequest &request,
    const ViewportCurveInteractionPort *curve = nullptr,
    SceneSurfaceQueryScratch *surface_scratch = nullptr,
    bool include_occluded = false,
    bool collect_all_mesh_surfaces = false,
    bool include_component_body_surfaces = false) {
  const ViewportSelectionMode mode =
      frame.input_router.state().selection_mode;
  const bool scene_mode = mode == ViewportSelectionMode::Object ||
                          mode == ViewportSelectionMode::Group;
  std::vector<ResolvedSelectionHit> modeling_hits =
      include_occluded ? selection.query_modeling_depth_hits(request)
                       : std::vector<ResolvedSelectionHit>{};
  if (modeling_hits.empty()) {
    if (const std::optional<ResolvedSelectionHit> modeling_hit =
            selection.query_modeling_hit(request)) {
      modeling_hits.push_back(*modeling_hit);
    }
  }

  const CurveToolPreviewState *curve_controls =
      curve != nullptr && curve->controls_editable()
          ? &curve->preview_state()
          : nullptr;
  const double viewport_pixel_scale = static_cast<double>(
      viewport_sanitized_pixel_scale(frame.viewport_pixel_scale));
  return collect_viewport_scene_point_hits({
      .camera = frame.camera,
      .local_mouse = local_mouse,
      .billboards =
          scene_mode ? frame.scene_billboard_pick_candidates
                     : std::span<const ViewportSceneBillboardPickCandidate>{},
      .curves =
          scene_mode ? frame.scene_curve_pick_candidates
                     : std::span<const ViewportSceneCurvePickCandidate>{},
      .scene_surfaces = frame.scene_surfaces,
      .curve_controls = curve_controls,
      .resolved_selection_hits = modeling_hits,
      .include_mesh_surfaces =
          scene_mode || include_component_body_surfaces,
      .select_backfaces = request.select_backfaces,
      .include_curve_controls = curve_controls != nullptr,
      .include_occluded = include_occluded,
      .collect_all_mesh_surfaces = collect_all_mesh_surfaces,
      .curve_radius_pixels = 8.0 * viewport_pixel_scale,
      .curve_control_radius_pixels = 9.0 * viewport_pixel_scale,
      .surface_scratch = surface_scratch,
  });
}

struct ResolvedSelectionPointPick final {
  std::optional<ResolvedSelectionTarget> winner;
  std::optional<ResolvedComponentBodyTarget> component_body_target;
};

ResolvedSelectionPointPick resolve_selection_point_pick(
    const EditorSelectionService &selection,
    const ViewportInteractionFrame &frame, QVec2 local_mouse,
    const ModelingPickSelectRequest &request,
    const ViewportCurveInteractionPort *curve = nullptr,
    SceneSurfaceQueryScratch *surface_scratch = nullptr,
    bool include_component_body = false) {
  const ViewportSelectionMode mode =
      frame.input_router.state().selection_mode;
  const bool scene_mode = mode == ViewportSelectionMode::Object ||
                          mode == ViewportSelectionMode::Group;
  const bool component_body_enabled =
      include_component_body && !scene_mode && request.click_count > 0;
  const std::vector<ViewportSceneHitCandidate> hits =
      collect_selection_point_hits(selection, frame, local_mouse, request,
                                   curve, surface_scratch, false, false,
                                   component_body_enabled);
  if (!component_body_enabled) {
    const std::optional<ViewportSceneHitCandidate> winner =
        resolve_viewport_scene_hit(hits);
    return {
        .winner = winner.has_value() ? resolved_selection_target(*winner)
                                     : std::nullopt,
    };
  }

  std::vector<ViewportSceneHitCandidate> component_hits;
  component_hits.reserve(hits.size());
  std::optional<ViewportSceneHitCandidate> body_hit;
  for (const ViewportSceneHitCandidate &hit : hits) {
    const bool mesh_body = hit.kind == ViewportSceneHitKind::MeshSurface ||
                           hit.kind == ViewportSceneHitKind::FormalInstance;
    if (!mesh_body) {
      component_hits.push_back(hit);
      continue;
    }
    if (!body_hit.has_value() ||
        viewport_scene_hit_precedes(hit, *body_hit)) {
      body_hit = hit;
    }
  }

  const std::optional<ViewportSceneHitCandidate> component_winner =
      resolve_viewport_scene_hit(component_hits);
  ResolvedSelectionPointPick resolved{
      .winner = component_winner.has_value()
                    ? resolved_selection_target(*component_winner)
                    : std::nullopt,
  };
  if (!resolved.winner.has_value() && body_hit.has_value()) {
    if (const auto *mesh =
            std::get_if<ViewportSceneMeshHit>(&body_hit->source);
        mesh != nullptr &&
        mesh->participant.geometry_entity != kInvalidEntityId) {
      resolved.component_body_target = ResolvedComponentBodyTarget{
          .geometry_entity = mesh->participant.geometry_entity,
          .source_occurrence = mesh->participant.display_entity,
      };
    }
  }
  return resolved;
}

std::optional<ResolvedSelectionTarget> resolve_selection_point_target(
    const EditorSelectionService &selection,
    const ViewportInteractionFrame &frame, QVec2 local_mouse,
    const ModelingPickSelectRequest &request,
    const ViewportCurveInteractionPort *curve = nullptr,
    SceneSurfaceQueryScratch *surface_scratch = nullptr) {
  return resolve_selection_point_pick(selection, frame, local_mouse, request,
                                      curve, surface_scratch)
      .winner;
}

QuaderSelectionSummary synchronize_selection_summary(
    ViewportInputRouter &input_router,
    const ModelingSelectionSummary &selected) {
  const QuaderSelectionSummary summary = to_api_selection_summary(selected);
  input_router.set_selection_mode(
      app_selection_mode_to_viewport(to_modeling_selection_mode(summary.mode)));
  return summary;
}

ViewportSelectionApplyResult checked_selection_apply_result(
    ViewportInputRouter &input_router, ResolvedSelectionApplyResult applied,
    std::string_view label) {
  if (!applied.command.ok) {
    LOG_ERROR("{} failed: {}", label,
              applied.command.message.empty()
                  ? "Selection owner rejected the resolved target."
                  : applied.command.message);
  }
  return {
      .command = std::move(applied.command),
      .selection =
          synchronize_selection_summary(input_router, applied.selection),
  };
}

ViewportSelectionApplyResult
pick_selection(QuaderEditorApi &editor, ViewportInputRouter &input_router,
               const ViewportInteractionFrame &frame, QVec2 local_mouse,
               int click_count, bool preserve_selection_on_miss = false,
               bool preserve_selection_on_selected_hit = false,
               bool preserve_existing_component_selection_on_hit = false,
               ViewportModifiers click_modifiers = {},
               bool cycle_depth = false,
               ViewportModifiers cycle_modifiers = {},
               const EditorSelectionBoxBaseline *baseline = nullptr,
               ViewportCurveInteractionPort *curve = nullptr,
               SceneSurfaceQueryScratch *surface_scratch = nullptr,
               bool curve_controls_only = false) {
  QDR_PROFILE_FUNCTION();
  const ModelingPickSelectRequest request = make_pick_select_request(
          frame, local_mouse,
          cycle_depth ? cycle_modifiers : click_modifiers,
          cycle_depth ? 1 : click_count, preserve_selection_on_miss,
          preserve_selection_on_selected_hit,
          preserve_existing_component_selection_on_hit, cycle_depth);
  EditorSelectionService &selection = selection_owner(editor);
  if (cycle_depth) {
    const std::optional<ResolvedSelectionTarget> ordinary =
        resolve_selection_point_target(selection, frame, local_mouse, request,
                                       curve, surface_scratch);
    const std::vector<ViewportSceneHitCandidate> raw_depth_hits =
        collect_selection_point_hits(selection, frame, local_mouse, request,
                                     curve, surface_scratch, true, true);
    const std::vector<ViewportSceneHitCandidate> depth_hits =
        resolve_viewport_scene_marquee_hits(raw_depth_hits);
    std::vector<ResolvedSelectionTarget> depth_targets;
    depth_targets.reserve(depth_hits.size());
    for (const ViewportSceneHitCandidate &hit : depth_hits) {
      if (std::optional<ResolvedSelectionTarget> target =
              resolved_selection_target(hit)) {
        depth_targets.push_back(std::move(*target));
      }
    }
    const std::optional<ResolvedSelectionTarget> winner =
        selection.resolve_depth_arbitrated_target(depth_targets, ordinary,
                                                  request);
    ModelingPickSelectRequest cycle_policy = request;
    // An empty depth stack is always a no-op; it must never inherit ordinary
    // replace-on-miss behavior.
    cycle_policy.preserve_selection_on_miss = true;
    return checked_selection_apply_result(
        input_router,
        selection.apply_resolved_click({
            .winner = winner,
            .policy = cycle_policy,
            .baseline = baseline != nullptr
                            ? std::optional<EditorSelectionBoxBaseline>(*baseline)
                            : std::nullopt,
        }),
        "Viewport resolved depth selection");
  }
  ResolvedSelectionPointPick resolved = resolve_selection_point_pick(
      selection, frame, local_mouse, request, curve, surface_scratch, true);
  if (curve_controls_only &&
      (!resolved.winner.has_value() ||
       resolved.winner->domain !=
           ResolvedSelectionDomain::RegisteredSubElement ||
       resolved.winner->sub_selection_kind !=
           EditorSubSelectionKind::CurveControl)) {
    resolved.winner.reset();
    resolved.component_body_target.reset();
  }
  return checked_selection_apply_result(
      input_router,
      selection.apply_resolved_click({
          .winner = std::move(resolved.winner),
          .component_body_target =
              std::move(resolved.component_body_target),
          .policy = request,
          .baseline = baseline != nullptr
                          ? std::optional<EditorSelectionBoxBaseline>(*baseline)
                          : std::nullopt,
          .miss_sub_selection_kind =
              curve_controls_only ? EditorSubSelectionKind::CurveControl
                                  : EditorSubSelectionKind::None,
      }),
      "Viewport resolved selection");
}

ViewportSelectionApplyResult
box_selection(QuaderEditorApi &editor, ViewportInputRouter &input_router,
               const ViewportInteractionFrame &frame, QVec2 start, QVec2 end,
               quader_editor_core::SelectionEditOperation edit,
               const EditorSelectionBoxBaseline &baseline,
               ViewportCurveInteractionPort *curve = nullptr,
               bool include_curve_controls = false,
               EditorSubSelectionKind sub_selection_only =
                   EditorSubSelectionKind::None,
               SceneSurfaceQueryScratch *surface_scratch = nullptr) {
  QDR_PROFILE_FUNCTION();
  const ModelingBoxSelectRequest request = make_box_select_request(
      frame.camera, frame.settings, frame.input.viewport_size, start, end,
      edit);
  EditorSelectionService &selection = selection_owner(editor);
  const std::vector<ResolvedSelectionHit> modeling_hits =
      selection.query_modeling_marquee_hits(request);
  const bool scene_mode =
      baseline.mode == ModelingSelectionMode::Object ||
      baseline.mode == ModelingSelectionMode::Group;
  const CurveToolPreviewState *curve_controls =
      include_curve_controls && curve != nullptr && curve->controls_editable()
          ? &curve->preview_state()
          : nullptr;
  const std::vector<ViewportSceneHitCandidate> hits =
      collect_viewport_scene_marquee_hits({
          .camera = frame.camera,
          .start = start,
          .end = end,
          .rule = request.rule,
          .billboards =
              scene_mode
                  ? frame.scene_billboard_pick_candidates
                  : std::span<const ViewportSceneBillboardPickCandidate>{},
          .curves =
              scene_mode
                  ? frame.scene_curve_pick_candidates
                  : std::span<const ViewportSceneCurvePickCandidate>{},
          .scene_surfaces = frame.scene_surfaces,
          .curve_controls = curve_controls,
          .resolved_selection_hits = modeling_hits,
          .include_curve_controls = curve_controls != nullptr,
          .include_occluded_resolved_modeling_hits = request.select_backfaces,
          .surface_scratch = surface_scratch,
      });
  std::vector<ResolvedSelectionTarget> targets;
  targets.reserve(hits.size());
  for (const ViewportSceneHitCandidate &hit : hits) {
    if (std::optional<ResolvedSelectionTarget> target =
            resolved_selection_target(hit)) {
      if (sub_selection_only != EditorSubSelectionKind::None &&
          (target->domain != ResolvedSelectionDomain::RegisteredSubElement ||
           target->sub_selection_kind != sub_selection_only)) {
        continue;
      }
      targets.push_back(std::move(*target));
    }
  }
  return checked_selection_apply_result(
      input_router,
      selection.apply_resolved_marquee({
          .targets = std::move(targets),
          .edit = request.edit,
          .mode = baseline.mode,
          .rule = request.rule,
          .baseline = baseline,
      }),
      "Viewport resolved marquee selection");
}

ViewportSelectionApplyResult lasso_selection(
    QuaderEditorApi &editor, ViewportInputRouter &input_router,
    const ViewportInteractionFrame &frame, std::span<const QVec2> points,
    bool partial_intersection,
    quader_editor_core::SelectionEditOperation edit,
    const EditorSelectionBoxBaseline &baseline,
    ViewportCurveInteractionPort *curve, bool include_curve_controls,
    ViewportSceneLassoProjectionCache *projection_cache) {
  QDR_PROFILE_FUNCTION();
  EditorSelectionService &selection = selection_owner(editor);
  const quader_editor_core::SelectionLassoRegion region(points);
  if (!region.valid()) {
    return {.command = {.ok = true},
            .selection = synchronize_selection_summary(input_router,
                                                         selection.summary())};
  }
  const ModelingLassoSelectRequest request{
      .camera = ViewportCameraAdapter::modeling_camera(
          frame.camera, frame.input.viewport_size),
      .points = points,
      .partial_intersection = partial_intersection,
      .select_backfaces =
          frame.settings.bool_value(ViewportBoolSettingId::BackfaceSelection),
      .near_plane = frame.camera.state().near_plane,
      .far_plane = frame.camera.state().far_plane,
  };
  const bool scene_mode = baseline.mode == ModelingSelectionMode::Object ||
                          baseline.mode == ModelingSelectionMode::Group;
  std::vector<EntityId> eligible_entities;
  const std::vector<ResolvedSelectionHit> modeling_hits =
      selection.query_modeling_lasso_hits(request, &eligible_entities);
  const CurveToolPreviewState *curve_controls =
      include_curve_controls && curve != nullptr && curve->controls_editable()
          ? &curve->preview_state() : nullptr;
  const auto hits = collect_viewport_scene_lasso_hits({
      .camera = frame.camera,
      .points = points,
      .partial_intersection = partial_intersection,
      .billboards = scene_mode
          ? frame.scene_billboard_pick_candidates
          : std::span<const ViewportSceneBillboardPickCandidate>{},
      .curves = scene_mode
          ? frame.scene_curve_pick_candidates
          : std::span<const ViewportSceneCurvePickCandidate>{},
      .curve_controls = curve_controls,
      .resolved_selection_hits = modeling_hits,
      .include_curve_controls = curve_controls != nullptr,
      .eligible_entities = &eligible_entities,
      .projection_cache = projection_cache,
  });
  std::vector<ResolvedSelectionTarget> targets;
  targets.reserve(hits.size());
  for (const ViewportSceneHitCandidate &hit : hits) {
    if (auto target = resolved_selection_target(hit)) {
      targets.push_back(std::move(*target));
    }
  }
  if (baseline.mode == ModelingSelectionMode::Group) {
    targets = selection.filter_lasso_group_targets(
        targets, eligible_entities, partial_intersection);
  }
  return checked_selection_apply_result(
      input_router,
      selection.apply_resolved_marquee({
          .targets = std::move(targets),
          .edit = selection_edit_to_modeling(edit),
          .mode = baseline.mode,
          .rule = partial_intersection ? ModelingBoxSelectionRule::Crossing
                                       : ModelingBoxSelectionRule::Contain,
          .baseline = baseline,
      }),
      "Viewport resolved lasso selection");
}

} // namespace

void ViewportSelectionInteraction::reset() {
  if (box_state_.active || paint_selection_active_) {
    box_release_suppression_.begin(QuaderMouseButton::Left, true);
  }
  hover_clear_state_ = HoverClearState::Armed;
  quader_editor_core::clear_selection_box(box_state_);
  box_pointer_.reset();
  box_edit_ = quader_editor_core::SelectionEditOperation::Replace;
  box_cycle_depth_ = false;
  box_cycle_modifiers_ = {};
  cancel_pending_click();
  pending_pick_feedback_.reset();
  box_hovered_before_selection_.reset();
  box_baseline_.reset();
  lasso_context_.reset();
  marquee_cache_ = {};
  lasso_projection_cache_ = {};
  curve_control_box_active_ = false;
  committed_marquee_.reset();
  hover_suppressed_target_.reset();
  paint_selection_active_ = false;
  viewport_hover_state_initialized_ = false;
  viewport_hovered_last_frame_ = false;
  last_selection_command_result_ = {.ok = true};
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
  const ViewportRay current_ray =
      frame.camera.ray_from_viewport_point(pointer.press.local);
  const auto same_vector = [](QVec3 left, QVec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
  };
  return same_vector(ray.origin, current_ray.origin) &&
         same_vector(ray.direction, current_ray.direction);
}

bool ViewportSelectionInteraction::DepthClickBaseline::matches_frame(
    const ViewportInteractionFrame &frame, SceneSessionId current_scene) const {
  return viewport_pointer_within_drag_threshold(pointer,
                                                frame.left_click_local) &&
         matches_context(frame, current_scene);
}

void ViewportSelectionInteraction::cancel_pending_click() {
  if (depth_click_baseline_.has_value() &&
      depth_click_baseline_->delay_single_click && box_state_.active &&
      !box_state_.dragging && !lasso_context_.has_value()) {
    quader_editor_core::clear_selection_box(box_state_);
    box_edit_ = quader_editor_core::SelectionEditOperation::Replace;
    box_cycle_depth_ = false;
    box_cycle_modifiers_ = {};
    box_hovered_before_selection_.reset();
    box_baseline_.reset();
    box_pointer_.reset();
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
    const QuaderSelectionSummary summary =
        commit_pending_pick(editor, frame).selection;
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
        .pointer = viewport_pointer_gesture_from_latest_edge(
            frame.input.pointer_pressed_edges, QuaderMouseButton::Left,
            frame.left_click_local, frame.input.mouse_screen,
            frame.input.selection_click_delay_for_double_click
                ? frame.input.selection_click_modifiers
                : frame.input.modifiers,
            frame.pointer_drag_threshold_pixels,
            frame.monotonic_time_seconds +
                std::max(frame.double_click_interval_seconds, 0.0)),
        .selection = selection_owner(editor).capture_box_selection_baseline(),
        .camera = &frame.camera,
        .ray = {.origin = ray.origin, .direction = ray.direction},
        .viewport_size = frame.input.viewport_size,
        .scene_session_id = scene_session_id,
        .tool = frame.input_router.state().active_tool,
        .content_revision = selection.content_revision,
        .selection_revision = selection.selection_revision,
        .scene_revision = selection_state.scene_revision,
        .asset_revision = selection_state.asset_revision,
        .keybindings_revision = editor.commands().keybindings_revision(),
        .delay_single_click = frame.input.selection_click_delay_for_double_click,
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
  const ModelingPickSelectRequest &request = pending_first_pick_->request;
  return request.select_backfaces ==
             frame.settings.bool_value(ViewportBoolSettingId::BackfaceSelection) &&
         request.vertex_radius == viewport_scaled_world_pick_radius(
                                      frame.settings.float_value(
                                          ViewportFloatSettingId::PickVertexRadius),
                                      frame.viewport_pixel_scale,
                                      frame.input.device_pixel_ratio) &&
         request.edge_radius == viewport_scaled_world_pick_radius(
                                    frame.settings.float_value(
                                        ViewportFloatSettingId::PickEdgeRadius),
                                    frame.viewport_pixel_scale,
                                    frame.input.device_pixel_ratio);
}

ViewportSelectionApplyResult ViewportSelectionInteraction::commit_pending_pick(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  const PendingFirstPick pending = *pending_first_pick_;
  cancel_pending_click();
  pending_pick_feedback_ = PendingPickFeedback{
      .local = pending.pointer.press.local,
      .hovered_before_selection = pending.hovered_before_selection,
  };
  ViewportSelectionApplyResult result = checked_selection_apply_result(
      frame.input_router,
      selection_owner(editor).apply_resolved_click({
          .winner = pending.winner,
          .component_body_target = pending.component_body_target,
          .policy = pending.request,
          .miss_sub_selection_kind = pending.miss_sub_selection_kind,
      }),
      "Viewport deferred resolved selection");
  last_selection_command_result_ = result.command;
  return result;
}

std::optional<ViewportSelectionApplyResult>
ViewportSelectionInteraction::commit_pick_selection(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    QVec2 local_mouse, int click_count, bool preserve_selection_on_miss,
    bool preserve_selection_on_selected_hit,
    bool preserve_existing_component_selection_on_hit,
    ViewportModifiers click_modifiers, bool cycle_depth,
    ViewportModifiers cycle_modifiers,
    std::optional<ResolvedSelectionTarget> hovered_before_selection,
    ViewportCurveInteractionPort *curve, bool curve_controls_only) {
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
        depth_click_baseline_->delay_single_click
            ? depth_click_baseline_->pointer.modifiers
            : click_modifiers;
    ViewportPointerGesture pending_pointer = depth_click_baseline_->pointer;
    pending_pointer.press.local = local_mouse;
    pending_pointer.modifiers = modifiers;
    const ModelingPickSelectRequest pending_request = make_pick_select_request(
        frame, local_mouse, modifiers, click_count,
        preserve_selection_on_miss, preserve_selection_on_selected_hit,
        preserve_existing_component_selection_on_hit);
    ResolvedSelectionPointPick resolved = resolve_selection_point_pick(
        selection_owner(editor), frame, local_mouse, pending_request, curve,
        &scene_surface_scratch_, true);
    if (curve_controls_only &&
        (!resolved.winner.has_value() ||
         resolved.winner->domain !=
             ResolvedSelectionDomain::RegisteredSubElement ||
         resolved.winner->sub_selection_kind !=
             EditorSubSelectionKind::CurveControl)) {
      resolved.winner.reset();
      resolved.component_body_target.reset();
    }
    pending_first_pick_ = PendingFirstPick{
        .pointer = pending_pointer,
        .request = pending_request,
        .winner = std::move(resolved.winner),
        .component_body_target =
            std::move(resolved.component_body_target),
        .hovered_before_selection = hovered_before_selection,
        .miss_sub_selection_kind =
            curve_controls_only ? EditorSubSelectionKind::CurveControl
                                : EditorSubSelectionKind::None,
    };
    return std::nullopt;
  }
  const bool apply_first_click_baseline =
      cycle_depth && (cycle_modifiers.primary || cycle_modifiers.shift) &&
      depth_click_baseline_.has_value() &&
      depth_click_baseline_->double_click_pending;
  ViewportSelectionApplyResult result = pick_selection(
      editor, frame.input_router, frame, local_mouse, click_count,
      preserve_selection_on_miss, preserve_selection_on_selected_hit,
      preserve_existing_component_selection_on_hit, click_modifiers,
      cycle_depth, cycle_modifiers,
      apply_first_click_baseline ? &depth_click_baseline_->selection : nullptr,
      curve, &scene_surface_scratch_, curve_controls_only);
  last_selection_command_result_ = result.command;
  const QuaderSelectionSummary &summary = result.selection;
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
  return result;
}

const ViewportSelectionMarquee &ViewportSelectionInteraction::marquee() const {
  if (!box_state_.active || !box_state_.dragging) {
    static const ViewportSelectionMarquee kEmpty;
    return committed_marquee_.has_value() ? *committed_marquee_ : kEmpty;
  }
  if (!lasso_context_.has_value()) {
    marquee_cache_ = box_marquee(box_state_.start, box_state_.end);
  }
  return marquee_cache_;
}

std::optional<quader_editor_core::SelectionEditOperation>
ViewportSelectionInteraction::pointer_edit_operation(
    const ViewportInteractionFrame &frame) const {
  using Edit = quader_editor_core::SelectionEditOperation;
  const auto from_modifiers = [](ViewportModifiers modifiers) {
    return quader_editor_core::selection_edit_operation_from_modifiers(
        modifiers.primary, modifiers.shift);
  };

  if (box_state_.active) {
    return box_cycle_depth_ ? from_modifiers(box_cycle_modifiers_) : box_edit_;
  }
  if (paint_selection_active_) {
    return frame.input.modifiers.primary ? Edit::Remove : Edit::Add;
  }
  if (pending_first_pick_.has_value()) {
    const ModelingPickSelectRequest &request = pending_first_pick_->request;
    if (request.edge_traversal_requested(
            to_modeling_selection_mode(frame.selection_state.summary.mode)) !=
        ModelingEdgeSelectionTraversal::None) {
      return std::nullopt;
    }
    if (request.control_pressed || request.command_pressed) {
      return Edit::Remove;
    }
    return request.shift_pressed ? Edit::Add : Edit::Replace;
  }
  if (frame.input.cycle_selection_depth_pressed) {
    return frame.input.cycle_selection_depth_modifiers.alt
               ? std::nullopt
               : std::optional<Edit>(
                     from_modifiers(frame.input.cycle_selection_depth_modifiers));
  }
  // Alt modifies traversal, texture, and Curve gestures. Those are not
  // additive/subtractive selection operations even when Ctrl or Shift is held.
  if (frame.input.modifiers.alt) {
    return std::nullopt;
  }
  return from_modifiers(frame.input.modifiers);
}

bool ViewportSelectionInteraction::cancel_lasso_drag() {
  if (!lasso_context_.has_value()) {
    return false;
  }
  box_release_suppression_.begin(QuaderMouseButton::Left, true);
  clear_curve_control_box();
  box_baseline_.reset();
  box_hovered_before_selection_.reset();
  box_cycle_depth_ = false;
  box_cycle_modifiers_ = {};
  committed_marquee_.reset();
  cancel_pending_click();
  return true;
}

void ViewportSelectionInteraction::append_lasso_point(QVec2 point,
                                                       bool include_release) {
  if (!lasso_context_.has_value() || !box_pointer_.has_value() ||
      !std::isfinite(point.x) || !std::isfinite(point.y)) {
    return;
  }
  box_state_.end = point;
  box_state_.dragging = box_state_.dragging ||
      viewport_pointer_drag_threshold_reached(*box_pointer_, point);
  std::vector<QVec2> &points = marquee_cache_.lasso_points;
  const double distance_squared = points.empty()
      ? lasso_sample_distance_squared_
      : viewport_pointer_distance_squared(points.back(), point);
  if (points.empty() || distance_squared >= lasso_sample_distance_squared_ ||
      (include_release && distance_squared > 0.0)) {
    points.push_back(point);
    marquee_cache_.path_revision = ++lasso_path_revision_;
  }
  marquee_cache_.active = box_state_.dragging;
  marquee_cache_.end = point;
}

void ViewportSelectionInteraction::collect_lasso_samples(
    const ViewportInteractionFrame &frame) {
  if (!lasso_context_.has_value() || !box_pointer_.has_value()) {
    return;
  }
  const ViewportPointerButtonEdge &press = box_pointer_->press;
  const ViewportPointerButtonEdge *release =
      lasso_release_edge(frame.input, press);
  bool sampled = false;
  for (const ViewportPointerMotionSample &sample :
       frame.input.pointer_motion_samples) {
    if (sample.button != QuaderMouseButton::Left ||
        (press.sequence != 0u && sample.press_sequence != press.sequence) ||
        (sample.sequence != 0u &&
         sample.sequence <= lasso_sample_sequence_) ||
        (release != nullptr && sample.sequence > release->sequence)) {
      continue;
    }
    append_lasso_point(sample.local,
                       release != nullptr &&
                           sample.sequence == release->sequence);
    lasso_sample_sequence_ = sample.sequence;
    sampled = true;
  }
  if (release != nullptr) {
    append_lasso_point(release->local, true);
  } else if (!sampled && (frame.left_down || frame.left_released)) {
    append_lasso_point(frame.input.mouse_local, frame.left_released);
  }
}

void ViewportSelectionInteraction::clear_curve_control_box() {
  quader_editor_core::clear_selection_box(box_state_);
  box_pointer_.reset();
  lasso_context_.reset();
  marquee_cache_ = {};
  curve_control_box_active_ = false;
  box_edit_ = quader_editor_core::SelectionEditOperation::Replace;
}

void ViewportSelectionInteraction::handle_frame(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool,
    ViewportToolHoverPort *tool_hover, ViewportCurveInteractionPort *curve) {
  QDR_PROFILE_FUNCTION();
  committed_marquee_.reset();
  bool canceled_release_reserved = box_release_suppression_.consume(
      QuaderMouseButton::Left, frame.left_pressed, frame.left_down,
      frame.left_released);
  const bool curve_controls_editable =
      curve != nullptr && curve->controls_editable();
  if (curve_control_box_active_ && !curve_controls_editable) {
    box_release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    clear_curve_control_box();
  }
  const bool paint_selection_was_active = paint_selection_active_;
  const bool gizmo_consumed = before_tool.gizmo_consumed;
  const bool gizmo_blocks_hover = gizmo_consumed || before_tool.gizmo_hovered;
  const bool selection_fallback_active =
      frame.active_tool_allows_selection_fallback;
  const bool suppress_hover_work =
      frame.input_result.navigation_active ||
      frame.input_result.camera_changed || frame.input.text_input_active ||
      frame.input.modal_tool_active || frame.input.active_tool_owns_viewport ||
      gizmo_blocks_hover;
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
  if (lasso_context_.has_value() &&
      (!can_use_selection_fallback || !frame.input.viewport_focused ||
       frame.input_result.view_changed ||
       !lasso_context_->matches_context(
           frame, editor.scene_documents().active_scene_id().value_or(
                      SceneSessionId{})) ||
       lasso_context_->selection.mode !=
           to_modeling_selection_mode(frame.selection_state.summary.mode) ||
       lasso_context_->keybindings_revision !=
           editor.commands().keybindings_revision())) {
    (void)cancel_lasso_drag();
    canceled_release_reserved = true;
  }
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
    clear_curve_control_box();
    box_edit_ = quader_editor_core::SelectionEditOperation::Replace;
    box_cycle_depth_ = false;
    box_cycle_modifiers_ = {};
    box_hovered_before_selection_.reset();
    box_baseline_.reset();
    box_pointer_.reset();
  }
  if (box_state_.active &&
      (frame.input.pointer_interaction_canceled ||
       frame.input_result.cancel_requested)) {
    box_release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    clear_curve_control_box();
    box_cycle_depth_ = false;
    box_cycle_modifiers_ = {};
    box_hovered_before_selection_.reset();
    box_baseline_.reset();
    canceled_release_reserved = true;
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

  auto publish_empty_hover = [&](bool preserve_component_candidate) {
    const ModelingCommandResult clear_hover_result =
        preserve_component_candidate
            ? selection_owner(editor).preview_spatial_hover(std::nullopt)
            : selection_owner(editor).preview_resolved_hover(std::nullopt);
    if (!clear_hover_result.ok) {
      LOG_ERROR("Viewport hover clear failed: {}",
                clear_hover_result.message.empty()
                    ? "Hover clear failed."
                    : clear_hover_result.message);
    }
    hover_clear_state_ = preserve_component_candidate
                             ? HoverClearState::SpatialCandidatePreserved
                             : HoverClearState::FullyCleared;
  };
  auto clear_hover = [&]() { publish_empty_hover(false); };
  auto clear_spatial_hover = [&]() { publish_empty_hover(true); };

  auto suppress_hover_after_selection =
      [&](QVec2 local_mouse,
          std::optional<ResolvedSelectionTarget> hovered_before_selection) {
        if (hovered_before_selection.has_value()) {
          hover_suppressed_target_ = std::move(hovered_before_selection);
        } else {
          const ModelingPickSelectRequest hover_request =
              make_pick_select_request(
                  frame, local_mouse,
                  frame.input.modifiers, 0, true, false);
          hover_suppressed_target_ = resolve_selection_point_target(
              selection_owner(editor), frame, local_mouse, hover_request,
              curve, &scene_surface_scratch_);
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
    const std::optional<ResolvedSelectionTarget> hovered_before_selection =
        selection_owner(editor).resolved_hover_target();
    const std::optional<ViewportSelectionApplyResult> applied =
        commit_pick_selection(
            editor, frame, before_tool.selection_click_local,
            std::max(1, before_tool.selection_click_count), true,
            !before_tool.selection_cycle_depth,
            !before_tool.selection_cycle_depth,
            before_tool.selection_click_modifiers,
            before_tool.selection_cycle_depth,
            before_tool.selection_cycle_depth_modifiers,
            hovered_before_selection, curve);
    if (applied.has_value()) {
      const QuaderSelectionSummary &summary = applied->selection;
      paint_selection_active_ =
          applied->command.ok && is_component_selection_mode(
                                     to_modeling_selection_mode(summary.mode));
      if (applied->command.ok) {
        enter_picked_instance(editor, frame, summary,
                              before_tool.selection_click_count,
                              before_tool.selection_cycle_depth);
      }
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
    const std::optional<ResolvedSelectionTarget> hovered_before_selection =
        selection_owner(editor).resolved_hover_target();
    const std::optional<ViewportSelectionApplyResult> applied =
        commit_pick_selection(
        editor, frame, frame.input.mouse_local, 1, true, false, false,
        frame.input.modifiers, true,
        frame.input.cycle_selection_depth_modifiers,
        hovered_before_selection, curve);
    if (applied.has_value()) {
      last_selection_command_result_ = applied->command;
    }
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
        make_hover_request(frame, frame.right_click_local,
                           frame.input.modifiers));
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
    hover_clear_state_ = HoverClearState::Armed;
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
      frame.left_pressed && !canceled_release_reserved &&
      !frame.input.pointer_interaction_canceled &&
      !frame.input_result.cancel_requested &&
      !before_tool.selection_click_requested) {
    QDR_PROFILE_SCOPE("Viewport selection begin box");
    box_edit_ = quader_editor_core::selection_edit_operation_from_modifiers(
        frame.input.modifiers.primary, frame.input.modifiers.shift);
    box_cycle_depth_ = frame.input.cycle_selection_depth_pressed;
    box_cycle_modifiers_ = frame.input.cycle_selection_depth_modifiers;
    if (box_cycle_depth_ && !can_cycle_depth) {
      box_cycle_depth_ = false;
      return;
    }
    box_hovered_before_selection_ =
        selection_owner(editor).resolved_hover_target();
    box_baseline_ = !box_cycle_depth_ && depth_click_baseline_.has_value()
                        ? depth_click_baseline_->selection
                        : selection_owner(editor)
                              .capture_box_selection_baseline();
    if (curve_control_gesture_available) {
      curve_control_box_active_ = true;
    }
    box_pointer_ = viewport_pointer_gesture_from_latest_edge(
        frame.input.pointer_pressed_edges, QuaderMouseButton::Left,
        frame.left_click_local, frame.input.mouse_screen,
        frame.input.modifiers, frame.pointer_drag_threshold_pixels);
    box_edit_ = quader_editor_core::selection_edit_operation_from_modifiers(
        box_pointer_->modifiers.primary, box_pointer_->modifiers.shift);
    quader_editor_core::begin_selection_box(
        box_state_, box_pointer_->press.local, box_edit_);
    if (frame.input_router.state().active_tool == ViewportToolId::LassoSelect) {
      // Only a press resolves settings. Pointer motion retains this snapshot.
      const QuaderToolSettingsTarget settings = editor.tools().active_settings();
      const auto partial = std::find_if(
          settings.controls.begin(), settings.controls.end(),
          [](const QuaderToolSettingControl &control) {
            return control.id == "select_partial_intersection";
          });
      lasso_partial_intersection_ =
          partial != settings.controls.end() && partial->bool_value;
      const ViewportRay ray =
          frame.camera.ray_from_viewport_point(box_pointer_->press.local);
      lasso_context_ = DepthClickBaseline{
          .pointer = *box_pointer_,
          .selection = *box_baseline_,
          .camera = &frame.camera,
          .ray = {.origin = ray.origin, .direction = ray.direction},
          .viewport_size = frame.input.viewport_size,
          .scene_session_id = editor.scene_documents().active_scene_id().value_or(
              SceneSessionId{}),
          .tool = ViewportToolId::LassoSelect,
          .content_revision = frame.selection_state.summary.content_revision,
          .selection_revision = frame.selection_state.summary.selection_revision,
          .scene_revision = frame.selection_state.scene_revision,
          .asset_revision = frame.selection_state.asset_revision,
          .keybindings_revision = editor.commands().keybindings_revision(),
      };
      const double sample_distance = 2.0 * static_cast<double>(
          viewport_sanitized_pixel_scale(frame.viewport_pixel_scale));
      lasso_sample_distance_squared_ = sample_distance * sample_distance;
      lasso_sample_sequence_ = 0;
      marquee_cache_ = {
          .start = box_pointer_->press.local,
          .end = box_pointer_->press.local,
          .rule = lasso_partial_intersection_
              ? ModelingBoxSelectionRule::Crossing
              : ModelingBoxSelectionRule::Contain,
      };
      append_lasso_point(box_pointer_->press.local);
    }
  }

  if (lasso_context_.has_value()) {
    collect_lasso_samples(frame);
  }
  if (box_state_.active && (frame.left_down || lasso_context_.has_value())) {
    QDR_PROFILE_SCOPE("Viewport selection update box");
    if (!lasso_context_.has_value()) {
      quader_editor_core::update_selection_box(
        box_state_, frame.input.mouse_local,
        box_pointer_.has_value()
            ? static_cast<double>(box_pointer_->drag_threshold_pixels)
            : static_cast<double>(frame.pointer_drag_threshold_pixels));
    }
    if (box_state_.dragging) {
      // Mouse motion only presents the marquee. Resolving and publishing a
      // selection here would synchronously rebuild SDK, scene, UI, and overlay
      // state for every changing hit set; the final rectangle commits below.
      if (hover_clear_state_ != HoverClearState::FullyCleared) {
        clear_hover();
      }
    }
  }

  const bool release_provenance_available =
      !frame.input.pointer_released_edges.empty();
  const bool matching_box_release =
      box_pointer_.has_value() && frame.left_released &&
      (!release_provenance_available || box_pointer_->press.sequence == 0u ||
       (lasso_context_.has_value()
            ? lasso_release_edge(frame.input, box_pointer_->press)
            : viewport_pointer_edge_after(frame.input.pointer_released_edges,
                                           box_pointer_->press)) != nullptr);
  const bool exact_box_press =
      box_pointer_.has_value() && box_pointer_->press.sequence != 0u;
  const bool mismatched_box_release =
      box_state_.active && exact_box_press && frame.left_released &&
      release_provenance_available && !matching_box_release;
  const bool abandoned_exact_box =
      box_state_.active && exact_box_press && !frame.left_down &&
      !frame.left_released;
  if (mismatched_box_release || abandoned_exact_box) {
    clear_curve_control_box();
    box_cycle_depth_ = false;
    box_cycle_modifiers_ = {};
    box_hovered_before_selection_.reset();
    box_baseline_.reset();
  }
  const bool box_button_became_idle =
      !exact_box_press && !frame.left_down && !frame.left_released;
  if (box_state_.active && !canceled_release_reserved &&
      (matching_box_release || box_button_became_idle)) {
    QDR_PROFILE_SCOPE("Viewport selection finish box");
    const bool lasso_commit = lasso_context_.has_value();
    const QVec2 release_local = lasso_commit
        ? box_state_.end : frame.input.mouse_local;
    const quader_editor_core::SelectionBoxCommit commit =
        quader_editor_core::finish_selection_box(
            box_state_, release_local, box_edit_,
            box_pointer_.has_value()
                ? static_cast<double>(box_pointer_->drag_threshold_pixels)
                : static_cast<double>(frame.pointer_drag_threshold_pixels));
    const ViewportModifiers click_modifiers =
        box_pointer_.has_value() ? box_pointer_->modifiers
                                 : frame.input.modifiers;
    box_pointer_.reset();
    const std::optional<ResolvedSelectionTarget> hovered_before_selection =
        box_hovered_before_selection_;
    const bool include_curve_controls = curve_control_box_active_;
    curve_control_box_active_ = false;
    if (commit.dragged) {
      cancel_pending_click();
      committed_marquee_ = lasso_commit
          ? std::move(marquee_cache_) : box_marquee(commit.start, commit.end);
      const EditorSelectionBoxBaseline baseline =
          box_baseline_.value_or(
              selection_owner(editor).capture_box_selection_baseline());
      const ViewportSelectionApplyResult applied = lasso_commit
          ? lasso_selection(editor, frame.input_router, frame,
                            committed_marquee_->lasso_points,
                            lasso_partial_intersection_, box_edit_, baseline,
                            curve, include_curve_controls,
                            &lasso_projection_cache_)
          : box_selection(
                editor, frame.input_router, frame, commit.start, commit.end,
                box_edit_, baseline, curve, include_curve_controls,
                !can_pick && include_curve_controls
                    ? EditorSubSelectionKind::CurveControl
                    : EditorSubSelectionKind::None,
                &scene_surface_scratch_);
      last_selection_command_result_ = applied.command;
      suppress_hover_after_selection(commit.end, hovered_before_selection);
      refresh_hover_work_after_selection(commit.end, false);
    } else if (can_pick || include_curve_controls) {
      const std::optional<ViewportSelectionApplyResult> applied =
          commit_pick_selection(
              editor, frame, frame.left_click_local,
              std::max(1, frame.left_click_count), box_cycle_depth_, false,
              false, click_modifiers, box_cycle_depth_, box_cycle_modifiers_,
              hovered_before_selection, curve,
              !can_pick && include_curve_controls);
      if (applied.has_value()) {
        const QuaderSelectionSummary &summary = applied->selection;
        paint_selection_active_ =
            applied->command.ok && is_component_selection_mode(
                                       to_modeling_selection_mode(summary.mode));
        if (applied->command.ok) {
          enter_picked_instance(editor, frame, summary, frame.left_click_count,
                                box_cycle_depth_);
        }
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
    lasso_context_.reset();
    marquee_cache_ = {};
  }

  if (pending_first_pick_.has_value() && !frame.left_down) {
    if (!can_pick || !pending_click_matches_context(editor, frame)) {
      cancel_pending_click();
    } else if (viewport_pointer_deadline_reached(
                   depth_click_baseline_->pointer,
                   frame.monotonic_time_seconds)) {
      const ViewportSelectionApplyResult applied =
          commit_pending_pick(editor, frame);
      last_selection_command_result_ = applied.command;
      finish_pending_pick_feedback();
    }
  }

  if (!canceled_release_reserved &&
      (frame.left_released || !frame.left_down)) {
    if (box_state_.active &&
        (matching_box_release || box_button_became_idle)) {
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
    const std::optional<ResolvedSelectionTarget> hovered_before_selection =
        selection_owner(editor).resolved_hover_target();
    const ViewportSelectionApplyResult applied = pick_selection(
        editor, frame.input_router, frame, frame.input.mouse_local, 0, false,
        false, false, frame.input.modifiers, false, {}, nullptr, curve,
        &scene_surface_scratch_);
    last_selection_command_result_ = applied.command;
    const QuaderSelectionSummary &summary = applied.selection;
    paint_selection_active_ =
        applied.command.ok && is_component_selection_mode(
                                  to_modeling_selection_mode(summary.mode));
    suppress_hover_after_selection(frame.input.mouse_local,
                                   hovered_before_selection);
    refresh_hover_work_after_selection(frame.input.mouse_local, true);
  }

  const bool box_dragging = box_state_.active && box_state_.dragging;
  const bool can_hover = selection_fallback_active &&
                         frame.input.viewport_hovered && !suppress_hover_work &&
                         !box_dragging && !frame.left_down &&
                         !tool_result.handled;
  if (can_hover && !claim_hover_work()) {
    return;
  }

  bool hover_suppressed_this_frame = false;
  if (can_hover && hover_suppressed_target_.has_value()) {
    const ModelingPickSelectRequest hover_request = make_pick_select_request(
        frame, frame.input.mouse_local,
        frame.input.modifiers, 0, true, false);
    const std::optional<ResolvedSelectionTarget> hover =
        resolve_selection_point_target(selection_owner(editor), frame,
                                       frame.input.mouse_local, hover_request,
                                       curve, &scene_surface_scratch_);
    if (hover == hover_suppressed_target_) {
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
    const ModelingPickSelectRequest hover_request = make_pick_select_request(
        frame, frame.input.mouse_local,
        frame.input.modifiers, 0, true, false);
    const std::optional<ResolvedSelectionTarget> hover_target =
        resolve_selection_point_target(selection_owner(editor), frame,
                                       frame.input.mouse_local, hover_request,
                                       curve, &scene_surface_scratch_);
    const ModelingCommandResult hover =
        selection_owner(editor).preview_spatial_hover(
            hover_target, frame.input.modifiers.primary);
    if (!hover.ok) {
      LOG_ERROR("Viewport hover failed: {}",
                hover.message.empty() ? "Hover failed." : hover.message);
    }
    hover_clear_state_ = HoverClearState::Armed;
    return;
  }

  const bool knife_authored_hover_this_frame =
      tool_result.handled &&
      frame.input_router.state().active_tool == ViewportToolId::Knife &&
      frame.input.viewport_hovered && !frame.input.text_input_active &&
      !frame.input_result.navigation_active &&
      !frame.input_result.camera_changed && !gizmo_blocks_hover;
  if (knife_authored_hover_this_frame) {
    // Knife owns component hover while its point resolver is eligible to run.
    // Re-arm clearing so a later suppressed frame or pointer leave cannot retain
    // the last Knife target.
    hover_clear_state_ = HoverClearState::Armed;
    return;
  }

  const bool requires_full_hover_clear =
      !selection_fallback_active || frame.input.text_input_active ||
      frame.input.modal_tool_active || frame.input.active_tool_owns_viewport ||
      gizmo_blocks_hover || tool_result.handled;
  const bool should_clear_hover =
      !frame.input.viewport_hovered || requires_full_hover_clear;
  const bool preserve_component_candidate =
      pointer_left_viewport && selection_fallback_active &&
      !requires_full_hover_clear;
  const bool needs_spatial_clear =
      preserve_component_candidate && hover_clear_state_ == HoverClearState::Armed;
  const bool needs_full_clear =
      !preserve_component_candidate &&
      (hover_clear_state_ == HoverClearState::Armed ||
       (hover_clear_state_ == HoverClearState::SpatialCandidatePreserved &&
        requires_full_hover_clear));
  if (should_clear_hover && (needs_spatial_clear || needs_full_clear)) {
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
    if (preserve_component_candidate) {
      // Leaving the viewport retains the last component candidate wire.
      clear_spatial_hover();
    } else {
      // A gizmo is a different hover target, not an empty spatial sample.
      // Drop hover-only mesh wires; selected components retain their own wires.
      clear_hover();
    }
    if (gizmo_blocks_hover && tool_hover != nullptr) {
      // A handle can disappear under a stationary pointer. Re-arm once so
      // scene hover resumes even when its spatial work key has not changed.
      tool_hover->invalidate();
    }
  }
}

QuaderSelectionSummary ViewportSelectionInteraction::pick_context_menu_target(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  cancel_pending_click();
  const ViewportSelectionApplyResult applied = pick_selection(
      editor, frame.input_router, frame, frame.input.mouse_local, 1, true, true,
      true, frame.input.modifiers, false, {}, nullptr, nullptr,
      &scene_surface_scratch_);
  last_selection_command_result_ = applied.command;
  const QuaderSelectionSummary &summary = applied.selection;
  paint_selection_active_ =
      is_component_selection_mode(to_modeling_selection_mode(summary.mode));
  hover_clear_state_ = HoverClearState::Armed;
  hover_suppressed_target_.reset();
  return summary;
}

} // namespace quader
