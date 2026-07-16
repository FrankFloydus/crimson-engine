////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/tool_snapping.hpp>

#include <editor/interactions/tool_math_internal.hpp>
#include <editor/modeling/runtime/tool_constants.hpp>

#include <mesh/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace quader_editor_core {
namespace {

/**
 * Stores the Insert Edge Loop Snap Result data contract used by the modal modeling interaction layer.
 */
struct InsertEdgeLoopSnapResult {
	float factor = 0.5F;
	bool snapped_to_existing_edge = false;
};

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

	const float edge_length = quader_geometry::length(tool_geometry_vec3(b->position - a->position));
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

quader::QVec3 snap_knife_point_to_grid(quader::QVec3 point, double grid_size, bool snap_enabled)
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

std::optional<quader::QVec3> snap_knife_point_to_edge_grid(
		const quader_poly::Document& document,
		quader_poly::Edge edge,
		quader::QVec3 point,
		double grid_size,
		bool snap_enabled)
{
	edge = quader_poly::make_edge(edge.a, edge.b);
	const quader_poly::Vertex *first = quader_poly::find_vertex(document, edge.a);
	const quader_poly::Vertex *second = quader_poly::find_vertex(document, edge.b);
	const std::optional<float> factor = quader_poly::edge_factor_from_position(
		document, edge, point);
	if (first == nullptr || second == nullptr || !factor.has_value()) {
		return std::nullopt;
	}

	const quader::QVec3 edge_delta = second->position - first->position;
	const double edge_length = static_cast<double>(quader::length(edge_delta));
	if (!std::isfinite(edge_length) || edge_length <= 0.0001) {
		return std::nullopt;
	}
	const double raw_distance = static_cast<double>(*factor) * edge_length;
	const double resolved_distance =
		snap_enabled && std::isfinite(grid_size) && grid_size > 0.0001
			? snap_world_value(raw_distance, grid_size)
			: raw_distance;
	const float resolved_factor = static_cast<float>(
		std::clamp(resolved_distance / edge_length, 0.0, 1.0));
	return first->position + edge_delta * resolved_factor;
}

std::optional<quader::QVec3> snap_knife_point_to_face_grid(
		const quader_poly::Document& document,
		quader_poly::ElementId face_id,
		quader::QVec3 point,
		double grid_size,
		bool snap_enabled)
{
	if (!snap_enabled || !std::isfinite(grid_size) || grid_size <= 0.0001) {
		return point;
	}
	const quader_poly::Face *face = quader_poly::find_face(document, face_id);
	if (face == nullptr || face->vertices.size() < 3U) {
		return std::nullopt;
	}
	const quader_poly::Vertex *face_point =
		quader_poly::find_vertex(document, face->vertices.front());
	if (face_point == nullptr) {
		return std::nullopt;
	}
	const quader::QVec3 normal = vec3_normalize_or(
		quader_poly::face_normal(document, *face), {});
	quader::QVec3 preferred_axis { 1.0F, 0.0F, 0.0F };
	if (std::abs(quader::dot(normal, preferred_axis)) > 0.9F) {
		preferred_axis = { 0.0F, 0.0F, 1.0F };
	}
	const quader::QVec3 u_axis = vec3_normalize_or(
		preferred_axis - normal * quader::dot(preferred_axis, normal), {});
	const quader::QVec3 v_axis = vec3_normalize_or(
		quader::cross(normal, u_axis), {});
	if (quader::length_squared(normal) <= kToolEpsilon ||
		quader::length_squared(u_axis) <= kToolEpsilon ||
		quader::length_squared(v_axis) <= kToolEpsilon) {
		return std::nullopt;
	}

	const quader::QVec3 origin =
		normal * quader::dot(face_point->position, normal);
	const quader::QVec3 from_origin = point - origin;
	const double u = snap_world_value(
		static_cast<double>(quader::dot(from_origin, u_axis)), grid_size);
	const double v = snap_world_value(
		static_cast<double>(quader::dot(from_origin, v_axis)), grid_size);
	return origin + u_axis * static_cast<float>(u) +
		v_axis * static_cast<float>(v);
}

quader::QVec3 constrain_knife_point_to_axis(
		quader::QVec3 previous,
		quader::QVec3 point,
		quader::QVec3 face_normal,
		double grid_size,
		bool snap_enabled)
{
	const quader::QVec3 delta = point - previous;
        if (quader::length_squared(delta) <= kToolEpsilon) {
          return previous;
        }

        const std::array<quader::QVec3, 3> axes {
		quader::QVec3 { 1.0F, 0.0F, 0.0F },
		quader::QVec3 { 0.0F, 1.0F, 0.0F },
		quader::QVec3 { 0.0F, 0.0F, 1.0F },
	};
	const quader::QVec3 normal = vec3_normalize_or(face_normal, { 0.0F, 1.0F, 0.0F });
	const quader::QVec3 direction = vec3_normalize_or(delta, { 1.0F, 0.0F, 0.0F });
	const quader_geometry::QVec3f geometry_normal = tool_geometry_vec3(normal);
	const quader_geometry::QVec3f geometry_direction = tool_geometry_vec3(direction);
	quader::QVec3 best_axis = axes[0];
	double best_score = -std::numeric_limits<double>::infinity();
	for (quader::QVec3 axis : axes) {
		const quader_geometry::QVec3f geometry_axis = tool_geometry_vec3(axis);
		const quader_geometry::QVec3f projected_axis =
			quader_geometry::normalize_or_zero(geometry_axis - (geometry_normal * quader_geometry::dot(geometry_axis, geometry_normal)), 0.000001F);
		if (quader_geometry::length_squared(projected_axis) <= 0.000001F) {
			continue;
		}
		const double plane_score = 1.0 - std::abs(static_cast<double>(quader_geometry::dot(geometry_axis, geometry_normal)));
		const double direction_score = std::abs(static_cast<double>(quader_geometry::dot(projected_axis, geometry_direction)));
		const double score = plane_score * 2.0 + direction_score;
		if (score > best_score) {
			best_score = score;
			best_axis = tool_poly_vec3(projected_axis);
		}
	}

	double distance = quader_geometry::dot(tool_geometry_vec3(delta), tool_geometry_vec3(best_axis));
	if (snap_enabled && std::isfinite(grid_size) && grid_size > 0.0001) {
		distance = snap_world_value(distance, grid_size);
	}
	return previous + best_axis * static_cast<float>(distance);
}

std::optional<quader::QVec3> constrain_knife_point_to_face_directions(
		quader::QVec3 previous,
		quader::QVec3 point,
		std::span<const quader::QVec3> face_directions,
		double grid_size,
		bool snap_enabled)
{
	const quader::QVec3 delta = point - previous;
	if (quader::length_squared(delta) <= kToolEpsilon) {
		return previous;
	}

	const quader::QVec3 intended_direction = vec3_normalize_or(
		delta, { 1.0F, 0.0F, 0.0F });
	std::optional<quader::QVec3> best_direction;
	float best_score = -1.0F;
	for (const quader::QVec3 candidate : face_directions) {
		if (quader::length_squared(candidate) <= kToolEpsilon) {
			continue;
		}
		quader::QVec3 direction = vec3_normalize_or(
			candidate, { 1.0F, 0.0F, 0.0F });
		const float alignment = quader::dot(direction, intended_direction);
		const float score = std::abs(alignment);
		if (score <= best_score) {
			continue;
		}
		if (alignment < 0.0F) {
			direction = direction * -1.0F;
		}
		best_direction = direction;
		best_score = score;
	}
	if (!best_direction.has_value()) {
		return std::nullopt;
	}

	double distance = static_cast<double>(quader::dot(delta, *best_direction));
	if (snap_enabled && std::isfinite(grid_size) && grid_size > 0.0001) {
		distance = snap_world_value(distance, grid_size);
	}
	return previous + *best_direction * static_cast<float>(distance);
}

} // namespace quader_editor_core
