////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "test_support.hpp"

#include <mesh/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace qdr_test {

using namespace quader_poly;

/**
 * Stores the Quad Fixture data contract used by the automated test suite.
 */
struct QuadFixture {
	Document document;
        ElementId bottom_left = kInvalidElementId;
        ElementId bottom_right = kInvalidElementId;
        ElementId top_right = kInvalidElementId;
        ElementId top_left = kInvalidElementId;
        ElementId face = kInvalidElementId;
};

inline bool vec_nearly_equal(quader::QVec3 left, quader::QVec3 right, float epsilon = 0.0001F)
{
	const bool matches = nearly_equal(left.x, right.x, epsilon) &&
		nearly_equal(left.y, right.y, epsilon) &&
		nearly_equal(left.z, right.z, epsilon);
	return matches;
}

inline bool vec2_nearly_equal(quader::QVec2 left, quader::QVec2 right, float epsilon = 0.0001F)
{
	return nearly_equal(left.x, right.x, epsilon) && nearly_equal(left.y, right.y, epsilon);
}

inline QuadFixture make_quad_document(std::uint32_t material_slot = 0)
{
	QuadFixture fixture;
	fixture.bottom_left = add_vertex(fixture.document, { -1.0F, -1.0F, 0.0F });
	fixture.bottom_right = add_vertex(fixture.document, { 1.0F, -1.0F, 0.0F });
	fixture.top_right = add_vertex(fixture.document, { 1.0F, 1.0F, 0.0F });
	fixture.top_left = add_vertex(fixture.document, { -1.0F, 1.0F, 0.0F });
	const std::array<ElementId, 4> vertices {
		fixture.bottom_left,
		fixture.bottom_right,
		fixture.top_right,
		fixture.top_left,
	};
	fixture.face = add_face(fixture.document, vertices, material_slot);
        if (fixture.face == kInvalidElementId) {
          ADD_FAILURE() << "Quad fixture face was not created";
        }
        return fixture;
}

inline Selection vertex_selection(std::initializer_list<ElementId> vertices,
                                  ElementId active = kInvalidElementId) {
  Selection selection;
  selection.mode = SelectionMode::Vertex;
  selection.vertices.assign(vertices.begin(), vertices.end());
  if (active != kInvalidElementId) {
    selection.has_active = true;
    selection.active_kind = ElementKind::Vertex;
    selection.active_vertex = active;
  }
  return selection;
}

inline Selection edge_selection(std::initializer_list<Edge> edges, std::optional<Edge> active = std::nullopt)
{
	Selection selection;
        selection.mode = SelectionMode::Edge;
        selection.edges.assign(edges.begin(), edges.end());
	if (active.has_value()) {
		selection.has_active = true;
                selection.active_kind = ElementKind::Edge;
                selection.active_edge = *active;
	}
	return selection;
}

inline Selection face_selection(std::initializer_list<ElementId> faces,
                                ElementId active = kInvalidElementId) {
  Selection selection;
  selection.mode = SelectionMode::Face;
  selection.faces.assign(faces.begin(), faces.end());
  if (active != kInvalidElementId) {
    selection.has_active = true;
    selection.active_kind = ElementKind::Face;
    selection.active_face = active;
  }
  return selection;
}

inline Selection all_face_selection(const Document& document)
{
	Selection selection;
        selection.mode = SelectionMode::Face;
        for (const Face& face : document.faces) {
		selection.faces.push_back(face.id);
	}
	if (!selection.faces.empty()) {
		selection.has_active = true;
                selection.active_kind = ElementKind::Face;
                selection.active_face = selection.faces.back();
	}
	return selection;
}

inline quader::QVec3 face_centroid(const Document& document, const Face& face)
{
	quader::QVec3 center;
	std::size_t count = 0;
	for (ElementId vertex_id : face.vertices) {
		const Vertex* vertex = find_vertex(document, vertex_id);
		if (vertex == nullptr) {
			continue;
		}
		center += vertex->position;
		++count;
	}
	return count == 0U ? quader::QVec3 {} : center / static_cast<float>(count);
}

inline quader::QVec3 document_vertex_centroid(const Document& document)
{
	quader::QVec3 center;
	for (const Vertex& vertex : document.vertices) {
		center += vertex.position;
	}
	return document.vertices.empty() ? center : center / static_cast<float>(document.vertices.size());
}

inline bool all_faces_point_relative_to_center(const Document& document, bool outward)
{
	const quader::QVec3 center = document_vertex_centroid(document);
	return std::all_of(document.faces.begin(), document.faces.end(), [&document, center, outward](const Face& face) {
		const quader::QVec3 expected = face_centroid(document, face) - center;
		const float facing = dot(face_normal(document, face), expected);
		return outward ? facing > 0.0F : facing < 0.0F;
	});
}

inline bool face_has_repeated_vertices(const Face& face)
{
	for (auto vertex = face.vertices.begin(); vertex != face.vertices.end(); ++vertex) {
		if (std::find(std::next(vertex), face.vertices.end(), *vertex) != face.vertices.end()) {
			return true;
		}
	}
	return false;
}

inline bool face_contains_all_vertices(const Face& face, const std::vector<ElementId>& vertex_ids)
{
	return std::all_of(vertex_ids.begin(), vertex_ids.end(), [&face](ElementId vertex_id) {
		return std::find(face.vertices.begin(), face.vertices.end(), vertex_id) != face.vertices.end();
	});
}

inline bool face_vertices_match_cyclic(const Face& face, const std::vector<ElementId>& vertex_ids)
{
	if (face.vertices.size() != vertex_ids.size()) {
		return false;
	}
	if (vertex_ids.empty()) {
		return true;
	}

	for (std::size_t start = 0; start < face.vertices.size(); ++start) {
		bool matches = true;
		for (std::size_t index = 0; index < vertex_ids.size(); ++index) {
			if (face.vertices[(start + index) % face.vertices.size()] != vertex_ids[index]) {
				matches = false;
				break;
			}
		}
		if (matches) {
			return true;
		}
	}
	return false;
}

template <std::size_t Size>
bool face_contains_all_vertices(const Face& face, const std::array<ElementId, Size>& vertex_ids)
{
	return std::all_of(vertex_ids.begin(), vertex_ids.end(), [&face](ElementId vertex_id) {
		return std::find(face.vertices.begin(), face.vertices.end(), vertex_id) != face.vertices.end();
	});
}

inline int edge_incidence_count(const Document& document, Edge edge)
{
	edge = make_edge(edge.a, edge.b);
	int count = 0;
	for (const Face& face : document.faces) {
		for (std::size_t index = 0; index < face.vertices.size(); ++index) {
			if (make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]) == edge) {
				++count;
			}
		}
	}
	return count;
}

inline bool document_has_edge(const Document& document, Edge edge)
{
	const Edge normalized = make_edge(edge.a, edge.b);
	const std::vector<Edge> edges = document_edges(document);
	return std::find(edges.begin(), edges.end(), normalized) != edges.end();
}

inline bool edge_positions_match_unordered(const Document& document, Edge edge, quader::QVec3 first_position, quader::QVec3 second_position)
{
	const Vertex* first = find_vertex(document, edge.a);
	const Vertex* second = find_vertex(document, edge.b);
	if (first == nullptr || second == nullptr) {
		return false;
	}
	return (vec_nearly_equal(first->position, first_position) && vec_nearly_equal(second->position, second_position)) ||
		(vec_nearly_equal(first->position, second_position) && vec_nearly_equal(second->position, first_position));
}

inline std::size_t open_edge_count(const Document& document)
{
	std::size_t count = 0;
	for (const Edge& edge : document_edges(document)) {
		if (edge_incidence_count(document, edge) == 1) {
			++count;
		}
	}
	return count;
}

inline bool every_face_compiles_to_triangles(const Document& document)
{
	const CompiledMesh mesh = compile_document(document);
	return mesh.primitives.size() == document.faces.size() &&
		std::all_of(mesh.primitives.begin(), mesh.primitives.end(), [](const CompiledPrimitive& primitive) {
			return primitive.index_count >= 3U && primitive.index_count % 3U == 0U;
		});
}

inline bool every_edge_is_closed(const Document& document)
{
	std::map<std::pair<ElementId, ElementId>, int> counts;
	for (const Face& face : document.faces) {
		for (std::size_t index = 0; index < face.vertices.size(); ++index) {
			const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
			++counts[{ edge.a, edge.b }];
		}
	}
	return !counts.empty() && std::all_of(counts.begin(), counts.end(), [](const auto& entry) {
		return entry.second == 2;
	});
}

inline bool shared_edges_have_opposite_winding(const Document& document)
{
	/**
	 * Represents an Edge Use value used by the automated test suite.
	 */
	struct EdgeUse {
          ElementId from = kInvalidElementId;
          ElementId to = kInvalidElementId;
        };

	std::map<std::pair<ElementId, ElementId>, std::vector<EdgeUse>> uses_by_edge;
	for (const Face& face : document.faces) {
		for (std::size_t index = 0; index < face.vertices.size(); ++index) {
			const ElementId from = face.vertices[index];
			const ElementId to = face.vertices[(index + 1U) % face.vertices.size()];
			const auto key = from < to ? std::pair { from, to } : std::pair { to, from };
			uses_by_edge[key].push_back({ from, to });
		}
	}

	return std::all_of(uses_by_edge.begin(), uses_by_edge.end(), [](const auto& entry) {
		const std::vector<EdgeUse>& uses = entry.second;
		if (uses.size() < 2U) {
			return true;
		}
		return uses.size() == 2U && uses[0].from == uses[1].to && uses[0].to == uses[1].from;
	});
}

inline bool face_has_vertical_span(const Document& document, const Face& face)
{
	bool has_y = false;
	float min_y = 0.0F;
	float max_y = 0.0F;
	for (ElementId vertex_id : face.vertices) {
		const Vertex* vertex = find_vertex(document, vertex_id);
		if (vertex == nullptr) {
			continue;
		}
		if (!has_y) {
			min_y = vertex->position.y;
			max_y = vertex->position.y;
			has_y = true;
		} else {
			min_y = std::min(min_y, vertex->position.y);
			max_y = std::max(max_y, vertex->position.y);
		}
	}
	return has_y && max_y - min_y > 0.001F;
}

inline Document make_parallel_quad_document(float separation = 2.0F)
{
	Document document;
	const ElementId a = add_vertex(document, { -1.0F, -1.0F, -separation });
	const ElementId b = add_vertex(document, { 1.0F, -1.0F, -separation });
	const ElementId c = add_vertex(document, { 1.0F, 1.0F, -separation });
	const ElementId d = add_vertex(document, { -1.0F, 1.0F, -separation });
	const ElementId e = add_vertex(document, { -1.0F, -1.0F, separation });
	const ElementId f = add_vertex(document, { 1.0F, -1.0F, separation });
	const ElementId g = add_vertex(document, { 1.0F, 1.0F, separation });
	const ElementId h = add_vertex(document, { -1.0F, 1.0F, separation });
        if (add_face(document, std::array<ElementId, 4>{a, b, c, d}) ==
            kInvalidElementId) {
          ADD_FAILURE() << "First parallel quad face was not created";
        }
        if (add_face(document, std::array<ElementId, 4>{h, g, f, e}) ==
            kInvalidElementId) {
          ADD_FAILURE() << "Second parallel quad face was not created";
        }
        return document;
}

} // namespace qdr_test
