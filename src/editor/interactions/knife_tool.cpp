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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
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

	[[nodiscard]] CommandResult commit(EditorSession& session, KnifeToolState& state) const;
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
		const EditorSession& session,
		int object_id,
		const quader_poly::KnifePointTarget& seed)
{
	KnifeSegmentFaceRegion region;
	region.restricted = true;
	const PolygonObject *object = object_by_id(
		session.objects(), object_id);
	if (object == nullptr) {
		return region;
	}
	region.document = &object->document.read();
	region.seed = seed;
	return region;
}

KnifeSegmentFaceRegion knife_active_segment_face_region(
		const EditorSession& session,
		const KnifeToolState& state)
{
	if (!state.has_active_stroke ||
		state.previous_point_index >= state.points.size()) {
		return {};
	}
	return knife_target_face_region(
		session, state.target_object_id,
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

KnifeToolPreview knife_tool_preview(const KnifeToolState& state)
{
	KnifeToolPreview preview;
	preview.active = state.active;
	preview.has_active_stroke = state.has_active_stroke;
	preview.has_current = state.has_current;
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
	return preview;
}

void reset_knife_tool(KnifeToolState& state)
{
	state = {};
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
          state.preview_points.push_back({
              SelectionOverlayLayer::HoverVertex,
              state.current_point,
              state.current_target_valid ? 0xff56ff8fU : 0xff4f7dffU,
              8.0F,
              state.target_object_id,
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

bool fill_knife_vertex_hit(
		EditorSession& session,
		KnifeToolState& state,
		const PickHitRecord& hit,
		KnifeResolvedPoint& out_point)
{
	if (hit.kind != quader_poly::ElementKind::Vertex ||
		!knife_object_allowed(state, hit.object_id)) {
		return false;
	}
	const PolygonObject* object = object_by_id(session.objects(), hit.object_id);
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

bool fill_knife_vertex_target(
		EditorSession& session,
		KnifeToolState& state,
		const quader_poly::Ray& ray,
		int target_object_id,
		KnifeResolvedPoint& out_point)
{
	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Vertex;
        const std::optional<PickHitRecord> hit =
            session.pick_nearest({ray, options, target_object_id});
	return hit.has_value() &&
		fill_knife_vertex_hit(session, state, *hit, out_point);
}

bool fill_knife_edge_hit(
		EditorSession& session,
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
	const PolygonObject* object = object_by_id(session.objects(), hit.object_id);
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
		if (!state.has_active_stroke) {
			return false;
		}
		const quader_poly::Edge edge =
			quader_poly::make_edge(hit.edge.a, hit.edge.b);
		PickHitRecord endpoint_hit = hit;
		endpoint_hit.kind = quader_poly::ElementKind::Vertex;
		endpoint_hit.vertex_id = *factor <= 0.01F ? edge.a : edge.b;
		return fill_knife_vertex_hit(session, state, endpoint_hit, out_point);
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

bool fill_knife_edge_target(
		EditorSession& session,
		KnifeToolState& state,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled,
		int target_object_id,
		KnifeResolvedPoint& out_point)
{
	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Edge;
        const std::optional<PickHitRecord> hit =
            session.pick_nearest({ray, options, target_object_id});
	return hit.has_value() && fill_knife_edge_hit(
		session, state, *hit, grid_size, snap_enabled, out_point);
}

bool fill_knife_face_hit(
		EditorSession& session,
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
		object_by_id(session.objects(), surface_hit.object_id);
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

bool fill_knife_face_point(
		EditorSession& session,
		KnifeToolState& state,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis,
		int target_object_id,
		KnifeResolvedPoint& out_point)
{
	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Face;
        const std::optional<PickHitRecord> hit =
            session.pick_nearest({ray, options, target_object_id});
	return hit.has_value() && fill_knife_face_hit(
		session, state, *hit, grid_size, snap_enabled, constrain_axis,
		out_point);
}

KnifeResolvedPoint resolve_knife_point_from_pick(
		EditorSession& session,
		KnifeToolState& state,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis,
		int target_object_id)
{
	KnifeResolvedPoint point;
	if (fill_knife_vertex_target(session, state, ray, target_object_id, point)) {
		return point;
	}
	if (fill_knife_edge_target(session, state, ray, grid_size, snap_enabled,
			target_object_id, point)) {
		return point;
	}
	(void)fill_knife_face_point(session, state, ray, grid_size, snap_enabled,
		constrain_axis, target_object_id, point);
	return point;
}

KnifeResolvedPoint resolve_knife_point_from_hit(
		EditorSession& session,
		KnifeToolState& state,
		const PickHitRecord& hit,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis)
{
	KnifeResolvedPoint point;
	switch (hit.kind) {
	case quader_poly::ElementKind::Vertex:
		(void)fill_knife_vertex_hit(session, state, hit, point);
		break;
	case quader_poly::ElementKind::Edge:
		(void)fill_knife_edge_hit(session, state, hit, grid_size, snap_enabled,
			point);
		break;
	case quader_poly::ElementKind::Face:
		(void)fill_knife_face_hit(session, state, hit, grid_size, snap_enabled,
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
		EditorSession& session,
		KnifeToolState& state,
		SelectionHoverState& hover,
		const KnifeResolvedPoint& point,
		bool active)
{
	state.active = active;
	if (!point.has_point) {
		state.has_current = false;
		state.current_target_valid = false;
                state.status = KnifeToolStatus::HoverMiss;
                hover = {};
		rebuild_knife_overlay(state);
		return false;
	}
	if (state.target_object_id > 0 && point.object_id != state.target_object_id) {
		state.has_current = false;
		state.current_target_valid = false;
                state.status = KnifeToolStatus::InvalidTarget;
                hover = {};
		rebuild_knife_overlay(state);
		return false;
	}
	if (!state.has_active_stroke &&
		point.target.kind !=
			quader_poly::KnifePointTargetKind::ExistingEdge) {
		state.has_current = false;
		state.current_target_valid = false;
		state.current_target = {};
		state.current_point = {};
		state.status = KnifeToolStatus::InvalidTarget;
		hover = {};
		rebuild_knife_overlay(state);
		return false;
	}

	const KnifeSegmentFaceRegion segment_face_region =
		state.has_active_stroke
			? knife_active_segment_face_region(session, state)
			: point.target.kind ==
					quader_poly::KnifePointTargetKind::ExistingEdge
				? knife_target_face_region(
					session, point.object_id, point.target)
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
		state.status = KnifeToolStatus::InvalidTarget;
		hover = {};
		rebuild_knife_overlay(state);
		return false;
	}
	const bool target_valid = point.target_valid && target_in_segment_face;
	state.has_current = true;
	state.current_target_valid = target_valid;
	state.target_object_id = state.target_object_id > 0 ? state.target_object_id : point.object_id;
	state.current_point = point.point;
	state.current_target = normalized_target;
	state.face_normal = point.normal;
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
	const KnifeResolvedPoint point = resolve_knife_point_from_pick(
		session, state, ray, grid_size, snap_enabled, constrain_axis,
		target_object_id);
	return update_knife_preview_from_resolved_point(
		session, state, hover, point, true);
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
	const KnifeResolvedPoint point = resolve_knife_point_from_hit(
		session, state, hit, grid_size, snap_enabled, constrain_axis);
	return update_knife_preview_from_resolved_point(
		session, state, hover, point, true);
}

bool accept_knife_current_point(KnifeToolState& state, double grid_size)
{
	if (!state.has_current || !state.current_target_valid) {
		return false;
	}

	if (!state.has_active_stroke) {
		if (state.current_target.kind !=
				quader_poly::KnifePointTargetKind::ExistingEdge) {
			state.status = KnifeToolStatus::InvalidTarget;
			state.has_current = false;
			state.current_target_valid = false;
			rebuild_knife_overlay(state);
			return false;
		}
		const std::uint32_t point_index = append_knife_point(state);
		state.active_stroke_start = point_index;
		state.previous_point_index = point_index;
		state.has_active_stroke = true;
                state.status = KnifeToolStatus::PreviousSet;
                state.has_current = false;
		state.current_target_valid = false;
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
	rebuild_knife_overlay(state);
	return true;
}

CommandResult knife_segment(
		std::vector<PolygonObject>& objects,
		int object_id,
		const quader_poly::KnifePointTarget& previous,
		const quader_poly::KnifePointTarget& current,
		int& active_object_id,
		SelectionMode& mode)
{
	const int object_index = object_index_by_id(objects, object_id);
	if (object_index < 0) {
		return error_result("Knife segment needs a locked target mesh.");
	}

	PolygonObject& object = objects[object_index];
	quader_poly::Selection selection;
	const quader_poly::OperationResult result = quader_poly::knife_segment(object.document, selection, previous, current);
	if (!result.changed) {
		return error_result(result.message.empty() ? "Could not apply the knife segment." : result.message);
	}

	for (PolygonObject& candidate : objects) {
		candidate.selected = false;
		candidate.selection = quader_poly::Selection();
	}
	object.selected = true;
	object.selection = std::move(selection);
	active_object_id = object.id;
        mode = SelectionMode::Vertex;
        return ok_result(true);
}

CommandResult knife_stroke(
		std::vector<PolygonObject>& objects,
		int object_id,
		std::span<const quader_poly::KnifePointTarget> points,
		std::span<const quader_poly::KnifeStrokeSegment> segments,
		int& active_object_id,
		SelectionMode& mode)
{
	const int object_index = object_index_by_id(objects, object_id);
	if (object_index < 0) {
		return error_result("Knife stroke needs a locked target mesh.");
	}

	PolygonObject& object = objects[object_index];
	quader_poly::Selection selection;
	const quader_poly::OperationResult result = quader_poly::knife_stroke(object.document, selection, points, segments);
	if (!result.changed) {
		return error_result(result.message.empty() ? "Could not apply the knife stroke." : result.message);
	}

	for (PolygonObject& candidate : objects) {
		candidate.selected = false;
		candidate.selection = quader_poly::Selection();
	}
	object.selected = true;
	object.selection = std::move(selection);
	active_object_id = object.id;
        mode = SelectionMode::Vertex;
        return ok_result(true);
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

CommandResult EditorSession::commit_knife_tool(KnifeToolState& state)
{
	return knife_tool_impl().commit(*this, state);
}

CommandResult KnifeToolImpl::commit(EditorSession& session, KnifeToolState& state) const
{
	QDR_PROFILE_SCOPE("qdr_editor_core.KnifeTool.commit");
	if (state.segments.empty()) {
		reset_knife_tool(state);
		return ok_result(false);
	}
	if (state.target_object_id <= 0) {
          state.status = KnifeToolStatus::InvalidTarget;
          rebuild_knife_overlay(state);
          return error_result("Knife needs a locked target mesh.");
	}

	const CommandResult result = session.apply_history_command([&state](EditorSession& editor_session) {
		return editor_session.knife_stroke(
			state.target_object_id,
			state.points,
			state.segments);
	});
	if (!result.ok || !result.changed) {
          state.status = KnifeToolStatus::InvalidTarget;
          rebuild_knife_overlay(state);
          return result.ok ? error_result("Could not apply the knife stroke.")
                           : result;
	}

	reset_knife_tool(state);
	return result;
}

CommandResult EditorSession::knife_segment(int object_id, const quader_poly::KnifePointTarget& previous, const quader_poly::KnifePointTarget& current)
{
	const SelectionMode mode_before = state_.selection_mode;
	const CommandResult result = quader_editor_core::knife_segment(state_.objects, object_id, previous, current, state_.active_object_id, state_.selection_mode);
	note_content_result(result);
	if (result.changed || state_.selection_mode != mode_before) {
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::knife_stroke(int object_id, std::span<const quader_poly::KnifePointTarget> points, std::span<const quader_poly::KnifeStrokeSegment> segments)
{
	const SelectionMode mode_before = state_.selection_mode;
	const CommandResult result = quader_editor_core::knife_stroke(state_.objects, object_id, points, segments, state_.active_object_id, state_.selection_mode);
	note_content_result(result);
	if (result.changed || state_.selection_mode != mode_before) {
		note_selection_changed();
	}
	return result;
}

} // namespace quader_editor_core
