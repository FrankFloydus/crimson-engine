////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/runtime/session.hpp>

#include "diagnostics/profile.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader_editor_core {

namespace {

/**
 * Stores the Rectangle Face Key data contract used by the modeling runtime domain.
 */
struct RectangleFaceKey {
	int object_id = -1;
	int face_index = -1;

	bool operator==(const RectangleFaceKey& other) const
	{
		return object_id == other.object_id && face_index == other.face_index;
	}
};

/**
 * Represents a Rectangle Face Key Hash value used by the modeling runtime domain.
 */
struct RectangleFaceKeyHash {
	std::size_t operator()(const RectangleFaceKey& key) const
	{
		std::size_t hash = std::hash<int>()(key.object_id);
		hash ^= std::hash<int>()(key.face_index) + 0x9e3779b9U + (hash << 6) + (hash >> 2);
		return hash;
	}
};

/**
 * Stores the Rectangle Edge Key data contract used by the modeling runtime domain.
 */
struct RectangleEdgeKey {
	int a_index = -1;
	int b_index = -1;

	bool operator==(const RectangleEdgeKey& other) const
	{
		return a_index == other.a_index && b_index == other.b_index;
	}
};

/**
 * Represents a Rectangle Edge Key Hash value used by the modeling runtime domain.
 */
struct RectangleEdgeKeyHash {
	std::size_t operator()(const RectangleEdgeKey& key) const
	{
		std::size_t hash = std::hash<int>()(key.a_index);
		hash ^= std::hash<int>()(key.b_index) + 0x9e3779b9U + (hash << 6) + (hash >> 2);
		return hash;
	}
};

bool rectangle_preserves_existing(SelectionEditOperation operation)
{
  return operation == SelectionEditOperation::Add ||
         operation == SelectionEditOperation::Remove ||
         operation == SelectionEditOperation::Toggle;
}

bool rectangle_contains_id(const std::vector<int>& ids, int id)
{
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void rectangle_append_unique_id(std::vector<int>& ids, int id)
{
	if (id >= 0 && !rectangle_contains_id(ids, id)) {
		ids.push_back(id);
	}
}

bool rectangle_remove_id(std::vector<int>& ids, int id)
{
	const auto found = std::find(ids.begin(), ids.end(), id);
	if (found == ids.end()) {
		return false;
	}
	ids.erase(found);
	return true;
}

bool rectangle_face_is_valid(RectangleSelectionFace face)
{
	return face.object_id >= 0 && face.face_index >= 0;
}

bool rectangle_edge_is_valid(RectangleSelectionEdge edge)
{
	return edge.object_id >= 0 && edge.a_index >= 0 && edge.b_index >= 0;
}

bool rectangle_vertex_is_valid(RectangleSelectionVertex vertex)
{
	return vertex.object_id >= 0 && vertex.vertex_index >= 0;
}

RectangleFaceKey rectangle_face_key(RectangleSelectionFace face)
{
	return { face.object_id, face.face_index };
}

RectangleEdgeKey rectangle_edge_key(RectangleSelectionEdge edge)
{
	return { std::min(edge.a_index, edge.b_index), std::max(edge.a_index, edge.b_index) };
}

void rectangle_insert_face(std::unordered_set<RectangleFaceKey, RectangleFaceKeyHash>& faces, RectangleSelectionFace face)
{
	if (rectangle_face_is_valid(face)) {
		faces.insert(rectangle_face_key(face));
	}
}

void rectangle_insert_edge(std::unordered_map<int, std::unordered_set<RectangleEdgeKey, RectangleEdgeKeyHash>>& edges_by_object, RectangleSelectionEdge edge)
{
	if (rectangle_edge_is_valid(edge)) {
		edges_by_object[edge.object_id].insert(rectangle_edge_key(edge));
	}
}

void rectangle_insert_vertex(std::unordered_map<int, std::unordered_set<int>>& vertices_by_object, RectangleSelectionVertex vertex)
{
	if (rectangle_vertex_is_valid(vertex)) {
		vertices_by_object[vertex.object_id].insert(vertex.vertex_index);
	}
}

const RectangleSelectionUniverse &rectangle_request_universe(
    const RectangleSelectionRequest &request) {
  return request.universe_view != nullptr ? *request.universe_view
                                          : request.universe;
}

RectangleSelectionResult rectangle_select_objects(const RectangleSelectionRequest& request)
{
	QDR_PROFILE_SCOPE("Box selection policy object mode");
	RectangleSelectionResult result;
        result.selection.mode = SelectionMode::Object;
        result.selection.selected_object_ids = rectangle_preserves_existing(request.operation) ? request.current_selection.selected_object_ids : std::vector<int>();
	result.selection.active_object_id = rectangle_preserves_existing(request.operation) ? request.current_selection.active_object_id : -1;

	for (const int object_id : request.hits.object_ids) {
		if (object_id < 0) {
			continue;
		}
                if (request.operation == SelectionEditOperation::Remove) {
                  if (rectangle_remove_id(result.selection.selected_object_ids,
                                          object_id) &&
                      result.selection.active_object_id == object_id) {
                    result.selection.active_object_id = -1;
                  }
                } else if (request.operation ==
                           SelectionEditOperation::Toggle) {
                  if (rectangle_remove_id(result.selection.selected_object_ids,
                                          object_id)) {
                    if (result.selection.active_object_id == object_id) {
                      result.selection.active_object_id = -1;
                    }
                  } else {
                    rectangle_append_unique_id(
                        result.selection.selected_object_ids, object_id);
                    result.selection.active_object_id = object_id;
                  }
                } else {
                  rectangle_append_unique_id(
                      result.selection.selected_object_ids, object_id);
                  result.selection.active_object_id = object_id;
                }
        }
	if (!result.selection.selected_object_ids.empty() && !rectangle_contains_id(result.selection.selected_object_ids, result.selection.active_object_id)) {
		result.selection.active_object_id = result.selection.selected_object_ids.back();
	}
	if (result.selection.selected_object_ids.empty()) {
		result.selection.active_object_id = -1;
	}
	return result;
}

RectangleSelectionResult rectangle_select_faces(const RectangleSelectionRequest& request)
{
	QDR_PROFILE_SCOPE("Box selection policy face mode");
	const RectangleSelectionUniverse& universe = rectangle_request_universe(request);
	RectangleSelectionResult result;
        result.selection.mode = SelectionMode::Face;
        std::unordered_set<RectangleFaceKey, RectangleFaceKeyHash> selected_faces;
	if (rectangle_preserves_existing(request.operation)) {
		selected_faces.reserve(request.current_selection.selected_faces.size() + request.hits.faces.size());
		for (const RectangleSelectionFace face : request.current_selection.selected_faces) {
			rectangle_insert_face(selected_faces, face);
		}
	}
	for (const RectangleSelectionFace face : request.hits.faces) {
		if (!rectangle_face_is_valid(face)) {
			continue;
		}
		const RectangleFaceKey key = rectangle_face_key(face);
                if (request.operation == SelectionEditOperation::Remove) {
                  selected_faces.erase(key);
                } else if (request.operation ==
                           SelectionEditOperation::Toggle) {
                  if (selected_faces.find(key) != selected_faces.end()) {
                    selected_faces.erase(key);
                  } else {
                    selected_faces.insert(key);
                  }
                } else {
                  selected_faces.insert(key);
                }
        }
	result.selection.selected_faces.reserve(selected_faces.size());
	for (const RectangleSelectionFace face : universe.faces) {
		if (rectangle_face_is_valid(face) && selected_faces.find(rectangle_face_key(face)) != selected_faces.end()) {
			result.selection.selected_faces.push_back(face);
		}
	}
	if (!result.selection.selected_faces.empty()) {
		const RectangleSelectionFace active = result.selection.selected_faces.back();
		result.selection.active_object_id = active.object_id;
		result.selection.active_face_index = active.face_index;
	}
	return result;
}

RectangleSelectionResult rectangle_select_edges(const RectangleSelectionRequest& request)
{
	QDR_PROFILE_SCOPE("Box selection policy edge mode");
	const RectangleSelectionUniverse& universe = rectangle_request_universe(request);
	RectangleSelectionResult result;
        result.selection.mode = SelectionMode::Edge;
        std::unordered_map<int, std::unordered_set<RectangleEdgeKey, RectangleEdgeKeyHash>> selected_edges_by_object;
	if (rectangle_preserves_existing(request.operation)) {
		selected_edges_by_object.reserve(request.current_selection.selected_edges.size() + request.current_selection.selected_edge_full_object_ids.size());
		for (const RectangleSelectionEdge edge : request.current_selection.selected_edges) {
			rectangle_insert_edge(selected_edges_by_object, edge);
		}
		for (const int full_object_id : request.current_selection.selected_edge_full_object_ids) {
			if (full_object_id < 0) {
				continue;
			}
			for (const RectangleSelectionEdge edge : universe.edges) {
				if (edge.object_id == full_object_id) {
					rectangle_insert_edge(selected_edges_by_object, edge);
				}
			}
		}
	}
	for (const RectangleSelectionEdge edge : request.hits.edges) {
		if (!rectangle_edge_is_valid(edge)) {
			continue;
		}
		std::unordered_set<RectangleEdgeKey, RectangleEdgeKeyHash>& edge_keys = selected_edges_by_object[edge.object_id];
		const RectangleEdgeKey key = rectangle_edge_key(edge);
                if (request.operation == SelectionEditOperation::Remove) {
                  edge_keys.erase(key);
                } else if (request.operation ==
                           SelectionEditOperation::Toggle) {
                  if (edge_keys.find(key) != edge_keys.end()) {
                    edge_keys.erase(key);
                  } else {
                    edge_keys.insert(key);
                  }
                } else {
                  edge_keys.insert(key);
                }
        }

	std::unordered_map<int, int> edge_count_by_object;
	edge_count_by_object.reserve(universe.object_ids.size());
	for (const RectangleSelectionEdge edge : universe.edges) {
		if (rectangle_edge_is_valid(edge)) {
			edge_count_by_object[edge.object_id]++;
		}
	}
	std::unordered_set<int> full_object_ids;
	full_object_ids.reserve(selected_edges_by_object.size());
	for (const auto& entry : selected_edges_by_object) {
		const auto found_count = edge_count_by_object.find(entry.first);
		if (found_count != edge_count_by_object.end() && found_count->second > 0 && entry.second.size() >= static_cast<std::size_t>(found_count->second)) {
			full_object_ids.insert(entry.first);
		}
	}

	for (const int object_id : universe.object_ids) {
		if (full_object_ids.find(object_id) != full_object_ids.end()) {
			result.selection.selected_edge_full_object_ids.push_back(object_id);
		}
	}
	for (const RectangleSelectionEdge edge : universe.edges) {
		if (!rectangle_edge_is_valid(edge) || full_object_ids.find(edge.object_id) != full_object_ids.end()) {
			continue;
		}
		const auto found_edges = selected_edges_by_object.find(edge.object_id);
		if (found_edges != selected_edges_by_object.end() && found_edges->second.find(rectangle_edge_key(edge)) != found_edges->second.end()) {
			result.selection.selected_edges.push_back(edge);
		}
	}
	if (!result.selection.selected_edges.empty()) {
		result.selection.active_object_id = result.selection.selected_edges.back().object_id;
	} else if (!result.selection.selected_edge_full_object_ids.empty()) {
		result.selection.active_object_id = result.selection.selected_edge_full_object_ids.back();
	}
	return result;
}

RectangleSelectionResult rectangle_select_vertices(const RectangleSelectionRequest& request)
{
	QDR_PROFILE_SCOPE("Box selection policy vertex mode");
	const RectangleSelectionUniverse& universe = rectangle_request_universe(request);
	RectangleSelectionResult result;
        result.selection.mode = SelectionMode::Vertex;
        std::unordered_map<int, std::unordered_set<int>> selected_vertices_by_object;
	if (rectangle_preserves_existing(request.operation)) {
		selected_vertices_by_object.reserve(request.current_selection.selected_vertices.size() + request.current_selection.selected_vertex_full_object_ids.size());
		for (const RectangleSelectionVertex vertex : request.current_selection.selected_vertices) {
			rectangle_insert_vertex(selected_vertices_by_object, vertex);
		}
		for (const int full_object_id : request.current_selection.selected_vertex_full_object_ids) {
			if (full_object_id < 0) {
				continue;
			}
			for (const RectangleSelectionVertex vertex : universe.vertices) {
				if (vertex.object_id == full_object_id) {
					rectangle_insert_vertex(selected_vertices_by_object, vertex);
				}
			}
		}
	}
	for (const RectangleSelectionVertex vertex : request.hits.vertices) {
		if (!rectangle_vertex_is_valid(vertex)) {
			continue;
		}
		std::unordered_set<int>& vertex_indices = selected_vertices_by_object[vertex.object_id];
                if (request.operation == SelectionEditOperation::Remove) {
                  vertex_indices.erase(vertex.vertex_index);
                } else if (request.operation ==
                           SelectionEditOperation::Toggle) {
                  if (vertex_indices.find(vertex.vertex_index) !=
                      vertex_indices.end()) {
                    vertex_indices.erase(vertex.vertex_index);
                  } else {
                    vertex_indices.insert(vertex.vertex_index);
                  }
                } else {
                  vertex_indices.insert(vertex.vertex_index);
                }
        }

	std::unordered_map<int, int> vertex_count_by_object;
	vertex_count_by_object.reserve(universe.object_ids.size());
	for (const RectangleSelectionVertex vertex : universe.vertices) {
		if (rectangle_vertex_is_valid(vertex)) {
			vertex_count_by_object[vertex.object_id]++;
		}
	}
	std::unordered_set<int> full_object_ids;
	full_object_ids.reserve(selected_vertices_by_object.size());
	for (const auto& entry : selected_vertices_by_object) {
		const auto found_count = vertex_count_by_object.find(entry.first);
		if (found_count != vertex_count_by_object.end() && found_count->second > 0 && entry.second.size() >= static_cast<std::size_t>(found_count->second)) {
			full_object_ids.insert(entry.first);
		}
	}

	for (const int object_id : universe.object_ids) {
		if (full_object_ids.find(object_id) != full_object_ids.end()) {
			result.selection.selected_vertex_full_object_ids.push_back(object_id);
		}
	}
	for (const RectangleSelectionVertex vertex : universe.vertices) {
		if (!rectangle_vertex_is_valid(vertex) || full_object_ids.find(vertex.object_id) != full_object_ids.end()) {
			continue;
		}
		const auto found_vertices = selected_vertices_by_object.find(vertex.object_id);
		if (found_vertices != selected_vertices_by_object.end() && found_vertices->second.find(vertex.vertex_index) != found_vertices->second.end()) {
			result.selection.selected_vertices.push_back(vertex);
		}
	}
	if (!result.selection.selected_vertices.empty()) {
		result.selection.active_object_id = result.selection.selected_vertices.back().object_id;
	} else if (!result.selection.selected_vertex_full_object_ids.empty()) {
		result.selection.active_object_id = result.selection.selected_vertex_full_object_ids.back();
	}
	return result;
}

bool rectangle_ids_cover_universe(const std::vector<int>& selected_ids, const std::vector<int>& universe_ids)
{
	if (selected_ids.empty() || selected_ids.size() < universe_ids.size() || universe_ids.empty()) {
		return false;
	}
	std::unordered_set<int> selected_set;
	selected_set.reserve(selected_ids.size());
	for (const int id : selected_ids) {
		if (id >= 0) {
			selected_set.insert(id);
		}
	}
	if (selected_set.size() < universe_ids.size()) {
		return false;
	}
	for (const int id : universe_ids) {
		if (id < 0 || selected_set.find(id) == selected_set.end()) {
			return false;
		}
	}
	return true;
}

std::unordered_set<RectangleFaceKey, RectangleFaceKeyHash> rectangle_face_set_from_selection(const RectangleSelectionState& selection)
{
	std::unordered_set<RectangleFaceKey, RectangleFaceKeyHash> faces;
	faces.reserve(selection.selected_faces.size());
	for (const RectangleSelectionFace face : selection.selected_faces) {
		rectangle_insert_face(faces, face);
	}
	return faces;
}

std::unordered_map<int, std::unordered_set<RectangleEdgeKey, RectangleEdgeKeyHash>> rectangle_edge_set_from_selection(
		const RectangleSelectionState& selection,
		const RectangleSelectionUniverse& universe)
{
	std::unordered_map<int, std::unordered_set<RectangleEdgeKey, RectangleEdgeKeyHash>> edges_by_object;
	edges_by_object.reserve(selection.selected_edges.size() + selection.selected_edge_full_object_ids.size());
	for (const RectangleSelectionEdge edge : selection.selected_edges) {
		rectangle_insert_edge(edges_by_object, edge);
	}
	for (const int full_object_id : selection.selected_edge_full_object_ids) {
		if (full_object_id < 0) {
			continue;
		}
		for (const RectangleSelectionEdge edge : universe.edges) {
			if (edge.object_id == full_object_id) {
				rectangle_insert_edge(edges_by_object, edge);
			}
		}
	}
	return edges_by_object;
}

std::unordered_map<int, std::unordered_set<int>> rectangle_vertex_set_from_selection(
		const RectangleSelectionState& selection,
		const RectangleSelectionUniverse& universe)
{
	std::unordered_map<int, std::unordered_set<int>> vertices_by_object;
	vertices_by_object.reserve(selection.selected_vertices.size() + selection.selected_vertex_full_object_ids.size());
	for (const RectangleSelectionVertex vertex : selection.selected_vertices) {
		rectangle_insert_vertex(vertices_by_object, vertex);
	}
	for (const int full_object_id : selection.selected_vertex_full_object_ids) {
		if (full_object_id < 0) {
			continue;
		}
		for (const RectangleSelectionVertex vertex : universe.vertices) {
			if (vertex.object_id == full_object_id) {
				rectangle_insert_vertex(vertices_by_object, vertex);
			}
		}
	}
	return vertices_by_object;
}

bool rectangle_faces_cover_universe(const RectangleSelectionState& selection, const RectangleSelectionUniverse& universe)
{
	if (universe.faces.empty()) {
		return false;
	}
	const std::unordered_set<RectangleFaceKey, RectangleFaceKeyHash> selected_faces = rectangle_face_set_from_selection(selection);
	if (selected_faces.size() < universe.faces.size()) {
		return false;
	}
	for (const RectangleSelectionFace face : universe.faces) {
		if (!rectangle_face_is_valid(face) || selected_faces.find(rectangle_face_key(face)) == selected_faces.end()) {
			return false;
		}
	}
	return true;
}

bool rectangle_edges_cover_universe(const RectangleSelectionState& selection, const RectangleSelectionUniverse& universe)
{
	if (universe.edges.empty()) {
		return false;
	}
	const auto selected_edges_by_object = rectangle_edge_set_from_selection(selection, universe);
	for (const RectangleSelectionEdge edge : universe.edges) {
		if (!rectangle_edge_is_valid(edge)) {
			return false;
		}
		const auto found_edges = selected_edges_by_object.find(edge.object_id);
		if (found_edges == selected_edges_by_object.end() || found_edges->second.find(rectangle_edge_key(edge)) == found_edges->second.end()) {
			return false;
		}
	}
	return true;
}

bool rectangle_vertices_cover_universe(const RectangleSelectionState& selection, const RectangleSelectionUniverse& universe)
{
	if (universe.vertices.empty()) {
		return false;
	}
	const auto selected_vertices_by_object = rectangle_vertex_set_from_selection(selection, universe);
	for (const RectangleSelectionVertex vertex : universe.vertices) {
		if (!rectangle_vertex_is_valid(vertex)) {
			return false;
		}
		const auto found_vertices = selected_vertices_by_object.find(vertex.object_id);
		if (found_vertices == selected_vertices_by_object.end() || found_vertices->second.find(vertex.vertex_index) == found_vertices->second.end()) {
			return false;
		}
	}
	return true;
}

RectangleSelectionResult rectangle_result_from_edge_set(
		SelectionMode mode,
		const RectangleSelectionUniverse& universe,
		const std::unordered_map<int, std::unordered_set<RectangleEdgeKey, RectangleEdgeKeyHash>>& selected_edges_by_object,
		bool first_full_object_is_active)
{
	RectangleSelectionResult result;
	result.selection.mode = mode;

	std::unordered_map<int, int> edge_count_by_object;
	edge_count_by_object.reserve(universe.object_ids.size());
	for (const RectangleSelectionEdge edge : universe.edges) {
		if (rectangle_edge_is_valid(edge)) {
			edge_count_by_object[edge.object_id]++;
		}
	}
	std::unordered_set<int> full_object_ids;
	full_object_ids.reserve(selected_edges_by_object.size());
	for (const auto& entry : selected_edges_by_object) {
		const auto found_count = edge_count_by_object.find(entry.first);
		if (found_count != edge_count_by_object.end() && found_count->second > 0 && entry.second.size() >= static_cast<std::size_t>(found_count->second)) {
			full_object_ids.insert(entry.first);
		}
	}

	for (const int object_id : universe.object_ids) {
		if (full_object_ids.find(object_id) != full_object_ids.end()) {
			result.selection.selected_edge_full_object_ids.push_back(object_id);
		}
	}
	for (const RectangleSelectionEdge edge : universe.edges) {
		if (!rectangle_edge_is_valid(edge) || full_object_ids.find(edge.object_id) != full_object_ids.end()) {
			continue;
		}
		const auto found_edges = selected_edges_by_object.find(edge.object_id);
		if (found_edges != selected_edges_by_object.end() && found_edges->second.find(rectangle_edge_key(edge)) != found_edges->second.end()) {
			result.selection.selected_edges.push_back(edge);
		}
	}
	if (!result.selection.selected_edges.empty()) {
		result.selection.active_object_id = result.selection.selected_edges.back().object_id;
	} else if (!result.selection.selected_edge_full_object_ids.empty()) {
		result.selection.active_object_id = first_full_object_is_active ? result.selection.selected_edge_full_object_ids.front() : result.selection.selected_edge_full_object_ids.back();
	}
	return result;
}

RectangleSelectionResult rectangle_result_from_vertex_set(
		SelectionMode mode,
		const RectangleSelectionUniverse& universe,
		const std::unordered_map<int, std::unordered_set<int>>& selected_vertices_by_object,
		bool first_full_object_is_active)
{
	RectangleSelectionResult result;
	result.selection.mode = mode;

	std::unordered_map<int, int> vertex_count_by_object;
	vertex_count_by_object.reserve(universe.object_ids.size());
	for (const RectangleSelectionVertex vertex : universe.vertices) {
		if (rectangle_vertex_is_valid(vertex)) {
			vertex_count_by_object[vertex.object_id]++;
		}
	}
	std::unordered_set<int> full_object_ids;
	full_object_ids.reserve(selected_vertices_by_object.size());
	for (const auto& entry : selected_vertices_by_object) {
		const auto found_count = vertex_count_by_object.find(entry.first);
		if (found_count != vertex_count_by_object.end() && found_count->second > 0 && entry.second.size() >= static_cast<std::size_t>(found_count->second)) {
			full_object_ids.insert(entry.first);
		}
	}

	for (const int object_id : universe.object_ids) {
		if (full_object_ids.find(object_id) != full_object_ids.end()) {
			result.selection.selected_vertex_full_object_ids.push_back(object_id);
		}
	}
	for (const RectangleSelectionVertex vertex : universe.vertices) {
		if (!rectangle_vertex_is_valid(vertex) || full_object_ids.find(vertex.object_id) != full_object_ids.end()) {
			continue;
		}
		const auto found_vertices = selected_vertices_by_object.find(vertex.object_id);
		if (found_vertices != selected_vertices_by_object.end() && found_vertices->second.find(vertex.vertex_index) != found_vertices->second.end()) {
			result.selection.selected_vertices.push_back(vertex);
		}
	}
	if (!result.selection.selected_vertices.empty()) {
		result.selection.active_object_id = result.selection.selected_vertices.back().object_id;
	} else if (!result.selection.selected_vertex_full_object_ids.empty()) {
		result.selection.active_object_id = first_full_object_is_active ? result.selection.selected_vertex_full_object_ids.front() : result.selection.selected_vertex_full_object_ids.back();
	}
	return result;
}

void face_mode_append_unique_face(
		std::vector<RectangleSelectionFace>& selected_faces,
		std::unordered_set<RectangleFaceKey, RectangleFaceKeyHash>& selected_face_keys,
		const RectangleSelectionFace& face)
{
	if (!rectangle_face_is_valid(face)) {
		return;
	}
	const RectangleFaceKey key = rectangle_face_key(face);
	if (selected_face_keys.insert(key).second) {
		selected_faces.push_back(face);
	}
}

void face_mode_append_faces_for_object(
		std::vector<RectangleSelectionFace>& selected_faces,
		std::unordered_set<RectangleFaceKey, RectangleFaceKeyHash>& selected_face_keys,
		const RectangleSelectionUniverse& universe,
		int object_id)
{
	if (object_id < 0) {
		return;
	}
	for (const RectangleSelectionFace& face : universe.faces) {
		if (face.object_id == object_id) {
			face_mode_append_unique_face(selected_faces, selected_face_keys, face);
		}
	}
}

void face_mode_insert_vertex(
		std::unordered_map<int, std::unordered_set<int>>& vertices_by_object,
		std::vector<int>& ordered_object_ids,
		int object_id,
		int vertex_index)
{
	if (object_id < 0 || vertex_index < 0) {
		return;
	}
	const auto inserted = vertices_by_object.emplace(object_id, std::unordered_set<int>());
	if (inserted.second) {
		ordered_object_ids.push_back(object_id);
	}
	inserted.first->second.insert(vertex_index);
}

void face_mode_insert_edge_vertices(
		std::unordered_map<int, std::unordered_set<int>>& vertices_by_object,
		std::vector<int>& ordered_object_ids,
		const RectangleSelectionEdge& edge)
{
	if (!rectangle_edge_is_valid(edge)) {
		return;
	}
	face_mode_insert_vertex(vertices_by_object, ordered_object_ids, edge.object_id, edge.a_index);
	face_mode_insert_vertex(vertices_by_object, ordered_object_ids, edge.object_id, edge.b_index);
}

bool face_mode_face_is_covered_by_vertices(
		const RectangleSelectionFace& face,
		const std::unordered_set<int>& selected_vertices)
{
	if (!rectangle_face_is_valid(face) || face.vertex_indices.empty()) {
		return false;
	}
	for (const int vertex_index : face.vertex_indices) {
		if (selected_vertices.find(vertex_index) == selected_vertices.end()) {
			return false;
		}
	}
	return true;
}

std::unordered_map<int, int> promotion_face_count_by_object(const RectangleSelectionUniverse& universe)
{
	std::unordered_map<int, int> count_by_object;
	count_by_object.reserve(universe.object_ids.size());
	for (const RectangleSelectionFace& face : universe.faces) {
		if (rectangle_face_is_valid(face)) {
			count_by_object[face.object_id]++;
		}
	}
	return count_by_object;
}

std::unordered_map<int, int> promotion_edge_count_by_object(const RectangleSelectionUniverse& universe)
{
	std::unordered_map<int, int> count_by_object;
	count_by_object.reserve(universe.object_ids.size());
	for (const RectangleSelectionEdge& edge : universe.edges) {
		if (rectangle_edge_is_valid(edge)) {
			count_by_object[edge.object_id]++;
		}
	}
	return count_by_object;
}

std::unordered_map<int, int> promotion_vertex_count_by_object(const RectangleSelectionUniverse& universe)
{
	std::unordered_map<int, int> count_by_object;
	count_by_object.reserve(universe.object_ids.size());
	for (const RectangleSelectionVertex& vertex : universe.vertices) {
		if (rectangle_vertex_is_valid(vertex)) {
			count_by_object[vertex.object_id]++;
		}
	}
	return count_by_object;
}

std::unordered_set<int> promotion_universe_object_set(const RectangleSelectionUniverse& universe)
{
	std::unordered_set<int> object_ids;
	object_ids.reserve(universe.object_ids.size());
	for (const int object_id : universe.object_ids) {
		if (object_id >= 0) {
			object_ids.insert(object_id);
		}
	}
	return object_ids;
}

std::unordered_set<int> promotion_id_set_from_vector(const std::vector<int>& ids)
{
	std::unordered_set<int> id_set;
	id_set.reserve(ids.size());
	for (const int id : ids) {
		if (id >= 0) {
			id_set.insert(id);
		}
	}
	return id_set;
}

RectangleSelectionState cleared_selection_state(const RectangleSelectionState& selection)
{
	RectangleSelectionState cleared;
	cleared.mode = selection.mode;
	return cleared;
}

void remove_faces_for_object(std::vector<RectangleSelectionFace>& faces, int object_id)
{
	faces.erase(
			std::remove_if(faces.begin(), faces.end(), [object_id](RectangleSelectionFace face) { return face.object_id == object_id; }),
			faces.end());
}

void remove_edges_for_object(RectangleSelectionState& selection, int object_id)
{
	selection.selected_edges.erase(
			std::remove_if(selection.selected_edges.begin(), selection.selected_edges.end(), [object_id](RectangleSelectionEdge edge) { return edge.object_id == object_id; }),
			selection.selected_edges.end());
	rectangle_remove_id(selection.selected_edge_full_object_ids, object_id);
	if (selection.active_object_id == object_id) {
		selection.active_edge_vertex_indices.clear();
		selection.active_vertex_indices.clear();
	}
}

void remove_vertices_for_object(RectangleSelectionState& selection, int object_id)
{
	selection.selected_vertices.erase(
			std::remove_if(selection.selected_vertices.begin(), selection.selected_vertices.end(), [object_id](RectangleSelectionVertex vertex) { return vertex.object_id == object_id; }),
			selection.selected_vertices.end());
	rectangle_remove_id(selection.selected_vertex_full_object_ids, object_id);
	if (selection.active_object_id == object_id) {
		selection.active_vertex_indices.clear();
	}
}

bool selection_has_partial_transform_selection(const RectangleSelectionState& selection)
{
	return !selection.selected_faces.empty() ||
		!selection.selected_edges.empty() ||
		!selection.selected_vertices.empty() ||
		!selection.active_edge_vertex_indices.empty() ||
		!selection.active_vertex_indices.empty() ||
		(selection.active_object_id >= 0 && selection.active_face_index >= 0);
}

bool selection_indices_equal_unordered(const std::vector<int>& left, const std::vector<int>& right)
{
	if (left.size() != right.size()) {
		return false;
	}
	for (const int value : left) {
		if (std::find(right.begin(), right.end(), value) == right.end()) {
			return false;
		}
	}
	return true;
}

std::vector<int> selection_indices_prefix(const std::vector<int>& indices, int count)
{
	std::vector<int> normalized;
	if (count <= 0) {
		return normalized;
	}
	normalized.reserve(static_cast<std::size_t>(count));
	for (int index = 0; index < count && index < static_cast<int>(indices.size()); ++index) {
		normalized.push_back(indices[static_cast<std::size_t>(index)]);
	}
	return normalized;
}

bool selection_group_is_valid(const SelectionElementGroup& group, int required_count)
{
	return group.object_id >= 0 && static_cast<int>(group.vertex_indices.size()) >= required_count;
}

SelectionElementGroup normalized_selection_group(const SelectionElementGroup& group, int required_count)
{
	return { group.object_id, selection_indices_prefix(group.vertex_indices, required_count) };
}

std::vector<SelectionElementGroup> normalized_handle_groups(const SelectionHandleTarget& handle, int required_count)
{
	std::vector<SelectionElementGroup> groups;
	for (const SelectionElementGroup& group : handle.element_groups) {
		if (selection_group_is_valid(group, required_count)) {
			groups.push_back(normalized_selection_group(group, required_count));
		}
	}
	if (!groups.empty()) {
		return groups;
	}
	if (handle.object_id >= 0 && static_cast<int>(handle.vertex_indices.size()) >= required_count) {
		groups.push_back({ handle.object_id, selection_indices_prefix(handle.vertex_indices, required_count) });
	}
	return groups;
}

bool edge_matches_group(RectangleSelectionEdge edge, const SelectionElementGroup& group)
{
	return rectangle_edge_is_valid(edge) &&
		group.object_id == edge.object_id &&
		selection_indices_equal_unordered({ edge.a_index, edge.b_index }, group.vertex_indices);
}

bool vertex_matches_group(RectangleSelectionVertex vertex, const SelectionElementGroup& group)
{
	return rectangle_vertex_is_valid(vertex) &&
		group.object_id == vertex.object_id &&
		group.vertex_indices.size() == 1 &&
		group.vertex_indices.front() == vertex.vertex_index;
}

bool edge_group_is_selected(const RectangleSelectionState& selection, const SelectionElementGroup& group)
{
	return std::find_if(selection.selected_edges.begin(), selection.selected_edges.end(), [&group](RectangleSelectionEdge edge) {
		return edge_matches_group(edge, group);
	}) != selection.selected_edges.end();
}

bool vertex_group_is_selected(const RectangleSelectionState& selection, const SelectionElementGroup& group)
{
	return std::find_if(selection.selected_vertices.begin(), selection.selected_vertices.end(), [&group](RectangleSelectionVertex vertex) {
		return vertex_matches_group(vertex, group);
	}) != selection.selected_vertices.end();
}

bool all_edge_groups_selected(const RectangleSelectionState& selection, const std::vector<SelectionElementGroup>& groups)
{
	if (groups.empty()) {
		return false;
	}
	for (const SelectionElementGroup& group : groups) {
		if (!edge_group_is_selected(selection, group)) {
			return false;
		}
	}
	return true;
}

bool all_vertex_groups_selected(const RectangleSelectionState& selection, const std::vector<SelectionElementGroup>& groups)
{
	if (groups.empty()) {
		return false;
	}
	for (const SelectionElementGroup& group : groups) {
		if (!vertex_group_is_selected(selection, group)) {
			return false;
		}
	}
	return true;
}

void add_missing_edge_groups(RectangleSelectionState& selection, const std::vector<SelectionElementGroup>& groups)
{
	for (const SelectionElementGroup& group : groups) {
		if (!selection_group_is_valid(group, 2) || edge_group_is_selected(selection, group)) {
			continue;
		}
		selection.selected_edges.push_back({ group.object_id, group.vertex_indices[0], group.vertex_indices[1] });
	}
}

void add_missing_vertex_groups(RectangleSelectionState& selection, const std::vector<SelectionElementGroup>& groups)
{
	for (const SelectionElementGroup& group : groups) {
		if (!selection_group_is_valid(group, 1) || vertex_group_is_selected(selection, group)) {
			continue;
		}
		selection.selected_vertices.push_back({ group.object_id, group.vertex_indices[0] });
	}
}

void remove_edge_groups(RectangleSelectionState& selection, const std::vector<SelectionElementGroup>& groups)
{
	for (const SelectionElementGroup& group : groups) {
		selection.selected_edges.erase(
				std::remove_if(selection.selected_edges.begin(), selection.selected_edges.end(), [&group](RectangleSelectionEdge edge) {
					return edge_matches_group(edge, group);
				}),
				selection.selected_edges.end());
	}
}

void remove_vertex_groups(RectangleSelectionState& selection, const std::vector<SelectionElementGroup>& groups)
{
	for (const SelectionElementGroup& group : groups) {
		selection.selected_vertices.erase(
				std::remove_if(selection.selected_vertices.begin(), selection.selected_vertices.end(), [&group](RectangleSelectionVertex vertex) {
					return vertex_matches_group(vertex, group);
				}),
				selection.selected_vertices.end());
	}
}

void clear_active_element_state(RectangleSelectionState& selection)
{
	selection.active_object_id = -1;
	selection.active_face_index = -1;
	selection.active_edge_vertex_indices.clear();
	selection.active_vertex_indices.clear();
}

void clear_element_selection_data(RectangleSelectionState& selection)
{
	selection.active_face_index = -1;
	selection.active_edge_vertex_indices.clear();
	selection.active_vertex_indices.clear();
	selection.selected_faces.clear();
	selection.selected_edges.clear();
	selection.selected_vertices.clear();
	selection.selected_edge_full_object_ids.clear();
	selection.selected_vertex_full_object_ids.clear();
}

void activate_object_target(RectangleSelectionState& selection, int object_id)
{
	selection.active_object_id = object_id;
	selection.active_face_index = -1;
	selection.active_edge_vertex_indices.clear();
	selection.active_vertex_indices.clear();
}

void activate_face_target(RectangleSelectionState& selection, RectangleSelectionFace face)
{
	selection.active_object_id = face.object_id;
	selection.active_face_index = face.face_index;
	selection.active_edge_vertex_indices.clear();
	selection.active_vertex_indices.clear();
}

void activate_edge_group(RectangleSelectionState& selection, const SelectionElementGroup& group)
{
	if (!selection_group_is_valid(group, 2)) {
		clear_active_element_state(selection);
		return;
	}
	selection.active_object_id = group.object_id;
	selection.active_face_index = -1;
	selection.active_edge_vertex_indices = selection_indices_prefix(group.vertex_indices, 2);
	selection.active_vertex_indices = selection.active_edge_vertex_indices;
}

void activate_vertex_group(RectangleSelectionState& selection, const SelectionElementGroup& group)
{
	if (!selection_group_is_valid(group, 1)) {
		clear_active_element_state(selection);
		return;
	}
	selection.active_object_id = group.object_id;
	selection.active_face_index = -1;
	selection.active_vertex_indices = selection_indices_prefix(group.vertex_indices, 1);
	selection.active_edge_vertex_indices.clear();
}

void activate_remaining_face_or_clear(RectangleSelectionState& selection)
{
	if (!selection.selected_faces.empty()) {
		activate_face_target(selection, selection.selected_faces.back());
		return;
	}
	clear_active_element_state(selection);
}

void activate_remaining_element_or_target(RectangleSelectionState& selection)
{
  if (selection.mode == SelectionMode::Edge &&
      !selection.selected_edges.empty()) {
    const RectangleSelectionEdge edge = selection.selected_edges.back();
    activate_edge_group(selection,
                        {edge.object_id, {edge.a_index, edge.b_index}});
    return;
  }
  if (selection.mode == SelectionMode::Vertex &&
      !selection.selected_vertices.empty()) {
    const RectangleSelectionVertex vertex = selection.selected_vertices.back();
    activate_vertex_group(selection, {vertex.object_id, {vertex.vertex_index}});
    return;
  }
        if (!selection.selected_object_ids.empty()) {
		activate_object_target(selection, selection.selected_object_ids.back());
		return;
	}
	clear_active_element_state(selection);
}

bool selection_has_element_handles_for_mode(const RectangleSelectionState& selection)
{
  if (selection.mode == SelectionMode::Face) {
    return !selection.selected_faces.empty() ||
           (selection.active_object_id >= 0 &&
            selection.active_face_index >= 0);
  }
  if (selection.mode == SelectionMode::Edge) {
    return !selection.selected_edges.empty() ||
           !selection.active_edge_vertex_indices.empty() ||
           !selection.selected_edge_full_object_ids.empty();
  }
  if (selection.mode == SelectionMode::Vertex) {
    return !selection.selected_vertices.empty() ||
           !selection.active_vertex_indices.empty() ||
           !selection.selected_vertex_full_object_ids.empty();
  }
        return false;
}

std::vector<int> canonical_object_targets_for_mode_switch(const RectangleSelectionState& selection, int previous_active_object_id)
{
	if (!selection.selected_object_ids.empty()) {
		return selection.selected_object_ids;
	}
	std::vector<int> targets;
	for (const RectangleSelectionFace face : selection.selected_faces) {
		rectangle_append_unique_id(targets, face.object_id);
	}
	for (const RectangleSelectionEdge edge : selection.selected_edges) {
		rectangle_append_unique_id(targets, edge.object_id);
	}
	for (const RectangleSelectionVertex vertex : selection.selected_vertices) {
		rectangle_append_unique_id(targets, vertex.object_id);
	}
	rectangle_append_unique_id(targets, previous_active_object_id);
	return targets;
}

void ensure_element_group_owners_as_targets(RectangleSelectionState& selection)
{
	for (const RectangleSelectionEdge edge : selection.selected_edges) {
		rectangle_append_unique_id(selection.selected_object_ids, edge.object_id);
	}
	for (const RectangleSelectionVertex vertex : selection.selected_vertices) {
		rectangle_append_unique_id(selection.selected_object_ids, vertex.object_id);
	}
}

void prune_transient_element_selection_to_targets(RectangleSelectionState& selection)
{
	ensure_element_group_owners_as_targets(selection);
	if (selection.selected_object_ids.empty()) {
		selection.selected_edges.clear();
		selection.selected_vertices.clear();
		selection.active_edge_vertex_indices.clear();
		selection.active_vertex_indices.clear();
		return;
	}
	selection.selected_edges.erase(
			std::remove_if(selection.selected_edges.begin(), selection.selected_edges.end(), [&selection](RectangleSelectionEdge edge) {
				return !rectangle_contains_id(selection.selected_object_ids, edge.object_id);
			}),
			selection.selected_edges.end());
	selection.selected_vertices.erase(
			std::remove_if(selection.selected_vertices.begin(), selection.selected_vertices.end(), [&selection](RectangleSelectionVertex vertex) {
				return !rectangle_contains_id(selection.selected_object_ids, vertex.object_id);
			}),
			selection.selected_vertices.end());
	if (selection.active_object_id < 0 || !rectangle_contains_id(selection.selected_object_ids, selection.active_object_id)) {
		selection.active_edge_vertex_indices.clear();
		selection.active_vertex_indices.clear();
	}
}

bool toggle_or_add_object(RectangleSelectionState& selection, int object_id, bool additive)
{
	if (!additive) {
		selection.selected_object_ids = { object_id };
		return true;
	}
	const bool removed = rectangle_remove_id(selection.selected_object_ids, object_id);
	if (!removed) {
		selection.selected_object_ids.push_back(object_id);
	}
	return !removed;
}

bool toggle_face(RectangleSelectionState& selection, RectangleSelectionFace face, bool additive)
{
	if (!rectangle_face_is_valid(face)) {
		return false;
	}
	if (!additive) {
		selection.selected_faces = { face };
		return true;
	}
	const auto found = std::find_if(selection.selected_faces.begin(), selection.selected_faces.end(), [face](RectangleSelectionFace selected_face) {
		return selected_face.object_id == face.object_id && selected_face.face_index == face.face_index;
	});
	if (found != selection.selected_faces.end()) {
		selection.selected_faces.erase(found);
		return false;
	}
	selection.selected_faces.push_back(face);
	return true;
}

bool remove_element_object_target(RectangleSelectionState& selection, int object_id)
{
	bool removed = rectangle_remove_id(selection.selected_object_ids, object_id);
	const std::size_t edge_count = selection.selected_edges.size();
	selection.selected_edges.erase(
			std::remove_if(selection.selected_edges.begin(), selection.selected_edges.end(), [object_id](RectangleSelectionEdge edge) {
				return edge.object_id == object_id;
			}),
			selection.selected_edges.end());
	removed = removed || edge_count != selection.selected_edges.size();
	const std::size_t vertex_count = selection.selected_vertices.size();
	selection.selected_vertices.erase(
			std::remove_if(selection.selected_vertices.begin(), selection.selected_vertices.end(), [object_id](RectangleSelectionVertex vertex) {
				return vertex.object_id == object_id;
			}),
			selection.selected_vertices.end());
	removed = removed || vertex_count != selection.selected_vertices.size();
	if (selection.active_object_id == object_id) {
		selection.active_edge_vertex_indices.clear();
		selection.active_vertex_indices.clear();
	}
	return removed;
}

bool selection_handle_is_empty(const SelectionHandleTarget& handle)
{
  return handle.kind == ActiveSelectionKind::None && handle.object_id < 0 &&
         handle.face_index < 0 && handle.vertex_indices.empty() &&
         handle.element_groups.empty();
}

bool selection_hit_is_empty(const SelectionHitTarget& hit)
{
	return hit.object_id < 0 && hit.face_index < 0;
}

} // namespace

RectangleSelectionResult apply_rectangle_selection(const RectangleSelectionRequest& request)
{
	QDR_PROFILE_SCOPE("Box selection apply rectangle policy");
	switch (request.current_selection.mode) {
        case SelectionMode::Object:
          return rectangle_select_objects(request);
        case SelectionMode::Face:
          return rectangle_select_faces(request);
        case SelectionMode::Edge:
          return rectangle_select_edges(request);
        case SelectionMode::Vertex:
          return rectangle_select_vertices(request);
        default:
          break;
	}
	RectangleSelectionResult result;
	result.selection.mode = request.current_selection.mode;
	if (rectangle_preserves_existing(request.operation)) {
		result.selection = request.current_selection;
	}
	return result;
}

RectangleSelectionResult apply_selection_set_command(const SelectionSetCommandRequest& request)
{
	RectangleSelectionResult result;
	result.selection.mode = request.current_selection.mode;
        const bool invert = request.command == SelectionSetCommand::Invert;
        switch (request.current_selection.mode) {
        case SelectionMode::Object: {
          std::unordered_set<int> selected_ids;
          selected_ids.reserve(
              request.current_selection.selected_object_ids.size());
          for (const int object_id :
               request.current_selection.selected_object_ids) {
            if (object_id >= 0) {
              selected_ids.insert(object_id);
            }
          }
          const bool all_selected = rectangle_ids_cover_universe(
              request.current_selection.selected_object_ids,
              request.universe.object_ids);
          if (!invert && all_selected) {
            return result;
          }
          for (const int object_id : request.universe.object_ids) {
            if (object_id < 0) {
              continue;
            }
            const bool currently_selected =
                selected_ids.find(object_id) != selected_ids.end();
            if (!invert || !currently_selected) {
              result.selection.selected_object_ids.push_back(object_id);
            }
          }
          if (!result.selection.selected_object_ids.empty()) {
            result.selection.active_object_id =
                result.selection.selected_object_ids.front();
          }
          return result;
        }
        case SelectionMode::Face: {
          const std::unordered_set<RectangleFaceKey, RectangleFaceKeyHash>
              selected_faces =
                  rectangle_face_set_from_selection(request.current_selection);
          const bool all_selected = rectangle_faces_cover_universe(
              request.current_selection, request.universe);
          if (!invert && all_selected) {
            return result;
          }
          for (const RectangleSelectionFace face : request.universe.faces) {
            if (!rectangle_face_is_valid(face)) {
              continue;
            }
            const bool currently_selected =
                selected_faces.find(rectangle_face_key(face)) !=
                selected_faces.end();
            if (!invert || !currently_selected) {
              result.selection.selected_faces.push_back(face);
            }
          }
          if (!result.selection.selected_faces.empty()) {
            const RectangleSelectionFace active =
                result.selection.selected_faces.back();
            result.selection.active_object_id = active.object_id;
            result.selection.active_face_index = active.face_index;
          }
          return result;
        }
        case SelectionMode::Edge: {
          if (!invert) {
            if (rectangle_edges_cover_universe(request.current_selection,
                                               request.universe)) {
              return result;
            }
            result.selection.selected_edge_full_object_ids =
                request.universe.object_ids;
            if (!result.selection.selected_edge_full_object_ids.empty()) {
              result.selection.active_object_id =
                  result.selection.selected_edge_full_object_ids.front();
            }
            return result;
          }
          const auto selected_edges = rectangle_edge_set_from_selection(
              request.current_selection, request.universe);
          std::unordered_map<
              int, std::unordered_set<RectangleEdgeKey, RectangleEdgeKeyHash>>
              inverted_edges_by_object;
          inverted_edges_by_object.reserve(request.universe.object_ids.size());
          for (const RectangleSelectionEdge edge : request.universe.edges) {
            if (!rectangle_edge_is_valid(edge)) {
              continue;
            }
            const auto found_edges = selected_edges.find(edge.object_id);
            const bool currently_selected =
                found_edges != selected_edges.end() &&
                found_edges->second.find(rectangle_edge_key(edge)) !=
                    found_edges->second.end();
            if (!currently_selected) {
              inverted_edges_by_object[edge.object_id].insert(
                  rectangle_edge_key(edge));
            }
          }
          return rectangle_result_from_edge_set(SelectionMode::Edge,
                                                request.universe,
                                                inverted_edges_by_object, true);
        }
        case SelectionMode::Vertex: {
          if (!invert) {
            if (rectangle_vertices_cover_universe(request.current_selection,
                                                  request.universe)) {
              return result;
            }
            result.selection.selected_vertex_full_object_ids =
                request.universe.object_ids;
            if (!result.selection.selected_vertex_full_object_ids.empty()) {
              result.selection.active_object_id =
                  result.selection.selected_vertex_full_object_ids.front();
            }
            return result;
          }
          const auto selected_vertices = rectangle_vertex_set_from_selection(
              request.current_selection, request.universe);
          std::unordered_map<int, std::unordered_set<int>>
              inverted_vertices_by_object;
          inverted_vertices_by_object.reserve(
              request.universe.object_ids.size());
          for (const RectangleSelectionVertex vertex :
               request.universe.vertices) {
            if (!rectangle_vertex_is_valid(vertex)) {
              continue;
            }
            const auto found_vertices =
                selected_vertices.find(vertex.object_id);
            const bool currently_selected =
                found_vertices != selected_vertices.end() &&
                found_vertices->second.find(vertex.vertex_index) !=
                    found_vertices->second.end();
            if (!currently_selected) {
              inverted_vertices_by_object[vertex.object_id].insert(
                  vertex.vertex_index);
            }
          }
          return rectangle_result_from_vertex_set(
              SelectionMode::Vertex, request.universe,
              inverted_vertices_by_object, true);
        }
                default:
			break;
	}
	return result;
}

RectangleSelectionResult convert_selection_to_face_mode(const FaceModeConversionRequest& request)
{
	RectangleSelectionResult result;
        result.selection.mode = SelectionMode::Face;

        std::unordered_set<RectangleFaceKey, RectangleFaceKeyHash> selected_face_keys;
	selected_face_keys.reserve(request.universe.faces.size());
	result.selection.selected_faces.reserve(request.universe.faces.size());

	for (const int object_id : request.current_selection.selected_vertex_full_object_ids) {
		face_mode_append_faces_for_object(result.selection.selected_faces, selected_face_keys, request.universe, object_id);
	}
	for (const int object_id : request.current_selection.selected_edge_full_object_ids) {
		face_mode_append_faces_for_object(result.selection.selected_faces, selected_face_keys, request.universe, object_id);
	}

	std::unordered_map<int, std::unordered_set<int>> selected_vertices_by_object;
	std::vector<int> ordered_vertex_object_ids;
	selected_vertices_by_object.reserve(request.current_selection.selected_vertices.size() + request.current_selection.selected_edges.size());
	for (const RectangleSelectionVertex& vertex : request.current_selection.selected_vertices) {
		face_mode_insert_vertex(selected_vertices_by_object, ordered_vertex_object_ids, vertex.object_id, vertex.vertex_index);
	}
	for (const RectangleSelectionEdge& edge : request.current_selection.selected_edges) {
		face_mode_insert_edge_vertices(selected_vertices_by_object, ordered_vertex_object_ids, edge);
	}

	for (const int object_id : ordered_vertex_object_ids) {
		const auto found_vertices = selected_vertices_by_object.find(object_id);
		if (found_vertices == selected_vertices_by_object.end()) {
			continue;
		}
		for (const RectangleSelectionFace& face : request.universe.faces) {
			if (face.object_id == object_id && face_mode_face_is_covered_by_vertices(face, found_vertices->second)) {
				face_mode_append_unique_face(result.selection.selected_faces, selected_face_keys, face);
			}
		}
	}

	if (!result.selection.selected_faces.empty()) {
		const RectangleSelectionFace& active = result.selection.selected_faces.back();
		result.selection.active_object_id = active.object_id;
		result.selection.active_face_index = active.face_index;
		return result;
	}

	if (!request.current_selection.selected_object_ids.empty()) {
		result.selection.selected_object_ids = request.current_selection.selected_object_ids;
		result.selection.active_object_id = result.selection.selected_object_ids.back();
		result.selection.active_face_index = -1;
	}
	return result;
}

FullElementPromotionResult promote_full_element_selection_to_object(const FullElementPromotionRequest& request)
{
	FullElementPromotionResult result;
	std::unordered_set<int> full_object_ids;
	bool lossless = true;

        if (request.current_selection.mode == SelectionMode::Face) {
          const std::unordered_set<RectangleFaceKey, RectangleFaceKeyHash>
              selected_faces =
                  rectangle_face_set_from_selection(request.current_selection);
          if (selected_faces.empty()) {
            return result;
          }
          const std::unordered_map<int, int> face_count_by_object =
              promotion_face_count_by_object(request.universe);
          std::unordered_map<int, int> selected_face_count_by_object;
          selected_face_count_by_object.reserve(face_count_by_object.size());
          int matched_face_key_count = 0;
          for (const RectangleSelectionFace &face : request.universe.faces) {
            if (rectangle_face_is_valid(face) &&
                selected_faces.find(rectangle_face_key(face)) !=
                    selected_faces.end()) {
              selected_face_count_by_object[face.object_id]++;
              matched_face_key_count++;
            }
          }
          lossless =
              matched_face_key_count == static_cast<int>(selected_faces.size());
          if (lossless) {
            for (const auto &entry : selected_face_count_by_object) {
              const auto found_count = face_count_by_object.find(entry.first);
              if (found_count == face_count_by_object.end() ||
                  found_count->second <= 0 ||
                  entry.second != found_count->second) {
                lossless = false;
                break;
              }
              full_object_ids.insert(entry.first);
            }
          }
        } else if (request.current_selection.mode == SelectionMode::Edge) {
          const std::unordered_map<int, int> edge_count_by_object =
              promotion_edge_count_by_object(request.universe);
          const std::unordered_set<int> selected_full_object_ids =
              promotion_id_set_from_vector(
                  request.current_selection.selected_edge_full_object_ids);
          const auto selected_edges_by_object =
              rectangle_edge_set_from_selection(request.current_selection,
                                                request.universe);
          if (selected_full_object_ids.empty() &&
              selected_edges_by_object.empty()) {
            return result;
          }
          for (const int object_id : selected_full_object_ids) {
            const auto found_count = edge_count_by_object.find(object_id);
            if (found_count == edge_count_by_object.end() ||
                found_count->second <= 0) {
              lossless = false;
              break;
            }
            full_object_ids.insert(object_id);
          }
          if (lossless) {
            for (const auto &entry : selected_edges_by_object) {
              const auto found_count = edge_count_by_object.find(entry.first);
              if (found_count == edge_count_by_object.end() ||
                  found_count->second <= 0) {
                lossless = false;
                break;
              }
              int matched_edge_key_count = 0;
              for (const RectangleSelectionEdge &edge :
                   request.universe.edges) {
                if (edge.object_id == entry.first &&
                    entry.second.find(rectangle_edge_key(edge)) !=
                        entry.second.end()) {
                  matched_edge_key_count++;
                }
              }
              if (matched_edge_key_count !=
                  static_cast<int>(entry.second.size())) {
                lossless = false;
                break;
              }
              if (selected_full_object_ids.find(entry.first) !=
                  selected_full_object_ids.end()) {
                continue;
              }
              if (matched_edge_key_count != found_count->second) {
                lossless = false;
                break;
              }
              full_object_ids.insert(entry.first);
            }
          }
        } else if (request.current_selection.mode == SelectionMode::Vertex) {
          const std::unordered_map<int, int> vertex_count_by_object =
              promotion_vertex_count_by_object(request.universe);
          const std::unordered_set<int> selected_full_object_ids =
              promotion_id_set_from_vector(
                  request.current_selection.selected_vertex_full_object_ids);
          const auto selected_vertices_by_object =
              rectangle_vertex_set_from_selection(request.current_selection,
                                                  request.universe);
          if (selected_full_object_ids.empty() &&
              selected_vertices_by_object.empty()) {
            return result;
          }
          for (const int object_id : selected_full_object_ids) {
            const auto found_count = vertex_count_by_object.find(object_id);
            if (found_count == vertex_count_by_object.end() ||
                found_count->second <= 0) {
              lossless = false;
              break;
            }
            full_object_ids.insert(object_id);
          }
          if (lossless) {
            for (const auto &entry : selected_vertices_by_object) {
              const auto found_count = vertex_count_by_object.find(entry.first);
              if (found_count == vertex_count_by_object.end() ||
                  found_count->second <= 0) {
                lossless = false;
                break;
              }
              int matched_vertex_count = 0;
              for (const RectangleSelectionVertex &vertex :
                   request.universe.vertices) {
                if (vertex.object_id == entry.first &&
                    entry.second.find(vertex.vertex_index) !=
                        entry.second.end()) {
                  matched_vertex_count++;
                }
              }
              if (matched_vertex_count !=
                  static_cast<int>(entry.second.size())) {
                lossless = false;
                break;
              }
              if (selected_full_object_ids.find(entry.first) !=
                  selected_full_object_ids.end()) {
                continue;
              }
              if (matched_vertex_count != found_count->second) {
                lossless = false;
                break;
              }
              full_object_ids.insert(entry.first);
            }
          }
        } else {
          return result;
        }

        if (!lossless || full_object_ids.empty()) {
		return result;
	}

	std::unordered_set<int> promoted_object_ids = full_object_ids;
	const std::unordered_set<int> universe_object_ids = promotion_universe_object_set(request.universe);
	for (const int object_id : request.current_selection.selected_object_ids) {
		if (object_id < 0 || universe_object_ids.find(object_id) == universe_object_ids.end()) {
			return result;
		}
		promoted_object_ids.insert(object_id);
	}

        result.selection.mode = SelectionMode::Object;
        for (const int object_id : request.universe.object_ids) {
		if (promoted_object_ids.find(object_id) != promoted_object_ids.end()) {
			result.selection.selected_object_ids.push_back(object_id);
		}
	}
	if (result.selection.selected_object_ids.empty()) {
		return result;
	}

	if (promoted_object_ids.find(request.preferred_active_object_id) != promoted_object_ids.end()) {
		result.selection.active_object_id = request.preferred_active_object_id;
	} else if (promoted_object_ids.find(request.current_selection.active_object_id) != promoted_object_ids.end()) {
		result.selection.active_object_id = request.current_selection.active_object_id;
	} else {
		result.selection.active_object_id = result.selection.selected_object_ids.front();
	}
	result.promoted = true;
	return result;
}

RectangleSelectionResult select_object_ids(const SelectionObjectIdsRequest& request)
{
	RectangleSelectionResult result;
	result.selection = cleared_selection_state(request.current_selection);
	if (request.object_ids.empty()) {
		return result;
	}
	if (request.force_object_mode) {
          result.selection.mode = SelectionMode::Object;
        }
	result.selection.selected_object_ids = request.object_ids;
	result.selection.active_object_id = request.object_ids.front();
	result.selection.active_face_index = -1;
	return result;
}

RectangleSelectionResult select_object(const SelectionObjectRequest& request)
{
	RectangleSelectionResult result;
	if (request.object_id < 0) {
		result.selection = request.current_selection;
		return result;
	}
	SelectionObjectIdsRequest object_ids_request;
	object_ids_request.current_selection = request.current_selection;
	object_ids_request.object_ids = { request.object_id };
	object_ids_request.force_object_mode = request.force_object_mode;
	return select_object_ids(object_ids_request);
}

RectangleSelectionResult select_face(const SelectionFaceRequest& request)
{
	RectangleSelectionResult result;
	if (!rectangle_face_is_valid(request.face)) {
		result.selection = request.current_selection;
		return result;
	}
	result.selection = cleared_selection_state(request.current_selection);
	if (request.force_face_mode) {
          result.selection.mode = SelectionMode::Face;
        }
	result.selection.active_object_id = request.face.object_id;
	result.selection.active_face_index = request.face.face_index;
	result.selection.selected_faces.push_back(request.face);
	return result;
}

RectangleSelectionResult select_object_wide_elements(const SelectionObjectWideElementsRequest& request)
{
	RectangleSelectionResult result;
        if (request.object_id < 0 || (request.mode != SelectionMode::Face &&
                                      request.mode != SelectionMode::Edge &&
                                      request.mode != SelectionMode::Vertex)) {
          result.selection = request.current_selection;
          return result;
        }

        result.selection = request.additive ? request.current_selection : cleared_selection_state(request.current_selection);
	result.selection.mode = request.mode;
	result.selection.active_face_index = -1;
	result.selection.active_edge_vertex_indices.clear();
	result.selection.active_vertex_indices.clear();

        if (request.mode == SelectionMode::Face) {
          result.selection.selected_object_ids.clear();
          result.selection.selected_edges.clear();
          result.selection.selected_edge_full_object_ids.clear();
          result.selection.selected_vertices.clear();
          result.selection.selected_vertex_full_object_ids.clear();
          remove_faces_for_object(result.selection.selected_faces,
                                  request.object_id);
          for (const RectangleSelectionFace &face : request.object_faces) {
            if (rectangle_face_is_valid(face) &&
                face.object_id == request.object_id) {
              result.selection.selected_faces.push_back(face);
            }
          }
          if (!result.selection.selected_faces.empty()) {
            const RectangleSelectionFace active =
                result.selection.selected_faces.back();
            result.selection.active_object_id = active.object_id;
            result.selection.active_face_index = active.face_index;
          } else {
            result.selection.active_object_id = -1;
          }
          return result;
        }

        result.selection.selected_faces.clear();
	result.selection.active_face_index = -1;
	rectangle_append_unique_id(result.selection.selected_object_ids, request.object_id);

        if (request.mode == SelectionMode::Edge) {
          result.selection.selected_vertices.clear();
          result.selection.selected_vertex_full_object_ids.clear();
          result.selection.active_vertex_indices.clear();
          remove_edges_for_object(result.selection, request.object_id);
          rectangle_append_unique_id(
              result.selection.selected_edge_full_object_ids,
              request.object_id);
          result.selection.active_object_id = request.object_id;
          return result;
        }

        result.selection.selected_edges.clear();
	result.selection.selected_edge_full_object_ids.clear();
	result.selection.active_edge_vertex_indices.clear();
	remove_vertices_for_object(result.selection, request.object_id);
	rectangle_append_unique_id(result.selection.selected_vertex_full_object_ids, request.object_id);
	result.selection.active_object_id = request.object_id;
	return result;
}

SelectionActiveTarget active_target_from_selection(const RectangleSelectionState& selection)
{
	SelectionActiveTarget active;
	active.selection_mode = selection.mode;
        if (selection.mode == SelectionMode::Face) {
          if (selection.active_object_id >= 0 &&
              selection.active_face_index >= 0) {
            active.has_active = true;
            active.kind = ActiveSelectionKind::Face;
            active.object_id = selection.active_object_id;
            active.face_index = selection.active_face_index;
            return active;
          }
          if (!selection.selected_faces.empty()) {
            const RectangleSelectionFace face = selection.selected_faces.back();
            active.has_active = true;
            active.kind = ActiveSelectionKind::Face;
            active.object_id = face.object_id;
            active.face_index = face.face_index;
            active.vertex_indices = face.vertex_indices;
            return active;
          }
          return active;
        }
        if (selection.mode == SelectionMode::Edge) {
          if (selection.active_object_id >= 0 &&
              selection.active_edge_vertex_indices.size() >= 2) {
            active.has_active = true;
            active.kind = ActiveSelectionKind::Edge;
            active.object_id = selection.active_object_id;
            active.vertex_indices = selection_indices_prefix(
                selection.active_edge_vertex_indices, 2);
            return active;
          }
          if (!selection.selected_edges.empty()) {
            const RectangleSelectionEdge edge = selection.selected_edges.back();
            active.has_active = true;
            active.kind = ActiveSelectionKind::Edge;
            active.object_id = edge.object_id;
            active.vertex_indices = {edge.a_index, edge.b_index};
            return active;
          }
          return active;
        }
        if (selection.mode == SelectionMode::Vertex) {
          if (selection.active_object_id >= 0 &&
              !selection.active_vertex_indices.empty()) {
            active.has_active = true;
            active.kind = ActiveSelectionKind::Vertex;
            active.object_id = selection.active_object_id;
            active.vertex_indices =
                selection_indices_prefix(selection.active_vertex_indices, 1);
            return active;
          }
          if (!selection.selected_vertices.empty()) {
            const RectangleSelectionVertex vertex =
                selection.selected_vertices.back();
            active.has_active = true;
            active.kind = ActiveSelectionKind::Vertex;
            active.object_id = vertex.object_id;
            active.vertex_indices = {vertex.vertex_index};
            return active;
          }
          return active;
        }

        if (selection.active_object_id >= 0 &&
			(selection.selected_object_ids.empty() || rectangle_contains_id(selection.selected_object_ids, selection.active_object_id))) {
		active.has_active = true;
                active.kind = ActiveSelectionKind::Object;
                active.object_id = selection.active_object_id;
		return active;
	}
	if (!selection.selected_object_ids.empty()) {
		active.has_active = true;
                active.kind = ActiveSelectionKind::Object;
                active.object_id = selection.selected_object_ids.back();
	}
	return active;
}

RectangleSelectionResult clear_empty_element_click_selection(const RectangleSelectionState& selection)
{
	RectangleSelectionResult result;
	result.selection = selection;
        if (selection.mode == SelectionMode::Face ||
            selection.mode == SelectionMode::Edge ||
            selection.mode == SelectionMode::Vertex) {
          if (selection_has_element_handles_for_mode(result.selection)) {
            clear_element_selection_data(result.selection);
            if (selection.mode == SelectionMode::Edge ||
                selection.mode == SelectionMode::Vertex ||
                result.selection.active_object_id < 0) {
              activate_remaining_element_or_target(result.selection);
            }
            return result;
          }
        }
        result.selection = cleared_selection_state(selection);
	return result;
}

RectangleSelectionResult select_handle(const SelectionHandleRequest& request)
{
	RectangleSelectionResult result;
	result.selection = request.current_selection;
	const SelectionHandleTarget& handle = request.handle;
        if (handle.object_id < 0 ||
            (handle.kind != ActiveSelectionKind::Vertex &&
             handle.kind != ActiveSelectionKind::Edge &&
             handle.kind != ActiveSelectionKind::Face)) {
          return result;
        }

        if (handle.kind == ActiveSelectionKind::Face) {
          if (handle.face_index < 0) {
            return result;
          }
          if (!request.additive) {
            result.selection =
                cleared_selection_state(request.current_selection);
          } else {
            result.selection.selected_object_ids.clear();
          }
          result.selection.mode = SelectionMode::Face;
          activate_face_target(
              result.selection,
              {handle.object_id, handle.face_index, handle.vertex_indices});
          const bool face_added = toggle_face(
              result.selection,
              {handle.object_id, handle.face_index, handle.vertex_indices},
              request.additive);
          if (request.additive && !face_added) {
            activate_remaining_face_or_clear(result.selection);
          }
          return result;
        }

        const int required_count =
            handle.kind == ActiveSelectionKind::Edge ? 2 : 1;
        if (static_cast<int>(handle.vertex_indices.size()) < required_count) {
		return result;
	}
	const std::vector<SelectionElementGroup> groups = normalized_handle_groups(handle, required_count);
	if (groups.empty()) {
		return result;
	}

	std::vector<int> selected_ids;
	if (request.additive) {
		selected_ids = result.selection.selected_object_ids;
	}
	for (const SelectionElementGroup& group : groups) {
		rectangle_append_unique_id(selected_ids, group.object_id);
	}
	result.selection.selected_object_ids = selected_ids;

	if (!request.additive) {
		clear_element_selection_data(result.selection);
	} else {
		result.selection.selected_faces.clear();
	}

        if (handle.kind == ActiveSelectionKind::Vertex) {
          result.selection.selected_edges.clear();
          activate_vertex_group(result.selection, groups.front());
          const bool remove_group =
              request.additive &&
              all_vertex_groups_selected(result.selection, groups);
          if (remove_group) {
            remove_vertex_groups(result.selection, groups);
            activate_remaining_element_or_target(result.selection);
          } else {
            add_missing_vertex_groups(result.selection, groups);
          }
        } else {
          result.selection.selected_vertices.clear();
          activate_edge_group(result.selection, groups.front());
          const bool remove_group =
              request.additive &&
              all_edge_groups_selected(result.selection, groups);
          if (remove_group) {
            remove_edge_groups(result.selection, groups);
            activate_remaining_element_or_target(result.selection);
          } else {
            add_missing_edge_groups(result.selection, groups);
          }
        }

        if (request.additive && !selection_has_any(result.selection)) {
		result.selection = cleared_selection_state(request.current_selection);
	}
	return result;
}

RectangleSelectionResult select_hit(const SelectionHitRequest& request)
{
	RectangleSelectionResult result;
	result.selection = request.current_selection;
	const SelectionHitTarget& hit = request.hit;
	if (hit.object_id < 0) {
		return result;
	}

	if (!request.additive) {
		result.selection = cleared_selection_state(request.current_selection);
	}

        if (result.selection.mode == SelectionMode::Object) {
          const bool object_added = toggle_or_add_object(
              result.selection, hit.object_id, request.additive);
          if (object_added) {
            activate_object_target(result.selection, hit.object_id);
          } else if (result.selection.selected_object_ids.empty()) {
            result.selection =
                cleared_selection_state(request.current_selection);
          } else {
            activate_object_target(result.selection,
                                   result.selection.selected_object_ids.back());
          }
          return result;
        }

        if (result.selection.mode == SelectionMode::Face) {
          if (request.additive) {
            result.selection.selected_object_ids.clear();
          }
          activate_face_target(result.selection,
                               {hit.object_id, hit.face_index, {}});
          const bool face_added =
              toggle_face(result.selection, {hit.object_id, hit.face_index, {}},
                          request.additive);
          if (request.additive && !face_added) {
            activate_remaining_face_or_clear(result.selection);
          }
          return result;
        }

        if (result.selection.mode == SelectionMode::Edge ||
            result.selection.mode == SelectionMode::Vertex) {
          result.selection.selected_faces.clear();
          result.selection.active_edge_vertex_indices.clear();
          result.selection.active_vertex_indices.clear();
          if (!request.additive) {
            clear_element_selection_data(result.selection);
            activate_object_target(result.selection, hit.object_id);
            result.selection.selected_object_ids = {hit.object_id};
            return result;
          }
          if (rectangle_contains_id(result.selection.selected_object_ids,
                                    hit.object_id)) {
            remove_element_object_target(result.selection, hit.object_id);
          } else {
            rectangle_append_unique_id(result.selection.selected_object_ids,
                                       hit.object_id);
            activate_object_target(result.selection, hit.object_id);
            return result;
          }
          prune_transient_element_selection_to_targets(result.selection);
          activate_remaining_element_or_target(result.selection);
          if (!selection_has_any(result.selection)) {
            result.selection =
                cleared_selection_state(request.current_selection);
          }
        }
        return result;
}

RectangleSelectionResult switch_selection_mode(const SelectionModeSwitchRequest& request)
{
	RectangleSelectionResult result;
	result.selection = request.current_selection;
	if (request.current_selection.mode == request.mode) {
		return result;
	}

	const int previous_active_object_id = request.current_selection.active_object_id;
	const std::vector<int> canonical_targets = canonical_object_targets_for_mode_switch(request.current_selection, previous_active_object_id);
	result.selection.mode = request.mode;

        if (request.mode == SelectionMode::Edge ||
            request.mode == SelectionMode::Vertex) {
          clear_element_selection_data(result.selection);
          result.selection.selected_object_ids = canonical_targets;
          if (!canonical_targets.empty()) {
            const int active_id =
                rectangle_contains_id(canonical_targets,
                                      previous_active_object_id)
                    ? previous_active_object_id
                    : canonical_targets.back();
            activate_object_target(result.selection, active_id);
          } else {
            clear_active_element_state(result.selection);
          }
          return result;
        }

        if (request.mode == SelectionMode::Object) {
          result.selection = cleared_selection_state(request.current_selection);
          result.selection.mode = SelectionMode::Object;
          result.selection.selected_object_ids = canonical_targets;
          if (!canonical_targets.empty()) {
            activate_object_target(result.selection, canonical_targets.back());
          }
          return result;
        }

        clear_element_selection_data(result.selection);
	result.selection.selected_object_ids = canonical_targets;
	if (!canonical_targets.empty()) {
		const int active_id = rectangle_contains_id(canonical_targets, previous_active_object_id) ? previous_active_object_id : canonical_targets.back();
		activate_object_target(result.selection, active_id);
	} else {
		clear_active_element_state(result.selection);
	}
	return result;
}

std::vector<int> selected_or_active_object_ids(const RectangleSelectionState& selection)
{
	if (!selection.selected_object_ids.empty()) {
		return selection.selected_object_ids;
	}
	if (selection.active_object_id >= 0) {
		return { selection.active_object_id };
	}
	return {};
}

int selection_count(const RectangleSelectionState& selection)
{
	switch (selection.mode) {
        case SelectionMode::Object:
          return static_cast<int>(selection.selected_object_ids.size());
        case SelectionMode::Face:
          if (!selection.selected_faces.empty()) {
            return static_cast<int>(selection.selected_faces.size());
          }
          return selection.active_object_id >= 0 &&
                         selection.active_face_index >= 0
                     ? 1
                     : 0;
        case SelectionMode::Edge:
          if (!selection.selected_edge_full_object_ids.empty()) {
            return static_cast<int>(
                selection.selected_edge_full_object_ids.size());
          }
          if (!selection.selected_edges.empty()) {
            return static_cast<int>(selection.selected_edges.size());
          }
          if (!selection.active_edge_vertex_indices.empty()) {
            return 1;
          }
          return static_cast<int>(selection.selected_object_ids.size());
        case SelectionMode::Vertex:
          if (!selection.selected_vertex_full_object_ids.empty()) {
            return static_cast<int>(
                selection.selected_vertex_full_object_ids.size());
          }
          if (!selection.selected_vertices.empty()) {
            return static_cast<int>(selection.selected_vertices.size());
          }
          if (!selection.active_vertex_indices.empty()) {
            return 1;
          }
          return static_cast<int>(selection.selected_object_ids.size());
        default:
          break;
	}
	return 0;
}

bool selection_has_any(const RectangleSelectionState& selection)
{
	if (!selection.selected_object_ids.empty()) {
		return true;
	}
	if ((selection.active_object_id >= 0 && selection.active_face_index >= 0) || !selection.selected_faces.empty()) {
		return true;
	}
	if (!selection.selected_edges.empty() || !selection.active_edge_vertex_indices.empty() || !selection.selected_edge_full_object_ids.empty()) {
		return true;
	}
	return !selection.selected_vertices.empty() || !selection.active_vertex_indices.empty() || !selection.selected_vertex_full_object_ids.empty();
}

bool selection_has_object_selection(const RectangleSelectionState& selection)
{
	return !selection.selected_object_ids.empty();
}

bool selection_has_face_selection(const RectangleSelectionState& selection)
{
	return (selection.active_object_id >= 0 && selection.active_face_index >= 0) || !selection.selected_faces.empty();
}

bool selection_has_edge_selection(const RectangleSelectionState& selection)
{
	return !selection.selected_edges.empty() || !selection.active_edge_vertex_indices.empty() || !selection.selected_edge_full_object_ids.empty();
}

bool selection_has_vertex_selection(const RectangleSelectionState& selection)
{
	return (selection.active_object_id >= 0 && !selection.active_vertex_indices.empty()) ||
		!selection.selected_edges.empty() ||
		!selection.selected_vertices.empty();
}

bool selection_has_gpu_feedback(const RectangleSelectionState& selection)
{
	return !selection.selected_object_ids.empty() ||
		!selection.selected_faces.empty() ||
		!selection.selected_edges.empty() ||
		!selection.selected_vertices.empty();
}

std::vector<int> dirty_object_ids_for_selection(const RectangleSelectionState& selection)
{
	if (!selection.selected_object_ids.empty() &&
			selection.selected_faces.empty() &&
			selection.selected_edges.empty() &&
			selection.selected_vertices.empty() &&
			(selection.active_object_id < 0 || rectangle_contains_id(selection.selected_object_ids, selection.active_object_id))) {
		return selection.selected_object_ids;
	}

	std::vector<int> ids;
	for (const int object_id : selection.selected_object_ids) {
		rectangle_append_unique_id(ids, object_id);
	}
	for (const int object_id : selection.selected_edge_full_object_ids) {
		rectangle_append_unique_id(ids, object_id);
	}
	for (const int object_id : selection.selected_vertex_full_object_ids) {
		rectangle_append_unique_id(ids, object_id);
	}
	rectangle_append_unique_id(ids, selection.active_object_id);
	for (const RectangleSelectionFace face : selection.selected_faces) {
		rectangle_append_unique_id(ids, face.object_id);
	}
	for (const RectangleSelectionEdge edge : selection.selected_edges) {
		rectangle_append_unique_id(ids, edge.object_id);
	}
	for (const RectangleSelectionVertex vertex : selection.selected_vertices) {
		rectangle_append_unique_id(ids, vertex.object_id);
	}
	return ids;
}

RectangleSelectionState gpu_feedback_selection(const RectangleSelectionState& selection)
{
	SelectionFeedbackRequest request;
	request.current_selection = selection;
	return gpu_feedback_selection(request);
}

RectangleSelectionState gpu_feedback_selection(const SelectionFeedbackRequest& request)
{
	RectangleSelectionState selection = request.current_selection;
	std::vector<int> feedback_ids;
        if (selection.mode != SelectionMode::Face) {
          for (const int object_id : selection.selected_object_ids) {
            rectangle_append_unique_id(feedback_ids, object_id);
          }
        }

        if (selection.mode == SelectionMode::Edge ||
            selection.mode == SelectionMode::Vertex) {
          for (const int object_id : selection.selected_edge_full_object_ids) {
            rectangle_append_unique_id(feedback_ids, object_id);
          }
          for (const int object_id : request.compact_edge_object_ids) {
            rectangle_append_unique_id(feedback_ids, object_id);
          }
          for (const int object_id :
               selection.selected_vertex_full_object_ids) {
            rectangle_append_unique_id(feedback_ids, object_id);
          }
          rectangle_append_unique_id(feedback_ids, selection.active_object_id);
          for (const RectangleSelectionEdge edge : selection.selected_edges) {
            rectangle_append_unique_id(feedback_ids, edge.object_id);
          }
          for (const RectangleSelectionVertex vertex :
               selection.selected_vertices) {
            rectangle_append_unique_id(feedback_ids, vertex.object_id);
          }
          selection.selected_faces.clear();
          selection.selected_edges.clear();
          selection.selected_vertices.clear();
          selection.active_vertex_indices.clear();
          selection.active_edge_vertex_indices.clear();
          selection.selected_edge_full_object_ids.clear();
          selection.selected_vertex_full_object_ids.clear();
          selection.mode = SelectionMode::Object;
        }

        selection.selected_object_ids = feedback_ids;
	return selection;
}

RectangleSelectionState gpu_wire_feedback_selection(const RectangleSelectionState& selection)
{
	SelectionFeedbackRequest request;
	request.current_selection = selection;
	return gpu_wire_feedback_selection(request);
}

RectangleSelectionState gpu_wire_feedback_selection(const SelectionFeedbackRequest& request)
{
	return gpu_feedback_selection(request);
}

RectangleSelectionState preview_element_handle_mesh_selection(const RectangleSelectionState& selection)
{
	SelectionFeedbackRequest request;
	request.current_selection = selection;
	return preview_element_handle_mesh_selection(request);
}

RectangleSelectionState preview_element_handle_mesh_selection(const SelectionFeedbackRequest& request)
{
	RectangleSelectionState selection = request.current_selection;
	std::vector<int> target_ids = dirty_object_ids_for_selection(request.current_selection);
	for (const int object_id : request.compact_edge_object_ids) {
		rectangle_append_unique_id(target_ids, object_id);
	}
	for (const int object_id : request.preview_changed_object_ids) {
		rectangle_append_unique_id(target_ids, object_id);
	}
        selection.selected_object_ids = selection.mode == SelectionMode::Face
                                            ? std::vector<int>()
                                            : target_ids;
        selection.active_object_id = -1;
	selection.active_face_index = -1;
        if (selection.mode != SelectionMode::Vertex) {
          selection.active_vertex_indices.clear();
          selection.selected_vertex_full_object_ids.clear();
          selection.selected_vertices.clear();
        }
        if (selection.mode != SelectionMode::Edge) {
          selection.active_edge_vertex_indices.clear();
          selection.selected_edge_full_object_ids.clear();
          selection.selected_edges.clear();
        }
        return selection;
}

RectangleSelectionState without_cpu_element_handle_overlay_selection(const RectangleSelectionState& selection)
{
	RectangleSelectionState projection = selection;
	projection.active_object_id = -1;
	projection.active_face_index = -1;
	projection.active_vertex_indices.clear();
	projection.active_edge_vertex_indices.clear();
	projection.selected_edges.clear();
	projection.selected_vertices.clear();
	projection.selected_edge_full_object_ids.clear();
	projection.selected_vertex_full_object_ids.clear();
	return projection;
}

RectangleSelectionState empty_feedback_selection(const RectangleSelectionState& selection)
{
	RectangleSelectionState projection = selection;
        projection.mode = SelectionMode::Object;
        projection.active_object_id = -1;
	projection.active_face_index = -1;
	projection.selected_object_ids.clear();
	projection.selected_faces.clear();
	projection.selected_edges.clear();
	projection.selected_vertices.clear();
	projection.active_vertex_indices.clear();
	projection.active_edge_vertex_indices.clear();
	projection.selected_edge_full_object_ids.clear();
	projection.selected_vertex_full_object_ids.clear();
	return projection;
}

RectangleSelectionState face_feedback_selection(const RectangleSelectionState& selection)
{
  if (selection.mode == SelectionMode::Edge ||
      selection.mode == SelectionMode::Vertex) {
    return empty_feedback_selection(selection);
  }
        return gpu_feedback_selection(selection);
}

RectangleSelectionState visual_face_feedback_selection(const SelectionFeedbackRequest& request)
{
  if (request.current_selection.mode == SelectionMode::Edge ||
      request.current_selection.mode == SelectionMode::Vertex) {
    return empty_feedback_selection(request.current_selection);
  }
        return gpu_feedback_selection(request);
}

bool selection_face_feedback_should_xray(const RectangleSelectionState& selection, const std::vector<SelectionObjectFaceCount>& object_face_counts)
{
  if (selection.mode == SelectionMode::Object) {
    return !selection.selected_object_ids.empty();
  }
  if (selection.mode != SelectionMode::Face ||
      selection.selected_faces.empty()) {
    return false;
  }

        std::unordered_map<int, int> total_faces_by_object;
	total_faces_by_object.reserve(object_face_counts.size());
	for (const SelectionObjectFaceCount count : object_face_counts) {
		if (count.object_id >= 0 && count.face_count > 0) {
			total_faces_by_object[count.object_id] = count.face_count;
		}
	}

	std::unordered_map<int, int> selected_faces_by_object;
	selected_faces_by_object.reserve(selection.selected_faces.size());
	for (const RectangleSelectionFace face : selection.selected_faces) {
		if (!rectangle_face_is_valid(face)) {
			return false;
		}
		selected_faces_by_object[face.object_id]++;
	}

	for (const auto& selected_count : selected_faces_by_object) {
		const auto found_total = total_faces_by_object.find(selected_count.first);
		if (found_total == total_faces_by_object.end() || selected_count.second < found_total->second) {
			return false;
		}
	}
	return true;
}

bool selection_face_feedback_should_xray(const SelectionFaceXrayRequest& request)
{
	return selection_face_feedback_should_xray(request.selection, request.object_face_counts);
}

bool overlay_edge_is_valid(SelectionOverlayEdge edge)
{
	return edge.object_id >= 0 && edge.a_index >= 0 && edge.b_index >= 0;
}

SelectionOverlayEdge normalized_overlay_edge(SelectionOverlayEdge edge)
{
	if (edge.a_index > edge.b_index) {
		std::swap(edge.a_index, edge.b_index);
	}
	return edge;
}

void overlay_append_unique_face(std::vector<RectangleSelectionFace>& faces, RectangleSelectionFace face)
{
	if (!rectangle_face_is_valid(face)) {
		return;
	}
	const RectangleFaceKey key = rectangle_face_key(face);
	const auto found = std::find_if(faces.begin(), faces.end(), [key](RectangleSelectionFace existing) {
		return rectangle_face_key(existing) == key;
	});
	if (found == faces.end()) {
		faces.push_back(face);
	}
}

void overlay_append_unique_edge(std::vector<SelectionOverlayEdge>& edges, SelectionOverlayEdge edge)
{
	edge = normalized_overlay_edge(edge);
	if (!overlay_edge_is_valid(edge)) {
		return;
	}
	const auto found = std::find_if(edges.begin(), edges.end(), [edge](SelectionOverlayEdge existing) {
		existing = normalized_overlay_edge(existing);
		return existing.object_id == edge.object_id && existing.a_index == edge.a_index && existing.b_index == edge.b_index;
	});
	if (found == edges.end()) {
		edges.push_back(edge);
	}
}

void overlay_append_unique_vertex(std::vector<RectangleSelectionVertex>& vertices, RectangleSelectionVertex vertex)
{
	if (!rectangle_vertex_is_valid(vertex)) {
		return;
	}
	const auto found = std::find_if(vertices.begin(), vertices.end(), [vertex](RectangleSelectionVertex existing) {
		return existing.object_id == vertex.object_id && existing.vertex_index == vertex.vertex_index;
	});
	if (found == vertices.end()) {
		vertices.push_back(vertex);
	}
}

bool selection_has_overlay_element_entries(const RectangleSelectionState& selection)
{
	return !selection.selected_faces.empty() ||
		!selection.selected_edges.empty() ||
		!selection.selected_vertices.empty() ||
		!selection.active_vertex_indices.empty() ||
		!selection.active_edge_vertex_indices.empty() ||
		!selection.selected_edge_full_object_ids.empty() ||
		!selection.selected_vertex_full_object_ids.empty();
}

bool selection_has_center_element_entries(const RectangleSelectionState& selection)
{
	return !selection.selected_edges.empty() ||
		!selection.selected_vertices.empty() ||
		!selection.selected_edge_full_object_ids.empty() ||
		!selection.selected_vertex_full_object_ids.empty();
}

std::vector<int> object_operation_object_ids_for_selection(const RectangleSelectionState& selection)
{
	std::vector<int> object_ids = selection.selected_object_ids;
	for (const int object_id : selection.selected_edge_full_object_ids) {
		rectangle_append_unique_id(object_ids, object_id);
	}
	for (const int object_id : selection.selected_vertex_full_object_ids) {
		rectangle_append_unique_id(object_ids, object_id);
	}
	if (!selection.selected_edge_full_object_ids.empty() || !selection.selected_vertex_full_object_ids.empty()) {
		rectangle_append_unique_id(object_ids, selection.active_object_id);
		return object_ids;
	}
	if (!object_ids.empty() && !selection_has_overlay_element_entries(selection)) {
		if (selection.active_object_id < 0 || rectangle_contains_id(object_ids, selection.active_object_id)) {
			return object_ids;
		}
	}
	for (const RectangleSelectionFace face : selection.selected_faces) {
		rectangle_append_unique_id(object_ids, face.object_id);
	}
	for (const RectangleSelectionEdge edge : selection.selected_edges) {
		rectangle_append_unique_id(object_ids, edge.object_id);
	}
	for (const RectangleSelectionVertex vertex : selection.selected_vertices) {
		rectangle_append_unique_id(object_ids, vertex.object_id);
	}
	rectangle_append_unique_id(object_ids, selection.active_object_id);
	return object_ids;
}

SelectionCenterPlan plan_selection_center(const SelectionCenterPlanRequest& request)
{
	SelectionCenterPlan plan;
	plan.normalized_selection = request.current_selection;
	plan.has_element_entries = selection_has_center_element_entries(plan.normalized_selection);
        if ((plan.normalized_selection.mode == SelectionMode::Edge ||
             plan.normalized_selection.mode == SelectionMode::Vertex) &&
            !plan.normalized_selection.selected_object_ids.empty() &&
            plan.has_element_entries) {
          plan.ignored_selected_objects_for_element_entries = true;
          plan.normalized_selection.selected_object_ids.clear();
        }

        const RectangleSelectionState& selection = plan.normalized_selection;
	const bool object_only_selection = !selection.selected_object_ids.empty() &&
		selection.selected_faces.empty() &&
		selection.selected_edges.empty() &&
		selection.selected_vertices.empty() &&
		selection.selected_edge_full_object_ids.empty() &&
		selection.selected_vertex_full_object_ids.empty();
	if (object_only_selection) {
          plan.strategy = SelectionCenterStrategy::SelectedObjects;
          plan.object_ids = selection.selected_object_ids;
          return plan;
	}

	if (selection.selected_object_ids.empty() && selection.selected_faces.empty()) {
		const bool use_vertex_full_objects = !selection.selected_vertex_full_object_ids.empty();
		const std::vector<int>& full_object_ids = use_vertex_full_objects ? selection.selected_vertex_full_object_ids : selection.selected_edge_full_object_ids;
		const int references_per_object = use_vertex_full_objects ? request.vertex_references_per_full_object : request.edge_references_per_full_object;
		plan.dense_reference_count = static_cast<int>(full_object_ids.size()) * references_per_object;
		if (!full_object_ids.empty() && plan.dense_reference_count > request.dense_center_reference_limit) {
                  plan.strategy = SelectionCenterStrategy::DenseFullObjects;
                  plan.object_ids = full_object_ids;
                  return plan;
		}
	}

	if (selection.selected_object_ids.empty() &&
			selection.selected_faces.empty() &&
			(!selection.selected_edges.empty() ||
					!selection.selected_vertices.empty() ||
					!selection.selected_edge_full_object_ids.empty() ||
					!selection.selected_vertex_full_object_ids.empty())) {
          plan.strategy = SelectionCenterStrategy::SparseElements;
          return plan;
	}

        plan.strategy = SelectionCenterStrategy::FallbackPoints;
        return plan;
}

SelectionOverlayLookup build_selection_overlay_lookup(const RectangleSelectionState& selection)
{
	SelectionOverlayLookupRequest request;
	request.current_selection = selection;
	return build_selection_overlay_lookup(request);
}

SelectionOverlayLookup build_selection_overlay_lookup(const SelectionOverlayLookupRequest& request)
{
	const RectangleSelectionState& selection = request.current_selection;
	SelectionOverlayLookup lookup;

	for (const int object_id : selection.selected_object_ids) {
          if (selection.mode == SelectionMode::Object) {
            rectangle_append_unique_id(lookup.selected_object_ids, object_id);
          }
                rectangle_append_unique_id(lookup.target_object_ids, object_id);
	}
	for (const int object_id : request.extra_target_object_ids) {
		rectangle_append_unique_id(lookup.target_object_ids, object_id);
	}
	for (const RectangleSelectionFace face : selection.selected_faces) {
		overlay_append_unique_face(lookup.selected_faces, face);
		rectangle_append_unique_id(lookup.target_object_ids, face.object_id);
	}

        if (selection.mode == SelectionMode::Edge) {
          for (const RectangleSelectionEdge edge : selection.selected_edges) {
            overlay_append_unique_edge(
                lookup.selected_edges,
                {edge.object_id, edge.a_index, edge.b_index});
            rectangle_append_unique_id(lookup.target_object_ids,
                                       edge.object_id);
          }
          if (selection.active_object_id >= 0 &&
              selection.active_edge_vertex_indices.size() >= 2) {
            overlay_append_unique_edge(
                lookup.selected_edges,
                {selection.active_object_id,
                 selection.active_edge_vertex_indices[0],
                 selection.active_edge_vertex_indices[1]});
            rectangle_append_unique_id(lookup.target_object_ids,
                                       selection.active_object_id);
          }
          for (const int object_id : selection.selected_edge_full_object_ids) {
            rectangle_append_unique_id(lookup.selected_object_ids, object_id);
            rectangle_append_unique_id(lookup.target_object_ids, object_id);
          }
        }

        if (selection.mode == SelectionMode::Vertex) {
          for (const RectangleSelectionVertex vertex :
               selection.selected_vertices) {
            overlay_append_unique_vertex(lookup.selected_vertices, vertex);
            rectangle_append_unique_id(lookup.target_object_ids,
                                       vertex.object_id);
          }
          if (selection.active_object_id >= 0) {
            for (const int vertex_index : selection.active_vertex_indices) {
              overlay_append_unique_vertex(
                  lookup.selected_vertices,
                  {selection.active_object_id, vertex_index});
              rectangle_append_unique_id(lookup.target_object_ids,
                                         selection.active_object_id);
            }
          }
          for (const int object_id :
               selection.selected_vertex_full_object_ids) {
            rectangle_append_unique_id(lookup.selected_object_ids, object_id);
            rectangle_append_unique_id(lookup.target_object_ids, object_id);
          }
        }

        if (!lookup.target_object_ids.empty()) {
		return lookup;
	}
        if (selection.mode == SelectionMode::Edge ||
            selection.mode == SelectionMode::Vertex) {
          return lookup;
        }
        if (selection.active_object_id >= 0) {
		rectangle_append_unique_id(lookup.target_object_ids, selection.active_object_id);
		return lookup;
	}
        if (selection.mode == SelectionMode::Face) {
          return lookup;
        }
        rectangle_append_unique_id(lookup.target_object_ids, request.hovered_object_id);
	return lookup;
}

bool selection_overlay_lookup_has_selected_object(const SelectionOverlayLookup& lookup, int object_id)
{
	return rectangle_contains_id(lookup.selected_object_ids, object_id);
}

bool selection_overlay_lookup_targets_object(const SelectionOverlayLookup& lookup, int object_id)
{
	return rectangle_contains_id(lookup.target_object_ids, object_id);
}

bool selection_overlay_lookup_has_selected_face(const SelectionOverlayLookup& lookup, RectangleSelectionFace face)
{
	if (!rectangle_face_is_valid(face)) {
		return false;
	}
	const RectangleFaceKey key = rectangle_face_key(face);
	return std::find_if(lookup.selected_faces.begin(), lookup.selected_faces.end(), [key](RectangleSelectionFace existing) {
		return rectangle_face_key(existing) == key;
	}) != lookup.selected_faces.end();
}

bool selection_overlay_lookup_has_selected_edge(const SelectionOverlayLookup& lookup, SelectionOverlayEdge edge)
{
	edge = normalized_overlay_edge(edge);
	if (!overlay_edge_is_valid(edge)) {
		return false;
	}
	return std::find_if(lookup.selected_edges.begin(), lookup.selected_edges.end(), [edge](SelectionOverlayEdge existing) {
		existing = normalized_overlay_edge(existing);
		return existing.object_id == edge.object_id && existing.a_index == edge.a_index && existing.b_index == edge.b_index;
	}) != lookup.selected_edges.end();
}

bool selection_overlay_lookup_has_selected_vertex(const SelectionOverlayLookup& lookup, RectangleSelectionVertex vertex)
{
	if (!rectangle_vertex_is_valid(vertex)) {
		return false;
	}
	return std::find_if(lookup.selected_vertices.begin(), lookup.selected_vertices.end(), [vertex](RectangleSelectionVertex existing) {
		return existing.object_id == vertex.object_id && existing.vertex_index == vertex.vertex_index;
	}) != lookup.selected_vertices.end();
}

int add_limited_selection_reference_count(int count, int add, int limit)
{
	if (add <= 0) {
		return count;
	}
	if (limit > 0 && count > limit) {
		return count;
	}
	if (limit > 0 && add > limit - count) {
		return limit + 1;
	}
	return count + add;
}

SelectionOverlayPolicy selection_overlay_policy(const SelectionOverlayPolicyRequest& request)
{
	const RectangleSelectionState& selection = request.current_selection;
	SelectionOverlayPolicy policy;
        policy.element_mode = selection.mode == SelectionMode::Edge ||
                              selection.mode == SelectionMode::Vertex;
        policy.suppress_selected_object_feedback = request.suppress_selected_object_overlay || request.suppress_selected_object_detail;

	policy.selected_vertex_reference_count = add_limited_selection_reference_count(
			policy.selected_vertex_reference_count,
			static_cast<int>(selection.selected_vertices.size()),
			request.selected_vertex_limit);
	policy.selected_vertex_reference_count = add_limited_selection_reference_count(
			policy.selected_vertex_reference_count,
			static_cast<int>(selection.active_vertex_indices.size()),
			request.selected_vertex_limit);
	policy.selected_vertex_reference_count = add_limited_selection_reference_count(
			policy.selected_vertex_reference_count,
			static_cast<int>(selection.selected_vertex_full_object_ids.size()) * request.vertex_references_per_full_object,
			request.selected_vertex_limit);

	policy.selected_edge_reference_count = add_limited_selection_reference_count(
			policy.selected_edge_reference_count,
			static_cast<int>(selection.selected_edges.size()),
			request.selected_edge_limit);
	if (selection.active_edge_vertex_indices.size() >= 2) {
		policy.selected_edge_reference_count = add_limited_selection_reference_count(
				policy.selected_edge_reference_count,
				1,
				request.selected_edge_limit);
	}
	policy.selected_edge_reference_count = add_limited_selection_reference_count(
			policy.selected_edge_reference_count,
			static_cast<int>(selection.selected_edge_full_object_ids.size()) * request.edge_references_per_full_object,
			request.selected_edge_limit);

        const int element_limit = selection.mode == SelectionMode::Vertex
                                      ? request.selected_vertex_limit
                                      : request.selected_edge_limit;
        const int references_per_object =
            selection.mode == SelectionMode::Edge
                ? request.edge_references_per_full_object
                : request.vertex_references_per_full_object;
        if (policy.element_mode) {
		policy.element_target_reference_count = add_limited_selection_reference_count(
				policy.element_target_reference_count,
				static_cast<int>(selection.selected_object_ids.size()) * references_per_object,
				element_limit);
                if (selection.mode == SelectionMode::Vertex) {
                  policy.element_target_reference_count =
                      add_limited_selection_reference_count(
                          policy.element_target_reference_count,
                          static_cast<int>(
                              selection.selected_vertex_full_object_ids
                                  .size()) *
                              request.vertex_references_per_full_object,
                          element_limit);
                } else {
                  policy.element_target_reference_count =
                      add_limited_selection_reference_count(
                          policy.element_target_reference_count,
                          static_cast<int>(
                              selection.selected_edge_full_object_ids.size()) *
                              request.edge_references_per_full_object,
                          element_limit);
                }
                if (policy.element_target_reference_count == 0 && selection.active_object_id >= 0) {
			policy.element_target_reference_count = add_limited_selection_reference_count(
					policy.element_target_reference_count,
					references_per_object,
					element_limit);
		}
	}
        policy.dense_selected_vertex_overlay =
            selection.mode == SelectionMode::Vertex &&
            (policy.selected_vertex_reference_count >
                 request.selected_vertex_limit ||
             policy.element_target_reference_count >
                 request.selected_vertex_limit);
        policy.dense_selected_edge_overlay =
            selection.mode == SelectionMode::Edge &&
            (policy.selected_edge_reference_count >
                 request.selected_edge_limit ||
             policy.element_target_reference_count >
                 request.selected_edge_limit);
        policy.dense_element_minimal_overlay = policy.dense_selected_vertex_overlay || policy.dense_selected_edge_overlay;
        policy.should_build_lookup =
            !policy.dense_element_minimal_overlay &&
            !(policy.suppress_selected_object_feedback &&
              selection.mode == SelectionMode::Object);
        return policy;
}

SelectionSourceWireSuppression source_wire_suppression_for_selection(const RectangleSelectionState& selection)
{
	SelectionSourceWireSuppression suppression;
        if (selection.mode == SelectionMode::Object) {
          for (const int object_id : selection.selected_object_ids) {
            rectangle_append_unique_id(suppression.hidden_object_ids,
                                       object_id);
          }
          return suppression;
        }
        if (selection.mode == SelectionMode::Face) {
          for (const RectangleSelectionFace face : selection.selected_faces) {
            overlay_append_unique_face(suppression.hidden_faces, face);
          }
          return suppression;
        }
        if (selection.mode == SelectionMode::Edge) {
          for (const int object_id : selection.selected_edge_full_object_ids) {
            rectangle_append_unique_id(suppression.hidden_object_ids,
                                       object_id);
          }
          for (const RectangleSelectionEdge edge : selection.selected_edges) {
            overlay_append_unique_edge(
                suppression.hidden_edges,
                {edge.object_id, edge.a_index, edge.b_index});
          }
          if (selection.active_object_id >= 0 &&
              selection.active_edge_vertex_indices.size() >= 2) {
            overlay_append_unique_edge(
                suppression.hidden_edges,
                {selection.active_object_id,
                 selection.active_edge_vertex_indices[0],
                 selection.active_edge_vertex_indices[1]});
          }
        }
        return suppression;
}

} // namespace quader_editor_core
