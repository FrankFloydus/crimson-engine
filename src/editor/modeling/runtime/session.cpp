////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.h"

#include <editor/modeling/runtime/session.h>
#include <editor/interactions/interaction_api.h>

#include <mesh/geometry/geometry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace quader_editor_core {
namespace {


quader_geometry::QVec3f to_geometry_vec3(quader::QVec3 value)
{
	return { value.x, value.y, value.z };
}

quader::QVec3 to_poly_vec3(quader_geometry::QVec3f value)
{
	return { value.x, value.y, value.z };
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

quader_poly::Selection all_edge_selection(const quader_poly::Document& document)
{
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::edge;
	selection.edges = quader_poly::document_edges(document);
	if (!selection.edges.empty()) {
		selection.has_active = true;
		selection.active_kind = quader_poly::ElementKind::edge;
		selection.active_edge = selection.edges.back();
	}
	return selection;
}

quader_poly::Selection all_face_selection(const quader_poly::Document& document)
{
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::face;
	selection.faces.reserve(document.faces.size());
	for (const quader_poly::Face& face : document.faces) {
		selection.faces.push_back(face.id);
	}
	if (!selection.faces.empty()) {
		selection.has_active = true;
		selection.active_kind = quader_poly::ElementKind::face;
		selection.active_face = selection.faces.back();
	}
	return selection;
}

quader_poly::Selection all_component_selection(const quader_poly::Document& document, SelectionMode mode)
{
	switch (mode) {
		case SelectionMode::edge:
			return all_edge_selection(document);
		case SelectionMode::face:
			return all_face_selection(document);
		case SelectionMode::vertex:
		case SelectionMode::object:
		default:
			return all_vertex_selection(document);
	}
}

void activate_component_selection(quader_poly::Selection& selection)
{
	selection.has_active = false;
	selection.active_kind = quader_poly::ElementKind::vertex;
	selection.active_vertex = quader_poly::invalid_element_id;
	selection.active_edge = {};
	selection.active_face = quader_poly::invalid_element_id;
	switch (selection.mode) {
		case quader_poly::SelectionMode::vertex:
			if (!selection.vertices.empty()) {
				selection.has_active = true;
				selection.active_kind = quader_poly::ElementKind::vertex;
				selection.active_vertex = selection.vertices.back();
			}
			break;
		case quader_poly::SelectionMode::edge:
			if (!selection.edges.empty()) {
				selection.has_active = true;
				selection.active_kind = quader_poly::ElementKind::edge;
				selection.active_edge = selection.edges.back();
			}
			break;
		case quader_poly::SelectionMode::face:
			if (!selection.faces.empty()) {
				selection.has_active = true;
				selection.active_kind = quader_poly::ElementKind::face;
				selection.active_face = selection.faces.back();
			}
			break;
	}
}

int first_selected_index(const std::vector<PolygonObject>& objects)
{
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (objects[index].selected) {
			return index;
		}
	}
	return -1;
}

quader::QVec3 bounds_center(const Bounds3& bounds)
{
	if (bounds.empty) {
		return {};
	}
	return to_poly_vec3(quader_geometry::aabb_center(quader_geometry::QAabb3f { to_geometry_vec3(bounds.min), to_geometry_vec3(bounds.max) }));
}

void translate_document_vertices(quader_poly::Document& document, quader::QVec3 delta)
{
	for (quader_poly::Vertex& vertex : document.vertices) {
		vertex.position += delta;
	}
}

bool finite_vec3(quader::QVec3 value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

quader::QVec3 average_points(std::span<const quader::QVec3> points)
{
	quader::QVec3 center;
	for (quader::QVec3 point : points) {
		center += point;
	}
	return points.empty() ? center : center / static_cast<float>(points.size());
}

quader_geometry::QVec3f oriented_quad_area_vector(
		std::span<const quader::QVec3> corners,
		const std::array<int, 4>& indices)
{
	quader_geometry::QVec3f area {};
	for (std::size_t index = 0; index < indices.size(); ++index) {
		const quader_geometry::QVec3f current = to_geometry_vec3(corners[static_cast<std::size_t>(indices[index])]);
		const quader_geometry::QVec3f next = to_geometry_vec3(corners[static_cast<std::size_t>(indices[(index + 1U) % indices.size()])]);
		area = area + quader_geometry::cross(current, next);
	}
	return area;
}

bool add_oriented_box_quad(
		quader_poly::Document& document,
		const std::array<quader_poly::ElementId, 8>& vertex_ids,
		std::span<const quader::QVec3> corners,
		std::array<int, 4> indices,
		quader::QVec3 box_center)
{
	const std::array<quader::QVec3, 4> face_points {
		corners[static_cast<std::size_t>(indices[0])],
		corners[static_cast<std::size_t>(indices[1])],
		corners[static_cast<std::size_t>(indices[2])],
		corners[static_cast<std::size_t>(indices[3])],
	};
	const quader::QVec3 face_center = average_points(face_points);
	const quader::QVec3 outward = face_center - box_center;
	const quader_geometry::QVec3f area = oriented_quad_area_vector(corners, indices);
	if (quader_geometry::length_squared(area) <= create_box_epsilon * create_box_epsilon ||
			quader::length_squared(outward) <= create_box_epsilon * create_box_epsilon) {
		return false;
	}
	if (quader_geometry::dot(area, to_geometry_vec3(outward)) < 0.0F) {
		std::reverse(indices.begin(), indices.end());
	}

	const std::array face_vertices {
		vertex_ids[static_cast<std::size_t>(indices[0])],
		vertex_ids[static_cast<std::size_t>(indices[1])],
		vertex_ids[static_cast<std::size_t>(indices[2])],
		vertex_ids[static_cast<std::size_t>(indices[3])],
	};
	return quader_poly::add_face(document, face_vertices) != quader_poly::invalid_element_id;
}


CommandResult single_selected_face_command(
		std::vector<PolygonObject>& objects,
		quader_poly::SelectionMode required_mode,
		std::string_view empty_message,
		auto&& command)
{
	const int index = first_selected_index(objects);
	if (index < 0) {
		return error_result(std::string(empty_message));
	}
	PolygonObject& object = objects[index];
	object.selection.mode = required_mode;
	quader_poly::OperationResult result = command(object.document, object.selection);
	if (!result.changed) {
		return error_result(result.message);
	}
	quader_poly::clear_face_uvs(object.document);
	object.selected = !object.selection.empty();
	return ok_result(true);
}

} // namespace

CommandResult ok_result(bool changed)
{
	return { true, changed, {} };
}
CommandResult error_result(std::string message)
{
	return { false, false, std::move(message) };
}
bool state_revisions_differ(const SessionState& left, const SessionState& right)
{
	return left.content_revision != right.content_revision ||
		left.selection_revision != right.selection_revision;
}
bool basis_is_identity(const quader_poly::Transform3& transform)
{
	return quader_geometry::length_squared(to_geometry_vec3(transform.x_axis - quader::QVec3 { 1.0F, 0.0F, 0.0F })) <= transform_basis_epsilon &&
		quader_geometry::length_squared(to_geometry_vec3(transform.y_axis - quader::QVec3 { 0.0F, 1.0F, 0.0F })) <= transform_basis_epsilon &&
		quader_geometry::length_squared(to_geometry_vec3(transform.z_axis - quader::QVec3 { 0.0F, 0.0F, 1.0F })) <= transform_basis_epsilon;
}
std::uint32_t saturated_history_size(std::size_t size)
{
	return static_cast<std::uint32_t>(std::min<std::size_t>(size, std::numeric_limits<std::uint32_t>::max()));
}

SelectionMode sanitize_selection_mode(int mode)
{
	return static_cast<SelectionMode>(std::clamp(mode, int(SelectionMode::vertex), int(SelectionMode::object)));
}

SelectionEditOperation sanitize_selection_operation(int operation)
{
	if (operation == int(SelectionEditOperation::add) || operation == int(SelectionEditOperation::remove)) {
		return static_cast<SelectionEditOperation>(operation);
	}
	return SelectionEditOperation::replace;
}

quader_poly::SelectionMode component_selection_mode(SelectionMode mode)
{
	switch (mode) {
		case SelectionMode::edge:
			return quader_poly::SelectionMode::edge;
		case SelectionMode::face:
			return quader_poly::SelectionMode::face;
		case SelectionMode::vertex:
		case SelectionMode::object:
		default:
			return quader_poly::SelectionMode::vertex;
	}
}

int selected_object_index(const std::vector<PolygonObject>& objects, int active_object_id)
{
	if (active_object_id > 0) {
		for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
			if (objects[index].id == active_object_id && objects[index].selected) {
				return index;
			}
		}
	}
	return first_selected_index(objects);
}

int object_index_by_id(const std::vector<PolygonObject>& objects, int object_id)
{
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (objects[index].id == object_id) {
			return index;
		}
	}
	return -1;
}

int selected_object_id(const std::vector<PolygonObject>& objects, int active_object_id)
{
	const int index = selected_object_index(objects, active_object_id);
	return index >= 0 ? objects[index].id : 0;
}

std::vector<int> selected_object_ids(const std::vector<PolygonObject>& objects)
{
	std::vector<int> ids;
	for (const PolygonObject& object : objects) {
		if (object.selected && object.id > 0) {
			ids.push_back(object.id);
		}
	}
	return ids;
}

std::vector<int> selected_object_indices(const std::vector<PolygonObject>& objects)
{
	std::vector<int> indices;
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		if (objects[index].selected) {
			indices.push_back(index);
		}
	}
	return indices;
}

std::vector<int> component_command_object_indices(const std::vector<PolygonObject>& objects)
{
	std::vector<int> indices = selected_object_indices(objects);
	if (!indices.empty()) {
		return indices;
	}
	indices.reserve(objects.size());
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		indices.push_back(index);
	}
	return indices;
}

void normalize_active_object_id(const std::vector<PolygonObject>& objects, int& active_object_id)
{
	if (selected_object_index(objects, active_object_id) >= 0) {
		return;
	}
	active_object_id = selected_object_id(objects, -1);
	if (active_object_id <= 0) {
		active_object_id = -1;
	}
}

void set_selection_mode(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	for (PolygonObject& object : objects) {
		if (mode == SelectionMode::object) {
			object.selection = quader_poly::Selection();
			continue;
		}
		quader_poly::convert_selection_mode(object.document, object.selection, component_selection_mode(mode));
	}
	normalize_active_object_id(objects, active_object_id);
}

void align_component_selection_to_mode(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode == SelectionMode::object) {
		for (PolygonObject& object : objects) {
			object.selection = quader_poly::Selection();
		}
		normalize_active_object_id(objects, active_object_id);
		return;
	}
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		quader_poly::convert_selection_mode(object.document, object.selection, component_selection_mode(mode));
		object.selected = !object.selection.empty();
	}
	normalize_active_object_id(objects, active_object_id);
}

void clear_selection(std::vector<PolygonObject>& objects, int& active_object_id)
{
	for (PolygonObject& object : objects) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}
	active_object_id = -1;
}

bool has_selection(const std::vector<PolygonObject>& objects, SelectionMode mode, int active_object_id)
{
	const int index = selected_object_index(objects, active_object_id);
	if (index < 0) {
		return false;
	}
	return mode == SelectionMode::object || !objects[index].selection.empty();
}

std::optional<quader::QVec3> selection_center(const std::vector<PolygonObject>& objects, SelectionMode mode, int active_object_id)
{
	(void)active_object_id;
	const Bounds3 bounds = selection_bounds(objects, mode);
	if (bounds.empty) {
		return std::nullopt;
	}
	return (bounds.min + bounds.max) * 0.5F;
}

ActiveSelection active_selection(const std::vector<PolygonObject>& objects, SelectionMode mode, int active_object_id)
{
	ActiveSelection active;
	active.selection_mode = mode;
	const int index = selected_object_index(objects, active_object_id);
	if (index < 0) {
		return active;
	}
	const PolygonObject& object = objects[index];
	if (object.id <= 0 || !object.selected) {
		return active;
	}
	active.has_active = true;
	active.object_id = object.id;
	if (mode == SelectionMode::object) {
		active.kind = ActiveSelectionKind::object;
		return active;
	}
	if (object.selection.empty() || !object.selection.has_active) {
		active.has_active = false;
		return active;
	}
	if (object.selection.active_kind == quader_poly::ElementKind::vertex) {
		active.kind = ActiveSelectionKind::vertex;
		active.vertex_id = object.selection.active_vertex;
	} else if (object.selection.active_kind == quader_poly::ElementKind::edge) {
		active.kind = ActiveSelectionKind::edge;
		active.edge = object.selection.active_edge;
	} else if (object.selection.active_kind == quader_poly::ElementKind::face) {
		active.kind = ActiveSelectionKind::face;
		active.face_id = object.selection.active_face;
	}
	return active;
}

int next_polygon_object_id(const std::vector<PolygonObject>& objects, int next_object_id)
{
	int next_id = std::max(1, next_object_id);
	for (const PolygonObject& object : objects) {
		if (object.id >= next_id) {
			next_id = object.id + 1;
		}
	}
	return next_id;
}

quader_poly::ElementId next_document_vertex_id(const quader_poly::Document& document)
{
	quader_poly::ElementId next_id = std::max<quader_poly::ElementId>(document.next_vertex_id, 1U);
	for (const quader_poly::Vertex& vertex : document.vertices) {
		next_id = std::max(next_id, vertex.id + 1U);
	}
	return next_id;
}

quader_poly::ElementId next_document_face_id(const quader_poly::Document& document)
{
	quader_poly::ElementId next_id = std::max<quader_poly::ElementId>(document.next_face_id, 1U);
	for (const quader_poly::Face& face : document.faces) {
		next_id = std::max(next_id, face.id + 1U);
	}
	return next_id;
}

quader_poly::Edge remap_edge(quader_poly::Edge edge, const std::unordered_map<quader_poly::ElementId, quader_poly::ElementId>& vertex_ids)
{
	const auto found_a = vertex_ids.find(edge.a);
	const auto found_b = vertex_ids.find(edge.b);
	if (found_a == vertex_ids.end() || found_b == vertex_ids.end()) {
		return {};
	}
	return quader_poly::make_edge(found_a->second, found_b->second);
}

void append_document_with_remapped_ids(
		quader_poly::Document& target,
		const quader_poly::Document& source,
		std::unordered_map<quader_poly::ElementId, quader_poly::ElementId>& out_vertex_ids,
		std::unordered_map<quader_poly::ElementId, quader_poly::ElementId>* out_face_ids = nullptr)
{
	target.next_vertex_id = next_document_vertex_id(target);
	target.next_face_id = next_document_face_id(target);
	out_vertex_ids.clear();
	out_vertex_ids.reserve(source.vertices.size());
	if (out_face_ids != nullptr) {
		out_face_ids->clear();
		out_face_ids->reserve(source.faces.size());
	}
	for (const quader_poly::Vertex& vertex : source.vertices) {
		quader_poly::Vertex copied_vertex = vertex;
		copied_vertex.id = target.next_vertex_id++;
		out_vertex_ids.emplace(vertex.id, copied_vertex.id);
		target.vertices.push_back(copied_vertex);
	}

	for (const quader_poly::Face& face : source.faces) {
		quader_poly::Face copied_face = face;
		copied_face.id = target.next_face_id++;
		if (out_face_ids != nullptr) {
			out_face_ids->emplace(face.id, copied_face.id);
		}
		for (quader_poly::ElementId& vertex_id : copied_face.vertices) {
			const auto remapped = out_vertex_ids.find(vertex_id);
			vertex_id = remapped != out_vertex_ids.end() ? remapped->second : quader_poly::invalid_element_id;
		}
		target.faces.push_back(std::move(copied_face));
	}
}

std::vector<quader_poly::ElementId> remap_face_ids(
		const std::vector<quader_poly::ElementId>& face_ids,
		const std::unordered_map<quader_poly::ElementId, quader_poly::ElementId>& remapped_face_ids)
{
	std::vector<quader_poly::ElementId> remapped;
	remapped.reserve(face_ids.size());
	for (const quader_poly::ElementId face_id : face_ids) {
		const auto found = remapped_face_ids.find(face_id);
		if (found == remapped_face_ids.end()) {
			return {};
		}
		remapped.push_back(found->second);
	}
	return remapped;
}

/**
 * Represents a Bridge Boundary Group value used by the modeling runtime domain.
 */
struct BridgeBoundaryGroup {
	std::vector<quader_poly::ElementId> vertices;
	std::vector<quader_poly::Edge> edges;
	bool closed = false;
};

quader::QVec3 bridge_vertex_position(const quader_poly::Document& document, quader_poly::ElementId vertex_id)
{
	const quader_poly::Vertex* vertex = quader_poly::find_vertex(document, vertex_id);
	return vertex != nullptr ? vertex->position : quader::QVec3 {};
}

std::vector<quader_poly::Edge> bridge_edges_from_vertices(const std::vector<quader_poly::ElementId>& vertices, bool closed)
{
	std::vector<quader_poly::Edge> edges;
	if (vertices.size() < 2) {
		return edges;
	}
	const std::size_t edge_count = closed ? vertices.size() : vertices.size() - 1U;
	edges.reserve(edge_count);
	for (std::size_t index = 0; index < edge_count; ++index) {
		edges.push_back(quader_poly::make_edge(vertices[index], vertices[(index + 1U) % vertices.size()]));
	}
	return edges;
}

std::optional<BridgeBoundaryGroup> ordered_bridge_boundary_group(
		const quader_poly::Document& document,
		const std::vector<quader_poly::Edge>& selected_edges)
{
	std::vector<quader_poly::Edge> edges;
	edges.reserve(selected_edges.size());
	std::map<quader_poly::ElementId, std::vector<quader_poly::ElementId>> adjacency;
	for (quader_poly::Edge edge : selected_edges) {
		edge = quader_poly::make_edge(edge.a, edge.b);
		if (edge.a == quader_poly::invalid_element_id ||
				edge.b == quader_poly::invalid_element_id ||
				edge.a == edge.b ||
				quader_poly::find_vertex(document, edge.a) == nullptr ||
				quader_poly::find_vertex(document, edge.b) == nullptr ||
				std::find(edges.begin(), edges.end(), edge) != edges.end()) {
			return std::nullopt;
		}
		edges.push_back(edge);
		adjacency[edge.a].push_back(edge.b);
		adjacency[edge.b].push_back(edge.a);
	}
	if (edges.empty()) {
		return std::nullopt;
	}

	std::vector<quader_poly::ElementId> endpoints;
	for (auto& entry : adjacency) {
		std::ranges::sort(entry.second);
		if (entry.second.empty() || entry.second.size() > 2U) {
			return std::nullopt;
		}
		if (entry.second.size() == 1U) {
			endpoints.push_back(entry.first);
		}
	}
	const bool closed = endpoints.empty();
	if (!closed && endpoints.size() != 2U) {
		return std::nullopt;
	}

	const quader_poly::ElementId start = closed ? adjacency.begin()->first : std::min(endpoints[0], endpoints[1]);
	std::vector<quader_poly::ElementId> vertices;
	vertices.reserve(closed ? edges.size() : edges.size() + 1U);
	quader_poly::ElementId previous = quader_poly::invalid_element_id;
	quader_poly::ElementId current = start;
	for (std::size_t step = 0; step <= edges.size(); ++step) {
		vertices.push_back(current);
		const auto found = adjacency.find(current);
		if (found == adjacency.end()) {
			return std::nullopt;
		}

		quader_poly::ElementId next = quader_poly::invalid_element_id;
		for (const quader_poly::ElementId neighbor : found->second) {
			if (neighbor != previous) {
				next = neighbor;
				break;
			}
		}
		if (next == quader_poly::invalid_element_id) {
			break;
		}
		if (closed && next == start) {
			break;
		}
		previous = current;
		current = next;
	}

	const std::vector<quader_poly::Edge> ordered_edges = bridge_edges_from_vertices(vertices, closed);
	if (ordered_edges.size() != edges.size()) {
		return std::nullopt;
	}
	for (const quader_poly::Edge& edge : edges) {
		if (std::find(ordered_edges.begin(), ordered_edges.end(), edge) == ordered_edges.end()) {
			return std::nullopt;
		}
	}

	return BridgeBoundaryGroup { vertices, ordered_edges, closed };
}

std::vector<quader_poly::ElementId> bridge_aligned_vertices(
		const BridgeBoundaryGroup& group,
		std::size_t offset,
		bool reversed)
{
	std::vector<quader_poly::ElementId> vertices;
	vertices.reserve(group.vertices.size());
	const std::size_t count = group.vertices.size();
	if (count == 0U) {
		return vertices;
	}
	if (!group.closed) {
		vertices = group.vertices;
		if (reversed) {
			std::ranges::reverse(vertices);
		}
		return vertices;
	}

	for (std::size_t index = 0; index < count; ++index) {
		const std::size_t source_index = reversed ?
				(offset + count - (index % count)) % count :
				(offset + index) % count;
		vertices.push_back(group.vertices[source_index]);
	}
	return vertices;
}

float bridge_alignment_score(
		const quader_poly::Document& document,
		const BridgeBoundaryGroup& first,
		const std::vector<quader_poly::ElementId>& second_vertices)
{
	if (first.vertices.size() != second_vertices.size()) {
		return std::numeric_limits<float>::infinity();
	}

	float score = 0.0F;
	for (std::size_t index = 0; index < first.vertices.size(); ++index) {
		const quader::QVec3 delta = bridge_vertex_position(document, first.vertices[index]) - bridge_vertex_position(document, second_vertices[index]);
		score += quader::length_squared(delta);
	}
	return score;
}

std::optional<std::vector<std::pair<quader_poly::Edge, quader_poly::Edge>>> bridge_edge_pairs_from_groups(
		const quader_poly::Document& document,
		const std::vector<quader_poly::Edge>& first_edges,
		const std::vector<quader_poly::Edge>& second_edges)
{
	const std::optional<BridgeBoundaryGroup> first_group = ordered_bridge_boundary_group(document, first_edges);
	const std::optional<BridgeBoundaryGroup> second_group = ordered_bridge_boundary_group(document, second_edges);
	if (!first_group.has_value() || !second_group.has_value()) {
		return std::nullopt;
	}
	if (first_group->closed != second_group->closed || first_group->edges.size() != second_group->edges.size()) {
		return std::nullopt;
	}

	float best_score = std::numeric_limits<float>::infinity();
	std::vector<quader_poly::ElementId> best_second_vertices;
	const std::size_t offsets = second_group->closed ? second_group->vertices.size() : 1U;
	for (std::size_t offset = 0; offset < offsets; ++offset) {
		for (const bool reversed : { false, true }) {
			std::vector<quader_poly::ElementId> candidate_vertices = bridge_aligned_vertices(*second_group, offset, reversed);
			const float score = bridge_alignment_score(document, *first_group, candidate_vertices);
			if (score < best_score) {
				best_score = score;
				best_second_vertices = std::move(candidate_vertices);
			}
		}
	}
	if (best_second_vertices.empty() || !std::isfinite(best_score)) {
		return std::nullopt;
	}

	const std::vector<quader_poly::Edge> aligned_second_edges = bridge_edges_from_vertices(best_second_vertices, second_group->closed);
	if (aligned_second_edges.size() != first_group->edges.size()) {
		return std::nullopt;
	}

	std::vector<std::pair<quader_poly::Edge, quader_poly::Edge>> edge_pairs;
	edge_pairs.reserve(first_group->edges.size());
	for (std::size_t index = 0; index < first_group->edges.size(); ++index) {
		edge_pairs.emplace_back(first_group->edges[index], aligned_second_edges[index]);
	}
	return edge_pairs;
}

/**
 * Represents a Bridge Aligned Boundary Groups value used by the modeling runtime domain.
 */
struct BridgeAlignedBoundaryGroups {
	std::vector<quader_poly::ElementId> first_vertices;
	std::vector<quader_poly::ElementId> second_vertices;
	bool closed = false;
};

std::optional<BridgeAlignedBoundaryGroups> bridge_aligned_boundary_groups(
		const quader_poly::Document& document,
		const std::vector<quader_poly::Edge>& first_edges,
		const std::vector<quader_poly::Edge>& second_edges)
{
	const std::optional<BridgeBoundaryGroup> first_group = ordered_bridge_boundary_group(document, first_edges);
	const std::optional<BridgeBoundaryGroup> second_group = ordered_bridge_boundary_group(document, second_edges);
	if (!first_group.has_value() || !second_group.has_value()) {
		return std::nullopt;
	}
	if (first_group->closed != second_group->closed || first_group->edges.size() != second_group->edges.size()) {
		return std::nullopt;
	}

	float best_score = std::numeric_limits<float>::infinity();
	std::vector<quader_poly::ElementId> best_second_vertices;
	const std::size_t offsets = second_group->closed ? second_group->vertices.size() : 1U;
	for (std::size_t offset = 0; offset < offsets; ++offset) {
		for (const bool reversed : { false, true }) {
			std::vector<quader_poly::ElementId> candidate_vertices = bridge_aligned_vertices(*second_group, offset, reversed);
			const float score = bridge_alignment_score(document, *first_group, candidate_vertices);
			if (score < best_score) {
				best_score = score;
				best_second_vertices = std::move(candidate_vertices);
			}
		}
	}
	if (best_second_vertices.empty() || !std::isfinite(best_score)) {
		return std::nullopt;
	}

	const std::vector<quader_poly::Edge> aligned_second_edges = bridge_edges_from_vertices(best_second_vertices, second_group->closed);
	if (aligned_second_edges.size() != first_group->edges.size()) {
		return std::nullopt;
	}

	return BridgeAlignedBoundaryGroups { first_group->vertices, std::move(best_second_vertices), first_group->closed };
}

CommandResult select_all(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (objects.empty()) {
		return ok_result(false);
	}

	bool changed = false;
	if (mode == SelectionMode::object) {
		for (PolygonObject& object : objects) {
			if (!object.selected || !object.selection.empty()) {
				object.selected = true;
				object.selection = quader_poly::Selection();
				changed = true;
			}
		}
		active_object_id = objects.back().id;
		normalize_active_object_id(objects, active_object_id);
		return ok_result(changed);
	}

	const std::vector<int> target_indices = component_command_object_indices(objects);
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		const bool target = std::find(target_indices.begin(), target_indices.end(), index) != target_indices.end();
		PolygonObject& object = objects[index];
		if (!target) {
			continue;
		}
		quader_poly::Selection next_selection = all_component_selection(object.document, mode);
		next_selection.mode = component_selection_mode(mode);
		const bool was_selected = object.selected;
		const bool changed_object =
			!was_selected ||
			object.selection.mode != next_selection.mode ||
			object.selection.vertices != next_selection.vertices ||
			object.selection.edges != next_selection.edges ||
			object.selection.faces != next_selection.faces;
		object.selection = std::move(next_selection);
		object.selected = !object.selection.empty();
		changed = changed || changed_object;
		if (object.selected) {
			active_object_id = object.id;
		}
	}
	normalize_active_object_id(objects, active_object_id);
	return ok_result(changed);
}

CommandResult invert_selection(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (objects.empty()) {
		return ok_result(false);
	}

	bool changed = false;
	if (mode == SelectionMode::object) {
		for (PolygonObject& object : objects) {
			object.selected = !object.selected;
			object.selection = quader_poly::Selection();
			changed = true;
		}
		normalize_active_object_id(objects, active_object_id);
		return ok_result(changed);
	}

	const std::vector<int> target_indices = component_command_object_indices(objects);
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		const bool target = std::find(target_indices.begin(), target_indices.end(), index) != target_indices.end();
		if (!target) {
			continue;
		}
		PolygonObject& object = objects[index];
		quader_poly::Selection next_selection;
		next_selection.mode = component_selection_mode(mode);
		switch (mode) {
			case SelectionMode::edge:
				for (const quader_poly::Edge& edge : quader_poly::document_edges(object.document)) {
					if (!quader_poly::selection_contains(object.selection, edge)) {
						next_selection.edges.push_back(edge);
					}
				}
				break;
			case SelectionMode::face:
				for (const quader_poly::Face& face : object.document.faces) {
					if (!quader_poly::selection_contains_face(object.selection, face.id)) {
						next_selection.faces.push_back(face.id);
					}
				}
				break;
			case SelectionMode::vertex:
			case SelectionMode::object:
			default:
				for (const quader_poly::Vertex& vertex : object.document.vertices) {
					if (!quader_poly::selection_contains(object.selection, vertex.id)) {
						next_selection.vertices.push_back(vertex.id);
					}
				}
				break;
		}
		activate_component_selection(next_selection);
		object.selection = std::move(next_selection);
		object.selected = !object.selection.empty();
		changed = true;
		if (object.selected) {
			active_object_id = object.id;
		}
	}
	normalize_active_object_id(objects, active_object_id);
	return ok_result(changed);
}

CommandResult snap_selected_vertices_to_active(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::vertex) {
		return error_result("Snap to Vertex only works in vertex mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select vertices before snapping.");
	}

	const int index = selected_object_index(objects, active_object_id);
	if (index < 0) {
		return error_result("Select vertices before snapping.");
	}

	PolygonObject& active_object = objects[index];
	active_object.selection.mode = quader_poly::SelectionMode::vertex;
	if (!active_object.selection.has_active ||
			active_object.selection.active_kind != quader_poly::ElementKind::vertex ||
			!quader_poly::selection_contains(active_object.selection, active_object.selection.active_vertex)) {
		return error_result("Snap to Vertex needs an active selected vertex.");
	}

	const quader_poly::Vertex* active_vertex = quader_poly::find_vertex(active_object.document, active_object.selection.active_vertex);
	if (active_vertex == nullptr) {
		return error_result("Active vertex was not found.");
	}

	const quader::QVec3 target_position = active_vertex->position;
	bool has_other_vertex = false;
	bool changed = false;
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}

		object.selection.mode = quader_poly::SelectionMode::vertex;
		bool object_changed = false;
		for (const quader_poly::ElementId vertex_id : object.selection.vertices) {
			if (vertex_id == quader_poly::invalid_element_id ||
					(object.id == active_object.id && vertex_id == active_object.selection.active_vertex)) {
				continue;
			}

			quader_poly::Vertex* vertex = quader_poly::find_vertex(object.document, vertex_id);
			if (vertex == nullptr) {
				continue;
			}

			has_other_vertex = true;
			if (quader::length_squared(vertex->position - target_position) <= transform_point_epsilon) {
				continue;
			}

			vertex->position = target_position;
			object_changed = true;
			changed = true;
		}

		if (object_changed) {
			quader_poly::clear_face_uvs(object.document);
		}
		object.selected = !object.selection.empty();
	}
	if (!has_other_vertex) {
		return error_result("Select at least one other vertex to snap.");
	}
	if (!changed) {
		return error_result("Selected vertices are already at the active vertex.");
	}

	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult snap_selected_edges_to_active(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::edge) {
		return error_result("Edge Snap only works in edge mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select edges before snapping.");
	}

	const int index = selected_object_index(objects, active_object_id);
	if (index < 0) {
		return error_result("Select edges before snapping.");
	}

	PolygonObject& active_object = objects[index];
	active_object.selection.mode = quader_poly::SelectionMode::edge;
	if (!active_object.selection.has_active ||
			active_object.selection.active_kind != quader_poly::ElementKind::edge ||
			!quader_poly::selection_contains(active_object.selection, active_object.selection.active_edge)) {
		return error_result("Edge Snap needs an active selected edge.");
	}

	const quader_poly::Edge target_edge = quader_poly::make_edge(active_object.selection.active_edge.a, active_object.selection.active_edge.b);
	const quader_poly::Vertex* target_a = quader_poly::find_vertex(active_object.document, target_edge.a);
	const quader_poly::Vertex* target_b = quader_poly::find_vertex(active_object.document, target_edge.b);
	if (target_a == nullptr || target_b == nullptr) {
		return error_result("Active edge vertices were not found.");
	}

	const quader::QVec3 target_a_position = target_a->position;
	const quader::QVec3 target_b_position = target_b->position;
	bool has_other_edge = false;
	bool changed = false;
	for (PolygonObject& object : objects) {
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}

		std::unordered_map<quader_poly::ElementId, quader::QVec3> targets;
		object.selection.mode = quader_poly::SelectionMode::edge;
		for (quader_poly::Edge edge : object.selection.edges) {
			edge = quader_poly::make_edge(edge.a, edge.b);
			if (edge.a == quader_poly::invalid_element_id || edge.b == quader_poly::invalid_element_id || edge.a == edge.b) {
				continue;
			}
			if (object.id == active_object.id && edge == target_edge) {
				continue;
			}

			const quader_poly::Vertex* source_a = quader_poly::find_vertex(object.document, edge.a);
			const quader_poly::Vertex* source_b = quader_poly::find_vertex(object.document, edge.b);
			if (source_a == nullptr || source_b == nullptr) {
				continue;
			}

			has_other_edge = true;
			const float direct_cost =
					quader::length_squared(source_a->position - target_a_position) +
					quader::length_squared(source_b->position - target_b_position);
			const float flipped_cost =
					quader::length_squared(source_a->position - target_b_position) +
					quader::length_squared(source_b->position - target_a_position);
			const quader::QVec3 desired_a = direct_cost <= flipped_cost ? target_a_position : target_b_position;
			const quader::QVec3 desired_b = direct_cost <= flipped_cost ? target_b_position : target_a_position;

			const auto add_target = [&targets](quader_poly::ElementId vertex_id, quader::QVec3 desired_position) -> std::optional<std::string> {
				const auto existing = targets.find(vertex_id);
				if (existing == targets.end()) {
					targets[vertex_id] = desired_position;
					return std::nullopt;
				}
				if (quader::length_squared(existing->second - desired_position) > transform_point_epsilon) {
					return std::string("Edge Snap has conflicting selected-edge endpoint targets.");
				}
				return std::nullopt;
			};
			if (!(object.id == active_object.id && (edge.a == target_edge.a || edge.a == target_edge.b))) {
				if (std::optional<std::string> error = add_target(edge.a, desired_a)) {
					return error_result(*error);
				}
			}
			if (!(object.id == active_object.id && (edge.b == target_edge.a || edge.b == target_edge.b))) {
				if (std::optional<std::string> error = add_target(edge.b, desired_b)) {
					return error_result(*error);
				}
			}
		}

		bool object_changed = false;
		for (const auto& entry : targets) {
			quader_poly::Vertex* vertex = quader_poly::find_vertex(object.document, entry.first);
			if (vertex == nullptr) {
				continue;
			}
			const quader::QVec3 desired = entry.second;
			if (quader::length_squared(vertex->position - desired) <= transform_point_epsilon) {
				continue;
			}
			vertex->position = desired;
			object_changed = true;
			changed = true;
		}

		if (object_changed) {
			quader_poly::clear_face_uvs(object.document);
		}
		object.selected = !object.selection.empty();
	}

	if (!has_other_edge) {
		return error_result("Select at least one other edge to snap.");
	}
	if (!changed) {
		return error_result("Selected edges are already at the active edge.");
	}

	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult merge_selected_vertices_to_active(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::vertex) {
		return error_result("Merge only works in vertex mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select vertices before merging.");
	}

	const int index = selected_object_index(objects, active_object_id);
	if (index < 0) {
		return error_result("Select vertices before merging.");
	}

	PolygonObject& object = objects[index];
	object.selection.mode = quader_poly::SelectionMode::vertex;
	const quader_poly::OperationResult result = quader_poly::merge_selected_vertices_to_active(object.document, object.selection);
	if (!result.changed) {
		return error_result(result.message);
	}

	object.selected = !object.selection.empty();
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult merge_selected_vertices_to_center(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::vertex) {
		return error_result("Merge only works in vertex mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select vertices before merging.");
	}

	const int index = selected_object_index(objects, active_object_id);
	if (index < 0) {
		return error_result("Select vertices before merging.");
	}

	PolygonObject& object = objects[index];
	object.selection.mode = quader_poly::SelectionMode::vertex;
	const quader_poly::OperationResult result = quader_poly::merge_selected_vertices_to_center(object.document, object.selection);
	if (!result.changed) {
		return error_result(result.message);
	}

	object.selected = !object.selection.empty();
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult merge_selected_vertices_by_distance(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id, double tolerance)
{
	constexpr std::string_view no_distance_merge_message = "No selected vertices are within the merge distance.";
	if (mode != SelectionMode::vertex) {
		return error_result("Merge by Distance only works in vertex mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select vertices before merging by distance.");
	}

	std::vector<PolygonObject> candidate = objects;
	bool has_selected_vertices = false;
	bool changed = false;
	std::string unchanged_message;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.vertices.empty()) {
			continue;
		}

		has_selected_vertices = true;
		object.selection.mode = quader_poly::SelectionMode::vertex;
		const quader_poly::OperationResult result =
			quader_poly::merge_selected_vertices_by_distance(object.document, object.selection, static_cast<float>(tolerance));
		if (!result.changed) {
			if (!result.message.empty()) {
				if (result.message != no_distance_merge_message) {
					return error_result(result.message);
				}
				if (unchanged_message.empty()) {
					unchanged_message = result.message;
				}
			}
			continue;
		}

		object.selected = !object.selection.empty();
		changed = true;
	}

	if (!has_selected_vertices) {
		return error_result("Select at least two vertices to merge by distance.");
	}
	if (!changed) {
		return CommandResult { true, false, unchanged_message };
	}

	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult bevel_selected_vertices(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id, float distance)
{
	if (mode != SelectionMode::vertex) {
		return error_result("Bevel Vertex only works in vertex mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select vertices before beveling.");
	}

	std::vector<PolygonObject> candidate = objects;
	bool has_selected_vertices = false;
	bool changed = false;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.vertices.empty()) {
			continue;
		}

		has_selected_vertices = true;
		object.selection.mode = quader_poly::SelectionMode::vertex;
		const quader_poly::OperationResult result = quader_poly::bevel_selected_vertices(object.document, object.selection, distance);
		if (!result.changed) {
			return error_result(result.message.empty() ? std::string("Could not bevel selected vertices.") : result.message);
		}

		object.selected = !object.selection.empty();
		changed = true;
	}

	if (!has_selected_vertices) {
		return error_result("Select one or more vertices to bevel.");
	}
	if (!changed) {
		return error_result("No bevelable selected vertices were found.");
	}

	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult connect_selected_vertices(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::vertex) {
		return error_result("Connect only works in vertex mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select vertices before connecting.");
	}

	std::vector<PolygonObject> candidate = objects;
	PolygonObject* target_object = nullptr;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.vertices.empty()) {
			continue;
		}
		if (target_object != nullptr) {
			return error_result("Connect needs selected vertices from one mesh.");
		}
		target_object = &object;
	}

	if (target_object == nullptr) {
		return error_result("Select at least two vertices to connect.");
	}

	target_object->selection.mode = quader_poly::SelectionMode::vertex;
	const quader_poly::OperationResult result = quader_poly::connect_selected_vertices(target_object->document, target_object->selection);
	if (!result.changed) {
		return error_result(result.message.empty() ? std::string("Could not connect selected vertices.") : result.message);
	}

	target_object->selected = !target_object->selection.empty();
	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult dissolve_selected_vertices(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::vertex) {
		return error_result("Dissolve Vertex only works in vertex mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select vertices before dissolving.");
	}

	std::vector<PolygonObject> candidate = objects;
	bool has_selected_vertices = false;
	bool changed = false;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.vertices.empty()) {
			continue;
		}

		has_selected_vertices = true;
		object.selection.mode = quader_poly::SelectionMode::vertex;
		const quader_poly::OperationResult result = quader_poly::dissolve_selected_vertices(object.document, object.selection);
		if (!result.changed) {
			return error_result(result.message.empty() ? std::string("Could not dissolve selected vertices.") : result.message);
		}

		object.selected = !object.selection.empty();
		changed = true;
	}

	if (!has_selected_vertices) {
		return error_result("Select one or more vertices to dissolve.");
	}
	if (!changed) {
		return error_result("No dissolvable selected vertices were found.");
	}

	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult connect_selected_edges(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::edge) {
		return error_result("Connect Edges only works in edge mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select edges before connecting.");
	}

	std::vector<PolygonObject> candidate = objects;
	PolygonObject* target_object = nullptr;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}
		if (target_object != nullptr) {
			return error_result("Connect Edges needs selected edges from one mesh.");
		}
		target_object = &object;
	}

	if (target_object == nullptr) {
		return error_result("Select at least two edges to connect.");
	}

	target_object->selection.mode = quader_poly::SelectionMode::edge;
	const quader_poly::OperationResult result = quader_poly::connect_selected_edges(target_object->document, target_object->selection);
	if (!result.changed) {
		return error_result(result.message.empty() ? std::string("Could not connect selected edges.") : result.message);
	}

	target_object->selected = !target_object->selection.empty();
	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult split_selected_edges(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::edge) {
		return error_result("Edge Split only works in edge mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select edges before splitting.");
	}

	std::vector<PolygonObject> candidate = objects;
	bool has_selected_edges = false;
	bool changed = false;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}

		has_selected_edges = true;
		object.selection.mode = quader_poly::SelectionMode::edge;
		const quader_poly::OperationResult result = quader_poly::split_selected_edges(object.document, object.selection);
		if (!result.changed) {
			return error_result(result.message.empty() ? std::string("Could not split selected edges.") : result.message);
		}

		object.selected = !object.selection.empty();
		changed = true;
	}

	if (!has_selected_edges) {
		return error_result("Select one or more edges to split.");
	}
	if (!changed) {
		return error_result("No splittable selected edges were found.");
	}

	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult dissolve_selected_edges(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::edge) {
		return error_result("Dissolve only works in edge mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select edges before dissolving.");
	}

	std::vector<PolygonObject> candidate = objects;
	PolygonObject* target_object = nullptr;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}
		if (target_object != nullptr) {
			return error_result("Dissolve needs selected edges from one mesh.");
		}
		target_object = &object;
	}

	if (target_object == nullptr) {
		return error_result("Select one or more edges to dissolve.");
	}

	target_object->selection.mode = quader_poly::SelectionMode::edge;
	const quader_poly::OperationResult result = quader_poly::dissolve_selected_edges(target_object->document, target_object->selection);
	if (!result.changed) {
		return error_result(result.message.empty() ? std::string("Could not dissolve selected edges.") : result.message);
	}

	target_object->selected = !target_object->selection.empty();
	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult fill_hole_from_selected_edges(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::edge) {
		return error_result("Fill Hole only works in edge mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select open edges before filling a hole.");
	}

	std::vector<PolygonObject> candidate = objects;
	PolygonObject* target_object = nullptr;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}
		if (target_object != nullptr) {
			return error_result("Fill Hole needs selected open edges from one mesh.");
		}
		target_object = &object;
	}

	if (target_object == nullptr) {
		return error_result("Select at least three open edges to fill a hole.");
	}

	target_object->selection.mode = quader_poly::SelectionMode::edge;
	const quader_poly::OperationResult result = quader_poly::fill_hole_from_selected_edges(target_object->document, target_object->selection);
	if (!result.changed) {
		return error_result(result.message.empty() ? std::string("Could not fill the selected hole.") : result.message);
	}

	target_object->selected = !target_object->selection.empty();
	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult bevel_selected_edges(
		std::vector<PolygonObject>& objects,
		SelectionMode mode,
		int& active_object_id,
		const quader_poly::EdgeBevelSettings& settings)
{
	if (mode != SelectionMode::edge) {
		return ok_result(false);
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return ok_result(false);
	}

	std::vector<PolygonObject> candidate = objects;
	bool has_selected_edges = false;
	bool changed = false;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}

		has_selected_edges = true;
		object.selection.mode = quader_poly::SelectionMode::edge;
		const quader_poly::OperationResult result = quader_poly::bevel_selected_edges(object.document, object.selection, settings);
		if (!result.changed) {
			continue;
		}

		object.selected = !object.selection.empty();
		changed = true;
	}

	if (!has_selected_edges) {
		return ok_result(false);
	}
	if (!changed) {
		return ok_result(false);
	}

	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult bridge_selected_edges(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id, int steps)
{
	if (mode != SelectionMode::edge) {
		return error_result("Bridge only works in edge mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select two open edges before bridging.");
	}

	std::vector<PolygonObject> candidate = objects;
	std::vector<int> selected_edge_indices;
	std::size_t selected_edge_count = 0;
	for (int object_index = 0; object_index < static_cast<int>(candidate.size()); ++object_index) {
		PolygonObject& object = candidate[object_index];
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}
		selected_edge_indices.push_back(object_index);
		selected_edge_count += object.selection.edges.size();
	}

	if (selected_edge_indices.empty()) {
		return error_result("Select open edges to bridge.");
	}

	if (selected_edge_indices.size() == 1) {
		if (selected_edge_count != 2) {
			return error_result("Bridge needs exactly two selected open edges on one mesh, or equal edge groups on two meshes.");
		}
		PolygonObject& target_object = candidate[selected_edge_indices.front()];
		target_object.selection.mode = quader_poly::SelectionMode::edge;
		const quader_poly::OperationResult result = quader_poly::bridge_selected_edges(target_object.document, target_object.selection, steps);
		if (!result.changed) {
			return error_result(result.message.empty() ? std::string("Could not bridge selected edges.") : result.message);
		}

		target_object.selected = !target_object.selection.empty();
		objects = std::move(candidate);
		normalize_active_object_id(objects, active_object_id);
		return ok_result(true);
	}

	if (selected_edge_indices.size() != 2 ||
			candidate[selected_edge_indices[0]].selection.edges.size() != candidate[selected_edge_indices[1]].selection.edges.size()) {
		return error_result("Bridge needs equal selected open-edge counts on exactly two meshes.");
	}

	const int active_index = selected_object_index(candidate, active_object_id);
	const int first_index = selected_edge_indices[0];
	const int second_index = selected_edge_indices[1];
	const int survivor_index = (active_index == first_index || active_index == second_index) ? active_index : first_index;
	const int merged_index = survivor_index == first_index ? second_index : first_index;
	const int survivor_id = candidate[survivor_index].id;
	const int merged_id = candidate[merged_index].id;
	const std::vector<quader_poly::Edge> survivor_edges = candidate[survivor_index].selection.edges;
	const std::vector<quader_poly::Edge> source_edges = candidate[merged_index].selection.edges;

	quader_poly::Document bridge_document = candidate[survivor_index].document;
	std::unordered_map<quader_poly::ElementId, quader_poly::ElementId> merged_vertex_ids;
	append_document_with_remapped_ids(bridge_document, candidate[merged_index].document, merged_vertex_ids);
	std::vector<quader_poly::Edge> remapped_source_edges;
	remapped_source_edges.reserve(source_edges.size());
	for (std::size_t edge_index = 0; edge_index < survivor_edges.size(); ++edge_index) {
		const quader_poly::Edge remapped_source_edge = remap_edge(source_edges[edge_index], merged_vertex_ids);
		if (remapped_source_edge.a == quader_poly::invalid_element_id || remapped_source_edge.b == quader_poly::invalid_element_id) {
			return error_result("Bridge could not merge the selected mesh edge ids.");
		}
		remapped_source_edges.push_back(remapped_source_edge);
	}
	const std::optional<BridgeAlignedBoundaryGroups> bridge_groups =
			bridge_aligned_boundary_groups(bridge_document, survivor_edges, remapped_source_edges);
	if (!bridge_groups.has_value()) {
		return error_result("Bridge needs each selected edge group to form one matching connected boundary chain or loop.");
	}

	quader_poly::Selection bridge_selection;
	const quader_poly::OperationResult result = quader_poly::bridge_edge_boundaries(
			bridge_document,
			bridge_selection,
			bridge_groups->first_vertices,
			bridge_groups->second_vertices,
			bridge_groups->closed,
			steps);
	if (!result.changed) {
		return error_result(result.message.empty() ? std::string("Could not bridge selected edges.") : result.message);
	}

	for (PolygonObject& object : candidate) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}
	PolygonObject* survivor_object = nullptr;
	for (PolygonObject& object : candidate) {
		if (object.id == survivor_id) {
			survivor_object = &object;
			break;
		}
	}
	if (survivor_object == nullptr) {
		return error_result("Bridge could not find the merged survivor mesh.");
	}
	survivor_object->document = std::move(bridge_document);
	survivor_object->selection = std::move(bridge_selection);
	survivor_object->selected = !survivor_object->selection.empty();
	candidate.erase(
			std::remove_if(candidate.begin(), candidate.end(), [merged_id](const PolygonObject& object) { return object.id == merged_id; }),
			candidate.end());
	objects = std::move(candidate);
	active_object_id = survivor_id;
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult collapse_selected_edges(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::edge) {
		return error_result("Collapse only works in edge mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select edges before collapsing.");
	}

	std::vector<PolygonObject> candidate = objects;
	PolygonObject* target_object = nullptr;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}
		if (target_object != nullptr) {
			return error_result("Collapse needs selected edges from one mesh.");
		}
		target_object = &object;
	}

	if (target_object == nullptr) {
		return error_result("Select one or more edges to collapse.");
	}

	target_object->selection.mode = quader_poly::SelectionMode::edge;
	const quader_poly::OperationResult result = quader_poly::collapse_selected_edges(target_object->document, target_object->selection);
	if (!result.changed) {
		return error_result(result.message.empty() ? std::string("Could not collapse selected edges.") : result.message);
	}

	target_object->selected = !target_object->selection.empty();
	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult merge_selected_edges(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::edge) {
		return error_result("Merge Edges only works in edge mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select edges before merging.");
	}

	std::vector<PolygonObject> candidate = objects;
	PolygonObject* target_object = nullptr;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}
		if (target_object != nullptr) {
			return error_result("Merge Edges needs selected edges from one mesh.");
		}
		target_object = &object;
	}

	if (target_object == nullptr) {
		return error_result("Select at least two edges to merge.");
	}

	target_object->selection.mode = quader_poly::SelectionMode::edge;
	const quader_poly::OperationResult result = quader_poly::merge_selected_edges(target_object->document, target_object->selection);
	if (!result.changed) {
		return error_result(result.message.empty() ? std::string("Could not merge selected edges.") : result.message);
	}

	target_object->selected = !target_object->selection.empty();
	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult delete_selected(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select a polygon object or element first.");
	}
	if (mode == SelectionMode::object) {
		const std::size_t old_size = objects.size();
		objects.erase(
				std::remove_if(objects.begin(), objects.end(), [](const PolygonObject& object) { return object.selected; }),
				objects.end());
		const bool changed = old_size != objects.size();
		normalize_active_object_id(objects, active_object_id);
		return ok_result(changed);
	}

	bool changed = false;
	std::string last_message = "Select polygon elements first.";
	for (PolygonObject& object : objects) {
		if (!object.selected || object.selection.empty()) {
			continue;
		}
		object.selection.mode = component_selection_mode(mode);
		const quader_poly::OperationResult result = quader_poly::delete_selection(object.document, object.selection);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}
		object.selection.clear();
		object.selected = false;
		changed = true;
	}
	if (!changed) {
		return error_result(last_message);
	}
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult create_box_from_bounds(std::vector<PolygonObject>& objects, int object_id, quader::QVec3 min_bounds, quader::QVec3 max_bounds, int& active_object_id, SelectionMode& selection_mode)
{
	const float min_x = std::min(min_bounds.x, max_bounds.x);
	const float min_y = std::min(min_bounds.y, max_bounds.y);
	const float min_z = std::min(min_bounds.z, max_bounds.z);
	const float max_x = std::max(min_bounds.x, max_bounds.x);
	const float max_y = std::max(min_bounds.y, max_bounds.y);
	const float max_z = std::max(min_bounds.z, max_bounds.z);
	const float width = max_x - min_x;
	const float height = max_y - min_y;
	const float depth = max_z - min_z;
	if (width <= create_box_epsilon || height <= create_box_epsilon || depth <= create_box_epsilon) {
		return error_result("Box needs width, height, and depth.");
	}

	quader_poly::Document document = quader_poly::make_cube(quader_poly::CubePrimitiveSpec {
		width,
		height,
		depth,
		1,
		1,
		1,
	});
	translate_document_vertices(document, {
		(min_x + max_x) * 0.5F,
		(min_y + max_y) * 0.5F,
		(min_z + max_z) * 0.5F,
	});

	for (PolygonObject& object : objects) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}

	PolygonObject object;
	object.id = std::max(1, object_id);
	object.material_id = 0;
	object.selected = true;
	object.document = std::move(document);
	objects.push_back(std::move(object));
	active_object_id = objects.back().id;
	selection_mode = SelectionMode::object;
	return ok_result(true);
}

CommandResult create_box_from_corners(
		std::vector<PolygonObject>& objects,
		int object_id,
		std::span<const quader::QVec3> corners,
		int& active_object_id,
		SelectionMode& selection_mode)
{
	if (corners.size() != 8U) {
		return error_result("Box needs eight corner points.");
	}
	for (const quader::QVec3& corner : corners) {
		if (!finite_vec3(corner)) {
			return error_result("Box corners must be finite.");
		}
	}
	for (std::size_t left = 0; left < corners.size(); ++left) {
		for (std::size_t right = left + 1U; right < corners.size(); ++right) {
			if (quader::length_squared(corners[left] - corners[right]) <= create_box_epsilon * create_box_epsilon) {
				return error_result("Box needs width, height, and depth.");
			}
		}
	}

	quader_poly::Document document;
	std::array<quader_poly::ElementId, 8> vertex_ids {};
	for (std::size_t index = 0; index < corners.size(); ++index) {
		vertex_ids[index] = quader_poly::add_vertex(document, corners[index]);
	}

	const quader::QVec3 center = average_points(corners);
	const std::array<std::array<int, 4>, 6> faces {{
		{ 0, 1, 2, 3 },
		{ 4, 5, 6, 7 },
		{ 0, 4, 5, 1 },
		{ 1, 5, 6, 2 },
		{ 2, 6, 7, 3 },
		{ 3, 7, 4, 0 },
	}};
	for (const std::array<int, 4>& face : faces) {
		if (!add_oriented_box_quad(document, vertex_ids, corners, face, center)) {
			return error_result("Box needs width, height, and depth.");
		}
	}
	if (quader_poly::compile_document(document).indices.empty()) {
		return error_result("Box needs a valid closed volume.");
	}

	for (PolygonObject& object : objects) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}

	PolygonObject object;
	object.id = std::max(1, object_id);
	object.material_id = 0;
	object.selected = true;
	object.document = std::move(document);
	objects.push_back(std::move(object));
	active_object_id = objects.back().id;
	selection_mode = SelectionMode::object;
	return ok_result(true);
}

CommandResult create_polygon_from_points(
		std::vector<PolygonObject>& objects,
		int object_id,
		std::span<const quader::QVec3> points,
		quader::QVec3 face_normal,
		int& active_object_id,
		SelectionMode& selection_mode)
{
	constexpr float polygon_epsilon_squared = 0.00000001F;
	std::vector<quader::QVec3> unique_points;
	unique_points.reserve(points.size());
	for (const quader::QVec3& point : points) {
		if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
			return error_result("Poly vertices must be finite.");
		}
		if (unique_points.empty() || quader::length_squared(unique_points.back() - point) > polygon_epsilon_squared) {
			unique_points.push_back(point);
		}
	}
	if (unique_points.size() >= 2U &&
			quader::length_squared(unique_points.front() - unique_points.back()) <= polygon_epsilon_squared) {
		unique_points.pop_back();
	}
	if (unique_points.size() < 3U) {
		return error_result("Poly needs at least three distinct vertices.");
	}
	for (std::size_t i = 0; i < unique_points.size(); ++i) {
		for (std::size_t j = i + 1U; j < unique_points.size(); ++j) {
			if (quader::length_squared(unique_points[i] - unique_points[j]) <= polygon_epsilon_squared) {
				return error_result("Poly vertices must stay distinct.");
			}
		}
	}

	quader_geometry::QVec3f normal {};
	for (std::size_t index = 0; index < unique_points.size(); ++index) {
		const quader::QVec3 current = unique_points[index];
		const quader::QVec3 next = unique_points[(index + 1U) % unique_points.size()];
		normal = normal + quader_geometry::cross(to_geometry_vec3(current), to_geometry_vec3(next));
	}
	if (quader_geometry::length_squared(normal) <= polygon_epsilon_squared) {
		return error_result("Poly needs a non-degenerate polygon.");
	}
	if (quader::length_squared(face_normal) > polygon_epsilon_squared &&
			quader_geometry::dot(normal, to_geometry_vec3(face_normal)) < 0.0F) {
		std::reverse(unique_points.begin(), unique_points.end());
	}

	quader_poly::Document document;
	std::vector<quader_poly::ElementId> vertex_ids;
	vertex_ids.reserve(unique_points.size());
	for (const quader::QVec3& point : unique_points) {
		vertex_ids.push_back(quader_poly::add_vertex(document, point));
	}
	const quader_poly::ElementId face_id = quader_poly::add_face(document, vertex_ids);
	if (face_id == quader_poly::invalid_element_id) {
		return error_result("Could not create the poly face.");
	}
	if (quader_poly::compile_document(document).indices.empty()) {
		return error_result("Poly needs a triangulatable polygon.");
	}

	for (PolygonObject& object : objects) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}

	PolygonObject object;
	object.id = std::max(1, object_id);
	object.material_id = 0;
	object.selected = true;
	object.document = std::move(document);
	objects.push_back(std::move(object));
	active_object_id = objects.back().id;
	selection_mode = SelectionMode::object;
	return ok_result(true);
}

CommandResult extract_selected_faces(std::vector<PolygonObject>& objects, int object_id, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::face) {
		return error_result("Extract Faces only works in face mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select faces before extracting.");
	}

	const int index = selected_object_index(objects, active_object_id);
	if (index < 0) {
		return error_result("Select faces before extracting.");
	}

	std::vector<PolygonObject> candidate = objects;
	PolygonObject& source_object = candidate[index];
	const int source_object_id = source_object.id;
	const int source_material_id = source_object.material_id;
	source_object.selection.mode = quader_poly::SelectionMode::face;
	quader_poly::ExtractFacesResult result = quader_poly::extract_selected_faces(source_object.document, source_object.selection);
	if (!result.changed) {
		return error_result(result.message.empty() ? std::string("Could not extract selected faces.") : result.message);
	}

	for (PolygonObject& object : candidate) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}

	candidate.erase(
			std::remove_if(candidate.begin(), candidate.end(), [source_object_id](const PolygonObject& object) {
				return object.id == source_object_id && object.document.faces.empty();
			}),
			candidate.end());

	PolygonObject extracted_object;
	extracted_object.id = std::max(1, object_id);
	extracted_object.material_id = source_material_id;
	extracted_object.selected = true;
	extracted_object.document = std::move(result.extracted_document);
	extracted_object.selection = std::move(result.extracted_selection);
	candidate.push_back(std::move(extracted_object));

	objects = std::move(candidate);
	active_object_id = objects.back().id;
	return ok_result(true);
}

CommandResult detach_selected_faces(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::face) {
		return error_result("Detach Faces only works in face mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select faces before detaching.");
	}

	bool changed = false;
	std::string last_message = "Select faces before detaching.";
	for (PolygonObject& object : objects) {
		if (!object.selected || object.selection.faces.empty()) {
			continue;
		}

		object.selection.mode = quader_poly::SelectionMode::face;
		const quader_poly::OperationResult result = quader_poly::detach_selected_faces(object.document, object.selection);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}

		object.selected = !object.selection.empty();
		changed = true;
	}

	if (!changed) {
		return error_result(last_message);
	}

	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult bridge_selected_faces(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id, int steps)
{
	if (mode != SelectionMode::face) {
		return error_result("Bridge Faces only works in face mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select faces before bridging.");
	}

	std::vector<PolygonObject> candidate = objects;
	std::vector<int> selected_face_indices;
	std::size_t selected_face_count = 0;
	for (int object_index = 0; object_index < static_cast<int>(candidate.size()); ++object_index) {
		PolygonObject& object = candidate[object_index];
		if (!object.selected || object.selection.faces.empty()) {
			continue;
		}
		selected_face_indices.push_back(object_index);
		selected_face_count += object.selection.faces.size();
	}

	if (selected_face_indices.empty() || selected_face_count < 2U) {
		return error_result("Select faces before bridging.");
	}

	if (selected_face_indices.size() == 1U) {
		PolygonObject& target_object = candidate[selected_face_indices.front()];
		target_object.selection.mode = quader_poly::SelectionMode::face;
		const quader_poly::OperationResult result = quader_poly::bridge_selected_faces(target_object.document, target_object.selection, steps);
		if (!result.changed) {
			return error_result(result.message.empty() ? std::string("Could not bridge selected faces.") : result.message);
		}

		target_object.selected = !target_object.selection.empty();
		objects = std::move(candidate);
		active_object_id = target_object.id;
		normalize_active_object_id(objects, active_object_id);
		return ok_result(true);
	}

	if (selected_face_indices.size() != 2U) {
		return error_result("Bridge Faces needs selected faces from one mesh or two meshes.");
	}

	const int active_index = selected_object_index(candidate, active_object_id);
	const int first_index = selected_face_indices[0];
	const int second_index = selected_face_indices[1];
	const int survivor_index = (active_index == first_index || active_index == second_index) ? active_index : first_index;
	const int merged_index = survivor_index == first_index ? second_index : first_index;
	const int survivor_id = candidate[survivor_index].id;
	const int merged_id = candidate[merged_index].id;
	const quader_poly::Selection survivor_selection = candidate[survivor_index].selection;
	const quader_poly::Selection source_selection = candidate[merged_index].selection;

	quader_poly::Document bridge_document = candidate[survivor_index].document;
	std::unordered_map<quader_poly::ElementId, quader_poly::ElementId> merged_vertex_ids;
	std::unordered_map<quader_poly::ElementId, quader_poly::ElementId> merged_face_ids;
	append_document_with_remapped_ids(bridge_document, candidate[merged_index].document, merged_vertex_ids, &merged_face_ids);

	quader_poly::Selection bridge_selection;
	bridge_selection.mode = quader_poly::SelectionMode::face;
	bridge_selection.faces = survivor_selection.faces;
	const std::vector<quader_poly::ElementId> remapped_source_faces = remap_face_ids(source_selection.faces, merged_face_ids);
	if (remapped_source_faces.size() != source_selection.faces.size()) {
		return error_result("Bridge Faces could not merge the selected mesh face ids.");
	}
	bridge_selection.faces.insert(bridge_selection.faces.end(), remapped_source_faces.begin(), remapped_source_faces.end());
	if (survivor_selection.has_active && survivor_selection.active_kind == quader_poly::ElementKind::face) {
		bridge_selection.has_active = true;
		bridge_selection.active_kind = quader_poly::ElementKind::face;
		bridge_selection.active_face = survivor_selection.active_face;
	} else if (source_selection.has_active && source_selection.active_kind == quader_poly::ElementKind::face) {
		const auto active = merged_face_ids.find(source_selection.active_face);
		if (active != merged_face_ids.end()) {
			bridge_selection.has_active = true;
			bridge_selection.active_kind = quader_poly::ElementKind::face;
			bridge_selection.active_face = active->second;
		}
	}
	if (!bridge_selection.has_active) {
		activate_component_selection(bridge_selection);
	}

	const quader_poly::OperationResult result = quader_poly::bridge_selected_faces(bridge_document, bridge_selection, steps);
	if (!result.changed) {
		return error_result(result.message.empty() ? std::string("Could not bridge selected faces.") : result.message);
	}

	for (PolygonObject& object : candidate) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}
	PolygonObject* survivor_object = nullptr;
	for (PolygonObject& object : candidate) {
		if (object.id == survivor_id) {
			survivor_object = &object;
			break;
		}
	}
	if (survivor_object == nullptr) {
		return error_result("Bridge Faces could not find the merged survivor mesh.");
	}

	survivor_object->document = std::move(bridge_document);
	survivor_object->selection = std::move(bridge_selection);
	survivor_object->selected = !survivor_object->selection.empty();
	candidate.erase(
			std::remove_if(candidate.begin(), candidate.end(), [merged_id](const PolygonObject& object) { return object.id == merged_id; }),
			candidate.end());
	objects = std::move(candidate);
	active_object_id = survivor_id;
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

bool offset_document_along_face_normals(quader_poly::Document& document, float distance, std::string& message)
{
	constexpr float normal_epsilon_squared = 0.00000001F;
	if (!std::isfinite(distance) || std::abs(distance) <= 0.000001F) {
		message = "Create Outer Hull needs a non-zero finite distance.";
		return false;
	}

	std::map<quader_poly::ElementId, quader::QVec3> normal_sums;
	for (const quader_poly::Face& face : document.faces) {
		const quader::QVec3 normal = quader_poly::face_normal(document, face);
		if (quader::length_squared(normal) <= normal_epsilon_squared) {
			continue;
		}
		for (const quader_poly::ElementId vertex_id : face.vertices) {
			normal_sums[vertex_id] += normal;
		}
	}
	if (normal_sums.empty()) {
		message = "Create Outer Hull needs mesh faces with valid normals.";
		return false;
	}

	bool moved = false;
	for (quader_poly::Vertex& vertex : document.vertices) {
		const auto normal_sum = normal_sums.find(vertex.id);
		if (normal_sum == normal_sums.end()) {
			continue;
		}

		const quader::QVec3 normal = quader::normalize_or_zero(normal_sum->second);
		if (quader::length_squared(normal) <= normal_epsilon_squared) {
			continue;
		}

		vertex.position += normal * -distance;
		moved = true;
	}
	if (!moved) {
		message = "Create Outer Hull could not offset any mesh vertices.";
		return false;
	}
	if (quader_poly::compile_document(document).indices.empty()) {
		message = "Create Outer Hull would create invalid mesh geometry.";
		return false;
	}

	return true;
}

CommandResult create_outer_hull_from_selected_objects(
		std::vector<PolygonObject>& objects,
		int& next_object_id,
		int& active_object_id,
		float distance)
{
	if (!has_selection(objects, SelectionMode::object, active_object_id)) {
		return error_result("Select one or more meshes before creating an outer hull.");
	}

	std::vector<PolygonObject> hulls;
	hulls.reserve(objects.size());
	std::string last_message = "Select one or more meshes before creating an outer hull.";
	int local_next_id = next_polygon_object_id(objects, next_object_id);
	for (const PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}

		PolygonObject hull = object;
		hull.id = local_next_id++;
		hull.selected = true;
		hull.selection = quader_poly::Selection();
		if (!offset_document_along_face_normals(hull.document, distance, last_message)) {
			continue;
		}

		hulls.push_back(std::move(hull));
	}

	if (hulls.empty()) {
		return error_result(last_message);
	}

	for (PolygonObject& object : objects) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}

	std::vector<int> hull_ids;
	hull_ids.reserve(hulls.size());
	for (PolygonObject& hull : hulls) {
		hull_ids.push_back(hull.id);
		objects.push_back(std::move(hull));
	}

	next_object_id = local_next_id;
	active_object_id = hull_ids.empty() ? -1 : hull_ids.back();
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult thicken_selected_faces(
		std::vector<PolygonObject>& objects,
		SelectionMode mode,
		int& next_object_id,
		int& active_object_id,
		float thickness,
		bool from_center)
{
	if (mode != SelectionMode::face && mode != SelectionMode::object) {
		return error_result("Thicken Faces works in face mode. Create Outer Hull works in object mode.");
	}
	if (mode == SelectionMode::object) {
		(void)from_center;
		return create_outer_hull_from_selected_objects(objects, next_object_id, active_object_id, thickness);
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select open faces before thickening.");
	}

	bool changed = false;
	std::string last_message = "Select open faces before thickening.";
	for (PolygonObject& object : objects) {
		if (!object.selected || object.selection.faces.empty()) {
			continue;
		}

		object.selection.mode = quader_poly::SelectionMode::face;
		const quader_poly::OperationResult result = quader_poly::thicken_selected_faces(object.document, object.selection, thickness, from_center);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}

		object.selected = !object.selection.empty();
		changed = true;
	}

	if (!changed) {
		return error_result(last_message);
	}

	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult slice_selected_quads(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id, int x_slices, int y_slices)
{
	if (mode != SelectionMode::face) {
		return error_result("Slice Quad only works in face mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select quad faces before slicing.");
	}

	bool changed = false;
	std::string last_message = "Select quad faces before slicing.";
	for (PolygonObject& object : objects) {
		if (!object.selected || object.selection.faces.empty()) {
			continue;
		}

		object.selection.mode = quader_poly::SelectionMode::face;
		const quader_poly::OperationResult result = quader_poly::slice_selected_quads(object.document, object.selection, x_slices, y_slices);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}

		object.selected = !object.selection.empty();
		changed = true;
	}

	if (!changed) {
		return error_result(last_message);
	}

	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult combine_selected_faces(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::face) {
		return error_result("Combine Faces only works in face mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select adjacent faces before combining.");
	}

	std::vector<PolygonObject> candidate = objects;
	PolygonObject* target_object = nullptr;
	for (PolygonObject& object : candidate) {
		if (!object.selected || object.selection.faces.empty()) {
			continue;
		}
		if (target_object != nullptr) {
			return error_result("Combine Faces needs selected faces from one mesh.");
		}
		target_object = &object;
	}
	if (target_object == nullptr) {
		return error_result("Select adjacent faces before combining.");
	}

	target_object->selection.mode = quader_poly::SelectionMode::face;
	const quader_poly::OperationResult result = quader_poly::combine_selected_faces(target_object->document, target_object->selection);
	if (!result.changed) {
		return error_result(result.message.empty() ? std::string("Could not combine selected faces.") : result.message);
	}

	target_object->selected = !target_object->selection.empty();
	objects = std::move(candidate);
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult combine_selected_meshes(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::object) {
		return error_result("Combine Mesh only works in object mode.");
	}

	const std::vector<int> selected_indices = selected_object_indices(objects);
	if (selected_indices.size() < 2U) {
		return error_result("Select at least two meshes to combine.");
	}

	const int active_index = selected_object_index(objects, active_object_id);
	const int survivor_index =
		std::find(selected_indices.begin(), selected_indices.end(), active_index) != selected_indices.end() ?
			active_index :
			selected_indices.front();
	if (survivor_index < 0 || survivor_index >= static_cast<int>(objects.size())) {
		return error_result("Combine Mesh could not find a survivor mesh.");
	}
	const int survivor_id = objects[survivor_index].id;

	std::vector<PolygonObject> candidate = objects;
	PolygonObject* survivor_object = nullptr;
	for (PolygonObject& object : candidate) {
		if (object.id == survivor_id) {
			survivor_object = &object;
			break;
		}
	}
	if (survivor_object == nullptr) {
		return error_result("Combine Mesh could not find a survivor mesh.");
	}

	for (const int source_index : selected_indices) {
		if (source_index == survivor_index) {
			continue;
		}
		if (source_index < 0 || source_index >= static_cast<int>(objects.size())) {
			continue;
		}

		std::unordered_map<quader_poly::ElementId, quader_poly::ElementId> merged_vertex_ids;
		append_document_with_remapped_ids(survivor_object->document, objects[source_index].document, merged_vertex_ids);
	}

	for (PolygonObject& object : candidate) {
		object.selected = object.id == survivor_id;
		object.selection = quader_poly::Selection();
	}
	std::erase_if(candidate, [&selected_indices, &objects, survivor_index](const PolygonObject& object) {
		for (const int selected_index : selected_indices) {
			if (selected_index != survivor_index &&
				selected_index >= 0 &&
				selected_index < static_cast<int>(objects.size()) &&
				objects[selected_index].id == object.id) {
				return true;
			}
		}
		return false;
	});

	objects = std::move(candidate);
	active_object_id = survivor_id;
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult collapse_selected_faces(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::face) {
		return error_result("Collapse Faces only works in face mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select faces before collapsing.");
	}

	bool changed = false;
	std::string last_message = "Select faces before collapsing.";
	for (PolygonObject& object : objects) {
		if (!object.selected || object.selection.faces.empty()) {
			continue;
		}
		object.selection.mode = quader_poly::SelectionMode::face;
		const quader_poly::OperationResult result = quader_poly::collapse_selected_faces(object.document, object.selection);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}
		object.selected = !object.selection.empty();
		changed = true;
	}
	if (!changed) {
		return error_result(last_message);
	}
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult radial_align_selected(std::vector<PolygonObject>& objects, SelectionMode mode, int& active_object_id)
{
	if (mode != SelectionMode::vertex && mode != SelectionMode::edge && mode != SelectionMode::face) {
		return error_result("Radial Align works in vertex, edge, or face mode.");
	}
	if (!has_selection(objects, mode, active_object_id)) {
		return error_result("Select vertices, edges, or faces before radial align.");
	}

	bool changed = false;
	std::string last_message = "Select vertices, edges, or faces before radial align.";
	for (PolygonObject& object : objects) {
		if (!object.selected || object.selection.empty()) {
			continue;
		}
		object.selection.mode = component_selection_mode(mode);
		const quader_poly::OperationResult result = quader_poly::radial_align_selection(object.document, object.selection);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}
		object.selected = !object.selection.empty();
		changed = true;
	}
	if (!changed) {
		return error_result(last_message);
	}
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult extrude_selected_elements(std::vector<PolygonObject>& objects, SelectionMode mode, quader::QVec3 offset, float closed_edge_ledge_size)
{
	if (mode != SelectionMode::face && mode != SelectionMode::edge) {
		return error_result("Extrude needs face or edge selection mode.");
	}

	bool changed = false;
	std::string last_message = mode == SelectionMode::face ? "Select a polygon face first." : "Select a polygon edge first.";
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}

		object.selection.mode = component_selection_mode(mode);
		const quader_poly::OperationResult result = quader_poly::extrude_selected_elements(object.document, object.selection, offset, closed_edge_ledge_size);
		if (result.changed) {
			quader_poly::clear_face_uvs(object.document);
			object.selected = !object.selection.empty();
			changed = true;
		} else if (!result.message.empty()) {
			last_message = result.message;
		}
	}

	if (!changed) {
		return error_result(last_message);
	}
	return ok_result(true);
}

CommandResult inset_selected_elements(
		std::vector<PolygonObject>& objects,
		SelectionMode mode,
		const quader_poly::Transform3& transform,
		quader::QVec3 pivot)
{
	if (mode != SelectionMode::face && mode != SelectionMode::edge) {
		return error_result("Inset needs face or edge selection mode.");
	}

	bool changed = false;
	std::string last_message = mode == SelectionMode::face ? "Select a polygon face first." : "Select a polygon edge first.";
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}

		object.selection.mode = component_selection_mode(mode);
		const quader_poly::OperationResult result = quader_poly::inset_selected_elements(object.document, object.selection, transform, pivot);
		if (result.changed) {
			quader_poly::clear_face_uvs(object.document);
			object.selected = !object.selection.empty();
			changed = true;
		} else if (!result.message.empty()) {
			last_message = result.message;
		}
	}

	if (!changed) {
		return error_result(last_message);
	}
	return ok_result(true);
}

CommandResult extrude_selected_faces(std::vector<PolygonObject>& objects, float distance)
{
	return single_selected_face_command(objects, quader_poly::SelectionMode::face, "Select a polygon face first.", [distance](quader_poly::Document& document, quader_poly::Selection& selection) {
		return quader_poly::extrude_selected_faces(document, selection, distance);
	});
}

CommandResult inset_selected_faces(std::vector<PolygonObject>& objects, float amount)
{
	return single_selected_face_command(objects, quader_poly::SelectionMode::face, "Select a polygon face first.", [amount](quader_poly::Document& document, quader_poly::Selection& selection) {
		return quader_poly::inset_selected_faces(document, selection, amount);
	});
}

CommandResult insert_edge_loop(std::vector<PolygonObject>& objects, float factor)
{
	return single_selected_face_command(objects, quader_poly::SelectionMode::edge, "Select a polygon edge first.", [factor](quader_poly::Document& document, quader_poly::Selection& selection) {
		return quader_poly::insert_edge_loop(document, selection, factor);
	});
}

CommandResult insert_edge_loop(std::vector<PolygonObject>& objects, int object_id, quader_poly::Edge edge, float factor, int& active_object_id, SelectionMode& mode)
{
	const int object_index = object_index_by_id(objects, object_id);
	if (object_index < 0) {
		return error_result("Select an edge to insert an edge loop.");
	}

	PolygonObject& object = objects[object_index];
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::edge;
	selection.edges.push_back(quader_poly::make_edge(edge.a, edge.b));
	selection.has_active = true;
	selection.active_kind = quader_poly::ElementKind::edge;
	selection.active_edge = quader_poly::make_edge(edge.a, edge.b);

	quader_poly::OperationResult result = quader_poly::insert_edge_loop(object.document, selection, factor);
	if (!result.changed) {
		return error_result(result.message.empty() ? "Could not insert an edge loop." : result.message);
	}

	for (PolygonObject& candidate : objects) {
		candidate.selected = false;
		candidate.selection = quader_poly::Selection();
	}
	object.selected = true;
	object.selection = std::move(selection);
	active_object_id = object.id;
	mode = SelectionMode::edge;
	return { true, true, {} };
}

CommandResult plane_cut(
		std::vector<PolygonObject>& objects,
		std::span<const int> object_ids,
		const quader_poly::PlaneCutRequest& request,
		int& next_object_id,
		int& active_object_id,
		SelectionMode& mode)
{
	if (object_ids.empty()) {
		return error_result("Cut needs a selected mesh.");
	}

	std::vector<PolygonObject> candidate = objects;
	next_object_id = next_polygon_object_id(candidate, next_object_id);

	for (PolygonObject& object : candidate) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}

	std::vector<int> result_ids;
	std::string last_message = "Could not cut the selected mesh.";
	for (const int object_id : object_ids) {
		const int object_index = object_index_by_id(candidate, object_id);
		if (object_index < 0) {
			return error_result("Cut target mesh was not found.");
		}

		PolygonObject& source = candidate[static_cast<std::size_t>(object_index)];
		const quader_poly::PlaneCutResult cut_result = quader_poly::plane_cut(source.document, request);
		if (!cut_result.changed) {
			return error_result(cut_result.message.empty() ? last_message : cut_result.message);
		}

		if (request.mode == quader_poly::PlaneCutMode::keep_back) {
			source.document = cut_result.back_document;
			source.selection = cut_result.back_selection;
			source.selected = true;
			result_ids.push_back(source.id);
			continue;
		}

		source.document = cut_result.front_document;
		source.selection = cut_result.front_selection;
		source.selected = true;
		result_ids.push_back(source.id);

		if (request.mode == quader_poly::PlaneCutMode::keep_both) {
			PolygonObject back = source;
			back.id = next_object_id++;
			back.document = cut_result.back_document;
			back.selection = cut_result.back_selection;
			back.selected = true;
			result_ids.push_back(back.id);
			candidate.push_back(std::move(back));
		}
	}

	if (result_ids.empty()) {
		return error_result(last_message);
	}

	objects = std::move(candidate);
	mode = SelectionMode::face;
	active_object_id = result_ids.back();
	normalize_active_object_id(objects, active_object_id);
	return ok_result(true);
}

CommandResult mirror_objects_across_plane(
		std::vector<PolygonObject>& objects,
		std::span<const int> object_ids,
		std::span<const quader::QVec3> plane_points,
		int& next_object_id,
		int& active_object_id,
		SelectionMode& mode)
{
	const DrawnPlaneSurface surface = drawn_plane_surface_from_points(plane_points);
	return mirror_objects_across_surface(
		objects,
		object_ids,
		surface,
		next_object_id,
		active_object_id,
		mode);
}

CommandResult mirror_objects_across_world_axis(
		std::vector<PolygonObject>& objects,
		std::span<const int> object_ids,
		TransformAxis axis,
		int& next_object_id,
		int& active_object_id,
		SelectionMode& mode)
{
	return mirror_objects_across_surface(
		objects,
		object_ids,
		mirror_world_axis_surface(axis),
		next_object_id,
		active_object_id,
		mode);
}

CommandResult flip_objects_by_orientation(
		std::vector<PolygonObject>& objects,
		std::span<const int> object_ids,
		SelectionMode& mode,
		ViewportFlipOrientation orientation,
		int& active_object_id)
{
	return flip_objects_by_orientation(
		objects,
		object_ids,
		orientation,
		active_object_id,
		mode);
}

std::vector<PolygonObject> selected_object_copies(const std::vector<PolygonObject>& objects, SelectionMode mode)
{
	std::vector<PolygonObject> copied_objects;
	if (mode != SelectionMode::object) {
		return copied_objects;
	}
	for (const PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		PolygonObject copy = object;
		copy.selected = false;
		copy.selection = quader_poly::Selection();
		copied_objects.push_back(std::move(copy));
	}
	return copied_objects;
}

CommandResult duplicate_selected_objects(std::vector<PolygonObject>& objects, int& next_object_id, SelectionMode& mode, int& active_object_id, quader::QVec3 offset)
{
	std::vector<PolygonObject> copied_objects = selected_object_copies(objects, mode);
	if (copied_objects.empty()) {
		return error_result("Select one or more meshes in object mode before duplicating.");
	}

	for (PolygonObject& object : objects) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}

	next_object_id = next_polygon_object_id(objects, next_object_id);
	std::vector<int> duplicated_ids;
	duplicated_ids.reserve(copied_objects.size());
	for (PolygonObject& copy : copied_objects) {
		copy.id = next_object_id++;
		copy.selected = true;
		copy.selection = quader_poly::Selection();
		translate_document_vertices(copy.document, offset);
		duplicated_ids.push_back(copy.id);
		objects.push_back(std::move(copy));
	}

	mode = SelectionMode::object;
	active_object_id = duplicated_ids.empty() ? -1 : duplicated_ids.back();
	normalize_active_object_id(objects, active_object_id);
	return ok_result(!duplicated_ids.empty());
}

CommandResult paste_objects(
		std::vector<PolygonObject>& objects,
		int& next_object_id,
		SelectionMode& mode,
		int& active_object_id,
		const std::vector<PolygonObject>& copied_objects,
		std::optional<quader::QVec3> target_center)
{
	if (copied_objects.empty()) {
		return error_result("Copy one or more meshes before pasting.");
	}

	const Bounds3 copied_bounds = document_bounds(copied_objects);
	const quader::QVec3 copied_center = bounds_center(copied_bounds);
	if (!target_center.has_value()) {
		target_center = selection_center(objects, mode, active_object_id);
	}
	const quader::QVec3 paste_delta = target_center.has_value() ?
		(target_center.value() - copied_center) :
		quader::QVec3 { 1.0F, 0.0F, 1.0F };

	for (PolygonObject& object : objects) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}

	next_object_id = next_polygon_object_id(objects, next_object_id);
	std::vector<int> pasted_ids;
	pasted_ids.reserve(copied_objects.size());
	for (const PolygonObject& source : copied_objects) {
		PolygonObject pasted = source;
		pasted.id = next_object_id++;
		pasted.selected = true;
		pasted.selection = quader_poly::Selection();
		translate_document_vertices(pasted.document, paste_delta);
		pasted_ids.push_back(pasted.id);
		objects.push_back(std::move(pasted));
	}

	mode = SelectionMode::object;
	active_object_id = pasted_ids.empty() ? -1 : pasted_ids.back();
	normalize_active_object_id(objects, active_object_id);
	return ok_result(!pasted_ids.empty());
}

SessionState default_scene_state()
{
	const float footprint_world_size = default_scene_footprint_map_units / default_map_units_per_world_unit;
	const float height_world_size = default_scene_height_map_units / default_map_units_per_world_unit;
	quader_poly::CubePrimitiveSpec spec;
	spec.width = footprint_world_size;
	spec.height = height_world_size;
	spec.depth = footprint_world_size;

	PolygonObject object;
	object.id = 1;
	object.material_id = 0;
	object.selected = false;
	object.document = quader_poly::make_cube(spec);
	translate_document_vertices(object.document, { 0.0F, -height_world_size * 0.5F, 0.0F });

	SessionState state;
	state.objects.push_back(std::move(object));
	state.next_object_id = 2;
	state.active_object_id = -1;
	state.selection_mode = SelectionMode::object;
	state.dirty = true;
	state.content_revision = 1;
	state.selection_revision = 0;
	return state;
}

} // namespace quader_editor_core
