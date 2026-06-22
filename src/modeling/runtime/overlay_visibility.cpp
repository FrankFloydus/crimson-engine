////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <modeling/interactions/overlay_visibility.h>

#include <diagnostics/profile.h>
#include <modeling/runtime/session.h>
#include <modeling/interactions/viewport_projection.h>
#include <geometry/geometry.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace quader_editor_core {
namespace {

constexpr double visibility_epsilon = 0.000001;
constexpr std::size_t occluder_leaf_triangle_count = 8;

double axis_value(quader_poly::Vec3 value, quader_geometry::Axis axis)
{
	switch (axis) {
		case quader_geometry::Axis::x:
			return value.x;
		case quader_geometry::Axis::y:
			return value.y;
		case quader_geometry::Axis::z:
			return value.z;
	}
	return value.z;
}

quader_geometry::Vec3d to_geometry_vec3(quader_poly::Vec3 value)
{
	return { value.x, value.y, value.z };
}

quader_geometry::Vec3f to_geometry_vec3f(quader_poly::Vec3 value)
{
	return { value.x, value.y, value.z };
}

quader_poly::Vec3 to_poly_vec3(quader_geometry::Vec3f value)
{
	return { value.x, value.y, value.z };
}

OverlayVisibilityBounds to_overlay_bounds(const quader_geometry::Aabb3f& bounds)
{
	OverlayVisibilityBounds overlay_bounds;
	if (!quader_geometry::aabb_is_valid(bounds)) {
		return overlay_bounds;
	}
	overlay_bounds.empty = false;
	overlay_bounds.min = to_poly_vec3(bounds.min);
	overlay_bounds.max = to_poly_vec3(bounds.max);
	return overlay_bounds;
}

quader_geometry::Ray3<double> to_geometry_ray(quader_poly::Ray ray)
{
	return { to_geometry_vec3(ray.origin), to_geometry_vec3(ray.direction) };
}

quader_geometry::Aabb3f to_geometry_aabb(const OverlayVisibilityBounds& bounds)
{
	if (bounds.empty) {
		return quader_geometry::empty_aabb3<float>();
	}
	return { to_geometry_vec3f(bounds.min), to_geometry_vec3f(bounds.max) };
}

bool ray_intersects_bounds(quader_poly::Ray ray, const OverlayVisibilityBounds& bounds, double& out_distance)
{
	if (bounds.empty) {
		return false;
	}
	const quader_geometry::RayAabbHit<double> hit =
		quader_geometry::intersect_ray_aabb(
			to_geometry_ray(ray),
			quader_geometry::Aabb3d(to_geometry_aabb(bounds)),
			visibility_epsilon);
	out_distance = hit.min_distance;
	return hit.hit;
}

bool ray_intersects_triangle(
		quader_poly::Ray ray,
		quader_poly::Vec3 a,
		quader_poly::Vec3 b,
		quader_poly::Vec3 c,
		double& out_distance)
{
	const quader_geometry::Triangle3<double> triangle { to_geometry_vec3(a), to_geometry_vec3(b), to_geometry_vec3(c) };
	const quader_geometry::RayTriangleHit<double> hit =
		quader_geometry::intersect_ray_triangle(to_geometry_ray(ray), triangle, visibility_epsilon);
	out_distance = hit.distance;
	return hit.hit;
}

quader_poly::Vec3 viewport_forward(const ViewportCameraState& camera)
{
	quader_geometry::Vec3f forward = quader_geometry::length_squared(to_geometry_vec3f(camera.transform.z_axis)) <= visibility_epsilon ?
		quader_geometry::Vec3f { 0.0F, 0.0F, -1.0F } :
		quader_geometry::normalize_or_zero(to_geometry_vec3f(camera.transform.z_axis) * -1.0F, static_cast<float>(visibility_epsilon));
	if (quader_geometry::length_squared(forward) <= visibility_epsilon) {
		forward = { 0.0F, 0.0F, -1.0F };
	}
	return to_poly_vec3(forward);
}

} // namespace

void OverlayVisibilityOccluder::clear()
{
	vertices_.clear();
	triangles_.clear();
	order_.clear();
	nodes_.clear();
	stack_.clear();
}

void OverlayVisibilityOccluder::set_mesh(std::span<const quader_poly::Vec3> vertices, std::span<const std::uint32_t> indices)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.set_mesh");
	clear();
	vertices_.assign(vertices.begin(), vertices.end());
	build_triangles(vertices_, indices);
	if (!order_.empty()) {
		(void)build_node(0, order_.size());
	}
}

void OverlayVisibilityOccluder::set_objects(std::span<const PolygonObject> objects)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.set_objects");
	std::vector<quader_poly::Vec3> vertices;
	std::vector<std::uint32_t> indices;
	for (const PolygonObject& object : objects) {
		QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.set_objects.compile_object");
		const quader_poly::CompiledMesh compiled = quader_poly::compile_document(object.document);
		if (compiled.vertices.empty() || compiled.indices.empty()) {
			continue;
		}
		const std::uint64_t vertex_offset = vertices.size();
		if (vertex_offset + compiled.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
			throw std::overflow_error("Overlay visibility mesh exceeds the vertex limit.");
		}
		vertices.reserve(vertices.size() + compiled.vertices.size());
		for (const quader_poly::CompiledVertex& vertex : compiled.vertices) {
			vertices.push_back(vertex.position);
		}
		indices.reserve(indices.size() + compiled.indices.size());
		for (const std::uint32_t index : compiled.indices) {
			indices.push_back(index + static_cast<std::uint32_t>(vertex_offset));
		}
	}
	set_mesh(vertices, indices);
}

bool OverlayVisibilityOccluder::empty() const
{
	return nodes_.empty();
}

bool OverlayVisibilityOccluder::line_segment_is_visible(quader_poly::Vec3 a, quader_poly::Vec3 b, const ViewportCameraState& camera) const
{
	QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.line_segment_is_visible");
	return point_is_visible(a + ((b - a) * 0.2F), camera) &&
		point_is_visible((a + b) * 0.5F, camera) &&
		point_is_visible(a + ((b - a) * 0.8F), camera);
}

bool OverlayVisibilityOccluder::line_layer_uses_visibility(SelectionOverlayLayer layer)
{
	(void)layer;
	return false;
}

bool OverlayVisibilityOccluder::line_is_visible(const SelectionOverlayLine& line, const ViewportCameraState& camera) const
{
	QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.line_is_visible");
	if (!line_layer_uses_visibility(line.layer) || empty()) {
		return true;
	}
	return line_segment_is_visible(line.start, line.end, camera);
}

void OverlayVisibilityOccluder::append_visible_lines(std::vector<SelectionOverlayLine>& output, std::span<const SelectionOverlayLine> lines, const ViewportCameraState& camera) const
{
	QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.append_visible_lines");
	output.reserve(output.size() + lines.size());
	for (const SelectionOverlayLine& line : lines) {
		if (line_is_visible(line, camera)) {
			output.push_back(line);
		}
	}
}

std::vector<SelectionOverlayLine> OverlayVisibilityOccluder::visible_lines(std::span<const SelectionOverlayLine> lines, const ViewportCameraState& camera) const
{
	QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.visible_lines");
	std::vector<SelectionOverlayLine> visible;
	append_visible_lines(visible, lines, camera);
	return visible;
}

void OverlayVisibilityOccluder::build_triangles(std::span<const quader_poly::Vec3> vertices, std::span<const std::uint32_t> indices)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.build_triangles");
	for (std::size_t index = 0; index + 2 < indices.size(); index += 3) {
		const std::uint32_t a_index = indices[index];
		const std::uint32_t b_index = indices[index + 1];
		const std::uint32_t c_index = indices[index + 2];
		if (a_index >= vertices.size() || b_index >= vertices.size() || c_index >= vertices.size()) {
			continue;
		}
		const quader_poly::Vec3 a = vertices[a_index];
		const quader_poly::Vec3 b = vertices[b_index];
		const quader_poly::Vec3 c = vertices[c_index];
		const quader_geometry::Triangle3<float> geometry_triangle { to_geometry_vec3f(a), to_geometry_vec3f(b), to_geometry_vec3f(c) };
		const quader_geometry::Vec3f normal = quader_geometry::cross(geometry_triangle.b - geometry_triangle.a, geometry_triangle.c - geometry_triangle.a);
		if (quader_geometry::length_squared(normal) <= visibility_epsilon) {
			continue;
		}
		const quader_geometry::Aabb3f bounds = quader_geometry::triangle_aabb(geometry_triangle);
		OverlayVisibilityTriangle triangle;
		triangle.a = a_index;
		triangle.b = b_index;
		triangle.c = c_index;
		triangle.normal = to_poly_vec3(normal);
		triangle.centroid = (a + b + c) / 3.0F;
		triangle.bounds = to_overlay_bounds(quader_geometry::aabb_expanded(bounds, 0.0001F));
		order_.push_back(triangles_.size());
		triangles_.push_back(triangle);
	}
}

int OverlayVisibilityOccluder::build_node(std::size_t start, std::size_t count)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.build_node");
	const int node_index = static_cast<int>(nodes_.size());
	quader_geometry::Aabb3f bounds = quader_geometry::empty_aabb3<float>();
	quader_geometry::Aabb3f centroid_bounds = quader_geometry::empty_aabb3<float>();
	for (std::size_t index = 0; index < count; ++index) {
		const OverlayVisibilityTriangle& triangle = triangles_[order_[start + index]];
		quader_geometry::aabb_include(bounds, to_geometry_aabb(triangle.bounds));
		quader_geometry::aabb_include(centroid_bounds, to_geometry_vec3f(triangle.centroid));
	}
	nodes_.push_back({});
	const OverlayVisibilityBounds overlay_bounds = to_overlay_bounds(bounds);
	if (count <= occluder_leaf_triangle_count) {
		nodes_[node_index] = { overlay_bounds, start, count, -1, -1 };
		return node_index;
	}
	const quader_geometry::Axis axis = quader_geometry::aabb_longest_axis(centroid_bounds);
	std::sort(order_.begin() + static_cast<std::ptrdiff_t>(start), order_.begin() + static_cast<std::ptrdiff_t>(start + count), [&](std::size_t left, std::size_t right) {
		return axis_value(triangles_[left].centroid, axis) < axis_value(triangles_[right].centroid, axis);
	});
	const std::size_t left_count = count / 2;
	const std::size_t right_count = count - left_count;
	const int left = build_node(start, left_count);
	const int right = build_node(start + left_count, right_count);
	nodes_[node_index] = { overlay_bounds, 0, 0, left, right };
	return node_index;
}

bool OverlayVisibilityOccluder::point_is_visible(quader_poly::Vec3 point, const ViewportCameraState& camera) const
{
	QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.point_is_visible");
	if (empty()) {
		return true;
	}
	const quader_poly::Vec3 forward = viewport_forward(camera);
	quader_poly::Ray ray;
	double target_distance = 0.0;
	if (camera.projection == CameraProjection::orthographic) {
		target_distance = quader_geometry::dot(to_geometry_vec3f(point - camera.transform.origin), to_geometry_vec3f(forward));
		if (target_distance <= 0.0001) {
			return true;
		}
		ray.origin = point - (forward * static_cast<float>(target_distance));
		ray.direction = forward;
	} else {
		const quader_poly::Vec3 to_point = point - camera.transform.origin;
		target_distance = quader_geometry::length(to_geometry_vec3f(to_point));
		if (target_distance <= 0.0001) {
			return true;
		}
		ray.origin = camera.transform.origin;
		ray.direction = to_point / static_cast<float>(target_distance);
	}
	const double tolerance = std::max(0.001, target_distance * 0.00025);
	return !hits_before_target(ray, target_distance, tolerance);
}

bool OverlayVisibilityOccluder::hits_before_target(quader_poly::Ray ray, double target_distance, double tolerance) const
{
	QDR_PROFILE_SCOPE("qdr_editor_core.OverlayVisibilityOccluder.hits_before_target");
	const double max_distance = target_distance - tolerance;
	if (nodes_.empty() || max_distance <= 0.0001) {
		return false;
	}
	stack_.clear();
	stack_.push_back(0);
	while (!stack_.empty()) {
		const int node_index = stack_.back();
		stack_.pop_back();
		const OverlayVisibilityNode& node = nodes_[node_index];
		double bounds_distance = 0.0;
		if (!ray_intersects_bounds(ray, node.bounds, bounds_distance) || bounds_distance > max_distance) {
			continue;
		}
		if (node.count == 0) {
			if (node.left >= 0) {
				stack_.push_back(node.left);
			}
			if (node.right >= 0) {
				stack_.push_back(node.right);
			}
			continue;
		}
		for (std::size_t index = 0; index < node.count; ++index) {
			const OverlayVisibilityTriangle& triangle = triangles_[order_[node.start + index]];
			if (quader_geometry::dot(to_geometry_vec3f(triangle.normal), to_geometry_vec3f(ray.direction)) >= -visibility_epsilon) {
				continue;
			}
			double triangle_bounds_distance = 0.0;
			if (!ray_intersects_bounds(ray, triangle.bounds, triangle_bounds_distance) || triangle_bounds_distance > max_distance) {
				continue;
			}
			double hit_distance = 0.0;
			if (ray_intersects_triangle(ray, vertices_[triangle.a], vertices_[triangle.b], vertices_[triangle.c], hit_distance) &&
					hit_distance > 0.0001 &&
					hit_distance < max_distance) {
				return true;
			}
		}
	}
	return false;
}

bool selection_overlay_lines_use_visibility(std::span<const SelectionOverlayLine> lines)
{
	return std::any_of(lines.begin(), lines.end(), [](const SelectionOverlayLine& line) {
		return OverlayVisibilityOccluder::line_layer_uses_visibility(line.layer);
	});
}

} // namespace quader_editor_core
