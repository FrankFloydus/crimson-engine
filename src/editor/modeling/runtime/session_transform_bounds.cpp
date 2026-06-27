////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <editor/modeling/runtime/session.hpp>

#include <mesh/geometry/geometry.hpp>

#include <cmath>
#include <unordered_set>

namespace quader_editor_core {
namespace {

quader_geometry::QVec3f to_geometry_vec3(quader::QVec3 value)
{
	return quader_geometry::QVec3f(value);
}

quader::QVec3 to_poly_vec3(quader_geometry::QVec3f value)
{
	return { value.x, value.y, value.z };
}

void include_point(Bounds3& bounds, quader::QVec3 point)
{
	quader_geometry::QAabb3f geometry_bounds = bounds.empty ?
		quader_geometry::empty_aabb3<float>() :
		quader_geometry::QAabb3f { to_geometry_vec3(bounds.min), to_geometry_vec3(bounds.max) };
	quader_geometry::aabb_include(geometry_bounds, to_geometry_vec3(point));
	bounds.empty = !quader_geometry::aabb_is_valid(geometry_bounds);
	if (!bounds.empty) {
		bounds.min = to_poly_vec3(geometry_bounds.min);
		bounds.max = to_poly_vec3(geometry_bounds.max);
	}
}

quader::QVec3 basis_xform(const quader_poly::Transform3& transform, quader::QVec3 point)
{
	return {
		transform.x_axis.x * point.x + transform.y_axis.x * point.y + transform.z_axis.x * point.z,
		transform.x_axis.y * point.x + transform.y_axis.y * point.y + transform.z_axis.y * point.z,
		transform.x_axis.z * point.x + transform.y_axis.z * point.y + transform.z_axis.z * point.z,
	};
}

quader::QVec3 transformed_point(const quader_poly::Transform3& transform, quader::QVec3 pivot, quader::QVec3 point)
{
	const quader::QVec3 pivot_after_basis = basis_xform(transform, pivot);
	const quader::QVec3 translation = pivot + transform.origin - pivot_after_basis;
	return basis_xform(transform, point) + translation;
}

bool basis_has_scale_or_shear(const quader_poly::Transform3& transform)
{
	const quader_geometry::QVec3f x_axis = to_geometry_vec3(transform.x_axis);
	const quader_geometry::QVec3f y_axis = to_geometry_vec3(transform.y_axis);
	const quader_geometry::QVec3f z_axis = to_geometry_vec3(transform.z_axis);
	return std::abs(quader_geometry::length_squared(x_axis) - 1.0F) > transform_basis_epsilon ||
		std::abs(quader_geometry::length_squared(y_axis) - 1.0F) > transform_basis_epsilon ||
		std::abs(quader_geometry::length_squared(z_axis) - 1.0F) > transform_basis_epsilon ||
		std::abs(quader_geometry::dot(x_axis, y_axis)) > transform_basis_epsilon ||
		std::abs(quader_geometry::dot(x_axis, z_axis)) > transform_basis_epsilon ||
		std::abs(quader_geometry::dot(y_axis, z_axis)) > transform_basis_epsilon;
}

quader_poly::Selection all_vertex_selection(const quader_poly::Document& document)
{
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::vertex;
	selection.vertices.reserve(document.vertices.size());
	for (const quader_poly::Vertex& vertex : document.vertices) {
		selection.vertices.push_back(vertex.id);
	}
	return selection;
}

} // namespace

Bounds3 document_bounds(const std::vector<PolygonObject>& objects)
{
	Bounds3 bounds;
	for (const PolygonObject& object : objects) {
		const Bounds3 object_bounds = document_bounds(object);
		if (!object_bounds.empty) {
			include_point(bounds, object_bounds.min);
			include_point(bounds, object_bounds.max);
		}
	}
	return bounds;
}

Bounds3 document_bounds(const PolygonObject& object)
{
	Bounds3 bounds;
	for (const quader_poly::Vertex& vertex : object.document.vertices) {
		include_point(bounds, vertex.position);
	}
	return bounds;
}

Bounds3 selection_bounds(const std::vector<PolygonObject>& objects, SelectionMode mode)
{
	Bounds3 bounds;
	for (const PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		if (mode == SelectionMode::object) {
			for (const quader_poly::Vertex& vertex : object.document.vertices) {
				include_point(bounds, vertex.position);
			}
			continue;
		}
		const std::vector<quader_poly::ElementId> vertex_ids = quader_poly::selected_vertex_ids(object.document, object.selection);
		for (const quader_poly::ElementId vertex_id : vertex_ids) {
			const quader_poly::Vertex* vertex = quader_poly::find_vertex(object.document, vertex_id);
			if (vertex != nullptr) {
				include_point(bounds, vertex->position);
			}
		}
	}
	return bounds;
}

CommandResult translate_selected(std::vector<PolygonObject>& objects, SelectionMode mode, quader::QVec3 delta, bool texture_lock_enabled)
{
	bool changed = false;
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		const bool preserve_object_uvs = mode == SelectionMode::object && texture_lock_enabled;
		if (preserve_object_uvs) {
			quader_poly::ensure_face_uvs(object.document);
		}
		quader_poly::Selection selection = mode == SelectionMode::object ? all_vertex_selection(object.document) : object.selection;
		const quader_poly::OperationResult result = quader_poly::translate_selection(object.document, selection, delta);
		if (result.changed) {
			if (!preserve_object_uvs) {
				quader_poly::clear_face_uvs(object.document);
			}
			changed = true;
		}
	}
	return ok_result(changed);
}

CommandResult transform_selected(std::vector<PolygonObject>& objects, SelectionMode mode, const quader_poly::Transform3& transform, quader::QVec3 pivot, bool texture_lock_enabled, bool texture_lock_scale_enabled)
{
	bool changed = false;
	const bool identity_basis = basis_is_identity(transform);
	const bool scale_or_shear_basis = basis_has_scale_or_shear(transform);
	const bool pure_rotation_basis = !identity_basis && !scale_or_shear_basis;
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		const bool preserve_object_uvs = mode == SelectionMode::object &&
			(pure_rotation_basis ||
				(identity_basis && texture_lock_enabled) ||
				(scale_or_shear_basis && texture_lock_scale_enabled));
		if (preserve_object_uvs) {
			quader_poly::ensure_face_uvs(object.document);
		}
		std::unordered_set<quader_poly::ElementId> vertex_ids;
		if (mode == SelectionMode::object) {
			for (const quader_poly::Vertex& vertex : object.document.vertices) {
				vertex_ids.insert(vertex.id);
			}
		} else {
			const std::vector<quader_poly::ElementId> selected_ids = quader_poly::selected_vertex_ids(object.document, object.selection);
			vertex_ids.insert(selected_ids.begin(), selected_ids.end());
		}
		bool object_changed = false;
		for (quader_poly::Vertex& vertex : object.document.vertices) {
			if (!vertex_ids.contains(vertex.id)) {
				continue;
			}
			const quader::QVec3 after = transformed_point(transform, pivot, vertex.position);
			if (quader_geometry::length_squared(to_geometry_vec3(after - vertex.position)) <= transform_point_epsilon) {
				continue;
			}
			vertex.position = after;
			object_changed = true;
			changed = true;
		}
		if (object_changed && !preserve_object_uvs) {
			quader_poly::clear_face_uvs(object.document);
		}
	}
	return ok_result(changed);
}

} // namespace quader_editor_core
