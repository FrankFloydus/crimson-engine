#include <quader_editor_core/tool/measurement_tool.h>
#include <quader_editor_core/session.h>
#include <quader_editor_core/tool/box_tool.h>
#include <quader_editor_core/tool/move_tool.h>
#include <quader_editor_core/tool/rotate_tool.h>
#include <quader_editor_core/tool/scale_tool.h>
#include <quader_editor_core/tool/tool_transform_math.h>
#include <quader_editor_core/tool/transform_drag.h>

#include <quader_geometry/geometry.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace quader_editor_core {

namespace {

constexpr double measurement_geometry_epsilon = 0.000001;
constexpr double face_measurement_normal_world_offset = 0.18;
constexpr double face_measurement_screen_offset = 16.0;

struct MeasurementLabelSource {
	quader_poly::Vec3 world_position;
	std::vector<quader_poly::Vec3> candidates;
	quader_poly::Vec3 face_normal;
	bool has_face_normal = false;
	bool screen_outer = false;
	TransformAxis axis = TransformAxis::none;
	bool text_color = false;
	std::string text;
};

quader_geometry::Vec2d to_geometry_vec2(quader_poly::Vec2 value)
{
	return quader_geometry::Vec2d(value);
}

quader_geometry::Vec3d to_geometry_vec3(quader_poly::Vec3 value)
{
	return quader_geometry::Vec3d(value);
}

quader_geometry::Vec3f to_geometry_vec3f(quader_poly::Vec3 value)
{
	return quader_geometry::Vec3f(value);
}

quader_poly::Vec3 to_poly_vec3(quader_geometry::Vec3f value)
{
	return { value.x, value.y, value.z };
}

TransformAxis to_transform_axis(quader_geometry::Axis axis)
{
	switch (axis) {
		case quader_geometry::Axis::x:
			return TransformAxis::x;
		case quader_geometry::Axis::y:
			return TransformAxis::y;
		case quader_geometry::Axis::z:
			return TransformAxis::z;
	}
	return TransformAxis::z;
}

quader_poly::Vec3 normalize_or(quader_poly::Vec3 value, quader_poly::Vec3 fallback)
{
	const quader_geometry::Vec3f geometry_value = to_geometry_vec3f(value);
	if (quader_geometry::length(geometry_value) <= static_cast<float>(measurement_geometry_epsilon)) {
		return fallback;
	}
	return to_poly_vec3(quader_geometry::normalize_or_zero(geometry_value, static_cast<float>(measurement_geometry_epsilon)));
}

bool transform_axis_is_plane_local(TransformAxis axis)
{
	return axis == TransformAxis::xy ||
		axis == TransformAxis::xz ||
		axis == TransformAxis::yz;
}

bool document_bounds(const quader_poly::Document& document, quader_poly::Vec3& out_min, quader_poly::Vec3& out_max)
{
	quader_geometry::Aabb3f bounds = quader_geometry::empty_aabb3<float>();
	for (const quader_poly::Vertex& vertex : document.vertices) {
		quader_geometry::aabb_include(bounds, to_geometry_vec3f(vertex.position));
	}
	if (!quader_geometry::aabb_is_valid(bounds)) {
		return false;
	}
	out_min = to_poly_vec3(bounds.min);
	out_max = to_poly_vec3(bounds.max);
	return true;
}

void include_bounds_point(Bounds3& bounds, quader_poly::Vec3 point)
{
	if (bounds.empty) {
		bounds.empty = false;
		bounds.min = point;
		bounds.max = point;
		return;
	}
	bounds.min.x = std::min(bounds.min.x, point.x);
	bounds.min.y = std::min(bounds.min.y, point.y);
	bounds.min.z = std::min(bounds.min.z, point.z);
	bounds.max.x = std::max(bounds.max.x, point.x);
	bounds.max.y = std::max(bounds.max.y, point.y);
	bounds.max.z = std::max(bounds.max.z, point.z);
}

Bounds3 transform_bounds(const Bounds3& bounds, const quader_poly::Transform3& transform)
{
	if (bounds.empty) {
		return bounds;
	}

	Bounds3 transformed;
	const float xs[] = { bounds.min.x, bounds.max.x };
	const float ys[] = { bounds.min.y, bounds.max.y };
	const float zs[] = { bounds.min.z, bounds.max.z };
	for (float x : xs) {
		for (float y : ys) {
			for (float z : zs) {
				include_bounds_point(transformed, transform_point(transform, { x, y, z }));
			}
		}
	}
	return transformed;
}

bool scene_transform_preview_transform(const TransformDragState& drag, quader_poly::Transform3& out_transform)
{
	out_transform = {};
	if (!drag.active ||
			!drag.scene_transform_preview_eligible ||
			drag.operation != TransformDragOperation::transform) {
		return false;
	}

	if (drag.tool == ViewportInteractionTool::move) {
		out_transform.origin = drag.move.preview_delta;
		return true;
	}
	if (drag.tool == ViewportInteractionTool::rotate && drag.rotate.active) {
		RotateToolState rotate = drag.rotate;
		const RotatePreviewResult preview =
			preview_rotate_from_angle_degrees(rotate, drag.rotate.preview_angle_degrees, 0.0);
		if (!preview.ok) {
			return false;
		}
		out_transform = preview.pivoted_transform;
		return true;
	}
	if (drag.tool == ViewportInteractionTool::scale && drag.scale.active) {
		ScaleToolState scale = drag.scale;
		const ScalePreviewResult preview = preview_scale_from_scale(scale, drag.scale.preview_scale);
		if (!preview.ok) {
			return false;
		}
		out_transform = preview.pivoted_transform;
		return true;
	}
	return false;
}

Bounds3 selected_measurement_display_bounds(const MeasurementFrameInput& input, const Bounds3& bounds)
{
	if (input.transform_drag == nullptr || bounds.empty) {
		return bounds;
	}
	quader_poly::Transform3 transform;
	if (!scene_transform_preview_transform(*input.transform_drag, transform)) {
		return bounds;
	}
	return transform_bounds(bounds, transform);
}

void append_world_label(
		std::vector<MeasurementLabelSource>& labels,
		quader_poly::Vec3 world_position,
		std::string text,
		TransformAxis axis,
		bool text_color = false)
{
	if (text.empty()) {
		return;
	}
	MeasurementLabelSource label;
	label.world_position = world_position;
	label.candidates.push_back(world_position);
	label.text = std::move(text);
	label.axis = axis;
	label.text_color = text_color;
	labels.push_back(std::move(label));
}

bool append_edge_measurement_label(
		std::vector<MeasurementLabelSource>& labels,
		const quader_poly::Document& document,
		quader_poly::ElementId a_id,
		quader_poly::ElementId b_id,
		double map_units_per_world_unit,
		bool force_decimals)
{
	const quader_poly::Vertex *a = quader_poly::find_vertex(document, a_id);
	const quader_poly::Vertex *b = quader_poly::find_vertex(document, b_id);
	if (a == nullptr || b == nullptr) {
		return false;
	}
	const quader_poly::Vec3 edge = b->position - a->position;
	const double world_length = quader_geometry::length(to_geometry_vec3(edge));
	if (world_length <= measurement_geometry_epsilon) {
		return false;
	}
	append_world_label(
			labels,
			(a->position + b->position) * 0.5F,
			measurement_length_text(world_length, map_units_per_world_unit, force_decimals),
			to_transform_axis(quader_geometry::dominant_axis(to_geometry_vec3f(edge))));
	return true;
}

bool edge_directions_match(quader_poly::Vec3 existing, quader_poly::Vec3 direction)
{
	return std::abs(quader_geometry::dot(to_geometry_vec3(existing), to_geometry_vec3(direction))) >= 0.985F;
}

void append_face_dimension_measurements(
		std::vector<MeasurementLabelSource>& labels,
		const quader_poly::Document& document,
		const quader_poly::Selection& selection,
		double map_units_per_world_unit)
{
	struct EdgeGroup {
		quader_poly::Vec3 direction;
		std::string text;
		TransformAxis axis = TransformAxis::none;
		std::vector<quader_poly::Vec3> candidates;
	};

	for (const quader_poly::ElementId face_id : selection.faces) {
		const quader_poly::Face *face = quader_poly::find_face(document, face_id);
		if (face == nullptr || face->vertices.size() < 2) {
			continue;
		}
		quader_poly::Vec3 face_normal = quader_poly::face_normal(document, *face);
		if (quader_geometry::length_squared(to_geometry_vec3(face_normal)) > measurement_geometry_epsilon) {
			face_normal = normalize_or(face_normal, { 0.0F, 1.0F, 0.0F });
		}
		std::vector<EdgeGroup> groups;
		groups.reserve(2);
		for (std::size_t index = 0; index < face->vertices.size(); ++index) {
			const quader_poly::ElementId a_id = face->vertices[index];
			const quader_poly::ElementId b_id = face->vertices[(index + 1) % face->vertices.size()];
			const quader_poly::Vertex *a = quader_poly::find_vertex(document, a_id);
			const quader_poly::Vertex *b = quader_poly::find_vertex(document, b_id);
			if (a == nullptr || b == nullptr) {
				continue;
			}
			const quader_poly::Vec3 edge = b->position - a->position;
			const double edge_length = quader_geometry::length(to_geometry_vec3(edge));
			if (edge_length <= measurement_geometry_epsilon) {
				continue;
			}
			const quader_poly::Vec3 direction = edge / static_cast<float>(edge_length);
			EdgeGroup *matching_group = nullptr;
			for (EdgeGroup& group : groups) {
				if (edge_directions_match(group.direction, direction)) {
					matching_group = &group;
					break;
				}
			}
			const quader_poly::Vec3 midpoint = (a->position + b->position) * 0.5F;
			if (matching_group != nullptr) {
				matching_group->candidates.push_back(midpoint);
				continue;
			}
			EdgeGroup group;
			group.direction = direction;
			group.text = measurement_length_text(edge_length, map_units_per_world_unit, false);
			group.axis = to_transform_axis(quader_geometry::dominant_axis(to_geometry_vec3f(edge)));
			group.candidates.push_back(midpoint);
			groups.push_back(std::move(group));
		}
		for (std::size_t group_index = 0; group_index < groups.size() && group_index < 2; ++group_index) {
			const EdgeGroup& group = groups[group_index];
			if (group.candidates.empty()) {
				continue;
			}
			MeasurementLabelSource label;
			label.world_position = group.candidates[0];
			label.candidates = group.candidates;
			label.text = group.text;
			label.axis = group.axis;
			label.screen_outer = true;
			if (quader_geometry::length_squared(to_geometry_vec3(face_normal)) > measurement_geometry_epsilon) {
				label.face_normal = face_normal;
				label.has_face_normal = true;
			}
			labels.push_back(std::move(label));
		}
	}
}

void append_dimension_measurement(
		std::vector<MeasurementLabelSource>& labels,
		const std::vector<quader_poly::Vec3>& candidates,
		double world_length,
		TransformAxis axis,
		double map_units_per_world_unit)
{
	if (world_length <= measurement_geometry_epsilon || candidates.empty()) {
		return;
	}
	MeasurementLabelSource label;
	label.world_position = candidates[0];
	label.candidates = candidates;
	label.text = measurement_length_text(world_length, map_units_per_world_unit, false);
	label.axis = axis;
	labels.push_back(std::move(label));
}

void append_bounds_dimension_measurements(
		std::vector<MeasurementLabelSource>& labels,
		quader_poly::Vec3 min_value,
		quader_poly::Vec3 max_value,
		double map_units_per_world_unit)
{
	append_dimension_measurement(labels, {
			{ (min_value.x + max_value.x) * 0.5F, min_value.y, min_value.z },
			{ (min_value.x + max_value.x) * 0.5F, min_value.y, max_value.z },
			{ (min_value.x + max_value.x) * 0.5F, max_value.y, min_value.z },
			{ (min_value.x + max_value.x) * 0.5F, max_value.y, max_value.z },
		},
		static_cast<double>(max_value.x - min_value.x),
		TransformAxis::x,
		map_units_per_world_unit);
	append_dimension_measurement(labels, {
			{ min_value.x, (min_value.y + max_value.y) * 0.5F, min_value.z },
			{ min_value.x, (min_value.y + max_value.y) * 0.5F, max_value.z },
			{ max_value.x, (min_value.y + max_value.y) * 0.5F, min_value.z },
			{ max_value.x, (min_value.y + max_value.y) * 0.5F, max_value.z },
		},
		static_cast<double>(max_value.y - min_value.y),
		TransformAxis::y,
		map_units_per_world_unit);
	append_dimension_measurement(labels, {
			{ min_value.x, min_value.y, (min_value.z + max_value.z) * 0.5F },
			{ min_value.x, max_value.y, (min_value.z + max_value.z) * 0.5F },
			{ max_value.x, min_value.y, (min_value.z + max_value.z) * 0.5F },
			{ max_value.x, max_value.y, (min_value.z + max_value.z) * 0.5F },
		},
		static_cast<double>(max_value.z - min_value.z),
		TransformAxis::z,
		map_units_per_world_unit);
}

void append_document_dimension_measurements(std::vector<MeasurementLabelSource>& labels, const quader_poly::Document& document, double map_units_per_world_unit)
{
	quader_poly::Vec3 min_value;
	quader_poly::Vec3 max_value;
	if (!document_bounds(document, min_value, max_value)) {
		return;
	}
	append_bounds_dimension_measurements(labels, min_value, max_value, map_units_per_world_unit);
}

std::vector<MeasurementLabelSource> build_measurement_label_sources(const MeasurementFrameInput& input)
{
	std::vector<MeasurementLabelSource> labels;
	if (input.session == nullptr || input.hover == nullptr) {
		return labels;
	}
	if (input.hover->kind == SelectionHoverKind::edge) {
		for (const PolygonObject& object : input.session->objects()) {
			if (object.id != input.hover->object_id) {
				continue;
			}
			append_edge_measurement_label(
				labels,
				object.document,
				input.hover->edge.a,
				input.hover->edge.b,
				input.map_units_per_world_unit,
				true);
			break;
		}
	}

	const SelectionMode selection_mode = input.session->selection_mode();
	if (selection_mode == SelectionMode::object || selection_mode == SelectionMode::face) {
		const Bounds3 computed_bounds = input.selected_bounds_override == nullptr ? input.session->selection_bounds() : Bounds3 {};
		const Bounds3& bounds = input.selected_bounds_override == nullptr ? computed_bounds : *input.selected_bounds_override;
		if (!bounds.empty) {
			const Bounds3 display_bounds = selected_measurement_display_bounds(input, bounds);
			append_bounds_dimension_measurements(labels, display_bounds.min, display_bounds.max, input.map_units_per_world_unit);
		}
	}
	return labels;
}

quader_poly::Vec3 best_measurement_world_position(const ViewportCameraState& camera, const MeasurementLabelSource& label)
{
	if (label.candidates.empty()) {
		return label.world_position;
	}
	quader_poly::Vec3 center;
	for (quader_poly::Vec3 candidate : label.candidates) {
		center += candidate;
	}
	center = center / static_cast<float>(label.candidates.size());

	if (label.screen_outer) {
		quader_poly::Vec3 best_position = label.world_position;
		double best_score = -std::numeric_limits<double>::infinity();
		bool found = false;
		for (quader_poly::Vec3 candidate : label.candidates) {
			if (viewport_point_is_behind_camera(camera, candidate)) {
				continue;
			}
			quader_poly::Vec2 screen;
			if (!project_viewport_point(camera, candidate, screen)) {
				continue;
			}
			const double score = static_cast<double>(screen.y) + static_cast<double>(screen.x) * 0.001;
			if (!found || score > best_score) {
				best_score = score;
				best_position = candidate;
				found = true;
			}
		}
		if (found) {
			return best_position;
		}
	}

	const quader_poly::Vec3 camera_position = camera.transform.origin;
	const quader_poly::Vec3 view_direction = quader_geometry::length_squared(to_geometry_vec3(camera_position - center)) > measurement_geometry_epsilon ?
		normalize_or(camera_position - center, { 0.0F, 1.0F, 0.0F }) :
		quader_poly::Vec3 { 0.0F, 1.0F, 0.0F };
	quader_poly::Vec3 best_position = label.world_position;
	double best_score = -std::numeric_limits<double>::infinity();
	bool found = false;
	for (quader_poly::Vec3 candidate : label.candidates) {
		if (viewport_point_is_behind_camera(camera, candidate)) {
			continue;
		}
		const double score = quader_geometry::dot(to_geometry_vec3(candidate - center), to_geometry_vec3(view_direction));
		if (!found || score > best_score) {
			best_score = score;
			best_position = candidate;
			found = true;
		}
	}
	return best_position;
}

quader_poly::Vec2 screen_outer_measurement_offset(const ViewportCameraState& camera, const MeasurementLabelSource& label, quader_poly::Vec3 world_position)
{
	if (!label.screen_outer || !label.has_face_normal || quader_geometry::length_squared(to_geometry_vec3(label.face_normal)) <= measurement_geometry_epsilon) {
		return {};
	}
	const quader_poly::Vec3 normal = normalize_or(label.face_normal, { 0.0F, 1.0F, 0.0F });
	quader_poly::Vec2 base_screen;
	quader_poly::Vec2 normal_screen;
	if (!project_viewport_point(camera, world_position, base_screen) ||
			!project_viewport_point(camera, world_position + normal * static_cast<float>(face_measurement_normal_world_offset), normal_screen)) {
		return {};
	}
	quader_geometry::Vec2d delta = to_geometry_vec2(normal_screen) - to_geometry_vec2(base_screen);
	delta = quader_geometry::normalize_or_zero(delta, measurement_geometry_epsilon);
	if (quader_geometry::length_squared(delta) <= 0.0) {
		return {};
	}
	if (delta.y < -0.001F || (std::abs(delta.y) <= 0.001F && delta.x < 0.0F)) {
		delta.x = -delta.x;
		delta.y = -delta.y;
	}
	return {
		static_cast<float>(delta.x * face_measurement_screen_offset),
		static_cast<float>(delta.y * face_measurement_screen_offset),
	};
}

void append_screen_measurement(
		MeasurementFrame& frame,
		const std::string& text,
		quader_poly::Vec2 screen_position,
		TransformAxis axis,
		bool text_color)
{
	if (text.empty() || !std::isfinite(screen_position.x) || !std::isfinite(screen_position.y)) {
		return;
	}
	frame.labels.push_back({ axis, text_color, screen_position, text });
}

void append_screen_measurement_line(MeasurementFrame& frame, quader_poly::Vec2 start, quader_poly::Vec2 end, TransformAxis axis)
{
	if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(end.x) || !std::isfinite(end.y) ||
			quader_geometry::length_squared(to_geometry_vec2(start) - to_geometry_vec2(end)) <= 0.25) {
		return;
	}
	frame.lines.push_back({ axis, start, end });
}

void append_projected_measurement(MeasurementFrame& frame, const ViewportCameraState& camera, const MeasurementLabelSource& label)
{
	const quader_poly::Vec3 world_position = best_measurement_world_position(camera, label);
	if (viewport_point_is_behind_camera(camera, world_position)) {
		return;
	}
	quader_poly::Vec2 screen;
	if (!project_viewport_point(camera, world_position, screen)) {
		return;
	}
	const quader_poly::Vec2 outer_offset = screen_outer_measurement_offset(camera, label, world_position);
	screen.x += outer_offset.x;
	screen.y += outer_offset.y - 4.0F;
	append_screen_measurement(frame, label.text, screen, label.axis, label.text_color);
}

void append_world_segment_measurement(
		MeasurementFrame& frame,
		const ViewportCameraState& camera,
		quader_poly::Vec3 start,
		quader_poly::Vec3 end,
		TransformAxis axis,
		double map_units_per_world_unit)
{
	const double world_length = quader_geometry::length(to_geometry_vec3(end - start));
	if (world_length <= measurement_geometry_epsilon) {
		return;
	}
	const quader_poly::Vec3 midpoint = (start + end) * 0.5F;
	if (viewport_point_is_behind_camera(camera, midpoint)) {
		return;
	}
	quader_poly::Vec2 screen_start;
	quader_poly::Vec2 screen_end;
	quader_poly::Vec2 screen_mid;
	if (!project_viewport_point(camera, start, screen_start) ||
			!project_viewport_point(camera, end, screen_end) ||
			!project_viewport_point(camera, midpoint, screen_mid)) {
		return;
	}
	quader_poly::Vec2 offset { 8.0F, -8.0F };
	const quader_geometry::Vec2d screen_delta = to_geometry_vec2(screen_end) - to_geometry_vec2(screen_start);
	const quader_geometry::Vec2d screen_direction = quader_geometry::normalize_or_zero(screen_delta, 1.0);
	if (quader_geometry::length_squared(screen_direction) > 0.0) {
		offset = {
			static_cast<float>(-screen_direction.y * 10.0),
			static_cast<float>(screen_direction.x * 10.0 - 2.0),
		};
	}
	screen_mid.x += offset.x;
	screen_mid.y += offset.y;
	append_screen_measurement(frame, measurement_length_text(world_length, map_units_per_world_unit, false), screen_mid, axis, false);
}

TransformAxis measurement_axis_for_direction(quader_poly::Vec3 direction)
{
	if (quader_geometry::length_squared(to_geometry_vec3(direction)) <= measurement_geometry_epsilon) {
		return TransformAxis::none;
	}
	return to_transform_axis(quader_geometry::dominant_axis(to_geometry_vec3f(direction)));
}

void append_box_measurements(const MeasurementFrameInput& input, MeasurementFrame& frame)
{
	if (input.box == nullptr) {
		return;
	}
	const BoxPreviewDescriptor preview = box_preview_descriptor(*input.box, true, input.box_grid_size);
	if (!preview.ok || (preview.drag_state != BoxDragState::footprint && preview.drag_state != BoxDragState::height) ||
			preview.footprint.footprint.size() < 4) {
		return;
	}

	const std::vector<quader_poly::Vec3>& footprint = preview.footprint.footprint;
	append_world_segment_measurement(
			frame,
			input.camera,
			footprint[0],
			footprint[1],
			measurement_axis_for_direction(footprint[1] - footprint[0]),
			input.map_units_per_world_unit);
	append_world_segment_measurement(
			frame,
			input.camera,
			footprint[1],
			footprint[2],
			measurement_axis_for_direction(footprint[2] - footprint[1]),
			input.map_units_per_world_unit);
	if (preview.drag_state == BoxDragState::height) {
		const quader_poly::Vec3 height = preview.normal * static_cast<float>(preview.distance);
		append_world_segment_measurement(
				frame,
				input.camera,
				footprint[2],
				footprint[2] + height,
				measurement_axis_for_direction(height),
				input.map_units_per_world_unit);
	}
}

void append_transform_drag_measurements(const MeasurementFrameInput& input, MeasurementFrame& frame)
{
	if (input.transform_drag == nullptr || !input.transform_drag->active) {
		return;
	}
	const TransformDragState& drag = *input.transform_drag;
	if (drag.tool == ViewportInteractionTool::move) {
		double world_delta = quader_geometry::length(to_geometry_vec3(drag.move.preview_delta));
		TransformAxis measurement_axis = TransformAxis::none;
		if (!transform_axis_is_plane_local(drag.axis)) {
			const quader_poly::Vec3 axis = normalize_or(axis_vector(drag.axis), {});
			if (quader_geometry::length_squared(to_geometry_vec3(axis)) > measurement_geometry_epsilon) {
				world_delta = quader_geometry::dot(to_geometry_vec3(axis), to_geometry_vec3(drag.move.preview_delta));
				measurement_axis = drag.axis;
			}
		}
		const quader_poly::Vec3 start_position = drag.move.center_world;
		const quader_poly::Vec3 label_position = drag.move.center_world + drag.move.preview_delta;
		quader_poly::Vec2 start_screen;
		quader_poly::Vec2 end_screen;
		if (!viewport_point_is_behind_camera(input.camera, start_position) &&
				!viewport_point_is_behind_camera(input.camera, label_position) &&
				project_viewport_point(input.camera, start_position, start_screen) &&
				project_viewport_point(input.camera, label_position, end_screen)) {
			append_screen_measurement_line(frame, start_screen, end_screen, TransformAxis::z);
			quader_poly::Vec2 label_screen {
				(start_screen.x + end_screen.x) * 0.5F + 12.0F,
				(start_screen.y + end_screen.y) * 0.5F - 8.0F,
			};
			append_screen_measurement(frame, measurement_number_text(world_delta * input.map_units_per_world_unit, true), label_screen, measurement_axis, true);
		}
	}

	if (drag.tool == ViewportInteractionTool::rotate && drag.rotate.active) {
		quader_poly::Vec2 label_position = drag.rotate.screen_pivot;
		label_position.x += 74.0F;
		append_screen_measurement(frame, measurement_number_text(drag.rotate.preview_angle_degrees, true), label_position, drag.rotate.axis, true);
	}
}

} // namespace

MeasurementFrame build_measurement_frame(const MeasurementFrameInput& input)
{
	MeasurementFrame frame;
	for (const MeasurementLabelSource& label : build_measurement_label_sources(input)) {
		append_projected_measurement(frame, input.camera, label);
	}
	append_box_measurements(input, frame);
	append_transform_drag_measurements(input, frame);
	return frame;
}

} // namespace quader_editor_core
