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
#include <array>
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

void clear_knife_hover(KnifeToolState& state, SelectionHoverState& hover)
{
	state.has_current = false;
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
	constexpr std::array kPriorityModes {
		SelectionMode::Vertex,
		SelectionMode::Edge,
		SelectionMode::Face,
	};
	const int effective_target_object_id = target_object_id >= 0
		? target_object_id
		: state.target_object_id;
	const std::optional<PickHitRecord> hit = pick_nearest_priority(
		session.objects(),
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
		(void)fill_knife_vertex_hit(session, state, *hit, point);
		break;
	case quader_poly::ElementKind::Edge:
		(void)fill_knife_edge_hit(
			session, state, *hit, grid_size, snap_enabled, point);
		break;
	case quader_poly::ElementKind::Face:
		(void)fill_knife_face_hit(
			session, state, *hit, grid_size, snap_enabled, constrain_axis,
			point);
		break;
	}
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
			? knife_active_segment_face_region(session, state)
			: knife_target_can_seed_stroke(point.target)
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
