#include <quader_editor_core/tool/tool_snapping.h>

#include <quader_editor_core/tool/measurement_tool.h>
#include <quader_editor_core/tool_constants.h>

#include <quader_geometry/geometry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace quader_editor_core {
namespace {

struct InsertEdgeLoopSnapResult {
	float factor = 0.5F;
	bool snapped_to_existing_edge = false;
};

quader_geometry::Vec3f to_geometry_vec3(quader_poly::Vec3 value)
{
	return { value.x, value.y, value.z };
}

quader_poly::Vec3 to_poly_vec3(quader_geometry::Vec3f value)
{
	return { value.x, value.y, value.z };
}

quader_poly::Vec3 normalize_or(quader_poly::Vec3 value, quader_poly::Vec3 fallback)
{
	const quader_geometry::Vec3f geometry_value = to_geometry_vec3(value);
	return quader_geometry::length_squared(geometry_value) <= static_cast<float>(tool_epsilon * tool_epsilon) ?
		fallback :
		to_poly_vec3(quader_geometry::normalize_or_zero(geometry_value));
}

InsertEdgeLoopSnapResult snap_insert_edge_loop_factor_result(
		const quader_poly::Document& document,
		quader_poly::Edge edge,
		float factor,
		double grid_size,
		bool snap_enabled)
{
	InsertEdgeLoopSnapResult result;
	result.factor = std::clamp(factor, 0.01F, 0.99F);
	if (!snap_enabled || !std::isfinite(grid_size) || grid_size <= 0.0001) {
		return result;
	}

	edge = quader_poly::make_edge(edge.a, edge.b);
	const quader_poly::Vertex *a = quader_poly::find_vertex(document, edge.a);
	const quader_poly::Vertex *b = quader_poly::find_vertex(document, edge.b);
	if (a == nullptr || b == nullptr) {
		return result;
	}

	const float edge_length = quader_geometry::length(to_geometry_vec3(b->position - a->position));
	if (edge_length <= 0.0001F) {
		return result;
	}

	const double snapped_distance = snap_world_value(static_cast<double>(result.factor) * static_cast<double>(edge_length), grid_size);
	const double edge_length_double = static_cast<double>(edge_length);
	const double endpoint_epsilon = std::max(1.0e-6, edge_length_double * 1.0e-6);
	result.snapped_to_existing_edge =
		snapped_distance <= endpoint_epsilon ||
		snapped_distance >= edge_length_double - endpoint_epsilon;
	result.factor = std::clamp(static_cast<float>(snapped_distance / edge_length_double), 0.01F, 0.99F);
	return result;
}

} // namespace

std::string measurement_number_text(double value, bool force_decimals)
{
	if (std::abs(value) < 0.005) {
		value = 0.0;
	}
	if (!force_decimals) {
		const double rounded = std::round(value);
		if (std::abs(value - rounded) < 0.005) {
			return std::to_string(static_cast<std::int64_t>(rounded));
		}
	}
	char buffer[32] {};
	std::snprintf(buffer, sizeof(buffer), "%.2f", value);
	return buffer;
}

std::string measurement_length_text(double world_length, double map_units_per_world_unit, bool force_decimals)
{
	return measurement_number_text(world_length * map_units_per_world_unit, force_decimals);
}

double snap_world_value(double value, double grid_size)
{
	const double step = std::max(grid_size, 0.0001);
	return std::round(value / step) * step;
}

double maybe_snap_world_value(double value, double grid_size, bool grid_enabled)
{
	return grid_enabled ? snap_world_value(value, grid_size) : value;
}

float snap_insert_edge_loop_factor(const quader_poly::Document& document, quader_poly::Edge edge, float factor, double grid_size, bool snap_enabled)
{
	return snap_insert_edge_loop_factor_result(document, edge, factor, grid_size, snap_enabled).factor;
}

std::optional<float> snap_insert_edge_loop_preview_factor(const quader_poly::Document& document, quader_poly::Edge edge, float factor, double grid_size, bool snap_enabled)
{
	const InsertEdgeLoopSnapResult result = snap_insert_edge_loop_factor_result(document, edge, factor, grid_size, snap_enabled);
	return result.snapped_to_existing_edge ? std::optional<float> {} : std::optional<float> { result.factor };
}

quader_poly::Vec3 snap_knife_point_to_grid(quader_poly::Vec3 point, double grid_size, bool snap_enabled)
{
	if (!snap_enabled || !std::isfinite(grid_size) || grid_size <= 0.0001) {
		return point;
	}
	return {
		static_cast<float>(snap_world_value(point.x, grid_size)),
		static_cast<float>(snap_world_value(point.y, grid_size)),
		static_cast<float>(snap_world_value(point.z, grid_size)),
	};
}

quader_poly::Vec3 constrain_knife_point_to_axis(
		quader_poly::Vec3 previous,
		quader_poly::Vec3 point,
		quader_poly::Vec3 face_normal,
		double grid_size,
		bool snap_enabled)
{
	const quader_poly::Vec3 delta = point - previous;
	if (quader_poly::length_squared(delta) <= tool_epsilon) {
		return previous;
	}

	const std::array<quader_poly::Vec3, 3> axes {
		quader_poly::Vec3 { 1.0F, 0.0F, 0.0F },
		quader_poly::Vec3 { 0.0F, 1.0F, 0.0F },
		quader_poly::Vec3 { 0.0F, 0.0F, 1.0F },
	};
	const quader_poly::Vec3 normal = normalize_or(face_normal, { 0.0F, 1.0F, 0.0F });
	const quader_poly::Vec3 direction = normalize_or(delta, { 1.0F, 0.0F, 0.0F });
	const quader_geometry::Vec3f geometry_normal = to_geometry_vec3(normal);
	const quader_geometry::Vec3f geometry_direction = to_geometry_vec3(direction);
	quader_poly::Vec3 best_axis = axes[0];
	double best_score = -std::numeric_limits<double>::infinity();
	for (quader_poly::Vec3 axis : axes) {
		const quader_geometry::Vec3f geometry_axis = to_geometry_vec3(axis);
		const quader_geometry::Vec3f projected_axis =
			quader_geometry::normalize_or_zero(geometry_axis - (geometry_normal * quader_geometry::dot(geometry_axis, geometry_normal)), 0.000001F);
		if (quader_geometry::length_squared(projected_axis) <= 0.000001F) {
			continue;
		}
		const double plane_score = 1.0 - std::abs(static_cast<double>(quader_geometry::dot(geometry_axis, geometry_normal)));
		const double direction_score = std::abs(static_cast<double>(quader_geometry::dot(projected_axis, geometry_direction)));
		const double score = plane_score * 2.0 + direction_score;
		if (score > best_score) {
			best_score = score;
			best_axis = to_poly_vec3(projected_axis);
		}
	}

	double distance = quader_geometry::dot(to_geometry_vec3(delta), to_geometry_vec3(best_axis));
	if (snap_enabled && std::isfinite(grid_size) && grid_size > 0.0001) {
		distance = snap_world_value(distance, grid_size);
	}
	return previous + best_axis * static_cast<float>(distance);
}

} // namespace quader_editor_core
