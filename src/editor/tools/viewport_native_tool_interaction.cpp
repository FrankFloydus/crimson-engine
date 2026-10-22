////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/viewport_native_tool_interaction.hpp"

#include "api/quader_operation_input_context.hpp"

#include "assets/asset_registry.hpp"
#include "core/logger.hpp"
#include "core/release_features.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "editor/array/path_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/interactions/tool_drag_math.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_grid_placement_policy.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"
#include "viewport/viewport_pixel_scale.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace quader {
namespace {

float screen_distance_squared(QVec2 left, QVec2 right) {
  const QVec2 delta = left - right;
  return delta.x * delta.x + delta.y * delta.y;
}

QVec3 linear_array_drag_axis(LinearArrayAxisConstraint constraint,
                            bool vertical) {
  switch (constraint) {
  case LinearArrayAxisConstraint::X:
    return {1.0f, 0.0f, 0.0f};
  case LinearArrayAxisConstraint::Y:
    return {0.0f, 1.0f, 0.0f};
  case LinearArrayAxisConstraint::Z:
    return {0.0f, 0.0f, 1.0f};
  case LinearArrayAxisConstraint::Free:
    return vertical ? QVec3{0.0f, 1.0f, 0.0f} : QVec3{};
  }
  return {};
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

bool native_pointer_interrupted(
    const ViewportNativeToolInteractionFrame &frame) {
  return frame.input.pointer_interaction_canceled ||
         !frame.input.viewport_focused || frame.input.text_input_active ||
         frame.input.navigation_active ||
         frame.input_result.cancel_requested ||
         frame.input_result.navigation_active ||
         frame.input_result.camera_changed || frame.input_result.view_changed;
}

const ViewportPointerButtonEdge *matching_left_release(
    const ViewportNativeToolInteractionFrame &frame,
    const ViewportPointerGesture &gesture) {
  if (!frame.left_released ||
      gesture.press.button != QuaderMouseButton::Left) {
    return nullptr;
  }
  return viewport_pointer_edge_after(frame.input.pointer_released_edges,
                                     gesture.press);
}

bool synthetic_left_release_matches(
    const ViewportNativeToolInteractionFrame &frame,
    const ViewportPointerGesture &gesture) {
  return frame.left_released && gesture.press.sequence == 0u &&
         frame.input.pointer_released_edges.empty();
}

std::optional<CurveControlTarget> curve_control_hit(
    const ViewportNativeToolInteractionFrame &frame,
    const CurveToolPreviewState &preview, QVec2 mouse_local,
    bool anchors_only) {
  return frame.handles_visible && frame.curve_control_picker != nullptr
             ? frame.curve_control_picker->pick(preview, mouse_local,
                                                anchors_only)
             : std::nullopt;
}

bool curve_creation_kind_is_preset(CurveCreationKind kind) {
  return kind == CurveCreationKind::Circle || kind == CurveCreationKind::Oval;
}

std::array<float, 25> pointer_camera_key(
    const ViewportCameraController &camera, QVec2 pointer_local,
    float viewport_pixel_scale) {
  const ViewportCameraState &state = camera.state();
  const ViewportCameraPose pose = camera.pose();
  const QVec2 size = camera.viewport_size();
  return {pointer_local.x, pointer_local.y, viewport_pixel_scale,
          size.x, size.y, state.target.x, state.target.y, state.target.z,
          state.yaw_radians, state.pitch_radians, state.distance,
          static_cast<float>(state.projection), state.orthographic_size,
          state.fov_degrees, state.near_plane, state.far_plane,
          pose.eye.x, pose.eye.y, pose.eye.z,
          pose.target.x, pose.target.y, pose.target.z,
          pose.up.x, pose.up.y, pose.up.z};
}

} // namespace

ViewportNativeToolInteractionResult
ViewportNativeToolInteraction::handle_frame(
    ViewportNativeToolLifecyclePort &lifecycle, SceneDocument &document,
    AssetRegistry &assets,
    PivotToolService *pivot, LinearArrayToolService *linear_array,
    RadialArrayToolService *radial_array,
    PathArrayToolService *path_array,
    EditorSelectionService *selection,
    ViewportPointPlacementService *point_placement,
    ViewportCameraController &camera,
    const ViewportNativeToolInteractionFrame &frame,
    CurveToolService *curve) {
  if (!release_has_feature(ReleaseFeature::Pivot)) {
    pivot = nullptr;
  }
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    linear_array = nullptr;
  }
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    radial_array = nullptr;
  }
  if (!release_allows_tool(kPathArrayToolId)) {
    path_array = nullptr;
  }
  if (!release_has_feature(ReleaseFeature::Curves)) {
    curve = nullptr;
  }
  ViewportNativeToolInteractionResult output;
  pointer_cursor_ = ViewportPointerCursor::Default;
  curve_control_hover_accepted_ = false;
  path_source_hovered_ = false;
  linear_array_endpoint_hovered_ = false;
  pivot_handle_hovered_ = false;
  const ViewportInputFrame &input = frame.input;
  const ViewportInputResult &input_result = frame.input_result;
  const bool navigation_suspended =
      (input.navigation_active || input_result.navigation_active) &&
      !input.pointer_interaction_canceled && !input.text_input_active &&
      !input_result.cancel_requested;
  const bool canceled_left_release = left_release_suppression_.consume(
      QuaderMouseButton::Left, frame.left_pressed, frame.left_down,
      frame.left_released);
  if (canceled_left_release) {
    output.consumed = true;
    output.blocks_transform_gizmo = true;
  }
  const auto record_change = [&](const CommandResult &result) {
    if (result.changed) {
      output.invalidate_hover = true;
      // Curve controls have a separate retained overlay publication. Actual
      // curve edits already change scene/asset revisions; pointer-only changes
      // must not rebuild and copy an unrelated mesh selection overlay.
      output.mark_overlay_dirty = !output.curve_consumed;
    }
  };
  const auto record_commit = [&](std::string_view label,
                                 const CommandResult &result) {
    if (!result.ok) {
      LOG_ERROR("{} failed: {}", label,
                result.message.empty() ? "Command failed." : result.message);
    }
    output.sync_active_tool = true;
    output.invalidate_hover = true;
    output.mark_overlay_dirty = true;
  };

  if (release_has_feature(ReleaseFeature::Lights) &&
      lifecycle.light_creation_active()) {
    pointer_cursor_ = ViewportPointerCursor::PrecisionCross;
    output.consumed = true;
    output.blocks_transform_gizmo = true;

    const bool can_place = point_placement != nullptr &&
                           input.viewport_hovered &&
                           input.viewport_focused &&
                           !native_pointer_interrupted(frame) &&
                           !input.navigation_active &&
                           !frame.transform_gizmo_consumed &&
                           !frame.suppress_hover_work;
    if (frame.left_pressed && can_place && !canceled_left_release) {
      output.claim_interaction_pane = true;
      left_release_suppression_.begin(QuaderMouseButton::Left,
                                      frame.left_down);

      const float grid_size =
          viewport_grid_world_size_for_preset(input.grid_preset);
      const ViewportPointPlacementPlane active_grid =
          viewport_active_grid_placement_plane(camera);
      const ViewportPointPlacementResult placement = point_placement->resolve(
          document, assets, camera,
          ViewportPointPlacementRequest{
              .mouse_local = frame.left_click_local,
              .viewport_pixel_scale = frame.viewport_pixel_scale,
              .active_grid = active_grid,
              .grid_size = grid_size,
              .policy = ViewportPointPlacementPolicy{
                  .vertex = frame.scene_snap_enabled,
                  .edge_midpoint = frame.scene_snap_enabled,
                  .face_center = frame.scene_snap_enabled,
                  .surface = true,
                  .grid = frame.grid_snap_enabled,
                  .prefer_surface_normal_for_features = true,
                  .strict_kind_priority = true,
                  .grid_requires_snap_radius = false,
                  .snap_surface_to_grid = frame.grid_snap_enabled,
              },
              .fallback_plane = active_grid,
          });

      if (placement.valid) {
        output.light_creation_world_position = placement.world_position;
      } else {
        output.command_attempted = true;
        output.command_result = command_invalid_result(
            "Unable to resolve a viewport position for the light.");
      }
    }
    return output;
  }

  if (path_array != nullptr && path_array->active()) {
    output.path_array_consumed = true;
    output.consumed = true;
    output.blocks_transform_gizmo = true;

    const auto clear_source_hover = [&]() {
      path_source_hovered_ = false;
      if (selection == nullptr) {
        return;
      }
      const ModelingCommandResult cleared =
          selection->preview_resolved_hover(std::nullopt);
      if (!cleared.ok) {
        LOG_ERROR("Path Array hover clear failed: {}",
                  cleared.message.empty() ? "Hover clear failed."
                                          : cleared.message);
      }
      output.mark_overlay_dirty = output.mark_overlay_dirty || cleared.changed;
    };
    const auto resolve_source = [&](QVec2 local_mouse)
        -> std::optional<EntityId> {
      if (selection == nullptr) {
        return std::nullopt;
      }
      const PathArrayPreviewState &preview = path_array->preview_state();
      if (path_array_exclusion_owner_ != path_array ||
          path_array_exclusion_revision_ != preview.scene.revision ||
          path_array_exclusion_source_ != preview.source_root) {
        path_array_exclusion_owner_ = path_array;
        path_array_exclusion_revision_ = preview.scene.revision;
        path_array_exclusion_source_ = preview.source_root;
        path_array_excluded_entities_ = preview.scene.suppression.entities;
        std::ranges::sort(path_array_excluded_entities_);
        path_array_excluded_entities_.erase(
            std::ranges::unique(path_array_excluded_entities_).begin(),
            path_array_excluded_entities_.end());
        path_array_excluded_occurrences_.clear();
        path_array_excluded_occurrences_.reserve(
            preview.scene.suppression.occurrences.size());
        for (const ModelingToolPreviewOccurrenceSuppression &occurrence :
             preview.scene.suppression.occurrences) {
          path_array_excluded_occurrences_.push_back({
              .source_entity = occurrence.source_entity,
              .occurrence = occurrence.occurrence,
          });
        }
        std::ranges::sort(
            path_array_excluded_occurrences_, [](const auto &left,
                                                 const auto &right) {
              if (left.occurrence != right.occurrence) {
                return left.occurrence < right.occurrence;
              }
              return left.source_entity < right.source_entity;
            });
        path_array_excluded_occurrences_.erase(
            std::ranges::unique(path_array_excluded_occurrences_).begin(),
            path_array_excluded_occurrences_.end());
      }
      const std::optional<EntityId> picked =
          frame.path_array_source_picker != nullptr
              ? frame.path_array_source_picker->pick(
                    local_mouse, path_array_excluded_entities_,
                    path_array_excluded_occurrences_, frame.select_backfaces)
              : std::nullopt;
      if (!picked.has_value()) {
        return std::nullopt;
      }

      const EntityId promoted =
          selection->group_promoted_scene_target(*picked);
      if (promoted == kInvalidEntityId) {
        return std::nullopt;
      }

      // The source remains authored while renderer-neutral suppression is
      // active. Exclude its complete hierarchy from CPU picking too. A nested
      // group could otherwise promote to a different root and be selected
      // again even though none of its participants are visible.
      const EntityId current_source = path_array->source_root();
      if (current_source != kInvalidEntityId &&
          (promoted == current_source ||
           document.is_descendant_of(promoted, current_source))) {
        return std::nullopt;
      }
      return path_array->resolve_source_candidate(promoted);
    };

    if (input.modal_tool_finalize_pressed && path_array->preview_valid()) {
      record_commit("Path Array apply", lifecycle.commit_active());
      if (path_array_source_press_) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
      }
      path_array_source_press_.reset();
      clear_source_hover();
      return output;
    }

    if (native_pointer_interrupted(frame)) {
      if (path_array_source_press_) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
      }
      path_array_source_press_.reset();
      clear_source_hover();
      return output;
    }

    const bool can_hover = selection != nullptr && input.viewport_hovered &&
                           input.viewport_focused &&
                           !frame.suppress_hover_work;
    if (can_hover && !frame.left_down && !path_array_source_press_) {
      const std::optional<EntityId> candidate =
          resolve_source(input.mouse_local);
      const ModelingCommandResult hovered =
          selection->preview_resolved_hover(
              candidate.has_value()
                  ? std::optional<ResolvedSelectionTarget>{
                        ResolvedSelectionTarget{
                            .domain = ResolvedSelectionDomain::SceneEntity,
                            .entity = *candidate,
                        }}
                  : std::nullopt);
      path_source_hovered_ = hovered.ok && candidate.has_value();
      if (!hovered.ok) {
        LOG_ERROR("Path Array hover failed: {}",
                  hovered.message.empty() ? "Hover failed."
                                          : hovered.message);
      }
      output.mark_overlay_dirty = output.mark_overlay_dirty || hovered.changed;
    } else if (!can_hover) {
      clear_source_hover();
    }

    const bool plain_left_press =
        frame.left_pressed && !canceled_left_release &&
        !input.modifiers.primary && !input.modifiers.shift &&
        !input.modifiers.alt;
    if (plain_left_press && !path_array_source_press_ && can_hover) {
      const std::optional<EntityId> candidate =
          resolve_source(frame.left_click_local);
      if (candidate.has_value()) {
        path_array_source_press_ = PathArraySourcePress{
            .gesture = viewport_pointer_gesture_from_latest_edge(
                input.pointer_pressed_edges, QuaderMouseButton::Left,
                frame.left_click_local, input.mouse_screen, input.modifiers,
                frame.pointer_drag_threshold_pixels),
            .candidate = *candidate,
            .path_entity = path_array->path_entity(),
        };
        output.claim_interaction_pane = true;
      }
    }

    if (path_array_source_press_) {
      const ViewportPointerButtonEdge *release = matching_left_release(
          frame, path_array_source_press_->gesture);
      const bool matching_release =
          release != nullptr ||
          synthetic_left_release_matches(frame,
                                         path_array_source_press_->gesture);
      const QVec2 release_local =
          release != nullptr ? release->local : input.mouse_local;
      const bool moved = viewport_pointer_drag_threshold_reached(
          path_array_source_press_->gesture, release_local);
      const bool stale_path =
          path_array_source_press_->path_entity != path_array->path_entity();
      const bool abandoned = !frame.left_down && !frame.left_released;
      if (stale_path || abandoned) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
        path_array_source_press_.reset();
      } else if (matching_release) {
        const EntityId pressed_candidate =
            path_array_source_press_->candidate;
        const std::optional<EntityId> released_candidate =
            moved ? std::nullopt : resolve_source(release_local);
        path_array_source_press_.reset();
        if (released_candidate.has_value() &&
            *released_candidate == pressed_candidate) {
          const CommandResult selected =
              path_array->select_source(pressed_candidate);
          if (!selected.ok) {
            LOG_ERROR("Path Array source selection failed: {}",
                      selected.message.empty()
                          ? "Source selection failed."
                          : selected.message);
          }
          if (selected.changed) {
            output.invalidate_hover = true;
            output.mark_overlay_dirty = true;
            clear_source_hover();
          }
        }
      }
    }
    return output;
  }
  if (path_array_source_press_) {
    left_release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
    path_array_source_press_.reset();
  }

  const bool curve_interaction_requested =
      curve != nullptr && curve->controls_editable() &&
      (curve->active() || frame.transform_gizmo_consumed || curve->dragging() ||
       !input.curve_conversion_edges.empty() ||
       curve_conversion_press_.has_value() ||
       curve_point_construction_press_.has_value() ||
       curve_preset_placement_press_.has_value());
  if (curve_interaction_requested) {
    output.curve_consumed = true;
    output.consumed = true;
    output.blocks_transform_gizmo = true;
    record_change(curve->refresh());
    const auto clear_curve_hover = [&]() {
      if (curve->drawing() &&
          curve_creation_kind_is_preset(curve->creation_kind())) {
        record_change(curve->set_preset_hover(std::nullopt));
      } else {
        record_change(curve->set_hover(std::nullopt));
      }
    };
    const auto finish_construction_drag = [&](bool cancel) {
      const CommandResult finished = cancel ? curve->cancel_drag()
                                             : curve->commit_drag();
      record_change(finished);
      output.command_attempted = true;
      output.command_result = finished;
      if (finished.ok) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
        curve_point_construction_press_.reset();
        pointer_cursor_ = ViewportPointerCursor::Default;
      }
      return finished.ok;
    };
    if (!curve->point_construction_dragging()) {
      if (curve_point_construction_press_) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
      }
      curve_point_construction_press_.reset();
    }
    if (curve_preset_placement_press_ &&
        (!curve->drawing() || curve->point_count() != 0u ||
         curve->creation_kind() !=
             curve_preset_placement_press_->creation_kind)) {
      left_release_suppression_.begin(QuaderMouseButton::Left,
                                      frame.left_down);
      curve_preset_placement_press_.reset();
    }
    if (frame.transform_gizmo_consumed) {
      // TransformGizmoInteraction owns any selected-control transaction. This
      // native path must not reinterpret it as a free pointer-plane drag.
      if (curve_point_construction_press_ &&
          curve->point_construction_dragging()) {
        if (!finish_construction_drag(input_result.cancel_requested)) {
          return output;
        }
      }
      if (curve_preset_placement_press_) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
        curve_preset_placement_press_.reset();
      }
      if (curve_conversion_press_) {
        const bool released =
            viewport_pointer_edge_after(input.pointer_released_edges,
                                        curve_conversion_press_->gesture.press) !=
            nullptr;
        if (released || input.pointer_interaction_canceled) {
          curve_conversion_press_.reset();
        }
      }
      // Camera motion leaves the last world-space placement candidate intact.
      if (!navigation_suspended) {
        clear_curve_hover();
      }
      return output;
    }
    const bool blocked = native_pointer_interrupted(frame);
    if (blocked) {
      if (curve->point_construction_dragging()) {
        const bool focus_only = !input.viewport_focused &&
            !input.pointer_interaction_canceled && !input.text_input_active &&
            !input.navigation_active && !input_result.cancel_requested &&
            !input_result.navigation_active && !input_result.camera_changed &&
            !input_result.view_changed;
        if (focus_only && frame.left_down && !frame.left_released) {
          return output;
        }
        if (!finish_construction_drag(input_result.cancel_requested)) {
          return output;
        }
      }
      if (curve_preset_placement_press_) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
        curve_preset_placement_press_.reset();
      }
      if (curve_conversion_press_) {
        const bool released =
            viewport_pointer_edge_after(input.pointer_released_edges,
                                        curve_conversion_press_->gesture.press) !=
            nullptr;
        curve_conversion_press_->target.reset();
        if (released || input.pointer_interaction_canceled ||
            input_result.cancel_requested) {
          curve_conversion_press_.reset();
        }
      }
      if (!navigation_suspended) {
        clear_curve_hover();
      }
      return output;
    }

    const bool conversion_input = !input.curve_conversion_edges.empty();
    if (!curve->dragging() && !curve_preset_placement_press_ &&
        (conversion_input || curve_conversion_press_)) {
      if (conversion_input && input.viewport_hovered &&
          input.viewport_focused && !frame.suppress_hover_work) {
        for (const ViewportCurveConversionEdge &event :
             input.curve_conversion_edges) {
          // A preceding conversion can replace the preview document, so every
          // ordered event must validate against the latest identity.
          const auto &preview = curve->preview_state();
          const QuaderInputChord &trigger = event.semantic_chord;
          if (event.kind ==
              ViewportCurveConversionEdgeKind::ReservePress) {
            if (!event.pointer_edge ||
                event.pointer_edge->button != trigger.mouse_button) {
              continue;
            }
            const auto pressed_hit = curve_control_hit(
                frame, preview, event.pointer_edge->local, true);
            if (pressed_hit) {
              pointer_cursor_ = ViewportPointerCursor::OpenHand;
            }
            curve_conversion_press_ = CurveConversionPress{
                .gesture = {.press = *event.pointer_edge},
                .target = pressed_hit,
                .content = preview.document
                               ? preview.document->content_identity()
                               : modeling::CurveContentIdentity{},
                .entity = preview.entity,
            };
            output.claim_interaction_pane =
                output.claim_interaction_pane || pressed_hit.has_value();
            continue;
          }

          if (trigger.device != QuaderInputDevice::Mouse) {
            if (curve_conversion_press_) {
              continue;
            }
            const auto action_hit =
                curve_control_hit(frame, preview, input.mouse_local, true);
            if (action_hit) {
              pointer_cursor_ = ViewportPointerCursor::OpenHand;
              record_change(
                  curve->toggle_point_smoothness(action_hit->point));
            }
            continue;
          }
          if (!event.pointer_edge ||
              event.pointer_edge->button != trigger.mouse_button) {
            continue;
          }

          const QuaderMouseButton button = event.pointer_edge->button;
          const auto action_hit = curve_control_hit(
              frame, preview, event.pointer_edge->local, true);
          if (action_hit) {
            pointer_cursor_ = ViewportPointerCursor::OpenHand;
          }
          if (trigger.gesture == QuaderInputGesture::Click) {
            const bool valid_press =
                curve_conversion_press_ &&
                curve_conversion_press_->gesture.press.button == button &&
                curve_conversion_press_->target == action_hit &&
                preview.document &&
                curve_conversion_press_->content ==
                    preview.document->content_identity() &&
                curve_conversion_press_->entity == preview.entity &&
                (curve_conversion_press_->gesture.press.sequence == 0u ||
                 event.pointer_edge->sequence == 0u ||
                 event.pointer_edge->sequence >
                     curve_conversion_press_->gesture.press.sequence);
            if (action_hit && valid_press) {
              record_change(
                  curve->toggle_point_smoothness(action_hit->point));
            }
            curve_conversion_press_.reset();
            continue;
          }

          // Press, DoubleClick, and Drag actions complete immediately from the
          // exact originating press. Only ReservePress persists across frames
          // while awaiting a deferred Click.
          const CurveConversionPress immediate_press{
              .gesture = {.press = *event.pointer_edge},
              .target = action_hit,
              .content = preview.document
                             ? preview.document->content_identity()
                             : modeling::CurveContentIdentity{},
              .entity = preview.entity,
          };
          curve_conversion_press_.reset();
          output.claim_interaction_pane =
              output.claim_interaction_pane || action_hit.has_value();
          const bool valid_press =
              action_hit && immediate_press.target == action_hit &&
              preview.document &&
              immediate_press.content ==
                  preview.document->content_identity() &&
              immediate_press.entity == preview.entity;
          if (valid_press) {
            record_change(
                curve->toggle_point_smoothness(action_hit->point));
          }
        }
      }
      if (curve_conversion_press_ &&
          viewport_pointer_edge_after(
              input.pointer_released_edges,
              curve_conversion_press_->gesture.press) != nullptr) {
        curve_conversion_press_.reset();
      }
      clear_curve_hover();
      // A conversion miss is inert; it must not start placement or a handle drag.
      return output;
    }

    if (input.modal_tool_finalize_pressed && curve->can_apply()) {
      record_commit("Curve apply", lifecycle.commit_active());
      return output;
    }

    const float grid_size = viewport_grid_world_size_for_preset(input.grid_preset);
    const auto resolve_position = [&](QVec2 mouse_local,
                                      const ViewportPointPlacementPlane &plane) {
      if (point_placement == nullptr) {
        return ViewportPointPlacementResult{};
      }
      return point_placement->resolve(
          document, assets, camera,
          ViewportPointPlacementRequest{
              .mouse_local = mouse_local,
              .viewport_pixel_scale = frame.viewport_pixel_scale,
              .active_grid = plane,
              .grid_size = grid_size,
              .policy = ViewportPointPlacementPolicy{
                  .vertex = frame.scene_snap_enabled,
                  .edge_midpoint = frame.scene_snap_enabled,
                  .face_center = frame.scene_snap_enabled,
                  .surface = false,
                  .grid = frame.grid_snap_enabled,
                  .grid_requires_snap_radius = false,
              },
              .fallback_plane = plane,
          });
    };

    const ViewportPointPlacementPlane construction_plane =
        viewport_active_grid_placement_plane(camera);
    const CurveCreationKind creation_kind = curve->creation_kind();
    const bool preset_creation =
        curve_creation_kind_is_preset(creation_kind);
    const bool empty_preset = curve->drawing() && preset_creation &&
                              curve->point_count() == 0u;
    if (!curve->dragging() && !curve_preset_placement_press_ &&
        input.viewport_hovered && !frame.suppress_hover_work) {
      const CurveToolPreviewState &preview = curve->preview_state();
      const QVec2 control_local = frame.left_pressed ? frame.left_click_local
                                                     : input.mouse_local;
      const CurveControlHoverKey hover_key{
          .content = preview.document ? preview.document->content_identity()
                                      : modeling::CurveContentIdentity{},
          .entity = preview.entity,
          .scene_revision = document.revision(),
          .asset_revision = assets.revision(),
          .scene_surface_revision = frame.scene_surface_revision,
          .world_transform = preview.world_transform.values,
          .pointer_camera = pointer_camera_key(camera, control_local,
                                                frame.viewport_pixel_scale),
          .drawing = preview.drawing,
          .active = preview.active,
      };
      if (!frame.handles_visible || frame.curve_control_picker == nullptr) {
        curve_control_hover_key_.reset();
        curve_control_hover_target_.reset();
      } else if (!curve_control_hover_key_ ||
                 *curve_control_hover_key_ != hover_key) {
        curve_control_hover_key_ = hover_key;
        curve_control_hover_target_ =
            curve_control_hit(frame, preview, control_local, false);
      }
      const std::optional<CurveControlTarget> hit = curve_control_hover_target_;
      curve_control_hover_accepted_ = hit.has_value();
      if (hit) {
        pointer_cursor_ = ViewportPointerCursor::OpenHand;
      }
      if (frame.left_pressed && !canceled_left_release) {
        const bool plain_construction_gesture =
            !input.modifiers.primary && !input.modifiers.shift &&
            !input.modifiers.alt;
        if (empty_preset && plain_construction_gesture) {
          const auto placement = resolve_position(frame.left_click_local,
                                                   construction_plane);
          if (placement.valid) {
            const CurvePresetPlacementFrame preset_frame{
                .center = placement.world_position,
                .axis_u = construction_plane.u_axis,
                .axis_v = construction_plane.v_axis,
            };
            curve_preset_placement_press_ = CurvePresetPlacementPress{
                .gesture = viewport_pointer_gesture_from_latest_edge(
                    input.pointer_pressed_edges, QuaderMouseButton::Left,
                    frame.left_click_local, input.mouse_screen,
                    input.modifiers, frame.pointer_drag_threshold_pixels),
                .center = placement.world_position,
                .axis_u = construction_plane.u_axis,
                .axis_v = construction_plane.v_axis,
                .creation_kind = creation_kind,
            };
            record_change(curve->set_preset_hover(preset_frame));
            output.claim_interaction_pane = true;
          } else {
            record_change(curve->set_preset_hover(std::nullopt));
          }
        } else if (creation_kind == CurveCreationKind::Draw) {
          if (!hit && curve->drawing() && plain_construction_gesture) {
            const auto placement = resolve_position(frame.left_click_local,
                                                     construction_plane);
            if (placement.valid) {
              const CommandResult begun =
                  curve->begin_point(placement.world_position);
              record_change(begun);
              if (begun.ok && curve->point_construction_dragging()) {
                const QVec3 origin = *curve->drag_origin_world();
                CurvePointConstructionPress construction_press{
                    .gesture = viewport_pointer_gesture_from_latest_edge(
                        input.pointer_pressed_edges, QuaderMouseButton::Left,
                        frame.left_click_local, input.mouse_screen,
                        input.modifiers, frame.pointer_drag_threshold_pixels),
                    .pointer_offset =
                        camera.project_world_point(origin).position -
                        frame.left_click_local,
                    .plane = construction_plane,
                };
                construction_press.plane.origin = origin;
                curve_point_construction_press_ =
                    std::move(construction_press);
                output.claim_interaction_pane = true;
              }
            }
          }
        }
      } else if (curve->drawing()) {
        const auto placement = resolve_position(input.mouse_local,
                                                 construction_plane);
        if (preset_creation) {
          std::optional<CurvePresetPlacementFrame> preset_hover;
          if (empty_preset && placement.valid && !canceled_left_release) {
            preset_hover = CurvePresetPlacementFrame{
                .center = placement.world_position,
                .axis_u = construction_plane.u_axis,
                .axis_v = construction_plane.v_axis,
            };
          }
          record_change(curve->set_preset_hover(preset_hover));
        } else {
          record_change(curve->set_hover(
              placement.valid ? std::optional<QVec3>(placement.world_position)
                              : std::nullopt));
        }
      }
    } else if (!curve->dragging() && !curve_preset_placement_press_) {
      clear_curve_hover();
    }

    if (curve_preset_placement_press_) {
      const ViewportPointerButtonEdge *release = matching_left_release(
          frame, curve_preset_placement_press_->gesture);
      const bool matching_release =
          release != nullptr || synthetic_left_release_matches(
                                    frame,
                                    curve_preset_placement_press_->gesture);
      const QVec2 pointer_local =
          release != nullptr ? release->local : input.mouse_local;
      const bool moved = viewport_pointer_drag_threshold_reached(
          curve_preset_placement_press_->gesture, pointer_local);
      const bool foreign_or_stale_release =
          frame.left_released && !matching_release;
      const bool abandoned = !frame.left_down && !frame.left_released;
      if (moved || foreign_or_stale_release || abandoned ||
          canceled_left_release) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
        curve_preset_placement_press_.reset();
        record_change(curve->set_preset_hover(std::nullopt));
      } else if (matching_release) {
        const CurvePresetPlacementFrame placement{
            .center = curve_preset_placement_press_->center,
            .axis_u = curve_preset_placement_press_->axis_u,
            .axis_v = curve_preset_placement_press_->axis_v,
        };
        curve_preset_placement_press_.reset();
        record_change(curve->place_preset(placement));
      }
    }

    if (curve_point_construction_press_ &&
        curve->point_construction_dragging()) {
      const ViewportPointerButtonEdge *release = matching_left_release(
          frame, curve_point_construction_press_->gesture);
      const bool matching_release =
          release != nullptr || synthetic_left_release_matches(
                                    frame,
                                    curve_point_construction_press_->gesture);
      const QVec2 drag_local =
          release != nullptr ? release->local : input.mouse_local;
      const bool moved = viewport_pointer_drag_threshold_reached(
          curve_point_construction_press_->gesture, drag_local);
      if (moved && (frame.left_down || matching_release)) {
        if (frame.handles_visible) {
          pointer_cursor_ = ViewportPointerCursor::ClosedHand;
        }
        const auto placement = resolve_position(
            drag_local +
                curve_point_construction_press_->pointer_offset,
            curve_point_construction_press_->plane);
        if (placement.valid) {
          record_change(curve->update_drag(placement.world_position));
        }
      }
      if (matching_release && !canceled_left_release) {
        (void)finish_construction_drag(false);
      } else if (!frame.left_down && !frame.left_released) {
        // A lost release accepts the authored point/tangents in the current
        // draft. It does not publish the entire curve or reinterpret the cursor.
        (void)finish_construction_drag(false);
      }
    }
    return output;
  }
  if (curve_point_construction_press_) {
    if (curve != nullptr && curve->point_construction_dragging()) {
      const CommandResult finished = input_result.cancel_requested
          ? curve->cancel_drag() : curve->commit_drag();
      record_change(finished);
      output.command_attempted = true;
      output.command_result = finished;
      if (!finished.ok) {
        output.consumed = output.blocks_transform_gizmo = true;
        return output;
      }
    }
    left_release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
  }
  curve_point_construction_press_.reset();
  if (curve_preset_placement_press_) {
    left_release_suppression_.begin(QuaderMouseButton::Left, frame.left_down);
  }
  curve_preset_placement_press_.reset();
  curve_conversion_press_.reset();
  curve_control_hover_key_.reset();
  curve_control_hover_target_.reset();

  if (radial_array != nullptr && radial_array->active()) {
    output.radial_array_consumed = true;
    output.consumed = true;
    output.blocks_transform_gizmo = true;
    double unused_wheel_remainder = 0.0;
    double &wheel_remainder = frame.radial_array_wheel_remainder != nullptr
                                  ? *frame.radial_array_wheel_remainder
                                  : unused_wheel_remainder;
    const auto reconcile_wheel = [&](bool repeats) {
      reconcile_bounded_wheel_remainder(
          repeats ? radial_array->repeats() <= kRadialArrayMinimumRepeats
                  : radial_array->copies() <= kRadialArrayMinimumCopies,
          repeats ? radial_array->repeats() >= radial_array->maximum_repeats()
                  : radial_array->copies() >= radial_array->maximum_copies(),
          wheel_remainder);
    };
    const auto adjust_count = [&](double amount, bool repeats, bool wheel) {
      const auto kind = repeats ? ViewportRadialArrayInputEventKind::RepeatsWheel
                                : ViewportRadialArrayInputEventKind::CopiesWheel;
      if (radial_array_wheel_kind_ != kind) {
        wheel_remainder = 0.0;
        radial_array_wheel_kind_ = kind;
      }
      reconcile_wheel(repeats);
      const int delta = wheel
          ? accumulate_discrete_wheel_steps(amount, wheel_remainder)
          : amount > 0.0 ? 1 : amount < 0.0 ? -1 : 0;
      record_change(repeats ? radial_array->adjust_repeats(delta)
                            : radial_array->adjust_copies(delta));
      // Evaluate limits per physical event, preserving a following reversal.
      reconcile_wheel(repeats);
    };
    const bool can_accept_input = !native_pointer_interrupted(frame) &&
                                  !input.navigation_active;
    if (can_accept_input) {
      for (const ViewportRadialArrayInputEvent &event :
           input.radial_array_events) {
        switch (event.kind) {
        case ViewportRadialArrayInputEventKind::CopiesStep:
          adjust_count(event.amount, false, false);
          break;
        case ViewportRadialArrayInputEventKind::CopiesWheel:
          adjust_count(event.amount, false, true);
          break;
        case ViewportRadialArrayInputEventKind::RepeatsStep:
          adjust_count(event.amount, true, false);
          break;
        case ViewportRadialArrayInputEventKind::RepeatsWheel:
          adjust_count(event.amount, true, true);
          break;
        case ViewportRadialArrayInputEventKind::ResetWheel:
          wheel_remainder = 0.0;
          radial_array_wheel_kind_.reset();
          break;
        case ViewportRadialArrayInputEventKind::ToggleInstances:
          record_change(radial_array->set_create_instances(
              !radial_array->create_instances()));
          break;
        }
      }
    } else {
      wheel_remainder = 0.0;
      radial_array_wheel_kind_.reset();
    }

    // Hover placement may follow a toolbar activation while keyboard focus
    // remains in Qt. Fixing the center and accepting require viewport focus.
    const bool can_place = point_placement != nullptr &&
                           input.viewport_hovered &&
                           !input.pointer_interaction_canceled &&
                           !input.text_input_active &&
                           !input.navigation_active &&
                           !input_result.cancel_requested &&
                           !input_result.navigation_active &&
                           !input_result.camera_changed &&
                           !input_result.view_changed &&
                           !frame.suppress_hover_work;
    const float grid_size =
        viewport_grid_world_size_for_preset(input.grid_preset);
    const auto resolve_center = [&](QVec2 mouse_local) {
      return point_placement->resolve(
          document, assets, camera,
          ViewportPointPlacementRequest{
              .mouse_local = mouse_local,
              .viewport_pixel_scale = frame.viewport_pixel_scale,
              .active_grid = viewport_active_grid_placement_plane(camera),
              .grid_size = grid_size,
              .policy = ViewportPointPlacementPolicy::radial_array(),
          });
    };

    if (frame.left_pressed && can_accept_input && can_place &&
        !canceled_left_release && !radial_array->center_locked()) {
      // A complete click can arrive in one host frame. Its press position,
      // rather than the current hover or a retained valid center, fixes placement.
      const ViewportPointPlacementResult clicked =
          resolve_center(frame.left_click_local);
      if (clicked.valid) {
        const CommandResult updated = radial_array->update_center_candidate(
            clicked.world_position, clicked.world_normal, grid_size);
        if (updated.ok && radial_array->has_valid_center()) {
          left_release_suppression_.begin(QuaderMouseButton::Left,
                                          frame.left_down);
          output.claim_interaction_pane = true;
          record_change(radial_array->lock_center());
        }
      }
    } else if (!frame.left_pressed && can_accept_input &&
               input.modal_tool_finalize_pressed &&
               radial_array->has_valid_center()) {
      record_commit("Radial Array apply", lifecycle.commit_active());
    } else if (can_place && !radial_array->center_locked()) {
      output.claim_interaction_pane = true;
      const ViewportPointPlacementResult hover =
          resolve_center(input.mouse_local);
      if (hover.valid) {
        if (frame.handles_visible) {
          pointer_cursor_ = ViewportPointerCursor::OpenHand;
        }
        record_change(radial_array->update_center_candidate(
            hover.world_position, hover.world_normal, grid_size));
      }
    }
  }

  if (linear_array != nullptr && linear_array->active()) {
    output.linear_array_consumed = true;
    output.consumed = true;
    output.blocks_transform_gizmo = true;
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
      record_change(linear_array->set_axis_constraint(LinearArrayAxisConstraint::X));
    }
    if (input.tool_constraint_y_pressed) {
      record_change(linear_array->set_axis_constraint(LinearArrayAxisConstraint::Y));
    }
    if (input.tool_constraint_z_pressed) {
      record_change(linear_array->set_axis_constraint(LinearArrayAxisConstraint::Z));
    }
    if (input.modal_tool_finalize_pressed) {
      record_commit("Linear Array apply",
                    lifecycle.commit_active());
      if (linear_array_handle_press_) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
      }
      linear_array_handle_press_.reset();
    } else {
      const LinearArrayHandleState handle = linear_array->handle_state();
      const bool can_interact = handle.visible && frame.handles_visible &&
                                input.viewport_hovered &&
                                !native_pointer_interrupted(frame) &&
                                !frame.suppress_hover_work;
      bool handle_hit = false;
      if (can_interact &&
          (!linear_array_handle_press_ || frame.left_released)) {
        const QVec2 pointer_local =
            frame.left_pressed ? frame.left_click_local : input.mouse_local;
        const HandleHoverKey hover_key{
            .pointer_camera = pointer_camera_key(camera, pointer_local,
                                                  frame.viewport_pixel_scale),
            .position = {handle.endpoint_world.x, handle.endpoint_world.y,
                         handle.endpoint_world.z},
        };
        if (!linear_array_hover_key_ || *linear_array_hover_key_ != hover_key) {
          linear_array_hover_key_ = hover_key;
          linear_array_hover_hit_ = linear_array_handle_hit(
              camera, handle, pointer_local, frame.viewport_pixel_scale);
        }
        handle_hit = linear_array_hover_hit_;
      }
      if (handle_hit) {
        linear_array_endpoint_hovered_ = true;
        pointer_cursor_ = ViewportPointerCursor::OpenHand;
      }
      if (can_interact && frame.left_pressed && !canceled_left_release &&
          !linear_array_handle_press_ && handle_hit) {
        linear_array_handle_press_ = LinearArrayHandlePress{
            .gesture = viewport_pointer_gesture_from_latest_edge(
                input.pointer_pressed_edges, QuaderMouseButton::Left,
                frame.left_click_local, input.mouse_screen, input.modifiers),
            .endpoint_before = handle.endpoint_world,
            .constraint_before = handle.constraint,
        };
        rebase_linear_array_drag(
            *linear_array_handle_press_, camera, frame.left_click_local, handle,
            linear_array_handle_press_->gesture.modifiers.alt);
        linear_array->begin_endpoint_drag();
        output.claim_interaction_pane = true;
      }
      if (linear_array_handle_press_) {
        const LinearArrayHandleState drag_handle = linear_array->handle_state();
        const ViewportPointerButtonEdge *release = matching_left_release(
            frame, linear_array_handle_press_->gesture);
        const bool matching_release =
            release != nullptr || synthetic_left_release_matches(
                                      frame,
                                      linear_array_handle_press_->gesture);
        const bool interrupted = native_pointer_interrupted(frame) ||
                                 !drag_handle.visible || !frame.handles_visible;
        const bool abandoned = !frame.left_down && !frame.left_released;
        if (interrupted || abandoned) {
          // Retire the screen-space gesture when its camera changes, retaining
          // the authored endpoint while navigation owns the pointer.
          if (!navigation_suspended) {
            // A drag can author several axes before it is canceled. Restore
            // its entire endpoint, not only the currently constrained channel.
            if (drag_handle.constraint != LinearArrayAxisConstraint::Free) {
              (void)linear_array->set_axis_constraint(
                  LinearArrayAxisConstraint::Free);
            }
            (void)linear_array->set_endpoint(
                linear_array_handle_press_->endpoint_before, false,
                viewport_grid_world_size_for_preset(input.grid_preset));
            if (linear_array_handle_press_->constraint_before !=
                LinearArrayAxisConstraint::Free) {
              (void)linear_array->set_axis_constraint(
                  linear_array_handle_press_->constraint_before);
            }
          }
          linear_array->end_endpoint_drag();
          left_release_suppression_.begin(QuaderMouseButton::Left,
                                          frame.left_down);
          linear_array_handle_press_.reset();
          output.invalidate_hover = true;
          output.mark_overlay_dirty = true;
        } else if ((frame.left_down || matching_release) && drag_handle.visible) {
          const QVec2 pointer = release != nullptr ? release->local
                                                   : input.mouse_local;
          const ViewportModifiers modifiers = release != nullptr
              ? release->modifiers.value_or(input.modifiers) : input.modifiers;
          const bool vertical = drag_handle.constraint ==
                                    LinearArrayAxisConstraint::Free &&
                                modifiers.alt;
          LinearArrayHandlePress &press = *linear_array_handle_press_;
          if (press.motion_constraint != drag_handle.constraint ||
              press.vertical != vertical) {
            // Keep the authored endpoint when its motion plane changes. Future
            // displacement is relative to this pointer, not the original ray.
            rebase_linear_array_drag(press, camera, pointer, drag_handle,
                                     vertical);
          } else if (press.motion_started || viewport_pointer_distance_squared(
                                                press.motion_pointer, pointer) > 0.0) {
            press.motion_started = true;
            record_change(linear_array->set_endpoint(
                linear_array_drag_point(camera, pointer, press),
                frame.grid_snap_enabled,
                viewport_grid_world_size_for_preset(input.grid_preset),
                vertical ? LinearArrayEndpointMotion::Vertical
                         : LinearArrayEndpointMotion::Horizontal));
          }
        }
        if (linear_array_handle_press_ && matching_release &&
            !canceled_left_release) {
          linear_array->end_endpoint_drag();
          linear_array_handle_press_.reset();
        }
        if (linear_array_handle_press_) {
          pointer_cursor_ = ViewportPointerCursor::ClosedHand;
        }
      }
    }
  }

  if (pivot != nullptr) {
    const bool blocked_by_navigation = input.navigation_active ||
                                       input_result.fly_navigation_active ||
                                       input_result.navigation_active;
    if (input.quick_pivot_released) {
      if (pivot_handle_press_ && !pivot_handle_press_->persistent &&
          !navigation_suspended && native_pointer_interrupted(frame)) {
        pivot->set_quick_preview_position(pivot_handle_press_->position_before);
        output.invalidate_hover = true;
        output.mark_overlay_dirty = true;
      }
      pivot->end_quick_edit();
      if (pivot_handle_press_) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
      }
      pivot_handle_press_.reset();
      output.pivot_consumed = true;
    }
    if (!blocked_by_navigation && input.quick_pivot_down &&
        !pivot->quick_edit_active()) {
      if (frame.selection_transform_pivot.has_value()) {
        const QVec3 initial = *frame.selection_transform_pivot;
        pivot->begin_quick_edit(initial);
        output.pivot_consumed = true;
      }
    }
    if (pivot->persistent_active() && input.modal_tool_finalize_pressed) {
      record_commit("Pivot apply",
                    lifecycle.commit_active());
      if (pivot_handle_press_) {
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
      }
      pivot_handle_press_.reset();
      output.pivot_consumed = true;
    }

    const PivotHandleState handle = pivot->handle_state();
    const bool can_interact = handle.visible && frame.handles_visible &&
                             input.viewport_hovered &&
                             !native_pointer_interrupted(frame) &&
                             !frame.transform_gizmo_consumed &&
                             !frame.suppress_hover_work;
    const PivotHandleLayoutOptions layout_options =
        frame.pivot_layout_options.value_or(PivotHandleLayoutOptions{});
    PivotHandlePart handle_part = PivotHandlePart::None;
    if (can_interact && (!pivot_handle_press_ || frame.left_released)) {
      const QVec2 pointer_local =
          frame.left_pressed ? frame.left_click_local : input.mouse_local;
      const HandleHoverKey hover_key{
          .pointer_camera = pointer_camera_key(camera, pointer_local,
                                                frame.viewport_pixel_scale),
          .position = {handle.world_position.x, handle.world_position.y,
                       handle.world_position.z},
          .layout = {layout_options.icon_size_pixels,
                     layout_options.world_x.x, layout_options.world_x.y, layout_options.world_x.z,
                     layout_options.world_y.x, layout_options.world_y.y, layout_options.world_y.z,
                     layout_options.world_z.x, layout_options.world_z.y, layout_options.world_z.z},
          .persistent = handle.persistent,
      };
      if (!pivot_hover_key_ || *pivot_hover_key_ != hover_key) {
        pivot_hover_key_ = hover_key;
        pivot_hover_part_ = hit_pivot_handle(
            build_pivot_handle_layout(camera, handle, frame.viewport_pixel_scale,
                                      layout_options), pointer_local);
      }
      handle_part = pivot_hover_part_;
    }
    if (handle_part != PivotHandlePart::None) {
      pivot_handle_hovered_ = true;
      pointer_cursor_ = ViewportPointerCursor::OpenHand;
    }
    if (can_interact && frame.left_pressed && !canceled_left_release &&
        !pivot_handle_press_ && handle_part != PivotHandlePart::None) {
      PivotHandlePress press{
          .gesture = viewport_pointer_gesture_from_latest_edge(
              input.pointer_pressed_edges, QuaderMouseButton::Left,
              frame.left_click_local, input.mouse_screen, input.modifiers,
              frame.pointer_drag_threshold_pixels),
          .position_before = handle.world_position,
          .persistent = handle.persistent,
          .target = handle.target,
          .part = handle_part,
          .layout_options = layout_options,
      };
      bool accepted = handle_part == PivotHandlePart::Center;
      if (!accepted && handle.persistent) {
        const auto layout = build_pivot_handle_layout(
            camera, handle, frame.viewport_pixel_scale, layout_options);
        const int axis_index = static_cast<int>(handle_part) -
                               static_cast<int>(PivotHandlePart::X);
        if (axis_index >= 0 && axis_index < 3 && layout.axes[axis_index].visible) {
          const auto &axis = layout.axes[axis_index];
          const auto ray = camera.ray_from_viewport_point(frame.left_click_local);
          const auto runtime_camera = ViewportCameraAdapter::runtime_camera(camera, input.viewport_size);
          const auto begun = quader_editor_core::begin_move_drag({
              .axis = axis_index == 0 ? quader_editor_core::TransformAxis::X
                    : axis_index == 1 ? quader_editor_core::TransformAxis::Y
                                      : quader_editor_core::TransformAxis::Z,
              .orientation = {.x_axis = layout_options.world_x,
                              .y_axis = layout_options.world_y,
                              .z_axis = layout_options.world_z},
              .has_world_direction = true,
              .world_direction = axis.world_direction,
              .has_world_center = true,
              .world_center = handle.world_position,
              .has_screen_axis = true,
              .screen_pivot = layout.center_screen,
              .screen_tip = axis.screen_tip,
              .screen_position = frame.left_click_local,
              .has_start_ray = true,
              .start_ray = {.origin = ray.origin, .direction = ray.direction},
              .has_camera = true,
              .camera_transform = runtime_camera.transform,
          });
          if (begun.ok) {
            press.axis_drag = begun.state;
            accepted = true;
          }
        }
      }
      if (accepted) {
        pivot_handle_press_ = std::move(press);
        output.claim_interaction_pane = true;
        output.pivot_consumed = true;
      }
    }
    if (pivot_handle_press_) {
      output.pivot_consumed = true;
      const ViewportPointerButtonEdge *release =
          pivot_handle_press_->axis_drag.active
              ? (frame.left_released ? viewport_pointer_matching_release(
                    input.pointer_released_edges, input.pointer_pressed_edges,
                    pivot_handle_press_->gesture.press) : nullptr)
              : matching_left_release(frame, pivot_handle_press_->gesture);
      const bool matching_release =
          release != nullptr || synthetic_left_release_matches(
                                    frame, pivot_handle_press_->gesture);
      const bool same_target = handle.persistent == pivot_handle_press_->persistent &&
                               handle.target == pivot_handle_press_->target;
      const bool interrupted = native_pointer_interrupted(frame) ||
                               !handle.visible || !frame.handles_visible || !same_target;
      const bool abandoned = !frame.left_down && !frame.left_released;
      if (interrupted || abandoned) {
        if (!navigation_suspended && same_target) {
          if (pivot_handle_press_->persistent) {
            pivot->set_persistent_preview_position(
                pivot_handle_press_->position_before);
          } else {
            pivot->set_quick_preview_position(pivot_handle_press_->position_before);
          }
        }
        left_release_suppression_.begin(QuaderMouseButton::Left,
                                        frame.left_down);
        pivot_handle_press_.reset();
        output.invalidate_hover = true;
        output.mark_overlay_dirty = true;
      } else if ((frame.left_down || (matching_release && pivot_handle_press_->axis_drag.active)) && handle.visible &&
                 (point_placement != nullptr || pivot_handle_press_->axis_drag.active)) {
        const PivotSnapSettings snap_settings =
            handle.persistent ? pivot->snap_settings()
                              : PivotSnapSettings{.vertex = true,
                                                  .edge_midpoint = false,
                                                  .face_center = false,
                                                  .grid = true};
        const QVec2 pointer = release != nullptr && pivot_handle_press_->axis_drag.active
            ? release->local : input.mouse_local;
        const auto sample_modifiers = release != nullptr
            ? release->modifiers.value_or(input.modifiers) : input.modifiers;
        const float grid_size = viewport_grid_world_size_for_preset(input.grid_preset);
        QVec3 position = handle.world_position;
        if (pivot_handle_press_->axis_drag.active) {
          pivot_handle_press_->axis_motion_started =
              pivot_handle_press_->axis_motion_started ||
              viewport_pointer_drag_threshold_reached(pivot_handle_press_->gesture, pointer);
          if (!pivot_handle_press_->axis_motion_started) {
            if (matching_release && !canceled_left_release) pivot_handle_press_.reset();
            output.pivot_blocks_gizmo = true;
            output.consumed = true;
            output.blocks_transform_gizmo = true;
            if (pivot_handle_press_) pointer_cursor_ = ViewportPointerCursor::ClosedHand;
            return output;
          }
          const PivotAxisSampleKey sample_key{
              .pointer_camera = pointer_camera_key(camera, pointer, frame.viewport_pixel_scale),
              .scene_revision = document.revision(),
              .asset_revision = assets.revision(),
              .surface_revision = frame.scene_surface_revision,
              .settings_revision = pivot->tool_settings_revision(),
              .grid_preset = input.grid_preset,
              .precision = sample_modifiers.primary,
          };
          if (pivot_handle_press_->axis_sample_key == sample_key) {
            if (matching_release && !canceled_left_release) pivot_handle_press_.reset();
            output.pivot_blocks_gizmo = true;
            output.consumed = true;
            output.blocks_transform_gizmo = true;
            if (pivot_handle_press_) pointer_cursor_ = ViewportPointerCursor::ClosedHand;
            return output;
          }
          pivot_handle_press_->axis_sample_key = sample_key;
          ++pivot_axis_sample_count_;
          auto &drag = pivot_handle_press_->axis_drag;
          const auto ray = camera.ray_from_viewport_point(pointer);
          const auto runtime_camera = ViewportCameraAdapter::runtime_camera(camera, input.viewport_size);
          auto moved = quader_editor_core::preview_move_from_viewport(
              drag, pointer, {.origin = ray.origin, .direction = ray.direction},
              runtime_camera, sample_modifiers.primary, snap_settings.grid, grid_size, true);
          if (point_placement != nullptr &&
              (snap_settings.vertex || snap_settings.edge_midpoint || snap_settings.face_center)) {
            auto feature_settings = snap_settings;
            feature_settings.grid = false;
            const auto snapped = point_placement->resolve(document, assets, camera, {
                .mouse_local = pointer,
                .viewport_pixel_scale = frame.viewport_pixel_scale,
                .grid_size = grid_size,
                .policy = ViewportPointPlacementPolicy::pivot(feature_settings),
                .fallback_plane = ViewportPointPlacementPlane{.origin = pivot_handle_press_->position_before},
            });
            if (snapped.valid &&
                (snapped.snap_kind == ViewportPointSnapKind::Vertex ||
                 snapped.snap_kind == ViewportPointSnapKind::EdgeMidpoint ||
                 snapped.snap_kind == ViewportPointSnapKind::FaceCenter)) {
              moved = quader_editor_core::preview_move_to_axis_snap_target(drag, snapped.world_position);
            }
          }
          if (moved.ok) position = pivot_handle_press_->position_before + moved.preview_delta;
        } else if (point_placement != nullptr) {
          position = resolved_pivot_drag_position(
              camera, pointer, handle.world_position, document, assets,
              *point_placement, snap_settings, grid_size, frame.viewport_pixel_scale);
        }
        if (handle.persistent) {
          pivot->set_persistent_preview_position(position);
        } else {
          pivot->set_quick_preview_position(position);
        }
        output.invalidate_hover = true;
        output.mark_overlay_dirty = true;
      }
      if (pivot_handle_press_ && matching_release &&
          !canceled_left_release) {
        pivot_handle_press_.reset();
      }
      if (pivot_handle_press_) {
        pointer_cursor_ = ViewportPointerCursor::ClosedHand;
      }
    }

    const PivotHandleState after = pivot->handle_state();
    output.pivot_blocks_gizmo = after.visible && after.editing;
    output.consumed = output.consumed || output.pivot_consumed;
    output.blocks_transform_gizmo =
        output.blocks_transform_gizmo || output.pivot_consumed ||
        output.pivot_blocks_gizmo;
  }
  return output;
}

void ViewportNativeToolInteraction::clear_drag_state() {
  curve_control_hover_accepted_ = false;
  path_source_hovered_ = false;
  linear_array_endpoint_hovered_ = false;
  pivot_handle_hovered_ = false;
  pointer_cursor_ = ViewportPointerCursor::Default;
  radial_array_wheel_kind_.reset();
  curve_control_hover_key_.reset();
  curve_control_hover_target_.reset();
  linear_array_hover_key_.reset();
  pivot_hover_key_.reset();
  if (pivot_handle_press_ || linear_array_handle_press_ ||
      curve_point_construction_press_ || curve_preset_placement_press_ ||
      path_array_source_press_) {
    // Lifecycle cancellation can run before the next sampled frame. Keep the
    // originating left release reserved so it cannot become a selection click.
    left_release_suppression_.begin(QuaderMouseButton::Left, true);
  }
  pivot_handle_press_.reset();
  linear_array_handle_press_.reset();
  curve_point_construction_press_.reset();
  curve_preset_placement_press_.reset();
  curve_conversion_press_.reset();
  path_array_source_press_.reset();
}

void ViewportNativeToolInteraction::populate_operation_input_context(
    QuaderOperationInputContext &context) const {
  context.curve_anchor_hovered =
      context.curve_anchor_hovered ||
      (curve_control_hover_accepted_ && curve_control_hover_target_ &&
       curve_control_hover_target_->kind == CurveControlKind::Anchor);
  context.curve_tangent_hovered =
      context.curve_tangent_hovered ||
      (curve_control_hover_accepted_ && curve_control_hover_target_ &&
       curve_control_hover_target_->kind != CurveControlKind::Anchor);
  context.path_source_hovered = path_source_hovered_;
  context.linear_array_endpoint_hovered = linear_array_endpoint_hovered_;
  context.pivot_handle_hovered = pivot_handle_hovered_;
}

bool ViewportNativeToolInteraction::dragging() const {
  return pivot_handle_press_.has_value() ||
         linear_array_handle_press_.has_value() ||
         curve_point_construction_press_.has_value() ||
         curve_preset_placement_press_.has_value() ||
         curve_conversion_press_.has_value() ||
         path_array_source_press_.has_value();
}

std::optional<int> ViewportNativeToolInteraction::hit_drawn_plane_captured_point(
    const std::array<QVec3, 3> &points, int captured_point_count,
    QVec2 mouse_local, const ViewportCameraController &camera,
    float viewport_pixel_scale) {
  constexpr float kPointHitRadiusPixels = 10.0f;
  const float hit_radius_pixels =
      kPointHitRadiusPixels *
      viewport_sanitized_pixel_scale(viewport_pixel_scale);
  const int count = std::min(captured_point_count, 2);
  for (int index = count - 1; index >= 0; --index) {
    const ViewportScreenPoint screen =
        camera.project_world_point(points[static_cast<std::size_t>(index)]);
    if (screen.visible &&
        screen_distance_squared(mouse_local, screen.position) <=
            hit_radius_pixels * hit_radius_pixels) {
      return index;
    }
  }
  return std::nullopt;
}

bool ViewportNativeToolInteraction::hit_drawn_plane_line(
    const std::array<QVec3, 3> &points, int captured_point_count, bool has_line,
    QVec2 mouse_local, const ViewportCameraController &camera,
    float viewport_pixel_scale) {
  constexpr float kLineHitRadiusPixels = 10.0f;
  const float hit_radius_pixels =
      kLineHitRadiusPixels *
      viewport_sanitized_pixel_scale(viewport_pixel_scale);
  if (!has_line || captured_point_count < 2) {
    return false;
  }
  const ViewportScreenPoint start = camera.project_world_point(points[0]);
  const ViewportScreenPoint end = camera.project_world_point(points[1]);
  return start.visible && end.visible &&
         distance_to_segment_squared(mouse_local, start.position,
                                     end.position) <=
             hit_radius_pixels * hit_radius_pixels;
}

ModelingBoxToolEvent ViewportNativeToolInteraction::modeling_box_event(
    const ViewportCameraController &camera, QVec2 viewport_size,
    QVec2 mouse_local, float grid_size, bool snap_enabled) {
  const ViewportRay ray = camera.ray_from_viewport_point(mouse_local);
  return {
      .screen_position = mouse_local,
      .ray = {.origin = ray.origin, .direction = ray.direction},
      .grid_size = grid_size,
      .has_camera = true,
      .camera =
          ViewportCameraAdapter::modeling_camera(camera, viewport_size),
      .snap_enabled = snap_enabled,
  };
}

QVec3 ViewportNativeToolInteraction::resolved_pivot_drag_position(
    const ViewportCameraController &camera, QVec2 mouse_local,
    QVec3 current_position, const SceneDocument &document,
    const AssetRegistry &assets, ViewportPointPlacementService &placement,
    PivotSnapSettings settings, float grid_size,
    float viewport_pixel_scale) {
  const ViewportPointPlacementResult resolved = placement.resolve(
      document, assets, camera,
      ViewportPointPlacementRequest{
          .mouse_local = mouse_local,
          .viewport_pixel_scale = viewport_pixel_scale,
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
    QVec2 mouse_local, float viewport_pixel_scale,
    const PivotHandleLayoutOptions &options) {
  return hit_pivot_handle(build_pivot_handle_layout(camera, handle,
      viewport_pixel_scale, options), mouse_local) != PivotHandlePart::None;
}

bool ViewportNativeToolInteraction::linear_array_handle_hit(
    const ViewportCameraController &camera,
    const LinearArrayHandleState &handle, QVec2 mouse_local,
    float viewport_pixel_scale) {
  if (!handle.visible) {
    return false;
  }
  const ViewportScreenPoint screen =
      camera.project_world_point(handle.endpoint_world);
  constexpr float kPickRadiusPixels = 16.0f;
  const float pick_radius_pixels =
      kPickRadiusPixels *
      viewport_sanitized_pixel_scale(viewport_pixel_scale);
  return screen.visible &&
         screen_distance_squared(screen.position, mouse_local) <=
             pick_radius_pixels * pick_radius_pixels;
}

void ViewportNativeToolInteraction::rebase_linear_array_drag(
    LinearArrayHandlePress &press, const ViewportCameraController &camera,
    QVec2 pointer, const LinearArrayHandleState &handle, bool vertical) {
  press.motion_endpoint = handle.endpoint_world;
  press.motion_pointer = pointer;
  press.motion_constraint = handle.constraint;
  press.vertical = handle.constraint == LinearArrayAxisConstraint::Free && vertical;
  press.motion_started = false;
  const QVec3 axis = linear_array_drag_axis(handle.constraint, press.vertical);
  const QVec3 ray_direction = normalize_or_zero(
      camera.ray_from_viewport_point(pointer).direction);
  // Freeze the fallback for this motion segment. End-on axes and edge-on
  // planes must not amplify tiny changes or switch mappings under the cursor.
  constexpr float kFiveDegreeSine = 0.08715574f;
  constexpr float kFiveDegreeCosineSquared = 0.99240388f;
  const float alignment = dot(axis, ray_direction);
  press.screen_fallback = length_squared(axis) > 0.5f
      ? alignment * alignment >= kFiveDegreeCosineSquared
      : std::abs(ray_direction.y) <= kFiveDegreeSine;
}

QVec3 ViewportNativeToolInteraction::linear_array_drag_point(
    const ViewportCameraController &camera, QVec2 mouse_local,
    const LinearArrayHandlePress &press) {
  const QVec3 axis = linear_array_drag_axis(press.motion_constraint,
                                           press.vertical);
  if (!press.screen_fallback) {
    const ViewportRay start_ray = camera.ray_from_viewport_point(press.motion_pointer);
    const ViewportRay ray = camera.ray_from_viewport_point(mouse_local);
    if (length_squared(axis) > 0.5f) {
      const auto start = quader_editor_core::axis_distance_from_ray(
          axis, press.motion_endpoint, {start_ray.origin, start_ray.direction});
      const auto current = quader_editor_core::axis_distance_from_ray(
          axis, press.motion_endpoint, {ray.origin, ray.direction});
      if (start.ok && current.ok) {
        return press.motion_endpoint +
               axis * static_cast<float>(current.distance - start.distance);
      }
    } else {
      const auto start = quader_editor_core::horizontal_drag_point(
          {start_ray.origin, start_ray.direction}, press.motion_endpoint);
      const auto current = quader_editor_core::horizontal_drag_point(
          {ray.origin, ray.direction}, press.motion_endpoint);
      if (start.ok && current.ok) {
        return press.motion_endpoint + current.point - start.point;
      }
    }
    return press.motion_endpoint;
  }

  const ViewportCameraBasis basis = ViewportCameraAdapter::basis(camera);
  const ViewportCameraState &state = camera.state();
  const quader_editor_core::CameraDragState drag_camera{
      .valid = true,
      .projection = state.projection == ViewportProjection::Orthographic
          ? quader_editor_core::CameraProjection::Orthographic
          : quader_editor_core::CameraProjection::Perspective,
      .origin = basis.eye,
      .right = basis.right,
      .up = basis.up,
      .forward = basis.forward,
      .viewport_size = camera.viewport_size(),
      .orthographic_size = state.orthographic_size,
      .fov_degrees = state.fov_degrees,
  };
  if (length_squared(axis) > 0.5f) {
    // Looking along an axis gives it no usable screen direction. Upward drag
    // increases that axis, at the same world-per-pixel scale as the viewport.
    const double units_per_pixel = quader_editor_core::drag_world_units_per_pixel(
        drag_camera, press.motion_endpoint);
    return press.motion_endpoint + axis * static_cast<float>(
        (press.motion_pointer.y - mouse_local.y) * units_per_pixel);
  }
  return press.motion_endpoint + quader_editor_core::screen_fallback_move_delta(
      drag_camera, mouse_local, press.motion_pointer, press.motion_endpoint, false);
}

} // namespace quader
