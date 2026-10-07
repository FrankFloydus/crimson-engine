////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/transform_gizmo_interaction.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_operation_input_context.hpp"
#include "core/logger.hpp"
#include "core/release_features.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/viewport/viewport_surface_occlusion_policy.hpp"
#include "editor/viewport/viewport_vertex_snap_aggregation.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/render_signature_builder.hpp"
#include "scene/scene_transform.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_pixel_scale.hpp"
#include "viewport/viewport_settings.hpp"

#include <editor/interactions/selection_box.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <optional>
#include <utility>

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

quader_poly::Transform3 runtime_orientation(
    const QuaderTransform3 &orientation) {
  return {
      .x_axis = orientation.x_axis,
      .y_axis = orientation.y_axis,
      .z_axis = orientation.z_axis,
      .origin = orientation.origin,
  };
}

quader_editor_core::TransformGizmoFrameInput
make_gizmo_input(const ViewportCameraController &camera,
                  const ViewportSettings &settings, QVec2 viewport_size,
                  int grid_preset, float raster_scale, ViewportToolId tool,
                  QVec3 pivot, quader_poly::Transform3 orientation,
                  quader_editor_core::TransformAxis active_axis,
                  quader_editor_core::TransformAxis hover_axis) {
  const float viewport_pixel_scale =
      viewport_sanitized_pixel_scale(raster_scale);
  quader_editor_core::TransformGizmoFrameInput input;
  input.tool = viewport_tool_interaction_tool(tool);
  input.active_axis = active_axis;
  input.hover_axis = hover_axis;
  input.has_selection = true;
  input.has_pivot = true;
  input.pivot = pivot;
  input.orientation = orientation;
  input.camera = ViewportCameraAdapter::runtime_camera(
      camera, viewport_size);
  input.grid_size =
      viewport_grid_world_size_for_preset(grid_preset);
  input.gizmo_size_pixels =
      kOverlayTransformGizmoSizePixels * viewport_pixel_scale;
  input.pick_radius_pixels =
      kOverlayTransformGizmoPickRadiusPixels * viewport_pixel_scale;
  input.move_center_radius_pixels =
      kOverlayTransformGizmoMoveCenterRadiusPixels * viewport_pixel_scale;
  input.gizmo_scale = std::max(
      settings.float_value(ViewportFloatSettingId::GizmoSize), 0.1f);
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

struct CurveControlSnapResolution {
  quader_editor_core::TransformSnapTarget target;
  double ray_distance = 0.0;
  double pick_distance = 0.0;
};

std::optional<CurveControlSnapResolution> resolve_curve_control_snap_target(
    const ViewportInteractionFrame &frame, const ViewportRay &ray,
    double vertex_radius,
    std::span<const ViewportCurveAnchorSnapCandidate> curve_candidates,
    SceneSurfaceQueryScratch &surface_query_scratch) {
  std::optional<double> surface_ray_distance;
  if (frame.scene_surfaces != nullptr) {
    const std::optional<SceneSurfaceHit> surface =
        frame.scene_surfaces->nearest_hit({
            .world_origin = ray.origin,
            .world_direction = ray.direction,
            .eligibility = SceneSurfaceEligibility::RenderVisible,
        }, surface_query_scratch);
    if (surface.has_value()) {
      surface_ray_distance = surface->distance;
    }
  }

  std::optional<ViewportMeshVertexSnapTarget> mesh_target;
  if (frame.scene_mesh_snap_query) {
    const std::optional<double> maximum_mesh_ray_distance =
        surface_ray_distance.has_value()
            ? std::optional<double>{
                  *surface_ray_distance +
                  kViewportSurfaceOcclusionTolerance}
            : std::nullopt;
    mesh_target =
        frame
            .scene_mesh_snap_query(ray, vertex_radius,
                                   maximum_mesh_ray_distance)
            .vertex_target;
  }

  const std::optional<ViewportCurveAnchorSnapTarget> curve_target =
      curve_candidates.empty()
          ? std::nullopt
          : resolve_viewport_curve_anchor_snap_target(
                ray, curve_candidates, vertex_radius, {},
                surface_ray_distance);
  const auto resolved =
      aggregate_viewport_vertex_snap_targets(mesh_target, curve_target,
                                             surface_ray_distance);
  if (!resolved.has_value()) {
    return std::nullopt;
  }

  if (resolved->domain == ViewportVertexSnapDomain::Mesh) {
    const auto &mesh = std::get<ViewportMeshVertexSnapTarget>(resolved->source);
    return CurveControlSnapResolution{
        .target =
            {
                .kind =
                    quader_editor_core::TransformSnapLandmarkKind::Vertex,
                .object_id = mesh.modeling_object_id,
                .vertex_id = mesh.vertex_id,
                .position = resolved->world_position,
            },
        .ray_distance = resolved->ray_distance,
        .pick_distance = resolved->pick_distance,
    };
  }

  return CurveControlSnapResolution{
      .target =
          {
              .kind = quader_editor_core::TransformSnapLandmarkKind::Vertex,
              // World-space overlay projection does not resolve Curve anchors
              // through the polygon object registry.
              .object_id = -1,
              .vertex_id = quader_poly::kInvalidElementId,
              .position = resolved->world_position,
          },
      .ray_distance = resolved->ray_distance,
      .pick_distance = resolved->pick_distance,
  };
}

QuaderTransform3 curve_control_orientation(
    const CurveToolPreviewState &preview, QuaderTransformSpace space) {
  QuaderTransform3 orientation;
  if (space != QuaderTransformSpace::Local) {
    return orientation;
  }
  const std::optional<glm::mat3> basis =
      try_scene_pure_world_rotation_basis(preview.world_transform);
  if (!basis.has_value()) {
    return orientation;
  }
  orientation.x_axis = {(*basis)[0][0], (*basis)[0][1], (*basis)[0][2]};
  orientation.y_axis = {(*basis)[1][0], (*basis)[1][1], (*basis)[1][2]};
  orientation.z_axis = {(*basis)[2][0], (*basis)[2][1], (*basis)[2][2]};
  return orientation;
}

} // namespace

const quader_editor_core::TransformGizmoFrame *
TransformGizmoPresentation::prepare(
    QuaderEditorApi &editor, const TransformGizmoPresentationInput &input) {
  const ViewportCurveInteractionPort *curve =
      release_has_feature(ReleaseFeature::Curves) &&
              input.curve_control_route_available
          ? input.curve_tool
          : nullptr;
  const CurveToolPreviewState *curve_preview =
      curve != nullptr && curve->controls_editable() &&
              !curve->point_construction_dragging() &&
              !curve->control_selection().empty()
          ? &curve->preview_state()
          : nullptr;
  if (curve_preview != nullptr &&
      (!curve_preview->active || !curve_preview->document.has_value())) {
    curve_preview = nullptr;
  }
  if (!input.settings.bool_value(ViewportBoolSettingId::ShowGizmos) ||
      (curve_preview == nullptr &&
       !viewport_tool_supports_transform_gizmo(input.tool))) {
    frame_signature_.reset();
    frame_.reset();
    return nullptr;
  }

  // Gate scene queries before reading bounds, selected controls, or orientation.
  // Camera changes only invalidate the projected frame below.
  const auto &selection = input.selection_state;
  RenderSignatureBuilder selection_signature;
  selection_signature.add_value(input.source_identity)
      .add_value(reinterpret_cast<std::uintptr_t>(&editor))
      .add_value(selection.scene_revision)
      .add_value(selection.asset_revision)
      .add_value(selection.pivot_revision)
      .add_value(selection.summary.content_revision)
      .add_value(selection.summary.selection_revision)
      .add_value(static_cast<std::uint64_t>(selection.summary.mode))
      .add_bool(selection.summary.has_selection)
      .add_value(input.transform_preview_revision)
      .add_value(static_cast<std::uint64_t>(input.transform_space))
      .add_bool(curve_preview != nullptr);
  if (curve_preview != nullptr) {
    selection_signature.add_value(curve->control_selection_revision())
        .add_value(curve_preview->document->content_identity().value)
        .add_value(curve_preview->entity)
        .add_value(static_cast<std::uint64_t>(curve_preview->modeling_object_id))
        .add_mat4(curve_preview->world_transform)
        .add_bool(curve_preview->drawing);
  }
  if (selection_signature_ != selection_signature.value()) {
    selection_signature_ = selection_signature.value();
    pivot_.reset();
    orientation_ = {};
    curve_controls_ = false;
    if (curve_preview != nullptr) {
      ++selection_pivot_recompute_count_;
      pivot_ = curve->selected_controls_world_pivot();
      if (pivot_.has_value()) {
        curve_controls_ = true;
        orientation_ =
            curve_control_orientation(*curve_preview, input.transform_space);
      }
    }
    if (!pivot_.has_value() && selection.summary.has_selection) {
      ++selection_pivot_recompute_count_;
      pivot_ = editor.query().selection_transform_pivot();
      if (input.transform_space == QuaderTransformSpace::Local) {
        orientation_ = editor.query().selection_transform_orientation()
                           .value_or(QuaderTransform3{});
      }
    }
  }
  if (!pivot_.has_value()) {
    frame_signature_.reset();
    frame_.reset();
    return nullptr;
  }

  const auto &camera = input.camera.state();
  const auto pose = input.camera.pose();
  const ViewportToolId tool = curve_controls_ ? ViewportToolId::Move : input.tool;
  const auto *shared_frame = input.shared_interaction_frame;
  const auto *shared_info =
      shared_frame != nullptr && shared_frame->info.ok &&
              shared_frame->info.tool == viewport_tool_interaction_tool(tool)
          ? &shared_frame->info
          : nullptr;
  RenderSignatureBuilder frame_signature;
  frame_signature.add_value(*selection_signature_)
      .add_value(static_cast<std::uint64_t>(tool))
      .add_value(input.pane_settings_revision)
      .add_value(static_cast<std::uint64_t>(input.grid_preset))
      .add_float(viewport_sanitized_pixel_scale(input.raster_scale))
      .add_float(input.settings.float_value(ViewportFloatSettingId::GizmoSize))
      .add_vec2(input.camera.viewport_size())
      .add_vec3(pose.eye)
      .add_vec3(pose.target)
      .add_vec3(pose.up)
      .add_value(static_cast<std::uint64_t>(camera.projection))
      .add_float(camera.orthographic_size)
      .add_float(camera.fov_degrees)
      .add_float(camera.near_plane)
      .add_float(camera.far_plane)
      .add_bool(shared_info != nullptr);
  if (shared_info != nullptr) {
    frame_signature
        .add_value(static_cast<std::uint64_t>(shared_info->active_axis))
        .add_value(static_cast<std::uint64_t>(shared_info->hover_axis))
        .add_vec3(shared_info->pivot)
        .add_vec3(shared_info->orientation.x_axis)
        .add_vec3(shared_info->orientation.y_axis)
        .add_vec3(shared_info->orientation.z_axis)
        .add_vec3(shared_info->orientation.origin)
        .add_bool(shared_info->rotate_indicator.active)
        .add_value(static_cast<std::uint64_t>(shared_info->rotate_indicator.axis))
        .add_vec3(shared_info->rotate_indicator.world_axis)
        .add_vec3(shared_info->rotate_indicator.start_direction)
        .add_bool(shared_info->rotate_indicator.has_mouse_position)
        .add_vec3(shared_info->rotate_indicator.mouse_position)
        .add_value(std::bit_cast<std::uint64_t>(
            shared_info->rotate_indicator.angle_degrees));
  }
  if (frame_signature_ != frame_signature.value()) {
    frame_signature_ = frame_signature.value();
    ++frame_build_count_;
    const auto gizmo_input = make_gizmo_input(
        input.camera, input.settings, input.camera.viewport_size(),
        input.grid_preset, input.raster_scale, tool,
        shared_info != nullptr ? shared_info->pivot : *pivot_,
        shared_info != nullptr ? shared_info->orientation
                               : runtime_orientation(orientation_),
        shared_info != nullptr ? shared_info->active_axis
                               : quader_editor_core::TransformAxis::None,
        shared_info != nullptr ? shared_info->hover_axis
                               : quader_editor_core::TransformAxis::None);
    frame_ = quader_editor_core::build_transform_gizmo_frame(gizmo_input);
    if (shared_info != nullptr) {
      frame_->info.rotate_indicator = shared_info->rotate_indicator;
    }
  }
  return frame_.has_value() && frame_->info.ok ? &*frame_ : nullptr;
}

void TransformGizmoPresentation::reset() {
  selection_signature_.reset();
  frame_signature_.reset();
  pivot_.reset();
  orientation_ = {};
  curve_controls_ = false;
  frame_.reset();
  frame_build_count_ = 0;
  selection_pivot_recompute_count_ = 0;
}

std::optional<TransformGizmoInteraction::CurveControlSelectionSnapshot>
TransformGizmoInteraction::selected_curve_controls(
    ViewportCurveInteractionPort *curve_tool,
    QuaderTransformSpace transform_space) {
  if (curve_tool == nullptr || !curve_tool->controls_editable() ||
      curve_tool->point_construction_dragging()) {
    reset_curve_control_cache();
    return std::nullopt;
  }
  const CurveControlSelection &selection = curve_tool->control_selection();
  if (selection.empty()) {
    reset_curve_control_cache();
    return std::nullopt;
  }
  const CurveToolPreviewState &preview = curve_tool->preview_state();
  if (!preview.active || !preview.document.has_value()) {
    reset_curve_control_cache();
    return std::nullopt;
  }
  const CurveControlSelectionIdentity identity{
      .active = selection.active,
      .selected_count = selection.controls.size(),
      .selection_revision = curve_tool->control_selection_revision(),
      .entity = preview.entity,
      .modeling_object_id = preview.modeling_object_id,
      .drawing = preview.drawing,
  };
  const CurveControlCacheKey key{
      .identity = identity,
      .content_identity = preview.document->content_identity().value,
      .world_transform = preview.world_transform,
      .transform_space = transform_space,
  };
  const auto identity_matches =
      [](const CurveControlSelectionIdentity &left,
         const CurveControlSelectionIdentity &right) {
    return left.active == right.active &&
           left.selected_count == right.selected_count &&
           left.selection_revision == right.selection_revision &&
           left.entity == right.entity &&
           left.modeling_object_id == right.modeling_object_id &&
           left.drawing == right.drawing;
  };
  if (cached_curve_control_key_.has_value() &&
      identity_matches(cached_curve_control_key_->identity, key.identity) &&
      cached_curve_control_key_->content_identity == key.content_identity &&
      cached_curve_control_key_->world_transform.values ==
          key.world_transform.values &&
      cached_curve_control_key_->transform_space == key.transform_space) {
    ++selection_cache_stats_.curve_control_cache_hit_count;
    return cached_curve_control_snapshot_;
  }

  ++selection_cache_stats_.curve_control_pivot_recompute_count;
  const std::optional<QVec3> world_pivot =
      curve_tool->selected_controls_world_pivot();
  if (!world_pivot.has_value()) {
    // Retain a negative resolution for this exact authored publication. This
    // prevents a hidden zero-length handle from triggering a point scan every
    // idle frame while still invalidating on geometry or placement changes.
    cached_curve_control_key_ = key;
    cached_curve_control_snapshot_.reset();
    return std::nullopt;
  }
  CurveControlSelectionSnapshot snapshot{
      .identity = identity,
      .world_pivot = *world_pivot,
      .orientation = curve_control_orientation(preview, transform_space),
      .anchors_only = std::all_of(selection.controls.begin(),
                                  selection.controls.end(), [](const auto &control) {
        return control.kind == CurveControlKind::Anchor;
      }),
  };
  cached_curve_control_key_ = key;
  cached_curve_control_snapshot_ = snapshot;
  return snapshot;
}

bool TransformGizmoInteraction::curve_control_selection_identity_matches(
    ViewportCurveInteractionPort *curve_tool,
    const CurveControlSelectionIdentity &identity) {
  if (curve_tool == nullptr || !curve_tool->controls_editable()) {
    return false;
  }
  const CurveToolPreviewState &preview = curve_tool->preview_state();
  return preview.active && preview.document.has_value() &&
         preview.control_selection.active == identity.active &&
         preview.control_selection.controls.size() ==
             identity.selected_count &&
         curve_tool->control_selection_revision() ==
             identity.selection_revision &&
         preview.entity == identity.entity &&
         preview.modeling_object_id == identity.modeling_object_id &&
         preview.drawing == identity.drawing;
}

bool TransformGizmoInteraction::pending_drag_threshold_reached(
    QVec2 current) const {
  if (!pending_drag_.has_value()) {
    return false;
  }
  return viewport_pointer_drag_threshold_reached(pending_drag_->pointer,
                                                 current);
}

bool TransformGizmoInteraction::begin_pending_drag(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    ViewportCurveInteractionPort *curve_tool) {
  QDR_PROFILE_FUNCTION();
  if (!pending_drag_.has_value()) {
    return false;
  }

  const PendingGizmoDrag pending = *pending_drag_;
  if (pending.backend == DragBackend::CurveControl) {
    if (!pending.curve_selection.has_value() ||
        !curve_control_selection_identity_matches(
            curve_tool, *pending.curve_selection)) {
      reset_pending_drag();
      return false;
    }

    const ViewportRay start_ray = frame.camera.ray_from_viewport_point(
        pending.begin_request.screen_position);
    const quader_editor_core::ViewportCameraState camera =
        ViewportCameraAdapter::runtime_camera(frame.camera,
                                              frame.input.viewport_size);
    quader_editor_core::MoveBeginInput input;
    input.axis = pending.axis;
    input.orientation =
        runtime_orientation(pending.begin_request.orientation);
    input.has_world_direction =
        pending.begin_request.has_world_direction;
    input.world_direction = pending.begin_request.world_direction;
    input.center_local = pending.begin_request.pivot;
    input.has_world_center = true;
    input.world_center = pending.begin_request.pivot;
    input.has_screen_axis = true;
    input.screen_pivot = pending.begin_request.screen_pivot;
    input.screen_tip = pending.begin_request.screen_tip;
    input.screen_position = pending.begin_request.screen_position;
    input.has_start_ray = true;
    input.start_ray = {
        .origin = start_ray.origin,
        .direction = start_ray.direction,
    };
    input.has_camera = true;
    input.camera_transform = camera.transform;
    const quader_editor_core::MoveBeginResult begin =
        quader_editor_core::begin_move_drag(input);
    if (!begin.ok) {
      LOG_ERROR("Curve control move begin failed: {}",
                begin.message.empty() ? "Begin failed." : begin.message);
      reset_pending_drag();
      return false;
    }
    const CommandResult authored_begin =
        curve_tool->begin_selected_control_drag();
    if (!authored_begin.ok || !curve_tool->dragging()) {
      LOG_ERROR("Curve control edit begin failed: {}",
                authored_begin.message.empty() ? "Begin failed."
                                               : authored_begin.message);
      quader_editor_core::reset_move(curve_move_state_);
      reset_pending_drag();
      return false;
    }

    const CurveControlSelection &control_selection =
        curve_tool->control_selection();
    curve_anchor_snap_eligible_ =
        !control_selection.empty() &&
        quader_editor_core::move_axis_supports_vertex_snap(pending.axis) &&
        std::all_of(control_selection.controls.begin(),
                    control_selection.controls.end(),
                    [](CurveControlTarget target) {
                      return target.kind == CurveControlKind::Anchor;
                    });
    curve_anchor_snap_candidates_.clear();
    if (curve_anchor_snap_eligible_) {
      std::vector<modeling::CurvePointId> selected_anchors;
      selected_anchors.reserve(control_selection.controls.size());
      for (const CurveControlTarget target : control_selection.controls) {
        selected_anchors.push_back(target.point);
      }
      std::sort(selected_anchors.begin(), selected_anchors.end());
      selected_anchors.erase(
          std::unique(selected_anchors.begin(), selected_anchors.end()),
          selected_anchors.end());
      const EntityId active_entity = pending.curve_selection->entity;
      curve_anchor_snap_candidates_ =
          collect_viewport_curve_anchor_snap_candidates(
              frame.scene_curve_pick_candidates,
              [active_entity, &selected_anchors](
                  EntityId entity, modeling::CurvePointId point) {
                return entity == active_entity &&
                       std::binary_search(selected_anchors.begin(),
                                          selected_anchors.end(), point);
              });
      if (auto draft = collect_viewport_curve_draft_anchor_snap_candidate(
              curve_tool->preview_state(),
              [&selected_anchors](modeling::CurvePointId point) {
                return std::binary_search(selected_anchors.begin(),
                                          selected_anchors.end(), point);
              })) {
        curve_anchor_snap_candidates_.push_back(std::move(*draft));
      }
    }

    curve_move_state_ = begin.state;
    dragging_ = true;
    active_operation_ = QuaderTransformOperation::Transform;
    active_duplicate_ = false;
    active_backend_ = DragBackend::CurveControl;
    active_curve_selection_ = pending.curve_selection;
    active_axis_ = pending.axis;
    active_drag_pivot_ = pending.begin_request.pivot;
    active_world_direction_ = pending.begin_request.world_direction;
    active_orientation_ =
        runtime_orientation(pending.begin_request.orientation);
    reset_pending_drag();
    return true;
  }

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
  active_operation_ = dragging_ ? pending.begin_request.operation
                               : QuaderTransformOperation::Transform;
  active_duplicate_ = dragging_ && pending.begin_request.duplicate_selected;
  active_backend_ = dragging_ ? DragBackend::EditorTransform
                              : DragBackend::None;
  active_axis_ =
      dragging_ ? pending.axis : quader_editor_core::TransformAxis::None;
  active_drag_pivot_ = dragging_ ? begin.value().pivot : QVec3{};
  active_world_direction_ =
      dragging_ ? pending.begin_request.world_direction : QVec3{};
  active_orientation_ =
      dragging_ ? runtime_orientation(pending.begin_request.orientation)
                : quader_poly::Transform3{};
  rotation_indicator_start_direction_ =
      dragging_ && pending.axis != quader_editor_core::TransformAxis::All &&
              viewport_tool_interaction_tool(pending.tool) ==
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
    if (pending_drag_->backend == DragBackend::CurveControl) {
      // The curve control is already selected. Its gizmo click must not fall
      // through to scene selection when the pointer stays below threshold.
      result.gizmo_consumed = true;
    } else {
      result.selection_click_requested = true;
      result.selection_click_local = pending_drag_->click_local;
      result.selection_click_count = pending_drag_->click_count;
      result.selection_click_modifiers = pending_drag_->pointer.modifiers;
      result.selection_cycle_depth = pending_drag_->cycle_depth;
      result.selection_cycle_depth_modifiers =
          pending_drag_->cycle_depth_modifiers;
    }
  }
  reset_pending_drag();
  return result;
}

ViewportBeforeToolInteractionResult
TransformGizmoInteraction::handle_frame(QuaderEditorApi &editor,
                                        const ViewportInteractionFrame &frame,
                                        ViewportCurveInteractionPort *curve_tool) {
  QDR_PROFILE_FUNCTION();
  if (!release_has_feature(ReleaseFeature::Curves)) {
    curve_tool = nullptr;
  }
  ViewportBeforeToolInteractionResult result;

  bool curve_refresh_ok = true;
  if (curve_tool != nullptr) {
    const CommandResult refreshed = curve_tool->refresh();
    curve_refresh_ok = refreshed.ok;
    if (!refreshed.ok) {
      LOG_ERROR("Curve control refresh failed: {}",
                refreshed.message.empty() ? "Refresh failed."
                                          : refreshed.message);
    }
  }

  if (dragging_ && active_backend_ == DragBackend::CurveControl &&
      (!curve_refresh_ok || curve_tool == nullptr || !curve_tool->dragging() ||
       !active_curve_selection_.has_value() ||
       !curve_control_selection_identity_matches(
           curve_tool, *active_curve_selection_))) {
    result.gizmo_consumed = cancel_active_drag(editor, curve_tool);
    clear_frame();
    return result;
  }

  if (frame.input.pointer_interaction_canceled &&
      (pending_drag_.has_value() || dragging_)) {
    result.gizmo_consumed = frame.input_result.cancel_requested
                                ? cancel_active_drag(editor, curve_tool)
                                : finish_active_drag(editor, curve_tool);
    clear_frame();
    return result;
  }

  const bool curve_pointer_active =
      (dragging_ && active_backend_ == DragBackend::CurveControl) ||
      (pending_drag_.has_value() &&
       pending_drag_->backend == DragBackend::CurveControl);
  const bool curve_pointer_invalidated =
      frame.input.navigation_active || frame.input_result.navigation_active ||
      frame.input_result.camera_changed || frame.input_result.view_changed;
  if (curve_pointer_active && curve_pointer_invalidated) {
    result.gizmo_consumed = finish_active_drag(editor, curve_tool);
    clear_frame();
    return result;
  }

  if (pending_drag_.has_value() && !frame.input.viewport_focused) {
    // Qt focus loss synthesizes a release while the surface can remain hovered.
    // Dropping ownership must not turn that release into an ordinary front pick.
    reset_pending_drag();
    clear_frame();
    result.gizmo_consumed = true;
    return result;
  }

  if (dragging_ && !frame.input.viewport_focused) {
    // Moving keyboard focus alone does not author a new sample or settle the
    // operation. A lost pointer/release does finish the last authored sample.
    result.gizmo_consumed = true;
    if (frame.left_released || !frame.left_down) {
      (void)finish_active_drag(editor, curve_tool);
    }
    return result;
  }

  if (dragging_ && curve_pointer_invalidated) {
    result.gizmo_consumed = finish_active_drag(editor, curve_tool);
    clear_frame();
    return result;
  }

  if (pending_drag_.has_value() &&
      pending_drag_->backend == DragBackend::CurveControl &&
      (!curve_refresh_ok || !pending_drag_->curve_selection.has_value() ||
       !curve_control_selection_identity_matches(
           curve_tool, *pending_drag_->curve_selection))) {
    reset_pending_drag();
    clear_frame();
    result.gizmo_consumed = true;
    return result;
  }

  const ViewportToolId active_tool = frame.input_router.state().active_tool;
  const bool explicit_curve_route =
      active_tool == ViewportToolId::Curve && curve_tool != nullptr &&
      curve_tool->active();
  const bool passive_curve_route =
      frame.active_tool_allows_selection_fallback &&
      !frame.input.active_tool_owns_viewport && !frame.input.modal_tool_active;
  const bool curve_control_route_available =
      explicit_curve_route || passive_curve_route;
  const std::optional<CurveControlSelectionSnapshot> curve_controls =
      curve_control_route_available && !dragging_ && !pending_drag_.has_value()
          ? selected_curve_controls(curve_tool,
                                    editor.viewports().transform_space())
          : std::nullopt;
  const bool curve_route =
      (dragging_ && active_backend_ == DragBackend::CurveControl) ||
      (pending_drag_.has_value() &&
       pending_drag_->backend == DragBackend::CurveControl) ||
      curve_controls.has_value();
  const ViewportToolId tool =
      curve_route ? ViewportToolId::Move : active_tool;
  presented_curve_route_ = curve_route;
  presented_curve_anchors_only_ =
      dragging_ ? curve_anchor_snap_eligible_
                : (curve_controls.has_value() ? curve_controls->anchors_only
                   : (cached_curve_control_snapshot_.has_value() &&
                      cached_curve_control_snapshot_->anchors_only));
  if (!frame.settings.bool_value(ViewportBoolSettingId::ShowGizmos) ||
      (!curve_route && !viewport_tool_supports_transform_gizmo(tool))) {
    clear_frame();
    reset_pending_drag();
    if (dragging_) {
      result.gizmo_consumed = finish_active_drag(editor, curve_tool);
    }
    return result;
  }

  std::optional<QuaderSelectionSummary> selection;
  QVec3 pivot = {};
  quader_poly::Transform3 orientation;
  QuaderTransform3 request_orientation;
  if (dragging_) {
    pivot = active_drag_pivot_;
    orientation = active_orientation_;
  } else if (pending_drag_.has_value()) {
    pivot = pending_drag_->begin_request.pivot;
    orientation = runtime_orientation(pending_drag_->begin_request.orientation);
  } else if (curve_controls.has_value()) {
    pivot = curve_controls->world_pivot;
    request_orientation = curve_controls->orientation;
    orientation = runtime_orientation(request_orientation);
  } else {
    selection = frame.selection_state.summary;
    const bool selection_changed =
        !cached_selection_state_.has_value() ||
        !selection_states_match(*cached_selection_state_,
                                frame.selection_state);
    const std::uint64_t transform_space_revision =
        editor.viewports().transform_space_revision();
    const bool transform_space_changed =
        !has_cached_transform_space_revision_ ||
        cached_transform_space_revision_ != transform_space_revision;
    if (selection_changed) {
      cached_selection_state_ = frame.selection_state;
      cached_selection_pivot_.reset();
      if (selection->has_selection) {
        ++selection_cache_stats_.selection_pivot_recompute_count;
        cached_selection_pivot_ =
            editor.query().selection_transform_pivot();
      }
    }
    if (selection_changed || transform_space_changed) {
      cached_transform_space_revision_ = transform_space_revision;
      has_cached_transform_space_revision_ = true;
      cached_selection_orientation_ = {};
      if (selection->has_selection &&
          editor.viewports().transform_space() == QuaderTransformSpace::Local) {
        cached_selection_orientation_ =
            editor.query().selection_transform_orientation().value_or(
                QuaderTransform3{});
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
    request_orientation = cached_selection_orientation_;
    orientation = runtime_orientation(request_orientation);
  }

  const bool suppress_hover =
      !frame.input.viewport_focused || frame.input.pointer_interaction_canceled ||
      frame.input.navigation_active || frame.input_result.navigation_active ||
      frame.input_result.camera_changed || frame.input.text_input_active ||
      frame.input.modal_tool_active ||
      (!curve_route && frame.input.active_tool_owns_viewport);

  // Passive Curve editing keeps the ordinary object gizmo visible, but an
  // unselected authored control under this exact press still owns selection.
  // A semantic conversion press also retains priority on the selected anchor.
  // Use the same typed, mesh-depth-aware picker as shared selection so an
  // anchor at the object pivot cannot be stolen by the gizmo.
  const bool curve_control_press_has_priority = [&]() {
    if (!curve_control_route_available || curve_tool == nullptr ||
        !curve_tool->controls_editable() ||
        dragging_ || pending_drag_.has_value() || !frame.left_pressed ||
        !frame.input.viewport_hovered || suppress_hover ||
        frame.input.pointer_interaction_canceled ||
        frame.input_result.cancel_requested ||
        frame.input_result.navigation_active ||
        frame.input_result.camera_changed || frame.input_result.view_changed ||
        curve_tool->dragging()) {
      return false;
    }
    const bool conversion_press =
        !frame.input.curve_conversion_edges.empty();
    const double control_pick_radius_pixels =
        9.0 * static_cast<double>(
                  viewport_sanitized_pixel_scale(frame.viewport_pixel_scale));
    const auto hit = frame.scene_surfaces != nullptr
                         ? pick_unoccluded_viewport_curve_control(
                               frame.camera, curve_tool->preview_state(),
                               frame.left_click_local, *frame.scene_surfaces,
                               conversion_press, control_pick_radius_pixels)
                         : pick_viewport_curve_control(
                               frame.camera, curve_tool->preview_state(),
                               frame.left_click_local, conversion_press,
                               control_pick_radius_pixels);
    return hit.has_value() &&
           (conversion_press || curve_tool->control_selection().empty() ||
            !curve_tool->control_selection().contains(hit->target));
  }();

  quader_editor_core::TransformGizmoFrameInput gizmo_input = make_gizmo_input(
      frame.camera, frame.settings, frame.input.viewport_size,
      frame.input.grid_preset, frame.viewport_pixel_scale, tool, pivot, orientation,
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
      frame.input.viewport_hovered && !suppress_hover &&
      !curve_control_press_has_priority) {
    const quader_editor_core::TransformGizmoPickHit hit = [&]() {
      QDR_PROFILE_SCOPE("Transform gizmo click pick");
      return quader_editor_core::pick_transform_gizmo(gizmo_input,
                                                      frame.left_click_local);
    }();
    if (hit.hit && hit.axis != quader_editor_core::TransformAxis::None) {
      const ViewportRay start_ray =
          frame.camera.ray_from_viewport_point(frame.left_click_local);
      const bool curve_control_backend = curve_controls.has_value();
      const bool duplicate_selected =
          !curve_control_backend && selection.has_value() &&
          !is_component_selection_mode(
              to_modeling_selection_mode(selection->mode)) &&
          (frame.input.modifiers.shift || frame.input.modifiers.alt);
      const QuaderSelectionMode selection_mode =
          selection.has_value() ? selection->mode : QuaderSelectionMode::Object;
      const QuaderTransformOperation operation =
          curve_control_backend
              ? QuaderTransformOperation::Transform
              : transform_operation_for(tool, selection_mode,
                                        frame.input.modifiers);
      pending_drag_ = PendingGizmoDrag{
          .pointer = viewport_pointer_gesture_from_latest_edge(
              frame.input.pointer_pressed_edges, QuaderMouseButton::Left,
              frame.left_click_local, frame.input.mouse_screen,
              frame.input.modifiers, frame.pointer_drag_threshold_pixels),
          .begin_request =
              {
                  .tool = api_transform_tool_for(tool),
                  .operation = operation,
                  .axis = api_axis_for(hit.axis),
                  .space = editor.viewports().transform_space(),
                  .orientation = request_orientation,
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
                  .rotation_snap_degrees =
                      viewport_rotation_snap_active(
                          frame.settings, frame.input.modifiers.primary)
                          ? frame.settings.float_value(
                                ViewportFloatSettingId::RotationSnapDegrees)
                          : 0.0,
                  .precision = frame.input.modifiers.primary,
                  .duplicate_selected = duplicate_selected,
              },
          .tool = tool,
          .axis = hit.axis,
          .click_local = frame.left_click_local,
          .click_count = std::max(1, frame.left_click_count),
          .cycle_depth = frame.input.cycle_selection_depth_pressed,
          .cycle_depth_modifiers = frame.input.cycle_selection_depth_modifiers,
          .backend = curve_route ? DragBackend::CurveControl
                                 : DragBackend::EditorTransform,
          .curve_selection =
              curve_controls.has_value()
                  ? std::optional<CurveControlSelectionIdentity>(
                        curve_controls->identity)
                  : std::nullopt,
      };
      result.gizmo_consumed = true;
    }
  }

  QVec2 drag_local = frame.input.mouse_local;
  if (pending_drag_.has_value()) {
    // Preserve a newer double-click count even outside Mesh depth cycling:
    // component loops also need the second press coalesced with the first release.
    if ((frame.left_pressed || frame.left_released) &&
        (frame.input.cycle_selection_depth_pressed ||
         frame.left_click_count > pending_drag_->click_count)) {
      pending_drag_->cycle_depth = pending_drag_->cycle_depth ||
                                   frame.input.cycle_selection_depth_pressed;
      if (frame.input.cycle_selection_depth_pressed) {
        pending_drag_->cycle_depth_modifiers =
            frame.input.cycle_selection_depth_modifiers;
      }
      pending_drag_->click_count =
          std::max(pending_drag_->click_count, frame.left_click_count);
      pending_drag_->click_local = frame.left_click_local;
    }
    result.gizmo_consumed = true;
    const bool release_requested = frame.left_released || !frame.left_down;
    if (const auto *release = viewport_pointer_edge_after(
            frame.input.pointer_released_edges, pending_drag_->pointer.press)) {
      // A host frame may already contain hover after this physical release.
      // Only the release can promote a pending handle click into a drag.
      drag_local = release->local;
    }
    const bool threshold_reached =
        pending_drag_threshold_reached(drag_local);
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
    if (!begin_pending_drag(editor, frame, curve_tool)) {
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
        frame.camera.ray_from_viewport_point(drag_local);
    const bool grid_snap_active = viewport_grid_snap_active(
        frame.settings, frame.input.modifiers.primary);
    const double rotation_snap_degrees =
        viewport_rotation_snap_active(frame.settings,
                                      frame.input.modifiers.primary)
            ? frame.settings.float_value(
                  ViewportFloatSettingId::RotationSnapDegrees)
            : 0.0;
    const DragUpdateSignature update_signature{
        .screen_position = drag_local,
        .viewport_size = frame.input.viewport_size,
        .grid_preset = frame.input.grid_preset,
        .vertex_snap_radius =
            frame.input.transform_vertex_snap_down
                ? static_cast<float>(viewport_scaled_world_pick_radius(
                      frame.settings.float_value(
                          ViewportFloatSettingId::PickVertexRadius),
                      frame.viewport_pixel_scale,
                      frame.input.device_pixel_ratio))
                : 0.0f,
        .grid_snap_enabled = grid_snap_active,
        .rotation_snap_degrees = rotation_snap_degrees,
        .precision = frame.input.modifiers.primary,
    };
    if (!matches_last_drag_update(update_signature)) {
      if (active_backend_ == DragBackend::CurveControl) {
        snap_overlay_points_.clear();
        std::optional<CurveControlSnapResolution> snap_target;
        if (curve_anchor_snap_eligible_ &&
            update_signature.vertex_snap_radius > 0.0f) {
          ViewportRay snap_ray = drag_ray;
          const float direction_length = length(snap_ray.direction);
          if (direction_length > 0.000001f) {
            snap_ray.direction = snap_ray.direction / direction_length;
            snap_target = resolve_curve_control_snap_target(
                frame, snap_ray, update_signature.vertex_snap_radius,
                curve_anchor_snap_candidates_, surface_query_scratch_);
          }
        }
        const quader_editor_core::MovePreviewResult preview = [&]() {
          QDR_PROFILE_SCOPE("Curve control gizmo update drag");
          if (snap_target.has_value()) {
            return quader_editor_core::preview_move_to_axis_snap_target(
                curve_move_state_, snap_target->target.position);
          }
          return quader_editor_core::preview_move_from_viewport(
              curve_move_state_, drag_local,
              {.origin = drag_ray.origin, .direction = drag_ray.direction},
              ViewportCameraAdapter::runtime_camera(
                  frame.camera, frame.input.viewport_size),
              frame.input.modifiers.primary, grid_snap_active,
              viewport_grid_world_size_for_preset(frame.input.grid_preset),
              false);
        }();
        if (!preview.ok) {
          LOG_ERROR("Curve control move update failed: {}",
                    preview.message.empty() ? "Update failed."
                                            : preview.message);
        } else {
          const QVec3 world_pivot =
              curve_move_state_.center_world + preview.preview_delta;
          const CommandResult authored_update =
              curve_tool->update_drag(world_pivot);
          if (!authored_update.ok) {
            LOG_ERROR("Curve control edit update failed: {}",
                      authored_update.message.empty()
                          ? "Update failed."
                          : authored_update.message);
          } else {
            if (snap_target.has_value()) {
              quader_editor_core::fill_transform_snap_target_overlay_points(
                  snap_overlay_points_, snap_target->target);
            }
            last_drag_update_signature_ = update_signature;
            active_drag_pivot_ = world_pivot;
          }
        }
      } else {
        const QuaderApiResult<QuaderTransformDragPreview> preview = [&]() {
          QDR_PROFILE_SCOPE("Transform gizmo update drag");
          return editor.modeling().update_transform_drag({
              .screen_position = drag_local,
              .ray =
                  {
                      .origin = drag_ray.origin,
                      .direction = drag_ray.direction,
                  },
              .camera = to_api_viewport_camera(
                  ViewportCameraAdapter::modeling_camera(
                      frame.camera, frame.input.viewport_size)),
              .grid_size = viewport_grid_world_size_for_preset(
                  frame.input.grid_preset),
              .vertex_snap_radius = update_signature.vertex_snap_radius,
              .grid_snap_enabled = grid_snap_active,
              .rotation_snap_degrees = rotation_snap_degrees,
              .precision = frame.input.modifiers.primary,
          });
        }();
        if (!preview.ok) {
          LOG_ERROR(
              "Transform drag update failed: {}",
              preview.message.empty() ? "Update failed." : preview.message);
        }
        if (preview.ok || (preview.value().active && !preview.changed())) {
          // A rejected sample can retain the last valid geometry and gesture.
          // Do not retry that unchanged pointer position on every frame.
          last_drag_update_signature_ = update_signature;
        }
        if (preview.ok && preview.value().active) {
          active_drag_pivot_ = preview.value().pivot;
          if (viewport_tool_transform_tool(tool) ==
              ModelingTransformTool::Move) {
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
                  frame.camera, drag_local,
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
    }
    if (release_requested) {
      (void)finish_active_drag(editor, curve_tool);
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
         last.rotation_snap_degrees == signature.rotation_snap_degrees &&
         last.precision == signature.precision;
}

bool TransformGizmoInteraction::finish_active_drag(
    QuaderEditorApi &editor, ViewportCurveInteractionPort *curve_tool) {
  if (pending_drag_.has_value()) {
    // A press that never crossed the drag threshold authored no operation.
    reset_pending_drag();
    return true;
  }
  if (!dragging_) {
    return false;
  }
  std::string error;
  if (active_backend_ == DragBackend::CurveControl) {
    if (curve_tool == nullptr) {
      error = "Curve control drag lost its service owner.";
    } else {
      const CommandResult commit = curve_tool->commit_drag();
      if (!commit.ok) {
        error = commit.message.empty() ? "Curve control edit commit failed."
                                       : commit.message;
      }
    }
  } else {
    const auto commit = editor.modeling().commit_transform_drag();
    if (!commit.ok()) {
      error = commit.error_message().empty() ? "Transform drag commit failed."
                                             : commit.error_message();
    }
  }
  if (!error.empty()) {
    LOG_ERROR("Transform drag commit failed: {}", error);
    (void)editor.ui().notify({.level = QuaderNotificationLevel::Error,
                              .message = error});
    return true;
  }
  reset_drag_state();
  return true;
}

bool TransformGizmoInteraction::cancel_active_drag(QuaderEditorApi &editor) {
  return cancel_active_drag(editor, nullptr);
}

bool TransformGizmoInteraction::cancel_active_drag(
    QuaderEditorApi &editor, ViewportCurveInteractionPort *curve_tool) {
  if (pending_drag_.has_value()) {
    reset_pending_drag();
    return true;
  }
  if (!dragging_) {
    return false;
  }
  if (active_backend_ == DragBackend::CurveControl) {
    if (curve_tool == nullptr) {
      LOG_ERROR("Curve control drag cancel lost its service owner.");
    } else {
      const CommandResult cancel = curve_tool->cancel_drag();
      if (!cancel.ok) {
        LOG_ERROR("Curve control edit cancel failed: {}",
                  cancel.message.empty() ? "Cancel failed."
                                         : cancel.message);
      }
    }
  } else {
    const QuaderApiVoidResult cancel =
        editor.modeling().cancel_transform_drag();
    if (!cancel.ok) {
      LOG_ERROR("Transform drag cancel failed: {}",
                cancel.message.empty() ? "Cancel failed." : cancel.message);
    }
  }
  reset_drag_state();
  return true;
}

void TransformGizmoInteraction::reset(QuaderEditorApi *editor) {
  if (editor != nullptr && dragging_) {
    (void)cancel_active_drag(*editor);
  }
  reset(editor, nullptr);
}

void TransformGizmoInteraction::reset(
    QuaderEditorApi *editor, ViewportCurveInteractionPort *curve_tool) {
  if (dragging_ && active_backend_ == DragBackend::CurveControl &&
      curve_tool != nullptr) {
    const CommandResult cancel = curve_tool->cancel_drag();
    if (!cancel.ok) {
      LOG_ERROR("Curve control edit reset failed: {}",
                cancel.message.empty() ? "Cancel failed." : cancel.message);
    }
    reset_drag_state();
  } else if (editor != nullptr && dragging_) {
    (void)cancel_active_drag(*editor, curve_tool);
  } else {
    reset_drag_state();
  }
  reset_pending_drag();
  clear_frame();
  cached_selection_state_.reset();
  cached_selection_pivot_.reset();
  cached_selection_orientation_ = {};
  cached_transform_space_revision_ = 0;
  has_cached_transform_space_revision_ = false;
  reset_curve_control_cache();
}

const quader_editor_core::TransformGizmoFrame* TransformGizmoInteraction::frame() const {
  if (!current_frame_.has_value() || !current_frame_->info.ok) {
    return nullptr;
  }
  return &*current_frame_;
}

void TransformGizmoInteraction::reset_drag_state() {
  dragging_ = false;
  active_operation_ = QuaderTransformOperation::Transform;
  active_duplicate_ = false;
  active_backend_ = DragBackend::None;
  active_curve_selection_.reset();
  quader_editor_core::reset_move(curve_move_state_);
  curve_anchor_snap_eligible_ = false;
  curve_anchor_snap_candidates_.clear();
  snap_overlay_points_.clear();
  active_axis_ = quader_editor_core::TransformAxis::None;
  active_drag_pivot_ = {};
  active_world_direction_ = {};
  active_orientation_ = {};
  rotation_indicator_start_direction_.reset();
  last_drag_update_signature_.reset();
  active_rotate_indicator_ = {};
}

void TransformGizmoInteraction::reset_pending_drag() { pending_drag_.reset(); }

void TransformGizmoInteraction::reset_curve_control_cache() {
  cached_curve_control_key_.reset();
  cached_curve_control_snapshot_.reset();
}

void TransformGizmoInteraction::clear_frame() {
  current_frame_.reset();
  presented_curve_route_ = false;
  presented_curve_anchors_only_ = false;
  hover_axis_ = quader_editor_core::TransformAxis::None;
}

void TransformGizmoInteraction::populate_operation_input_context(
    QuaderOperationInputContext &context,
    const ViewportInteractionFrame &interaction) const {
  if (frame() == nullptr) return;
  const auto axis = dragging_ ? active_axis_
                    : pending_drag_ ? pending_drag_->axis : hover_axis_;
  using Axis = quader_editor_core::TransformAxis;
  if (axis == Axis::None) return;
  using Tool = quader_editor_core::ViewportInteractionTool;
  const Tool tool = current_frame_->info.tool;
  using Handle = QuaderInputTransformHandle;
  context.transform_handle =
      axis == Axis::All ? (tool == Tool::Rotate ? Handle::Trackball : Handle::Center)
      : (axis == Axis::Xy || axis == Axis::Xz || axis == Axis::Yz)
          ? Handle::Plane : Handle::Axis;
  context.pointer_dragging = dragging_;
  const auto operation = pending_drag_ ? pending_drag_->begin_request.operation
                                      : active_operation_;
  const bool duplicate = pending_drag_ ? pending_drag_->begin_request.duplicate_selected
                                      : active_duplicate_;
  using Operation = QuaderInputTransformOperation;
  context.transform_operation =
      duplicate ? Operation::Duplicate
      : operation == QuaderTransformOperation::Extrude ? Operation::Extrude
      : operation == QuaderTransformOperation::Inset ? Operation::Inset
      : tool == Tool::Rotate ? Operation::Rotate
      : tool == Tool::Scale ? Operation::Scale : Operation::Move;
  const auto &selection = interaction.selection_state.summary;
  const bool object_mode = selection.mode == QuaderSelectionMode::Object ||
                           selection.mode == QuaderSelectionMode::Group;
  const bool surface_mode = selection.mode == QuaderSelectionMode::Edge ||
                            selection.mode == QuaderSelectionMode::Face;
  context.can_duplicate = !presented_curve_route_ && object_mode;
  context.can_component_extrude = !presented_curve_route_ && surface_mode &&
                                 (tool == Tool::Move || tool == Tool::Rotate);
  context.can_component_inset = !presented_curve_route_ && surface_mode &&
                               tool == Tool::Scale;
  context.curve_anchor_selection_only = presented_curve_route_ &&
                                        presented_curve_anchors_only_;
  context.can_vertex_snap = tool == Tool::Move &&
      quader_editor_core::move_axis_supports_vertex_snap(axis) &&
      viewport_vertex_snap_radius(interaction.settings) > 0.0f &&
      (!presented_curve_route_ || presented_curve_anchors_only_);
}

} // namespace quader
