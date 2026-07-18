////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/move_tool.hpp>

#include "tool_math_internal.hpp"

#include <editor/modeling/runtime/editor_tool.hpp>
#include <editor/interactions/transform_drag.hpp>
#include <editor/interactions/tool_drag_math.hpp>
#include <editor/interactions/tool_snapping.hpp>

#include <diagnostics/profile.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader_editor_core {
namespace {

bool transform_axis_is_plane(TransformAxis axis)
{
  return axis == TransformAxis::Xy || axis == TransformAxis::Xz ||
         axis == TransformAxis::Yz;
}

bool transform_axis_is_single_axis(TransformAxis axis)
{
  return axis == TransformAxis::X || axis == TransformAxis::Y ||
         axis == TransformAxis::Z;
}

double axis_coordinate(quader::QVec3 position, TransformAxis axis)
{
	switch (axis) {
        case TransformAxis::X:
          return position.x;
        case TransformAxis::Y:
          return position.y;
        case TransformAxis::Z:
          return position.z;
	default:
		return 0.0;
	}
}

quader::QVec3 transform_plane_normal(TransformAxis axis)
{
  if (axis == TransformAxis::Xy) {
    return {0.0F, 0.0F, 1.0F};
  }
  if (axis == TransformAxis::Xz) {
    return {0.0F, 1.0F, 0.0F};
  }
  if (axis == TransformAxis::Yz) {
    return {1.0F, 0.0F, 0.0F};
  }
        return {};
}

quader::QVec3 plane_drag_delta(TransformAxis axis, quader::QVec3 raw_delta, double grid_size, bool grid_enabled)
{
	quader::QVec3 delta;
        if (axis == TransformAxis::Xy) {
          delta.x = static_cast<float>(
              maybe_snap_world_value(raw_delta.x, grid_size, grid_enabled));
          delta.y = static_cast<float>(
              maybe_snap_world_value(raw_delta.y, grid_size, grid_enabled));
        } else if (axis == TransformAxis::Xz) {
          delta.x = static_cast<float>(
              maybe_snap_world_value(raw_delta.x, grid_size, grid_enabled));
          delta.z = static_cast<float>(
              maybe_snap_world_value(raw_delta.z, grid_size, grid_enabled));
        } else if (axis == TransformAxis::Yz) {
          delta.y = static_cast<float>(
              maybe_snap_world_value(raw_delta.y, grid_size, grid_enabled));
          delta.z = static_cast<float>(
              maybe_snap_world_value(raw_delta.z, grid_size, grid_enabled));
        }
        return delta;
}

quader::QVec3 snap_center_drag_delta(TransformAxis axis, quader::QVec3 center_world, quader::QVec3 raw_delta, double grid_size)
{
	quader::QVec3 delta;
        if (axis == TransformAxis::X || axis == TransformAxis::Xy ||
            axis == TransformAxis::Xz) {
          delta.x = static_cast<float>(
              snap_world_value(
                  static_cast<double>(center_world.x + raw_delta.x),
                  grid_size) -
              center_world.x);
        }
        if (axis == TransformAxis::Y || axis == TransformAxis::Xy ||
            axis == TransformAxis::Yz) {
          delta.y = static_cast<float>(
              snap_world_value(
                  static_cast<double>(center_world.y + raw_delta.y),
                  grid_size) -
              center_world.y);
        }
        if (axis == TransformAxis::Z || axis == TransformAxis::Xz ||
            axis == TransformAxis::Yz) {
          delta.z = static_cast<float>(
              snap_world_value(
                  static_cast<double>(center_world.z + raw_delta.z),
                  grid_size) -
              center_world.z);
        }
        if (axis == TransformAxis::All) {
          delta.x = static_cast<float>(
              snap_world_value(
                  static_cast<double>(center_world.x + raw_delta.x),
                  grid_size) -
              center_world.x);
          delta.y = static_cast<float>(
              snap_world_value(
                  static_cast<double>(center_world.y + raw_delta.y),
                  grid_size) -
              center_world.y);
          delta.z = static_cast<float>(
              snap_world_value(
                  static_cast<double>(center_world.z + raw_delta.z),
                  grid_size) -
              center_world.z);
        }
        return delta;
}

bool edge_uses_vertex(quader_poly::Edge edge, quader_poly::ElementId vertex_id)
{
	return edge.a == vertex_id || edge.b == vertex_id;
}

bool face_uses_vertex(const quader_poly::Document& document, quader_poly::ElementId face_id, quader_poly::ElementId vertex_id)
{
	const quader_poly::Face* face = quader_poly::find_face(document, face_id);
	return face != nullptr && std::find(face->vertices.begin(), face->vertices.end(), vertex_id) != face->vertices.end();
}

bool moving_selection_contains_vertex(
		const PolygonObject& object,
		SelectionMode selection_mode,
		quader_poly::ElementId vertex_id)
{
  if (!object.selected || vertex_id == quader_poly::kInvalidElementId) {
    return false;
  }
  if (selection_mode == SelectionMode::Object) {
    return true;
  }
  if (selection_mode == SelectionMode::Vertex) {
    return quader_poly::selection_contains(object.selection, vertex_id);
  }
  if (selection_mode == SelectionMode::Edge) {
    return std::any_of(object.selection.edges.begin(),
                       object.selection.edges.end(),
                       [vertex_id](quader_poly::Edge edge) {
                         return edge_uses_vertex(edge, vertex_id);
                       });
  }
  if (selection_mode == SelectionMode::Face) {
    return std::any_of(
        object.selection.faces.begin(), object.selection.faces.end(),
        [&object, vertex_id](quader_poly::ElementId face_id) {
          return face_uses_vertex(object.document, face_id, vertex_id);
        });
  }
        return false;
}

const PolygonObject* object_for_hit(const std::vector<PolygonObject>& objects, const PickHitRecord& hit)
{
	if (hit.object_index >= 0 && hit.object_index < static_cast<int>(objects.size()) &&
			objects[static_cast<std::size_t>(hit.object_index)].id == hit.object_id) {
		return &objects[static_cast<std::size_t>(hit.object_index)];
	}
	const auto found = std::find_if(objects.begin(), objects.end(), [&hit](const PolygonObject& object) {
		return object.id == hit.object_id;
	});
	return found == objects.end() ? nullptr : &*found;
}

bool snap_targets_match(const TransformSnapTarget& left, const TransformSnapTarget& right)
{
	return left.kind == right.kind &&
		left.object_id == right.object_id &&
		left.vertex_id == right.vertex_id;
}

std::optional<TransformSnapTarget> resolve_transform_vertex_snap_target(
		const EditorSession& session,
		const TransformDragState& state,
		const TransformDragPreviewInput& input)
{
	if (!input.vertex_snap_enabled || !state.move.active || !transform_axis_is_single_axis(state.axis)) {
		return std::nullopt;
	}

	quader_poly::PickOptions options =
		selection_pick_options_from_radii(input.vertex_snap_radius, input.vertex_snap_radius);
        options.mode = quader_poly::SelectionMode::Vertex;
	const std::vector<PolygonObject>& snap_objects = input.snap_objects == nullptr
		? session.objects()
		: *input.snap_objects;
        const std::vector<PickHitRecord> hits = pick_all(
		snap_objects,
		{ input.ray, options },
		input.snap_object_bounds,
		input.snap_face_geometry);
	const SelectionMode selection_mode = session.selection_mode();
	for (const PickHitRecord& hit : hits) {
          if (!hit.hit || hit.kind != quader_poly::ElementKind::Vertex ||
              hit.vertex_id == quader_poly::kInvalidElementId) {
            continue;
          }
				const PolygonObject* object = object_for_hit(snap_objects, hit);
		if (object != nullptr && moving_selection_contains_vertex(*object, selection_mode, hit.vertex_id)) {
			continue;
		}
                return TransformSnapTarget{
                    TransformSnapLandmarkKind::Vertex,
                    hit.object_id,
                    hit.vertex_id,
                    hit.local_position,
                };
        }
	return std::nullopt;
}

/**
 * Implements Move Tool behavior for the modal modeling interaction layer.
 */
class MoveTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
          return ViewportInteractionTool::Move;
        }

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Move";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool) const override
	{
		clear_editor_runtime_transform_drag_state(runtime);
		clear_editor_runtime_gizmo_hover(runtime);
	}

	[[nodiscard]] MoveBeginResult begin_move_drag(const MoveBeginInput& input) const
	{
		quader::QVec3 next_world_axis = input.has_world_direction ? input.world_direction : axis_vector(input.axis);
		if (vec3_is_tiny(next_world_axis)) {
			next_world_axis = axis_vector(input.axis);
			if (vec3_is_tiny(next_world_axis)) {
				return { false, "Invalid move axis.", {} };
			}
		}

		const quader::QVec3 next_local_axis = inverse_transform_vector(input.model_transform, next_world_axis);
		if (vec3_is_tiny(next_local_axis)) {
			return { false, "Invalid local move axis.", {} };
		}

		MoveToolState state;
		state.active = true;
		state.axis = input.axis;
		state.world_axis = normalized_axis(next_world_axis);
		state.local_axis = normalized_axis(next_local_axis);
		state.center_local = input.center_local;
		state.center_world = input.has_world_center ? input.world_center : transform_point(input.model_transform, input.center_local);
		state.start_screen = input.screen_position;
		state.plane_start_point = state.center_world;
		if (transform_axis_is_plane(input.axis) && input.has_start_ray) {
			const DragPointResult plane_hit =
				drag_point_on_plane(input.start_ray, state.center_world, transform_plane_normal(input.axis));
			if (plane_hit.ok) {
				state.plane_start_point = plane_hit.point;
			}
		}
		state.screen_axis = { 1.0F, 0.0F };
		bool has_projected_screen_axis = false;
		if (input.has_screen_axis) {
			state.screen_axis = vec2_sub(input.screen_tip, input.screen_pivot);
			if (!vec2_is_tiny(state.screen_axis)) {
				state.screen_axis = vec2_normalize_or(state.screen_axis, { 1.0F, 0.0F });
				has_projected_screen_axis = true;
			}
		}
		if (!has_projected_screen_axis && input.has_camera) {
			state.screen_axis = {
				static_cast<float>(vec3_dot(state.world_axis, input.camera_transform.x_axis)),
				static_cast<float>(-vec3_dot(state.world_axis, input.camera_transform.y_axis)),
			};
			state.screen_axis = vec2_normalize_or(state.screen_axis, { 1.0F, 0.0F });
		}
		if (transform_axis_is_single_axis(input.axis) && input.has_start_ray) {
                  const DragDistanceResult start_distance =
                      axis_distance_from_ray(state.world_axis,
                                             state.center_world,
                                             input.start_ray, kScreenEpsilon);
                  if (start_distance.ok &&
                      std::isfinite(start_distance.distance)) {
                    state.has_axis_start_distance = true;
                    state.axis_start_distance = start_distance.distance;
                  }
		}
		return { true, {}, state };
	}

	[[nodiscard]] double move_axis_distance(const MoveToolState& state, const quader_poly::Ray& ray) const
	{
		if (!state.active || vec3_is_tiny(state.world_axis) || vec3_is_tiny(ray.direction)) {
			return std::numeric_limits<double>::infinity();
		}
                const DragDistanceResult distance = axis_distance_from_ray(
                    state.world_axis, state.center_world, ray, kScreenEpsilon);
                return distance.ok ? distance.distance : std::numeric_limits<double>::infinity();
	}

	[[nodiscard]] MovePreviewResult preview_move_from_distance(
			MoveToolState& state,
			double distance,
			bool precision,
			bool grid_enabled,
			double grid_size) const
	{
		if (!state.active) {
			return { false, "Move tool is not active.", {}, {}, {} };
		}
		state.preview_distance = snap_move_distance(distance, precision, grid_enabled, grid_size);
		state.preview_delta = vec3_scale(state.local_axis, state.preview_distance);
		return { true, {}, state.preview_distance, state.preview_distance, state.preview_delta };
	}

	[[nodiscard]] MovePreviewResult preview_move_from_drag_pixels(
			MoveToolState& state,
			double pixels,
			bool precision,
			bool grid_enabled,
			double grid_size) const
	{
          const double raw_distance =
              (pixels / kMovePixelsPerGridStep) * grid_size;
          return preview_move_from_distance(state, raw_distance, precision,
                                            grid_enabled, grid_size);
	}

	[[nodiscard]] MovePreviewResult preview_move_to_axis_snap_target(
			MoveToolState& state,
			quader::QVec3 target_position) const
	{
		if (!state.active) {
			return { false, "Move tool is not active.", {}, {}, {} };
		}
		if (!transform_axis_is_single_axis(state.axis)) {
			return { false, "Move snap target requires a single move axis.", {}, {}, {} };
		}
		const double distance =
			vec3_dot(target_position - state.center_world, state.world_axis);
		state.preview_distance = distance;
		state.preview_delta = vec3_scale(state.local_axis, distance);
		return { true, {}, distance, distance, state.preview_delta };
	}

	[[nodiscard]] MovePreviewResult preview_move_from_viewport(
			MoveToolState& state,
			quader::QVec2 screen_position,
			quader_poly::Ray ray,
			const ViewportCameraState& camera,
			bool precision,
			bool grid_enabled,
			double grid_size,
			bool snap_center_to_grid) const
	{
		if (!state.active) {
			return { false, "Move tool is not active.", {}, {}, {} };
		}

		quader::QVec3 delta;
		double distance = 0.0;
		if (transform_axis_is_plane(state.axis)) {
			const DragPointResult current_plane_point =
				drag_point_on_plane(ray, state.center_world, transform_plane_normal(state.axis));
			if (current_plane_point.ok) {
				const quader::QVec3 raw_delta = current_plane_point.point - state.plane_start_point;
				delta = snap_center_to_grid && grid_enabled
					? snap_center_drag_delta(state.axis, state.center_world, raw_delta, grid_size)
					: plane_drag_delta(state.axis, raw_delta, grid_size, grid_enabled);
				distance = quader::length(delta);
			}
		} else {
			const quader::QVec2 screen_delta = vec2_sub(screen_position, state.start_screen);
			const double pixel_delta = vec2_dot(screen_delta, state.screen_axis);
			double raw_distance = pixel_delta * viewport_world_units_per_pixel(camera, state.center_world);
			const double ray_distance = move_axis_distance(state, ray);
			if (state.has_axis_start_distance && std::isfinite(ray_distance)) {
				const double ray_delta = ray_distance - state.axis_start_distance;
                                if (std::abs(ray_delta) > kScreenEpsilon ||
                                    std::abs(pixel_delta) <= kScreenEpsilon) {
                                  raw_distance = ray_delta;
                                }
                        }
			if (snap_center_to_grid && grid_enabled) {
				const quader::QVec3 raw_delta = vec3_scale(state.world_axis, raw_distance);
				delta = snap_center_drag_delta(state.axis, state.center_world, raw_delta, grid_size);
				distance = vec3_dot(delta, state.world_axis);
			} else {
				distance = maybe_snap_world_value(raw_distance, grid_size, grid_enabled);
				delta = vec3_scale(state.local_axis, distance);
			}
		}

		if (precision && !transform_axis_is_plane(state.axis)) {
			distance = maybe_snap_world_value(distance, grid_size, false);
			delta = vec3_scale(state.local_axis, distance);
		}
		state.preview_distance = distance;
		state.preview_delta = delta;
		return { true, {}, distance, distance, delta };
	}

	void reset_move(MoveToolState& state) const
	{
		state = MoveToolState();
	}

	[[nodiscard]] TransformDragOperation sanitize_transform_drag_operation(
			TransformDragOperation operation,
			ViewportInteractionTool tool,
			SelectionMode selection_mode) const
	{
          const bool component_topology_selection =
              selection_mode == SelectionMode::Face ||
              selection_mode == SelectionMode::Edge;
          if (!component_topology_selection) {
            return TransformDragOperation::Transform;
          }
          if (operation == TransformDragOperation::Extrude &&
              (tool == ViewportInteractionTool::Move ||
               tool == ViewportInteractionTool::Rotate)) {
            return TransformDragOperation::Extrude;
          }
          if (operation == TransformDragOperation::Inset &&
              tool == ViewportInteractionTool::Scale) {
            return TransformDragOperation::Inset;
          }
          return TransformDragOperation::Transform;
        }

	void reset_transform_drag(TransformDragState& state) const
	{
		state = TransformDragState();
	}

	[[nodiscard]] CommandResult apply_transform_drag_preview(
			const EditorSession& session,
			TransformDragState& state,
			const TransformDragPreviewInput& input) const
	{
		QDR_PROFILE_SCOPE("qdr_editor_core.MoveTool.apply_transform_drag_preview");
		if (!state.active) {
			return { false, false, "Transform drag is not active." };
		}

		const double epsilon = std::max(input.epsilon, 0.0);
                if (state.tool == ViewportInteractionTool::Move) {
                  const quader::QVec3 previous_delta = state.move.preview_delta;
                  const TransformSnapTarget previous_snap_target =
                      state.snap_target;
                  state.snap_target = {};
                  MovePreviewResult preview;
                  if (const std::optional<TransformSnapTarget> snap_target =
                          resolve_transform_vertex_snap_target(session, state,
                                                               input)) {
                    state.snap_target = *snap_target;
                    preview = preview_move_to_axis_snap_target(
                        state.move, snap_target->position);
                  } else {
                    preview = preview_move_from_viewport(
                        state.move, input.screen_position, input.ray,
                        input.camera, input.precision, input.grid_enabled,
                        input.grid_size,
                        state.scene_transform_preview_eligible &&
                            state.operation ==
                                TransformDragOperation::Transform);
                  }
                  if (!preview.ok) {
                    return {false, false,
                            preview.message.empty() ? "Move preview failed."
                                                    : preview.message};
                  }
                  const bool snap_changed = !snap_targets_match(
                      previous_snap_target, state.snap_target);
                  const bool non_zero_preview =
                      quader::length_squared(previous_delta) >
                          static_cast<float>(epsilon * epsilon) ||
                      quader::length_squared(preview.preview_delta) >
                          static_cast<float>(epsilon * epsilon);
                  if (!snap_changed && non_zero_preview &&
                      vec3_distance_squared(previous_delta,
                                            preview.preview_delta) <=
                          epsilon * epsilon) {
                    return {true, false, {}};
                  }
                  return {true, non_zero_preview || snap_changed, {}};
                }

                if (state.tool == ViewportInteractionTool::Rotate) {
                  const double previous_angle_degrees =
                      state.rotate.preview_angle_degrees;
                  const RotatePreviewResult preview =
                      preview_rotate_from_screen_position(
                          state.rotate, input.screen_position,
                          input.rotation_snap_degrees);
                  if (!preview.ok) {
                    return {false, false,
                            preview.message.empty() ? "Rotate preview failed."
                                                    : preview.message};
                  }
                  return {true,
                          std::abs(preview.preview_angle_degrees -
                                   previous_angle_degrees) > epsilon,
                          {}};
                }

                if (state.tool == ViewportInteractionTool::Scale) {
                  const quader::QVec3 previous_scale =
                      state.scale.preview_scale;
                  const ScalePreviewResult preview =
                      preview_scale_from_screen_position(
                          state.scale, input.screen_position,
                          input.grid_enabled, input.grid_size);
                  if (!preview.ok) {
                    return {false, false,
                            preview.message.empty() ? "Scale preview failed."
                                                    : preview.message};
                  }
                  const double preview_change =
                      std::abs(preview.preview_scale.x - previous_scale.x) +
                      std::abs(preview.preview_scale.y - previous_scale.y) +
                      std::abs(preview.preview_scale.z - previous_scale.z);
                  return {true, preview_change > epsilon, {}};
                }

                return { false, false, "Unknown transform drag tool." };
	}
};

const MoveTool& move_tool_impl()
{
  static const MoveTool kTool;
  return kTool;
}

} // namespace

const EditorTool& move_tool()
{
	return move_tool_impl();
}

MoveBeginResult begin_move_drag(const MoveBeginInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.begin_move_drag");
	return move_tool_impl().begin_move_drag(input);
}

double move_axis_distance(const MoveToolState& state, const quader_poly::Ray& ray)
{
	return move_tool_impl().move_axis_distance(state, ray);
}

MovePreviewResult preview_move_from_distance(MoveToolState& state, double distance, bool precision, bool grid_enabled, double grid_size)
{
	return move_tool_impl().preview_move_from_distance(state, distance, precision, grid_enabled, grid_size);
}

MovePreviewResult preview_move_from_drag_pixels(MoveToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size)
{
	return move_tool_impl().preview_move_from_drag_pixels(state, pixels, precision, grid_enabled, grid_size);
}

MovePreviewResult preview_move_to_axis_snap_target(MoveToolState& state, quader::QVec3 target_position)
{
	return move_tool_impl().preview_move_to_axis_snap_target(state, target_position);
}

MovePreviewResult preview_move_from_viewport(
		MoveToolState& state,
		quader::QVec2 screen_position,
		quader_poly::Ray ray,
		const ViewportCameraState& camera,
		bool precision,
		bool grid_enabled,
		double grid_size,
		bool snap_center_to_grid)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.preview_move_from_viewport");
	return move_tool_impl().preview_move_from_viewport(
			state,
			screen_position,
			ray,
			camera,
			precision,
			grid_enabled,
			grid_size,
			snap_center_to_grid);
}

void reset_move(MoveToolState& state)
{
	move_tool_impl().reset_move(state);
}

TransformDragOperation sanitize_transform_drag_operation(TransformDragOperation operation, ViewportInteractionTool tool, SelectionMode selection_mode)
{
	return move_tool_impl().sanitize_transform_drag_operation(operation, tool, selection_mode);
}

void reset_transform_drag(TransformDragState& state)
{
	move_tool_impl().reset_transform_drag(state);
}

CommandResult apply_transform_drag_preview(const EditorSession& session, TransformDragState& state, const TransformDragPreviewInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.apply_transform_drag_preview");
	return move_tool_impl().apply_transform_drag_preview(session, state, input);
}

std::vector<SelectionOverlayPoint> transform_drag_snap_overlay_points(
		const TransformDragState& state,
		const SelectionOverlayStyle& style)
{
  if (!state.active ||
      state.snap_target.kind != TransformSnapLandmarkKind::Vertex) {
    return {};
  }
  return {
      SelectionOverlayPoint{
          SelectionOverlayLayer::HoverVertex,
          state.snap_target.position,
          style.hover_vertex_color_abgr,
          style.vertex_point_size_pixels +
              style.selected_hover_point_growth_pixels,
          state.snap_target.object_id,
          state.snap_target.vertex_id,
      },
      SelectionOverlayPoint{
          SelectionOverlayLayer::SnapTargetVertex,
          state.snap_target.position,
          style.snap_target_vertex_color_abgr,
          style.snap_target_point_size_pixels,
          state.snap_target.object_id,
          state.snap_target.vertex_id,
      },
  };
}

ModalMoveDragTool modal_move_drag_tool_from_id(std::string_view tool_id)
{
	if (tool_id == "extrude") {
          return ModalMoveDragTool::Extrude;
        }
        return ModalMoveDragTool::Move;
}

std::string modal_move_drag_tool_id(ModalMoveDragTool tool)
{
  if (tool == ModalMoveDragTool::Extrude) {
    return "extrude";
  }
        return "move";
}

ModalMoveBeginPlan plan_modal_move_drag_begin(const ModalMoveBeginInput& input)
{
	ModalMoveBeginPlan plan;
	plan.request_duplicate = input.duplicate_requested && input.can_duplicate_selection;
	plan.preview_uses_transform = input.selection_uses_transform_preview;
	plan.mark_selection_dirty = true;
	return plan;
}

ModalMoveUpdatePlan plan_modal_move_drag_update(const ModalMoveUpdateInput& input)
{
	ModalMoveUpdatePlan plan;
	if (!input.active) {
		return plan;
	}
	plan.handled = true;
	const double epsilon = std::max(input.epsilon, 0.0);
	if (vec3_distance_squared(input.delta, input.previous_delta) <= epsilon) {
		return plan;
	}
	plan.changed = true;
	if (quader::length_squared(input.delta) <= static_cast<float>(epsilon)) {
          plan.route = ModalMovePreviewRoute::ClearPreview;
          return plan;
	}
	plan.preview_active = true;
	if (input.tool == ModalMoveDragTool::Extrude) {
	  plan.route = ModalMovePreviewRoute::ExtrudePatchPreview;
	} else if (input.preview_uses_transform) {
          plan.route = ModalMovePreviewRoute::MoveTransformPreview;
        } else {
          plan.route = ModalMovePreviewRoute::MovePatchPreview;
        }
        return plan;
}

ModalMoveCommitPlan plan_modal_move_drag_commit(const ModalMoveCommitInput& input)
{
	ModalMoveCommitPlan plan;
	if (!input.active) {
		return plan;
	}
	plan.handled = true;
	const double epsilon = std::max(input.epsilon, 0.0);
	if (quader::length_squared(input.delta) <= static_cast<float>(epsilon)) {
		plan.restore_base_snapshot = input.duplicate_active;
		plan.restore_selection = input.duplicate_active;
		return plan;
	}
	plan.model_changed = true;
	if (input.tool == ModalMoveDragTool::Extrude) {
	  plan.route = ModalMoveCommitRoute::ExtrudePatch;
	  plan.undo_action = "Quader Extrude Face";
        } else {
          plan.route = ModalMoveCommitRoute::MovePatch;
          plan.undo_action = "Quader Move Selection";
        }
        return plan;
}

} // namespace quader_editor_core
