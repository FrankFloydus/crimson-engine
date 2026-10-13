////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/knife_tool.hpp>
#include <editor/interactions/tool_math_internal.hpp>
#include <editor/interactions/tool_snapping.hpp>
#include <diagnostics/profile.hpp>
#include <editor/modeling/runtime/tool_operation_contracts.hpp>

#include "editor/modeling/runtime/session_internal.hpp"
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_knife_helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <limits>
#include <numbers>
#include <utility>

namespace quader_editor_core {
namespace {

/**
 * Owns Knife Tool Impl behavior within the modal modeling interaction layer.
 */
class KnifeToolImpl final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override;
	[[nodiscard]] std::string_view id() const override;
	[[nodiscard]] std::string_view label() const override;
	void reset_runtime_state(EditorRuntimeState& runtime, bool keep_active_when_current_tool = false) const override;
};

const KnifeToolImpl& knife_tool_impl()
{
  static const KnifeToolImpl kTool;
  return kTool;
}

} // namespace

const EditorTool& knife_tool()
{
	return knife_tool_impl();
}

ViewportInteractionTool KnifeToolImpl::tool_id() const
{
  return ViewportInteractionTool::Knife;
}

std::string_view KnifeToolImpl::id() const
{
	return viewport_interaction_tool_id(tool_id());
}

std::string_view KnifeToolImpl::label() const
{
	return "Knife";
}

void KnifeToolImpl::reset_runtime_state(EditorRuntimeState& runtime, bool keep_active_when_current_tool) const
{
	reset_editor_runtime_knife_state(runtime, keep_active_when_current_tool);
}

namespace {

bool face_contains_edge(const quader_poly::Face& face, quader_poly::Edge edge)
{
	edge = quader_poly::make_edge(edge.a, edge.b);
	if (face.vertices.size() < 2U) {
		return false;
	}
	for (std::size_t index = 0; index < face.vertices.size(); ++index) {
		const quader_poly::ElementId a = face.vertices[index];
		const quader_poly::ElementId b = face.vertices[(index + 1U) % face.vertices.size()];
		if (quader_poly::make_edge(a, b) == edge) {
			return true;
		}
	}
	return false;
}

const PolygonObject *object_by_id(const std::vector<PolygonObject>& objects, int object_id)
{
	const int object_index = object_index_by_id(objects, object_id);
	return object_index < 0 ? nullptr : &objects[object_index];
}

/**
 * Represents the face region available to the active knife segment.
 */
struct KnifeSegmentFaceRegion {
	bool restricted = false;
	const quader_poly::Document *document = nullptr;
	quader_poly::KnifePointTarget seed;
};

KnifeSegmentFaceRegion knife_target_face_region(
		const std::vector<PolygonObject>& objects,
		int object_id,
		const quader_poly::KnifePointTarget& seed)
{
	KnifeSegmentFaceRegion region;
	region.restricted = true;
	const PolygonObject *object = object_by_id(
		objects, object_id);
	if (object == nullptr) {
		return region;
	}
	region.document = &object->document.read();
	region.seed = seed;
	return region;
}

KnifeSegmentFaceRegion knife_active_segment_face_region(
		const std::vector<PolygonObject>& objects,
		const KnifeToolState& state)
{
	if (!state.has_active_stroke ||
		state.previous_point_index >= state.points.size()) {
		return {};
	}
	return knife_target_face_region(
		objects, state.target_object_id,
		state.points[state.previous_point_index]);
}

bool knife_face_contains_target(
		const quader_poly::Face& face,
		const quader_poly::KnifePointTarget& target)
{
	switch (target.kind) {
	case quader_poly::KnifePointTargetKind::ExistingVertex:
	case quader_poly::KnifePointTargetKind::InsertedVertex:
		return std::find(face.vertices.begin(), face.vertices.end(),
			target.vertex_id) != face.vertices.end();
	case quader_poly::KnifePointTargetKind::ExistingEdge:
		return face_contains_edge(face, target.edge);
	case quader_poly::KnifePointTargetKind::FacePoint:
		return face.id == target.face_id;
	}
	return false;
}

bool knife_target_can_seed_stroke(
		const quader_poly::KnifePointTarget& target)
{
	return target.kind ==
			quader_poly::KnifePointTargetKind::ExistingVertex ||
		target.kind == quader_poly::KnifePointTargetKind::ExistingEdge;
}

bool normalize_knife_target_to_segment_face_region(
		const KnifeSegmentFaceRegion& region,
		quader_poly::KnifePointTarget& target)
{
	if (!region.restricted) {
		return true;
	}
	if (region.document == nullptr) {
		return false;
	}

	const quader_poly::Face *fallback_face = nullptr;
	for (const quader_poly::Face& face : region.document->faces) {
		if (!knife_face_contains_target(face, region.seed) ||
			!knife_face_contains_target(face, target)) {
			continue;
		}
		if (face.id == target.face_id) {
			return true;
		}
		fallback_face = fallback_face != nullptr ? fallback_face : &face;
	}
	if (fallback_face == nullptr) {
		return false;
	}
	target.face_id = fallback_face->id;
	return true;
}

} // namespace

KnifeStraightGuide build_knife_straight_guide(
		const quader_poly::Document& world_document,
		quader_poly::ElementId face_id, quader::QVec3 previous,
		quader::QVec3 current, quader::QVec3 reference_direction)
{
	const auto finite_point = [](quader::QVec3 point) {
		return std::isfinite(point.x) && std::isfinite(point.y) &&
			std::isfinite(point.z);
	};
	if (!finite_point(previous) || !finite_point(current) ||
		!finite_point(reference_direction)) {
		return {};
	}
	const quader_poly::Face* face = quader_poly::find_face(world_document, face_id);
	if (face == nullptr || face->vertices.size() < 3U) {
		return {};
	}
	const quader::QVec3 delta = current - previous;
	const double cut_length = std::hypot(
		static_cast<double>(delta.x), static_cast<double>(delta.y),
		static_cast<double>(delta.z));
	const double reference_length = std::hypot(
		static_cast<double>(reference_direction.x),
		static_cast<double>(reference_direction.y),
		static_cast<double>(reference_direction.z));
	if (!std::isfinite(cut_length) || !std::isfinite(reference_length) ||
		cut_length <= 0.000001 || reference_length <= 0.000001) {
		return {};
	}
	quader::QVec3 direction{
		static_cast<float>(reference_direction.x / reference_length),
		static_cast<float>(reference_direction.y / reference_length),
		static_cast<float>(reference_direction.z / reference_length)};
	const quader::QVec3 normal = quader_poly::face_normal(world_document, *face);
	if (!finite_point(normal) || quader::length_squared(normal) <= 0.000001F ||
		std::abs(quader::dot(direction, normal)) > 0.0001F) {
		return {};
	}
	const double plane_tolerance = std::max(0.00001, cut_length * 0.00001);
	if (std::abs(static_cast<double>(quader::dot(delta, normal))) > plane_tolerance) {
		return {};
	}
	const int dropped_axis = std::abs(normal.x) >= std::abs(normal.y) &&
		std::abs(normal.x) >= std::abs(normal.z) ? 0 :
		std::abs(normal.y) >= std::abs(normal.z) ? 1 : 2;
	const auto projected_loop =
		quader_poly::document_internal::knife_face_projected_loop(
			world_document, *face, dropped_axis);
	// face_normal() supplies a fallback axis for degenerate faces. Require a
	// real planar region before treating that axis as a guide support plane.
	const double face_area =
		quader_poly::document_internal::knife_signed_area_2d(projected_loop);
	if (projected_loop.size() != face->vertices.size() ||
		!std::isfinite(face_area) || std::abs(face_area) <= 0.000000000001 ||
		!quader_poly::document_internal::knife_point_in_or_on_polygon_2d(
			quader_poly::document_internal::knife_project_point(previous, dropped_axis),
			projected_loop) ||
		!quader_poly::document_internal::knife_point_in_or_on_polygon_2d(
			quader_poly::document_internal::knife_project_point(current, dropped_axis),
			projected_loop)) {
		return {};
	}
	if (quader::dot(direction, delta) < 0.0F) {
		direction = direction * -1.0F;
	}
	const auto projected_distance = [](quader::QVec3 offset, quader::QVec3 axis) {
		return static_cast<double>(offset.x) * axis.x +
			static_cast<double>(offset.y) * axis.y +
			static_cast<double>(offset.z) * axis.z;
	};
	// The first positive boundary crossing bounds the contiguous face region.
	// Validate its midpoint so a concave face cannot bridge an outside interval.
	const auto clipped_endpoint = [&](quader::QVec3 axis) -> std::optional<quader::QVec3> {
		const quader::QVec3 lateral = quader::cross(normal, axis);
		double exit_distance = std::numeric_limits<double>::infinity();
		for (std::size_t index = 0U; index < face->vertices.size(); ++index) {
			const quader_poly::Vertex* first =
				quader_poly::find_vertex(world_document, face->vertices[index]);
			const quader_poly::Vertex* second = quader_poly::find_vertex(
				world_document, face->vertices[(index + 1U) % face->vertices.size()]);
			if (first == nullptr || second == nullptr || !finite_point(first->position) ||
				!finite_point(second->position)) {
				return std::nullopt;
			}
			const quader::QVec3 first_offset = first->position - previous;
			const quader::QVec3 second_offset = second->position - previous;
			const double first_x = projected_distance(first_offset, axis);
			const double second_x = projected_distance(second_offset, axis);
			const double first_y = projected_distance(first_offset, lateral);
			const double second_y = projected_distance(second_offset, lateral);
			const double edge_length = std::hypot(second_x - first_x, second_y - first_y);
			const double tolerance = std::max(0.000001, edge_length * 0.000001);
			if (std::abs(projected_distance(first_offset, normal)) >
				std::max(plane_tolerance, edge_length * 0.00001)) {
				return std::nullopt;
			}
			if (std::abs(first_y) <= tolerance && std::abs(second_y) <= tolerance) {
				if (first_x > tolerance) {
					exit_distance = std::min(exit_distance, first_x);
				}
				if (second_x > tolerance) {
					exit_distance = std::min(exit_distance, second_x);
				}
				continue;
			}
			const double denominator = second_y - first_y;
			if (std::abs(denominator) <= tolerance) {
				continue;
			}
			const double factor = -first_y / denominator;
			if (factor < -0.000001 || factor > 1.000001) {
				continue;
			}
			const double distance = first_x + (second_x - first_x) * std::clamp(factor, 0.0, 1.0);
			if (distance > tolerance) {
				exit_distance = std::min(exit_distance, distance);
			}
		}
		if (!std::isfinite(exit_distance) || exit_distance <= 0.000001) {
			return std::nullopt;
		}
		const quader::QVec3 endpoint = previous + axis * static_cast<float>(exit_distance);
		const quader::QVec3 midpoint = previous + axis * static_cast<float>(exit_distance * 0.5);
		if (!finite_point(endpoint) ||
			!quader_poly::document_internal::knife_point_in_or_on_polygon_2d(
				quader_poly::document_internal::knife_project_point(midpoint, dropped_axis),
				projected_loop)) {
			return std::nullopt;
		}
		return endpoint;
	};
	std::optional<quader::QVec3> endpoint = clipped_endpoint(direction);
	if (!endpoint.has_value()) {
		// A reference is an unoriented line; a seed at a boundary may use its
		// other half when the nearest direction points outside that face.
		direction = direction * -1.0F;
		endpoint = clipped_endpoint(direction);
	}
	if (!endpoint.has_value()) {
		return {};
	}
	const double cut_x = static_cast<double>(delta.x) / cut_length;
	const double cut_y = static_cast<double>(delta.y) / cut_length;
	const double cut_z = static_cast<double>(delta.z) / cut_length;
	const double cosine = std::abs(direction.x * cut_x + direction.y * cut_y + direction.z * cut_z);
	const double sine = std::hypot(direction.y * cut_z - direction.z * cut_y,
		direction.z * cut_x - direction.x * cut_z,
		direction.x * cut_y - direction.y * cut_x);
	const double angle_degrees = std::atan2(sine, cosine) * 180.0 / std::numbers::pi;
	if (!std::isfinite(angle_degrees)) {
		return {};
	}
	return {
		.valid = true,
		.face_id = face_id,
		.start = previous,
		.end = *endpoint,
		.cut_point = current,
		.angle_degrees = std::clamp(angle_degrees, 0.0, 90.0),
	};
}

KnifeToolPreview knife_tool_preview(const KnifeToolState& state)
{
	KnifeToolPreview preview;
	preview.active = state.active;
	preview.has_active_stroke = state.has_active_stroke;
	preview.has_current = state.has_current;
	preview.current_closes_stroke = state.has_current &&
		state.current_target_valid && state.current_closes_stroke;
	preview.target_object_id = state.target_object_id;
	preview.previous_point =
		state.has_active_stroke && state.previous_point_index < state.point_positions.size() ?
			state.point_positions[state.previous_point_index] :
			quader::QVec3 {};
	preview.current_point = state.has_current ? state.current_point : quader::QVec3 {};
	preview.preview_point_count = saturated_history_size(state.preview_points.size());
	preview.preview_line_count = saturated_history_size(state.preview_lines.size());
	preview.point_count = saturated_history_size(state.points.size());
	preview.segment_count = saturated_history_size(state.segments.size());
	preview.can_commit = !state.segments.empty();
	preview.status = state.status;
	if (state.active && state.has_active_stroke && state.has_current &&
		state.current_target_valid) {
		preview.straight_guide = state.straight_guide;
	}
	return preview;
}

void reset_knife_tool(KnifeToolState& state)
{
	state = {};
}

void clear_knife_hover(KnifeToolState& state, SelectionHoverState& hover)
{
	state.straight_guide = {};
	state.has_current = false;
	state.current_closes_stroke = false;
	state.current_target_valid = false;
	state.current_object_id = -1;
	state.status = KnifeToolStatus::HoverMiss;
	hover = {};
	rebuild_knife_overlay(state);
}

void rebuild_knife_overlay(KnifeToolState& state)
{
	state.preview_lines.clear();
	state.preview_points.clear();
	if (!state.active) {
		return;
	}
	for (std::size_t index = 0; index < state.point_positions.size(); ++index) {
          state.preview_points.push_back({
              SelectionOverlayLayer::SelectedVertex,
              state.point_positions[index],
              0xff16d9ffU,
              index == state.previous_point_index && state.has_active_stroke
                  ? 10.0F
                  : 8.0F,
              state.target_object_id,
          });
        }
	for (const quader_poly::KnifeStrokeSegment& segment : state.segments) {
		if (segment.first_point >= state.point_positions.size() || segment.second_point >= state.point_positions.size()) {
			continue;
		}
                state.preview_lines.push_back({
                    SelectionOverlayLayer::HoverEdge,
                    state.point_positions[segment.first_point],
                    state.point_positions[segment.second_point],
                    0xff16d9ffU,
                    3.0F,
                    state.target_object_id,
                });
        }
	if (state.has_current) {
          const int current_object_id = state.target_object_id > 0
              ? state.target_object_id
              : state.current_object_id;
          state.preview_points.push_back({
              SelectionOverlayLayer::HoverVertex,
              state.current_point,
              state.current_target_valid ? 0xff56ff8fU : 0xff4f7dffU,
              8.0F,
              current_object_id,
          });
        }
	if (state.has_active_stroke && state.has_current && state.previous_point_index < state.point_positions.size()) {
          state.preview_lines.push_back({
              SelectionOverlayLayer::HoverEdge,
              state.point_positions[state.previous_point_index],
              state.current_point,
              state.current_target_valid ? 0xff16d9ffU : 0xff4f7dffU,
              3.0F,
              state.target_object_id,
          });
        }
}

bool knife_object_allowed(const KnifeToolState& state, int object_id)
{
	return object_id > 0 && (state.target_object_id <= 0 || object_id == state.target_object_id);
}

std::optional<quader::QVec3> edge_position_from_factor(
		const quader_poly::Document& document,
		quader_poly::Edge edge,
		float factor)
{
	edge = quader_poly::make_edge(edge.a, edge.b);
	const quader_poly::Vertex *a = quader_poly::find_vertex(document, edge.a);
	const quader_poly::Vertex *b = quader_poly::find_vertex(document, edge.b);
	if (a == nullptr || b == nullptr) {
		return std::nullopt;
	}

	const float clamped = std::clamp(std::isfinite(factor) ? factor : 0.5F, 0.01F, 0.99F);
	return a->position + ((b->position - a->position) * clamped);
}

bool knife_object_allowed(const KnifeToolState& state, int object_id);

quader::QVec3 project_point_to_plane(quader::QVec3 point, quader::QVec3 origin, quader::QVec3 normal)
{
	const float normal_length_squared = quader::length_squared(normal);
        if (normal_length_squared <= kTransformPointEpsilon) {
          return point;
        }

        const float signed_distance = quader::dot(point - origin, normal) / normal_length_squared;
	return point - (normal * signed_distance);
}

std::optional<quader::QVec3> project_point_to_face_plane(
		const quader_poly::Document& document,
		quader_poly::ElementId face_id,
		quader::QVec3 point)
{
	const quader_poly::Face *face = quader_poly::find_face(document, face_id);
	if (face == nullptr || face->vertices.empty()) {
		return std::nullopt;
	}

	const quader_poly::Vertex *origin = quader_poly::find_vertex(document, face->vertices.front());
	if (origin == nullptr) {
		return std::nullopt;
	}

	const quader::QVec3 normal = quader_poly::face_normal(document, *face);
        if (quader::length_squared(normal) <= kTransformPointEpsilon) {
          return std::nullopt;
        }
        return project_point_to_plane(point, origin->position, normal);
}

/**
 * Represents a Knife Resolved Point value used by the modal modeling interaction layer.
 */
struct KnifeResolvedPoint {
	bool has_point = false;
	bool target_valid = false;
	int object_id = -1;
        quader::QVec3 point;
	quader::QVec3 normal { 0.0F, 1.0F, 0.0F };
	quader_poly::KnifePointTarget target;
};

quader_poly::PickOptions knife_pick_options(
		quader_poly::SelectionMode mode)
{
	quader_poly::PickOptions options;
	options.mode = mode;
	options.pick_backfaces = true;
	// Match the default single-sided renderer: select the visible authored
	// front surface first, but keep reversed/open backfaces available when no
	// front-facing surface exists.
	options.prefer_front_facing_faces = true;
	return options;
}

bool fill_knife_vertex_hit(
		const std::vector<PolygonObject>& objects,
		KnifeToolState& state,
		const PickHitRecord& hit,
		KnifeResolvedPoint& out_point)
{
	if (hit.kind != quader_poly::ElementKind::Vertex ||
		!knife_object_allowed(state, hit.object_id)) {
		return false;
	}
	const PolygonObject* object = object_by_id(objects, hit.object_id);
	const quader_poly::Vertex* vertex = object != nullptr
		? quader_poly::find_vertex(object->document, hit.vertex_id)
		: nullptr;
	if (vertex == nullptr) {
		return false;
	}
	out_point.has_point = true;
	out_point.target_valid = true;
	out_point.object_id = hit.object_id;
	out_point.point = vertex->position;
	out_point.normal = vec3_normalize_or(
		hit.local_normal, {0.0F, 1.0F, 0.0F}, kTransformPointEpsilon);
	out_point.target.kind = quader_poly::KnifePointTargetKind::ExistingVertex;
	out_point.target.vertex_id = vertex->id;
	out_point.target.face_id = hit.face_id;
	out_point.target.position = vertex->position;
	return true;
}

bool fill_knife_edge_hit(
		const std::vector<PolygonObject>& objects,
		KnifeToolState& state,
		const PickHitRecord& hit,
		double grid_size,
		bool snap_enabled,
		KnifeResolvedPoint& out_point)
{
	if (hit.kind != quader_poly::ElementKind::Edge ||
		!knife_object_allowed(state, hit.object_id)) {
		return false;
	}
	const PolygonObject* object = object_by_id(objects, hit.object_id);
	if (object == nullptr) {
		return false;
	}
	quader::QVec3 factor_point = hit.local_position;
	if (snap_enabled) {
		const std::optional<quader::QVec3> snapped =
			snap_knife_point_to_edge_grid(
				object->document, hit.edge, factor_point, grid_size, true);
		if (!snapped.has_value()) {
			return false;
		}
		factor_point = *snapped;
	}
	const std::optional<float> factor = quader_poly::edge_factor_from_position(
		object->document, hit.edge, factor_point);
	const std::optional<quader::QVec3> point = factor.has_value()
		? edge_position_from_factor(object->document, hit.edge, *factor)
		: std::nullopt;
	if (!factor.has_value() || !point.has_value()) {
		return false;
	}
	if (*factor <= 0.01F || *factor >= 0.99F) {
		const quader_poly::Edge edge =
			quader_poly::make_edge(hit.edge.a, hit.edge.b);
		PickHitRecord endpoint_hit = hit;
		endpoint_hit.kind = quader_poly::ElementKind::Vertex;
		endpoint_hit.vertex_id = *factor <= 0.01F ? edge.a : edge.b;
		return fill_knife_vertex_hit(objects, state, endpoint_hit, out_point);
	}
	out_point.has_point = true;
	out_point.target_valid = true;
	out_point.object_id = hit.object_id;
	out_point.point = *point;
	out_point.normal = vec3_normalize_or(
		hit.local_normal, {0.0F, 1.0F, 0.0F}, kTransformPointEpsilon);
	out_point.target.kind = quader_poly::KnifePointTargetKind::ExistingEdge;
	out_point.target.edge = quader_poly::make_edge(hit.edge.a, hit.edge.b);
	out_point.target.edge_factor = std::clamp(*factor, 0.01F, 0.99F);
	out_point.target.face_id = hit.face_id;
	out_point.target.position = *point;
	return true;
}

bool fill_knife_face_hit(
		const std::vector<PolygonObject>& objects,
		KnifeToolState& state,
		const PickHitRecord& surface_hit,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis,
		KnifeResolvedPoint& out_point)
{
	if (surface_hit.kind != quader_poly::ElementKind::Face ||
		!knife_object_allowed(state, surface_hit.object_id)) {
		return false;
	}
	const PolygonObject* object =
		object_by_id(objects, surface_hit.object_id);
	quader::QVec3 point = surface_hit.local_position;
	if (state.has_active_stroke && constrain_axis &&
		state.previous_point_index < state.point_positions.size()) {
		point = constrain_knife_point_to_axis(
			state.point_positions[state.previous_point_index], point,
			state.face_normal, grid_size, snap_enabled);
	} else if (snap_enabled && object != nullptr) {
		const std::optional<quader::QVec3> snapped =
			snap_knife_point_to_face_grid(
				object->document, surface_hit.face_id, point, grid_size, true);
		if (!snapped.has_value()) {
			return false;
		}
		point = *snapped;
	}
	if (object != nullptr) {
		const std::optional<quader::QVec3> face_point =
			project_point_to_face_plane(object->document, surface_hit.face_id,
				point);
		if (face_point.has_value()) {
			point = *face_point;
		}
	}
	out_point.has_point = true;
	out_point.target_valid = true;
	out_point.object_id = surface_hit.object_id;
	out_point.point = point;
	out_point.normal = vec3_normalize_or(
		surface_hit.local_normal, {0.0F, 1.0F, 0.0F},
		kTransformPointEpsilon);
	out_point.target.kind = quader_poly::KnifePointTargetKind::FacePoint;
	out_point.target.face_id = surface_hit.face_id;
	out_point.target.position = point;
	return true;
}

KnifeResolvedPoint resolve_knife_point_from_pick(
		const std::vector<PolygonObject>& objects,
		KnifeToolState& state,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis,
		int target_object_id)
{
	KnifeResolvedPoint point;
	constexpr std::array kPriorityModes {
		SelectionMode::Vertex,
		SelectionMode::Edge,
		SelectionMode::Face,
	};
	const int effective_target_object_id = target_object_id >= 0
		? target_object_id
		: state.target_object_id;
	const std::optional<PickHitRecord> hit = pick_nearest_priority(
		objects,
		{
			ray,
			knife_pick_options(quader_poly::SelectionMode::Vertex),
			effective_target_object_id,
		},
		kPriorityModes);
	if (!hit.has_value()) {
		return point;
	}
	switch (hit->kind) {
	case quader_poly::ElementKind::Vertex:
		(void)fill_knife_vertex_hit(objects, state, *hit, point);
		break;
	case quader_poly::ElementKind::Edge:
		(void)fill_knife_edge_hit(
			objects, state, *hit, grid_size, snap_enabled, point);
		break;
	case quader_poly::ElementKind::Face:
		(void)fill_knife_face_hit(
			objects, state, *hit, grid_size, snap_enabled, constrain_axis,
			point);
		break;
	}
	return point;
}

KnifeResolvedPoint resolve_knife_point_from_hit(
		const std::vector<PolygonObject>& objects,
		KnifeToolState& state,
		const PickHitRecord& hit,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis)
{
	KnifeResolvedPoint point;
	switch (hit.kind) {
	case quader_poly::ElementKind::Vertex:
		(void)fill_knife_vertex_hit(objects, state, hit, point);
		break;
	case quader_poly::ElementKind::Edge:
		(void)fill_knife_edge_hit(objects, state, hit, grid_size, snap_enabled,
			point);
		break;
	case quader_poly::ElementKind::Face:
		(void)fill_knife_face_hit(objects, state, hit, grid_size, snap_enabled,
			constrain_axis, point);
		break;
	}
	return point;
}

bool knife_current_closes_active_stroke(const KnifeToolState& state, double grid_size)
{
	if (!state.has_active_stroke ||
		!state.has_current ||
		state.active_stroke_start >= state.point_positions.size() ||
		state.previous_point_index >= state.point_positions.size() ||
		state.previous_point_index < state.active_stroke_start + 2U ||
		state.previous_point_index == state.active_stroke_start) {
		return false;
	}

	const float threshold = static_cast<float>(std::max(0.025, std::isfinite(grid_size) && grid_size > 0.0001 ? grid_size * 0.10 : 0.025));
	return quader::length_squared(state.current_point - state.point_positions[state.active_stroke_start]) <= threshold * threshold;
}

std::uint32_t append_knife_point(KnifeToolState& state)
{
	state.points.push_back(state.current_target);
	state.point_positions.push_back(state.current_point);
	return static_cast<std::uint32_t>(state.points.size() - 1U);
}

bool update_knife_preview_from_resolved_point(
		const std::vector<PolygonObject>& objects,
		KnifeToolState& state,
		SelectionHoverState& hover,
		const KnifeResolvedPoint& point,
		bool active,
		double grid_size)
{
	state.straight_guide = {};
	state.active = active;
	state.current_closes_stroke = false;
	if (!point.has_point) {
		clear_knife_hover(state, hover);
		return false;
	}
	if (state.target_object_id > 0 && point.object_id != state.target_object_id) {
		state.has_current = false;
		state.current_target_valid = false;
                state.current_object_id = -1;
                state.status = KnifeToolStatus::InvalidTarget;
                hover = {};
		rebuild_knife_overlay(state);
		return false;
	}
	if (!state.has_active_stroke &&
		!knife_target_can_seed_stroke(point.target)) {
		state.has_current = false;
		state.current_target_valid = false;
		state.current_target = {};
		state.current_point = {};
		state.current_object_id = -1;
		state.status = KnifeToolStatus::InvalidTarget;
		hover = {};
		rebuild_knife_overlay(state);
		return false;
	}

	const KnifeSegmentFaceRegion segment_face_region =
		state.has_active_stroke
			? knife_active_segment_face_region(objects, state)
			: knife_target_can_seed_stroke(point.target)
				? knife_target_face_region(
					objects, point.object_id, point.target)
				: KnifeSegmentFaceRegion {};
	quader_poly::KnifePointTarget normalized_target = point.target;
	const bool target_in_segment_face =
		normalize_knife_target_to_segment_face_region(
			segment_face_region, normalized_target);
	if (point.target_valid && segment_face_region.restricted &&
		!target_in_segment_face) {
		state.has_current = false;
		state.current_target_valid = false;
		state.current_target = {};
		state.current_point = {};
		state.current_object_id = -1;
		state.status = KnifeToolStatus::InvalidTarget;
		hover = {};
		rebuild_knife_overlay(state);
		return false;
	}
	const bool target_valid = point.target_valid && target_in_segment_face;
	state.has_current = true;
	state.current_target_valid = target_valid;
	state.current_object_id = point.object_id;
	state.current_point = point.point;
	state.current_target = normalized_target;
	state.face_normal = point.normal;
	state.current_closes_stroke = target_valid &&
		knife_current_closes_active_stroke(state, grid_size);
        state.status = target_valid ? KnifeToolStatus::Preview
                                    : KnifeToolStatus::InvalidTarget;
        hover = {};
        hover.vertex_id = quader_poly::kInvalidElementId;
        hover.edge = {};
        if (target_valid &&
            normalized_target.kind ==
                quader_poly::KnifePointTargetKind::ExistingVertex) {
          hover.object_id = point.object_id;
          hover.kind = SelectionHoverKind::Vertex;
          hover.vertex_id = normalized_target.vertex_id;
          hover.face_id = normalized_target.face_id;
          hover.candidate_object_id = point.object_id;
        } else if (target_valid &&
                   normalized_target.kind ==
                       quader_poly::KnifePointTargetKind::ExistingEdge) {
          hover.object_id = point.object_id;
          hover.kind = SelectionHoverKind::Edge;
          hover.edge = normalized_target.edge;
          hover.face_id = normalized_target.face_id;
          hover.candidate_object_id = point.object_id;
        }
        hover.selected_modifier_pressed = false;
	rebuild_knife_overlay(state);
	return target_valid;
}

bool update_knife_preview_from_pick(
		EditorSession& session,
		KnifeToolState& state,
		SelectionHoverState& hover,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis,
		int target_object_id)
{
	return update_knife_preview_from_pick(
		session.objects(), state, hover, ray, grid_size, snap_enabled,
		constrain_axis, target_object_id);
}

bool update_knife_preview_from_pick(
		const std::vector<PolygonObject>& objects,
		KnifeToolState& state,
		SelectionHoverState& hover,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis,
		int target_object_id)
{
	const KnifeResolvedPoint point = resolve_knife_point_from_pick(
		objects, state, ray, grid_size, snap_enabled, constrain_axis,
		target_object_id);
	return update_knife_preview_from_resolved_point(
		objects, state, hover, point, true, grid_size);
}

bool update_knife_preview_from_hit(
		EditorSession& session,
		KnifeToolState& state,
		SelectionHoverState& hover,
		const PickHitRecord& hit,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis)
{
	return update_knife_preview_from_hit(
		session.objects(), state, hover, hit, grid_size, snap_enabled,
		constrain_axis);
}

bool update_knife_preview_from_hit(
		const std::vector<PolygonObject>& objects,
		KnifeToolState& state,
		SelectionHoverState& hover,
		const PickHitRecord& hit,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis)
{
	const KnifeResolvedPoint point = resolve_knife_point_from_hit(
		objects, state, hit, grid_size, snap_enabled, constrain_axis);
	return update_knife_preview_from_resolved_point(
		objects, state, hover, point, true, grid_size);
}

bool accept_knife_current_point(KnifeToolState& state, double grid_size)
{
	state.straight_guide = {};
	state.current_closes_stroke = false;
	if (!state.has_current || !state.current_target_valid) {
		return false;
	}

	if (!state.has_active_stroke) {
		if (state.current_object_id <= 0 ||
			!knife_target_can_seed_stroke(state.current_target)) {
			state.status = KnifeToolStatus::InvalidTarget;
			state.has_current = false;
			state.current_target_valid = false;
			state.current_object_id = -1;
			rebuild_knife_overlay(state);
			return false;
		}
		state.target_object_id = state.current_object_id;
		const std::uint32_t point_index = append_knife_point(state);
		state.active_stroke_start = point_index;
		state.previous_point_index = point_index;
		state.has_active_stroke = true;
                state.status = KnifeToolStatus::PreviousSet;
                state.has_current = false;
		state.current_target_valid = false;
		state.current_object_id = -1;
		rebuild_knife_overlay(state);
		return true;
	}

	if (knife_current_closes_active_stroke(state, grid_size)) {
		if (state.previous_point_index != state.active_stroke_start) {
			state.segments.push_back({
				state.previous_point_index,
				state.active_stroke_start,
			});
		}
		state.has_active_stroke = false;
                state.status = KnifeToolStatus::PreviousSet;
        } else {
		const std::uint32_t point_index = append_knife_point(state);
		if (point_index != state.previous_point_index) {
			state.segments.push_back({
				state.previous_point_index,
				point_index,
			});
		}
		state.previous_point_index = point_index;
                state.status = KnifeToolStatus::PreviousSet;
        }

	state.has_current = false;
	state.current_target_valid = false;
	state.current_object_id = -1;
	rebuild_knife_overlay(state);
	return true;
}

KnifeSegmentPreview build_knife_segment_preview(
		const std::vector<PolygonObject>& objects,
		int object_id,
		const quader_poly::KnifePointTarget& previous,
		const quader_poly::KnifePointTarget& current)
{
	KnifeSegmentPreview preview;
	preview.object_id = object_id;
	preview.previous = previous;
	preview.current = current;

	const int object_index = object_index_by_id(objects, object_id);
	if (object_index < 0) {
		preview.message = "Knife segment needs a locked target mesh.";
		return preview;
	}

	const quader_poly::KnifeSegmentPreview document_preview =
			quader_poly::build_knife_segment_preview(objects[object_index].document, previous, current);
	preview.valid = document_preview.valid;
	preview.message = document_preview.message;
	preview.lines.reserve(document_preview.overlay.lines.size());
	for (const quader_poly::OverlayLine& line : document_preview.overlay.lines) {
          preview.lines.push_back({
              SelectionOverlayLayer::HoverEdge,
              line.start,
              line.end,
              line.color,
              line.width_pixels,
          });
        }
	return preview;
}

} // namespace quader_editor_core
