////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "modeling/runtime/session_internal.h"

#include <diagnostics/profile.h>
#include <modeling/interactions/knife_tool.h>
#include <modeling/interactions/tool_snapping.h>
#include <modeling/runtime/tool_operation_contracts.h>

#include <geometry/geometry.h>

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

	[[nodiscard]] KnifeToolPreview preview(const KnifeToolState& state) const;
	void reset(KnifeToolState& state) const;
	void rebuild_overlay(KnifeToolState& state) const;
	[[nodiscard]] bool update_preview_from_pick(EditorSession& session, KnifeToolState& state, SelectionHoverState& hover, const quader_poly::Ray& ray, const ViewportCameraState& camera, double grid_size, bool snap_enabled, bool constrain_axis) const;
	void refresh_intersection_preview(EditorSession& session, KnifeToolState& state, const ViewportCameraState& camera) const;
	[[nodiscard]] bool accept_current_point(EditorSession& session, KnifeToolState& state, const ViewportCameraState& camera, double grid_size) const;
	[[nodiscard]] CommandResult commit(EditorSession& session, KnifeToolState& state) const;
	[[nodiscard]] CommandResult segment(std::vector<PolygonObject>& objects, int object_id, const quader_poly::KnifePointTarget& previous, const quader_poly::KnifePointTarget& current, int& active_object_id, SelectionMode& mode) const;
	[[nodiscard]] CommandResult stroke(std::vector<PolygonObject>& objects, int object_id, std::span<const quader_poly::KnifePointTarget> points, std::span<const quader_poly::KnifeStrokeSegment> segments, int& active_object_id, SelectionMode& mode) const;
	[[nodiscard]] KnifeSegmentPreview build_segment_preview(const std::vector<PolygonObject>& objects, int object_id, const quader_poly::KnifePointTarget& previous, const quader_poly::KnifePointTarget& current) const;
};

const KnifeToolImpl& knife_tool_impl()
{
	static const KnifeToolImpl tool;
	return tool;
}

} // namespace

const EditorTool& knife_tool()
{
	return knife_tool_impl();
}

ViewportInteractionTool KnifeToolImpl::tool_id() const
{
	return ViewportInteractionTool::knife;
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

quader_geometry::Vec3f to_geometry_vec3(quader_poly::Vec3 value)
{
	return { value.x, value.y, value.z };
}

quader_geometry::Vec2d to_geometry_vec2(quader_poly::Vec2 value)
{
	return { static_cast<double>(value.x), static_cast<double>(value.y) };
}

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
			quader_poly::Vec3 {};
	preview.current_point = state.has_current ? state.current_point : quader_poly::Vec3 {};
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
			SelectionOverlayLayer::selected_vertex,
			state.point_positions[index],
			0xff16d9ffU,
			index == state.previous_point_index && state.has_active_stroke ? 10.0F : 8.0F,
		});
	}
	for (const quader_poly::KnifeStrokeSegment& segment : state.segments) {
		if (segment.first_point >= state.point_positions.size() || segment.second_point >= state.point_positions.size()) {
			continue;
		}
		state.preview_lines.push_back({
			SelectionOverlayLayer::hover_edge,
			state.point_positions[segment.first_point],
			state.point_positions[segment.second_point],
			0xff16d9ffU,
			3.0F,
		});
	}
	for (const quader_poly::Vec3& point : state.intersection_preview_points) {
		state.preview_points.push_back({
			SelectionOverlayLayer::selected_vertex,
			point,
			0xff56ff8fU,
			6.0F,
		});
	}
	if (state.has_current) {
		state.preview_points.push_back({
			SelectionOverlayLayer::hover_vertex,
			state.current_point,
			state.current_target_valid ? 0xff56ff8fU : 0xff4f7dffU,
			8.0F,
		});
	}
	if (state.has_active_stroke && state.has_current && state.previous_point_index < state.point_positions.size()) {
		state.preview_lines.push_back({
			SelectionOverlayLayer::hover_edge,
			state.point_positions[state.previous_point_index],
			state.current_point,
			state.current_target_valid ? 0xff16d9ffU : 0xff4f7dffU,
			3.0F,
		});
	}
}

bool knife_object_allowed(const KnifeToolState& state, int object_id)
{
	return object_id > 0 && (state.target_object_id <= 0 || object_id == state.target_object_id);
}

std::optional<quader_poly::Vec3> edge_position_from_factor(
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

quader_poly::Vec3 normalize_or(quader_poly::Vec3 value, quader_poly::Vec3 fallback)
{
	const quader_poly::Vec3 normalized = quader_poly::normalize_or_zero(value);
	return quader_poly::length_squared(normalized) <= transform_point_epsilon ? fallback : normalized;
}

bool knife_object_allowed(const KnifeToolState& state, int object_id);

quader_poly::Vec3 project_point_to_plane(quader_poly::Vec3 point, quader_poly::Vec3 origin, quader_poly::Vec3 normal)
{
	const float normal_length_squared = quader_poly::length_squared(normal);
	if (normal_length_squared <= transform_point_epsilon) {
		return point;
	}

	const float signed_distance = quader_poly::dot(point - origin, normal) / normal_length_squared;
	return point - (normal * signed_distance);
}

std::optional<quader_poly::Vec3> project_point_to_face_plane(
		const quader_poly::Document& document,
		quader_poly::ElementId face_id,
		quader_poly::Vec3 point)
{
	const quader_poly::Face *face = quader_poly::find_face(document, face_id);
	if (face == nullptr || face->vertices.empty()) {
		return std::nullopt;
	}

	const quader_poly::Vertex *origin = quader_poly::find_vertex(document, face->vertices.front());
	if (origin == nullptr) {
		return std::nullopt;
	}

	const quader_poly::Vec3 normal = quader_poly::face_normal(document, *face);
	if (quader_poly::length_squared(normal) <= transform_point_epsilon) {
		return std::nullopt;
	}
	return project_point_to_plane(point, origin->position, normal);
}

quader_poly::Ray projection_ray_through_grid_point(
		const ViewportCameraState& camera,
		const quader_poly::Ray& pick_ray,
		quader_poly::Vec3 grid_point)
{
	const quader_poly::Vec3 fallback_direction = normalize_or(pick_ray.direction, { 0.0F, -1.0F, 0.0F });
	if (camera.projection == CameraProjection::perspective) {
		const quader_poly::Vec3 camera_delta = grid_point - camera.transform.origin;
		return {
			camera.transform.origin,
			normalize_or(camera_delta, fallback_direction),
		};
	}

	const float guide_distance = std::abs(quader_poly::dot(grid_point - pick_ray.origin, fallback_direction));
	const float origin_distance = std::max(1.0F, std::isfinite(guide_distance) ? guide_distance : 1.0F);
	return {
		grid_point - (fallback_direction * origin_distance),
		fallback_direction,
	};
}

std::optional<PickHitRecord> pick_snapped_knife_surface_hit(
		EditorSession& session,
		const KnifeToolState& state,
		const PickHitRecord& original_hit,
		const quader_poly::Ray& ray,
		const ViewportCameraState& camera,
		double grid_size)
{
	const CutGridPointResult grid_point = viewport_grid_point_from_ray(camera, ray, grid_size, true);
	if (!grid_point.ok) {
		return std::nullopt;
	}

	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::face;
	const quader_poly::Ray projected_ray = projection_ray_through_grid_point(camera, ray, grid_point.position);
	const std::optional<PickHitRecord> projected_hit = session.pick_nearest({ projected_ray, options });
	if (!projected_hit.has_value() ||
		projected_hit->kind != quader_poly::ElementKind::face ||
		projected_hit->object_id != original_hit.object_id ||
		!knife_object_allowed(state, projected_hit->object_id)) {
		return std::nullopt;
	}
	return projected_hit;
}

/**
 * Represents a Knife Resolved Point value used by the modal modeling interaction layer.
 */
struct KnifeResolvedPoint {
	bool has_point = false;
	bool target_valid = false;
	int object_id = -1;
	quader_poly::ElementId face_id = quader_poly::invalid_element_id;
	quader_poly::Vec3 point;
	quader_poly::Vec3 normal { 0.0F, 1.0F, 0.0F };
	quader_poly::KnifePointTarget target;
};

bool fill_knife_vertex_target(
		EditorSession& session,
		KnifeToolState& state,
		const quader_poly::Ray& ray,
		KnifeResolvedPoint& out_point)
{
	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::vertex;
	const std::optional<PickHitRecord> hit = session.pick_nearest({ ray, options });
	if (!hit.has_value() || hit->kind != quader_poly::ElementKind::vertex || !knife_object_allowed(state, hit->object_id)) {
		return false;
	}

	const PolygonObject *object = object_by_id(session.objects(), hit->object_id);
	const quader_poly::Vertex *vertex = object != nullptr ? quader_poly::find_vertex(object->document, hit->vertex_id) : nullptr;
	if (vertex == nullptr) {
		return false;
	}

	out_point.has_point = true;
	out_point.target_valid = true;
	out_point.object_id = hit->object_id;
	out_point.face_id = hit->face_id;
	out_point.point = vertex->position;
	out_point.normal = normalize_or(hit->local_normal, { 0.0F, 1.0F, 0.0F });
	out_point.target.kind = quader_poly::KnifePointTargetKind::existing_vertex;
	out_point.target.vertex_id = vertex->id;
	out_point.target.face_id = hit->face_id;
	out_point.target.position = vertex->position;
	return true;
}

bool fill_knife_edge_target(
		EditorSession& session,
		KnifeToolState& state,
		const quader_poly::Ray& ray,
		double grid_size,
		bool snap_enabled,
		KnifeResolvedPoint& out_point)
{
	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::edge;
	const std::optional<PickHitRecord> hit = session.pick_nearest({ ray, options });
	if (!hit.has_value() || hit->kind != quader_poly::ElementKind::edge || !knife_object_allowed(state, hit->object_id)) {
		return false;
	}

	const PolygonObject *object = object_by_id(session.objects(), hit->object_id);
	if (object == nullptr) {
		return false;
	}

	quader_poly::Vec3 factor_point = hit->local_position;
	if (snap_enabled) {
		factor_point = snap_knife_point_to_grid(factor_point, grid_size, true);
	}

	const std::optional<float> factor = quader_poly::edge_factor_from_position(object->document, hit->edge, factor_point);
	const std::optional<quader_poly::Vec3> point = factor.has_value() ?
		edge_position_from_factor(object->document, hit->edge, *factor) :
		std::nullopt;
	if (!factor.has_value() || !point.has_value()) {
		return false;
	}

	out_point.has_point = true;
	out_point.target_valid = true;
	out_point.object_id = hit->object_id;
	out_point.face_id = hit->face_id;
	out_point.point = *point;
	out_point.normal = normalize_or(hit->local_normal, { 0.0F, 1.0F, 0.0F });
	out_point.target.kind = quader_poly::KnifePointTargetKind::existing_edge;
	out_point.target.edge = quader_poly::make_edge(hit->edge.a, hit->edge.b);
	out_point.target.edge_factor = std::clamp(*factor, 0.01F, 0.99F);
	out_point.target.face_id = hit->face_id;
	out_point.target.position = *point;
	return true;
}

bool fill_knife_face_point(
		EditorSession& session,
		KnifeToolState& state,
		const quader_poly::Ray& ray,
		const ViewportCameraState& camera,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis,
		KnifeResolvedPoint& out_point)
{
	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::face;
	const std::optional<PickHitRecord> hit = session.pick_nearest({ ray, options });
	if (!hit.has_value() || hit->kind != quader_poly::ElementKind::face || !knife_object_allowed(state, hit->object_id)) {
		return false;
	}

	PickHitRecord surface_hit = *hit;
	bool used_projected_surface_hit = false;
	if (snap_enabled && !(state.has_active_stroke && constrain_axis && state.previous_point_index < state.point_positions.size())) {
		const std::optional<PickHitRecord> projected_hit =
			pick_snapped_knife_surface_hit(session, state, *hit, ray, camera, grid_size);
		if (projected_hit.has_value()) {
			surface_hit = *projected_hit;
			used_projected_surface_hit = true;
		}
	}

	const PolygonObject *object = object_by_id(session.objects(), surface_hit.object_id);
	quader_poly::Vec3 point = surface_hit.local_position;
	if (state.has_active_stroke && constrain_axis && state.previous_point_index < state.point_positions.size()) {
		point = constrain_knife_point_to_axis(
			state.point_positions[state.previous_point_index],
			point,
			state.face_normal,
			grid_size,
			snap_enabled);
	} else {
		if (!used_projected_surface_hit) {
			point = snap_knife_point_to_grid(point, grid_size, snap_enabled);
		}
	}
	if (object != nullptr) {
		const std::optional<quader_poly::Vec3> face_point =
			project_point_to_face_plane(object->document, surface_hit.face_id, point);
		if (face_point.has_value()) {
			point = *face_point;
		}
	}

	out_point.has_point = true;
	out_point.target_valid = true;
	out_point.object_id = surface_hit.object_id;
	out_point.face_id = surface_hit.face_id;
	out_point.point = point;
	out_point.normal = normalize_or(surface_hit.local_normal, { 0.0F, 1.0F, 0.0F });
	out_point.target.kind = quader_poly::KnifePointTargetKind::face_point;
	out_point.target.face_id = surface_hit.face_id;
	out_point.target.position = point;
	return true;
}

KnifeResolvedPoint resolve_knife_point_from_pick(
		EditorSession& session,
		KnifeToolState& state,
		const quader_poly::Ray& ray,
		const ViewportCameraState& camera,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis)
{
	KnifeResolvedPoint point;
	if (fill_knife_vertex_target(session, state, ray, point)) {
		return point;
	}
	if (fill_knife_edge_target(session, state, ray, grid_size, snap_enabled, point)) {
		return point;
	}
	(void)fill_knife_face_point(session, state, ray, camera, grid_size, snap_enabled, constrain_axis, point);
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
	return quader_poly::length_squared(state.current_point - state.point_positions[state.active_stroke_start]) <= threshold * threshold;
}

std::uint32_t append_knife_point(KnifeToolState& state)
{
	state.points.push_back(state.current_target);
	state.point_positions.push_back(state.current_point);
	return static_cast<std::uint32_t>(state.points.size() - 1U);
}

std::uint32_t append_knife_point(KnifeToolState& state, quader_poly::KnifePointTarget target, quader_poly::Vec3 position)
{
	state.points.push_back(target);
	state.point_positions.push_back(position);
	return static_cast<std::uint32_t>(state.points.size() - 1U);
}

std::optional<quader_poly::Edge> shared_face_edge(const quader_poly::Face& first, const quader_poly::Face& second)
{
	if (first.vertices.size() < 2U || second.vertices.size() < 2U) {
		return std::nullopt;
	}
	for (std::size_t index = 0; index < first.vertices.size(); ++index) {
		const quader_poly::Edge edge = quader_poly::make_edge(
			first.vertices[index],
			first.vertices[(index + 1U) % first.vertices.size()]);
		if (face_contains_edge(second, edge)) {
			return edge;
		}
	}
	return std::nullopt;
}

bool knife_target_is_on_edge(const quader_poly::KnifePointTarget& target, quader_poly::Edge edge)
{
	edge = quader_poly::make_edge(edge.a, edge.b);
	if ((target.kind == quader_poly::KnifePointTargetKind::existing_vertex ||
			target.kind == quader_poly::KnifePointTargetKind::inserted_vertex) &&
		(target.vertex_id == edge.a || target.vertex_id == edge.b)) {
		return true;
	}
	return target.kind == quader_poly::KnifePointTargetKind::existing_edge &&
		quader_poly::make_edge(target.edge.a, target.edge.b) == edge;
}

float edge_factor_nearest_segment(
		const quader_poly::Document& document,
		quader_poly::Edge edge,
		quader_poly::Vec3 segment_start,
		quader_poly::Vec3 segment_end)
{
	edge = quader_poly::make_edge(edge.a, edge.b);
	const quader_poly::Vertex *edge_start = quader_poly::find_vertex(document, edge.a);
	const quader_poly::Vertex *edge_end = quader_poly::find_vertex(document, edge.b);
	if (edge_start == nullptr || edge_end == nullptr) {
		return 0.5F;
	}

	quader_geometry::SegmentSegmentFactorOptions<float> options;
	options.length_squared_epsilon = transform_point_epsilon;
	options.closest_epsilon = transform_point_epsilon;
	options.parameter_epsilon = transform_point_epsilon;
	options.min_factor = 0.01F;
	options.max_factor = 0.99F;

	const quader_geometry::SegmentFactorClosestPoint3<float> closest =
		quader_geometry::closest_factor_on_segment_to_segment<float>(
			{ to_geometry_vec3(edge_start->position), to_geometry_vec3(edge_end->position) },
			{ to_geometry_vec3(segment_start), to_geometry_vec3(segment_end) },
			options);
	return closest.valid ? closest.segment_factor : 0.5F;
}

std::optional<float> edge_factor_from_camera_cut_plane(
		const ViewportCameraState& camera,
		const quader_poly::Document& document,
		quader_poly::Edge edge,
		quader_poly::Vec3 segment_start,
		quader_poly::Vec3 segment_end)
{
	edge = quader_poly::make_edge(edge.a, edge.b);
	const quader_poly::Vertex *edge_start = quader_poly::find_vertex(document, edge.a);
	const quader_poly::Vertex *edge_end = quader_poly::find_vertex(document, edge.b);
	if (edge_start == nullptr || edge_end == nullptr) {
		return std::nullopt;
	}

	const quader_poly::Vec3 camera_origin = camera.transform.origin;
	const quader_poly::Vec3 camera_forward = normalize_or(camera.transform.z_axis * -1.0F, { 0.0F, 0.0F, -1.0F });
	const quader_poly::Vec3 segment_delta = segment_end - segment_start;
	quader_poly::Vec3 cut_plane_normal;
	if (camera.projection == CameraProjection::orthographic) {
		cut_plane_normal = quader_poly::cross(segment_delta, camera_forward);
	} else {
		cut_plane_normal = quader_poly::cross(segment_start - camera_origin, segment_end - camera_origin);
	}
	if (quader_poly::length_squared(cut_plane_normal) <= transform_point_epsilon) {
		return std::nullopt;
	}

	const quader_geometry::Plane3<float> cut_plane = quader_geometry::plane_from_point_normal(
		to_geometry_vec3(segment_start),
		to_geometry_vec3(cut_plane_normal),
		transform_point_epsilon);
	const quader_geometry::SegmentPlaneHit<float> hit = quader_geometry::intersect_segment_plane(
		quader_geometry::Segment3<float> { to_geometry_vec3(edge_start->position), to_geometry_vec3(edge_end->position) },
		cut_plane,
		transform_point_epsilon);
	if (hit.type != quader_geometry::PlaneIntersectionType::point) {
		return std::nullopt;
	}

	const float factor = hit.segment_factor;
	if (!std::isfinite(factor) || factor <= 0.01F || factor >= 0.99F) {
		return std::nullopt;
	}
	return std::clamp(factor, 0.01F, 0.99F);
}

/**
 * Represents a Knife Screen Edge Hit value used by the modal modeling interaction layer.
 */
struct KnifeScreenEdgeHit {
	quader_poly::Edge edge;
	float edge_factor = 0.5F;
	quader_poly::ElementId face_id = quader_poly::invalid_element_id;
	double segment_factor = 0.0;
	quader_poly::Vec3 position;
};

double vec2_distance_squared(quader_poly::Vec2 left, quader_poly::Vec2 right)
{
	return quader_geometry::length_squared(to_geometry_vec2(left) - to_geometry_vec2(right));
}

std::optional<std::pair<double, double>> screen_segment_intersection_factors(
		quader_poly::Vec2 line_start,
		quader_poly::Vec2 line_end,
		quader_poly::Vec2 edge_start,
		quader_poly::Vec2 edge_end)
{
	const quader_geometry::ClassifiedSegmentIntersection2<double> intersection =
		quader_geometry::intersect_segments_2d(
			to_geometry_vec2(line_start),
			to_geometry_vec2(line_end),
			to_geometry_vec2(edge_start),
			to_geometry_vec2(edge_end));
	if (!intersection.hit || intersection.kind != quader_geometry::SegmentIntersectionKind2::point) {
		return std::nullopt;
	}

	const double line_factor = intersection.first_factors[0];
	const double edge_factor = intersection.second_factors[0];
	constexpr double endpoint_tolerance = 0.0001;
	if (line_factor <= endpoint_tolerance ||
		line_factor >= 1.0 - endpoint_tolerance ||
		edge_factor <= 0.01 ||
		edge_factor >= 0.99) {
		return std::nullopt;
	}

	return std::pair { line_factor, edge_factor };
}

std::optional<quader_poly::ElementId> knife_screen_edge_hit_face(
		EditorSession& session,
		int object_id,
		const ViewportCameraState& camera,
		quader_poly::Edge edge,
		quader_poly::Vec2 screen_hit)
{
	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::face;
	const std::optional<PickHitRecord> surface_hit =
		session.pick_nearest({ ray_from_viewport_screen_point(camera, screen_hit), options });
	if (!surface_hit.has_value() || surface_hit->object_id != object_id) {
		return std::nullopt;
	}

	const PolygonObject *object = object_by_id(session.objects(), object_id);
	const quader_poly::Face *face = object != nullptr ? quader_poly::find_face(object->document, surface_hit->face_id) : nullptr;
	return face != nullptr && face_contains_edge(*face, edge) ? std::optional<quader_poly::ElementId> { face->id } : std::nullopt;
}

std::vector<KnifeScreenEdgeHit> collect_knife_screen_edge_hits(
		EditorSession& session,
		KnifeToolState& state,
		const ViewportCameraState& camera)
{
	std::vector<KnifeScreenEdgeHit> hits;
	if (!state.has_active_stroke ||
		!state.has_current ||
		state.previous_point_index >= state.points.size() ||
		state.previous_point_index >= state.point_positions.size() ||
		state.target_object_id <= 0) {
		return hits;
	}

	const PolygonObject *object = object_by_id(session.objects(), state.target_object_id);
	if (object == nullptr) {
		return hits;
	}

	quader_poly::Vec2 line_start;
	quader_poly::Vec2 line_end;
	if (!project_viewport_point(camera, state.point_positions[state.previous_point_index], line_start) ||
		!project_viewport_point(camera, state.current_point, line_end) ||
		vec2_distance_squared(line_start, line_end) <= 1.0) {
		return hits;
	}

	const quader_poly::KnifePointTarget& previous_target = state.points[state.previous_point_index];
	const quader_poly::KnifePointTarget& current_target = state.current_target;
	for (quader_poly::Edge edge : quader_poly::document_edges(object->document)) {
		edge = quader_poly::make_edge(edge.a, edge.b);
		if (edge.a == quader_poly::invalid_element_id ||
			edge.b == quader_poly::invalid_element_id ||
			knife_target_is_on_edge(previous_target, edge) ||
			knife_target_is_on_edge(current_target, edge)) {
			continue;
		}

		const quader_poly::Vertex *first = quader_poly::find_vertex(object->document, edge.a);
		const quader_poly::Vertex *second = quader_poly::find_vertex(object->document, edge.b);
		if (first == nullptr || second == nullptr ||
			viewport_point_is_behind_camera(camera, first->position) ||
			viewport_point_is_behind_camera(camera, second->position)) {
			continue;
		}

		quader_poly::Vec2 edge_start;
		quader_poly::Vec2 edge_end;
		if (!project_viewport_point(camera, first->position, edge_start) ||
			!project_viewport_point(camera, second->position, edge_end)) {
			continue;
		}

		const std::optional<std::pair<double, double>> factors =
			screen_segment_intersection_factors(line_start, line_end, edge_start, edge_end);
		if (!factors.has_value()) {
			continue;
		}

		const quader_poly::Vec2 screen_hit {
			static_cast<float>(static_cast<double>(line_start.x) + (static_cast<double>(line_end.x - line_start.x) * factors->first)),
			static_cast<float>(static_cast<double>(line_start.y) + (static_cast<double>(line_end.y - line_start.y) * factors->first)),
		};
		const std::optional<quader_poly::ElementId> visible_face_id =
			knife_screen_edge_hit_face(session, state.target_object_id, camera, edge, screen_hit);
		if (!visible_face_id.has_value()) {
			continue;
		}

		const std::optional<float> cut_plane_edge_factor = edge_factor_from_camera_cut_plane(
			camera,
			object->document,
			edge,
			state.point_positions[state.previous_point_index],
			state.current_point);
		if (!cut_plane_edge_factor.has_value()) {
			continue;
		}

		const float edge_factor = *cut_plane_edge_factor;
		const std::optional<quader_poly::Vec3> position = edge_position_from_factor(object->document, edge, edge_factor);
		if (!position.has_value() ||
			quader_poly::length_squared(*position - state.point_positions[state.previous_point_index]) <= transform_point_epsilon ||
			quader_poly::length_squared(*position - state.current_point) <= transform_point_epsilon) {
			continue;
		}

		const auto duplicate = std::find_if(hits.begin(), hits.end(), [edge, edge_factor](const KnifeScreenEdgeHit& hit) {
			return hit.edge == edge || (std::abs(hit.edge_factor - edge_factor) <= 0.0001F && hit.edge == edge);
		});
		if (duplicate != hits.end()) {
			continue;
		}

		hits.push_back({
			edge,
			edge_factor,
			*visible_face_id,
			factors->first,
			*position,
		});
	}

	std::sort(hits.begin(), hits.end(), [](const KnifeScreenEdgeHit& left, const KnifeScreenEdgeHit& right) {
		if (std::abs(left.segment_factor - right.segment_factor) > 0.000001) {
			return left.segment_factor < right.segment_factor;
		}
		if (left.edge.a != right.edge.a) {
			return left.edge.a < right.edge.a;
		}
		return left.edge.b < right.edge.b;
	});
	return hits;
}

bool append_knife_screen_edge_hits(EditorSession& session, KnifeToolState& state, const ViewportCameraState& camera)
{
	bool appended = false;
	for (const KnifeScreenEdgeHit& hit : collect_knife_screen_edge_hits(session, state, camera)) {
		if (state.previous_point_index < state.point_positions.size() &&
			quader_poly::length_squared(hit.position - state.point_positions[state.previous_point_index]) <= transform_point_epsilon) {
			continue;
		}

		quader_poly::KnifePointTarget target;
		target.kind = quader_poly::KnifePointTargetKind::existing_edge;
		target.edge = hit.edge;
		target.edge_factor = hit.edge_factor;
		target.face_id = hit.face_id;
		target.position = hit.position;

		const std::uint32_t hit_index = append_knife_point(state, target, hit.position);
		if (hit_index != state.previous_point_index) {
			state.segments.push_back({
				state.previous_point_index,
				hit_index,
			});
			state.previous_point_index = hit_index;
			appended = true;
		}
	}
	return appended;
}

std::optional<std::uint32_t> append_knife_boundary_crossing_point(EditorSession& session, KnifeToolState& state)
{
	if (!state.has_active_stroke ||
		state.previous_point_index >= state.points.size() ||
		state.previous_point_index >= state.point_positions.size() ||
		state.target_object_id <= 0) {
		return std::nullopt;
	}

	const PolygonObject *object = object_by_id(session.objects(), state.target_object_id);
	if (object == nullptr) {
		return std::nullopt;
	}

	const quader_poly::KnifePointTarget& previous_target = state.points[state.previous_point_index];
	const quader_poly::KnifePointTarget& current_target = state.current_target;
	if (previous_target.face_id == quader_poly::invalid_element_id ||
		current_target.face_id == quader_poly::invalid_element_id ||
		previous_target.face_id == current_target.face_id) {
		return std::nullopt;
	}

	const quader_poly::Face *previous_face = quader_poly::find_face(object->document, previous_target.face_id);
	const quader_poly::Face *current_face = quader_poly::find_face(object->document, current_target.face_id);
	if (previous_face == nullptr || current_face == nullptr) {
		return std::nullopt;
	}

	const std::optional<quader_poly::Edge> crossing_edge = shared_face_edge(*previous_face, *current_face);
	if (!crossing_edge.has_value() ||
		knife_target_is_on_edge(previous_target, *crossing_edge) ||
		knife_target_is_on_edge(current_target, *crossing_edge)) {
		return std::nullopt;
	}

	const float factor = edge_factor_nearest_segment(
		object->document,
		*crossing_edge,
		state.point_positions[state.previous_point_index],
		state.current_point);
	const std::optional<quader_poly::Vec3> crossing_position = edge_position_from_factor(object->document, *crossing_edge, factor);
	if (!crossing_position.has_value() ||
		quader_poly::length_squared(*crossing_position - state.point_positions[state.previous_point_index]) <= transform_point_epsilon ||
		quader_poly::length_squared(*crossing_position - state.current_point) <= transform_point_epsilon) {
		return std::nullopt;
	}

	quader_poly::KnifePointTarget crossing_target;
	crossing_target.kind = quader_poly::KnifePointTargetKind::existing_edge;
	crossing_target.edge = quader_poly::make_edge(crossing_edge->a, crossing_edge->b);
	crossing_target.edge_factor = factor;
	crossing_target.face_id = quader_poly::invalid_element_id;
	crossing_target.position = *crossing_position;

	const std::uint32_t crossing_index = append_knife_point(state, crossing_target, *crossing_position);
	if (crossing_index != state.previous_point_index) {
		state.segments.push_back({
			state.previous_point_index,
			crossing_index,
		});
		state.previous_point_index = crossing_index;
	}
	return crossing_index;
}

bool update_knife_preview_from_pick(
		EditorSession& session,
		KnifeToolState& state,
		SelectionHoverState& hover,
		const quader_poly::Ray& ray,
		const ViewportCameraState& camera,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis)
{
	state.active = true;
	const KnifeResolvedPoint point = resolve_knife_point_from_pick(session, state, ray, camera, grid_size, snap_enabled, constrain_axis);
	if (!point.has_point) {
		state.has_current = false;
		state.current_target_valid = false;
		state.intersection_preview_points.clear();
		state.status = KnifeToolStatus::hover_miss;
		hover = {};
		rebuild_knife_overlay(state);
		return false;
	}
	if (state.target_object_id > 0 && point.object_id != state.target_object_id) {
		state.has_current = false;
		state.current_target_valid = false;
		state.intersection_preview_points.clear();
		state.status = KnifeToolStatus::invalid_target;
		hover = {};
		rebuild_knife_overlay(state);
		return false;
	}

	const bool target_valid = point.target_valid;
	state.has_current = true;
	state.current_target_valid = target_valid;
	state.target_object_id = state.target_object_id > 0 ? state.target_object_id : point.object_id;
	state.current_point = point.point;
	state.current_target = point.target;
	state.face_normal = point.normal;
	state.status = target_valid ? KnifeToolStatus::preview : KnifeToolStatus::invalid_target;
	hover = {};
	hover.vertex_id = quader_poly::invalid_element_id;
	hover.edge = {};
	if (point.target_valid && point.target.kind == quader_poly::KnifePointTargetKind::existing_vertex) {
		hover.object_id = point.object_id;
		hover.kind = SelectionHoverKind::vertex;
		hover.vertex_id = point.target.vertex_id;
		hover.face_id = point.face_id;
		hover.candidate_object_id = point.object_id;
	} else if (point.target_valid && point.target.kind == quader_poly::KnifePointTargetKind::existing_edge) {
		hover.object_id = point.object_id;
		hover.kind = SelectionHoverKind::edge;
		hover.edge = point.target.edge;
		hover.face_id = point.face_id;
		hover.candidate_object_id = point.object_id;
	}
	hover.selected_modifier_pressed = false;
	rebuild_knife_overlay(state);
	return target_valid;
}

void refresh_knife_intersection_preview(EditorSession& session, KnifeToolState& state, const ViewportCameraState& camera)
{
	state.intersection_preview_points.clear();
	if (state.has_active_stroke && state.has_current && state.current_target_valid) {
		for (const KnifeScreenEdgeHit& hit : collect_knife_screen_edge_hits(session, state, camera)) {
			const auto duplicate = std::find_if(
				state.intersection_preview_points.begin(),
				state.intersection_preview_points.end(),
				[&hit](const quader_poly::Vec3& point) {
					return quader_poly::length_squared(point - hit.position) <= transform_point_epsilon;
				});
			if (duplicate == state.intersection_preview_points.end()) {
				state.intersection_preview_points.push_back(hit.position);
			}
		}
	}
	rebuild_knife_overlay(state);
}

bool accept_knife_current_point(EditorSession& session, KnifeToolState& state, const ViewportCameraState& camera, double grid_size)
{
	if (!state.has_current || !state.current_target_valid) {
		return false;
	}

	if (!state.has_active_stroke) {
		const std::uint32_t point_index = append_knife_point(state);
		state.active_stroke_start = point_index;
		state.previous_point_index = point_index;
		state.has_active_stroke = true;
		state.status = KnifeToolStatus::previous_set;
		state.has_current = false;
		state.current_target_valid = false;
		state.intersection_preview_points.clear();
		rebuild_knife_overlay(state);
		return true;
	}

	if (knife_current_closes_active_stroke(state, grid_size)) {
		if (!append_knife_screen_edge_hits(session, state, camera)) {
			(void)append_knife_boundary_crossing_point(session, state);
		}
		if (state.previous_point_index != state.active_stroke_start) {
			state.segments.push_back({
				state.previous_point_index,
				state.active_stroke_start,
			});
		}
		state.has_active_stroke = false;
		state.status = KnifeToolStatus::previous_set;
	} else {
		if (!append_knife_screen_edge_hits(session, state, camera)) {
			(void)append_knife_boundary_crossing_point(session, state);
		}
		const std::uint32_t point_index = append_knife_point(state);
		if (point_index != state.previous_point_index) {
			state.segments.push_back({
				state.previous_point_index,
				point_index,
			});
		}
		state.previous_point_index = point_index;
		state.status = KnifeToolStatus::previous_set;
	}

	state.has_current = false;
	state.current_target_valid = false;
	state.intersection_preview_points.clear();
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
	mode = SelectionMode::vertex;
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
	mode = SelectionMode::vertex;
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
			SelectionOverlayLayer::hover_edge,
			line.start,
			line.end,
			line.color,
			line.width_pixels,
		});
	}
	return preview;
}

KnifeToolPreview KnifeToolImpl::preview(const KnifeToolState& state) const
{
	return knife_tool_preview(state);
}

void KnifeToolImpl::reset(KnifeToolState& state) const
{
	reset_knife_tool(state);
}

void KnifeToolImpl::rebuild_overlay(KnifeToolState& state) const
{
	rebuild_knife_overlay(state);
}

bool KnifeToolImpl::update_preview_from_pick(
		EditorSession& session,
		KnifeToolState& state,
		SelectionHoverState& hover,
		const quader_poly::Ray& ray,
		const ViewportCameraState& camera,
		double grid_size,
		bool snap_enabled,
		bool constrain_axis) const
{
	QDR_PROFILE_SCOPE("qdr_editor_core.KnifeTool.update_preview_from_pick");
	return update_knife_preview_from_pick(session, state, hover, ray, camera, grid_size, snap_enabled, constrain_axis);
}

void KnifeToolImpl::refresh_intersection_preview(EditorSession& session, KnifeToolState& state, const ViewportCameraState& camera) const
{
	refresh_knife_intersection_preview(session, state, camera);
}

bool KnifeToolImpl::accept_current_point(EditorSession& session, KnifeToolState& state, const ViewportCameraState& camera, double grid_size) const
{
	return accept_knife_current_point(session, state, camera, grid_size);
}

CommandResult KnifeToolImpl::segment(
		std::vector<PolygonObject>& objects,
		int object_id,
		const quader_poly::KnifePointTarget& previous,
		const quader_poly::KnifePointTarget& current,
		int& active_object_id,
		SelectionMode& mode) const
{
	return knife_segment(objects, object_id, previous, current, active_object_id, mode);
}

CommandResult KnifeToolImpl::stroke(
		std::vector<PolygonObject>& objects,
		int object_id,
		std::span<const quader_poly::KnifePointTarget> points,
		std::span<const quader_poly::KnifeStrokeSegment> segments,
		int& active_object_id,
		SelectionMode& mode) const
{
	return knife_stroke(objects, object_id, points, segments, active_object_id, mode);
}

KnifeSegmentPreview KnifeToolImpl::build_segment_preview(
		const std::vector<PolygonObject>& objects,
		int object_id,
		const quader_poly::KnifePointTarget& previous,
		const quader_poly::KnifePointTarget& current) const
{
	return build_knife_segment_preview(objects, object_id, previous, current);
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
		state.status = KnifeToolStatus::invalid_target;
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
		state.status = KnifeToolStatus::invalid_target;
		rebuild_knife_overlay(state);
		return result.ok ? error_result("Could not apply the knife stroke.") : result;
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
