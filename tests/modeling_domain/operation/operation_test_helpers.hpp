////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "test_support.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

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

/**
 * Stores an open-boundary fan loop selection fixture.
 */
struct BoundaryFanLoopFixture {
	Document document;
	Edge seed;
	std::vector<Edge> expected_loop_edges;
	std::vector<Edge> disconnected_fan_edges;
};

/**
 * Stores a shared-corner edge-loop selection fixture.
 */
struct ManifoldCornerLoopFixture {
	Document document;
	ElementId top_face = kInvalidElementId;
	ElementId side_face = kInvalidElementId;
	Edge seed;
	std::vector<Edge> expected_top_loop_edges;
	std::vector<Edge> side_only_edges;
};

/**
 * Stores a shared-lip edge-loop fixture with picked-face context.
 */
struct SharedLipLoopFixture {
	Document document;
	ElementId top_mid_face = kInvalidElementId;
	ElementId drop_face = kInvalidElementId;
	Edge seed;
	std::vector<Edge> expected_top_region_loop_edges;
	std::vector<Edge> excluded_edges;
};

/**
 * Stores an open-box boundary loop fixture with excluded face-perimeter edges.
 */
struct OpenBoxBoundaryLoopFixture {
	Document document;
	ElementId top_face = kInvalidElementId;
	Edge seed;
	std::vector<Edge> expected_open_side_loop_edges;
	std::vector<Edge> excluded_face_perimeter_edges;
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

inline BoundaryFanLoopFixture make_boundary_fan_loop_document()
{
	BoundaryFanLoopFixture fixture;
	const ElementId v0 = add_vertex(fixture.document, { 0.0F, 0.0F, 0.0F });
	const ElementId v1 = add_vertex(fixture.document, { 1.0F, 0.0F, 0.0F });
	const ElementId v2 = add_vertex(fixture.document, { 1.0F, 1.0F, 0.0F });
	const ElementId v3 = add_vertex(fixture.document, { 0.0F, 1.0F, 0.0F });
	const ElementId v4 = add_vertex(fixture.document, { 2.0F, 0.0F, 0.0F });
	const ElementId v5 = add_vertex(fixture.document, { 2.0F, 1.0F, 0.0F });

	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{v0, v1, v2, v3}) !=
	            kInvalidElementId);
	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{v1, v4, v5, v2}) !=
	            kInvalidElementId);

	const ElementId c1 = add_vertex(fixture.document, { 1.0F, -1.0F, 0.0F });
	const ElementId c2 = add_vertex(fixture.document, { 2.0F, -1.0F, 0.0F });
	const ElementId c3 = add_vertex(fixture.document, { 2.0F, -2.0F, 0.0F });
	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{v1, c1, c2, c3}) !=
	            kInvalidElementId);

	const ElementId d1 = add_vertex(fixture.document, { -1.0F, 0.0F, 0.0F });
	const ElementId d2 = add_vertex(fixture.document, { -1.0F, -1.0F, 0.0F });
	const ElementId d3 = add_vertex(fixture.document, { -2.0F, -1.0F, 0.0F });
	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{v0, d1, d2, d3}) !=
	            kInvalidElementId);

	fixture.seed = make_edge(v0, v1);
	fixture.expected_loop_edges = {
		make_edge(v0, v1),
		make_edge(v1, v4),
		make_edge(v4, v5),
		make_edge(v5, v2),
		make_edge(v2, v3),
		make_edge(v3, v0),
	};
	fixture.disconnected_fan_edges = {
		make_edge(v1, c1),
		make_edge(v1, c3),
		make_edge(v0, d1),
		make_edge(v0, d3),
	};
	return fixture;
}

inline ManifoldCornerLoopFixture make_manifold_corner_loop_document()
{
	ManifoldCornerLoopFixture fixture;
	const ElementId top_front_left =
		add_vertex(fixture.document, { -1.0F, -1.0F, 0.0F });
	const ElementId top_front_right =
		add_vertex(fixture.document, { 1.0F, -1.0F, 0.0F });
	const ElementId top_back_right =
		add_vertex(fixture.document, { 1.0F, 1.0F, 0.0F });
	const ElementId top_back_left =
		add_vertex(fixture.document, { -1.0F, 1.0F, 0.0F });
	const ElementId side_bottom_left =
		add_vertex(fixture.document, { -1.0F, -1.0F, -1.0F });
	const ElementId side_bottom_right =
		add_vertex(fixture.document, { 1.0F, -1.0F, -1.0F });

	fixture.top_face = add_face(
		fixture.document,
		std::array<ElementId, 4>{
			top_front_left,
			top_front_right,
			top_back_right,
			top_back_left,
		});
	EXPECT_TRUE(fixture.top_face != kInvalidElementId);
	fixture.side_face = add_face(
		fixture.document,
		std::array<ElementId, 4>{
			top_front_right,
			top_front_left,
			side_bottom_left,
			side_bottom_right,
		});
	EXPECT_TRUE(fixture.side_face != kInvalidElementId);

	fixture.seed = make_edge(top_front_left, top_front_right);
	fixture.expected_top_loop_edges = {
		make_edge(top_front_left, top_front_right),
		make_edge(top_front_right, top_back_right),
		make_edge(top_back_right, top_back_left),
		make_edge(top_back_left, top_front_left),
	};
	fixture.side_only_edges = {
		make_edge(top_front_left, side_bottom_left),
		make_edge(side_bottom_left, side_bottom_right),
		make_edge(side_bottom_right, top_front_right),
	};
	return fixture;
}

inline SharedLipLoopFixture make_shared_lip_loop_document()
{
	SharedLipLoopFixture fixture;
	const ElementId front0 = add_vertex(fixture.document, { 0.0F, 0.0F, 0.0F });
	const ElementId front1 = add_vertex(fixture.document, { 1.0F, 0.0F, 0.0F });
	const ElementId front2 = add_vertex(fixture.document, { 2.0F, 0.0F, 0.0F });
	const ElementId front3 = add_vertex(fixture.document, { 3.0F, 0.0F, 0.0F });
	const ElementId back0 = add_vertex(fixture.document, { 0.0F, 1.0F, 0.0F });
	const ElementId back1 = add_vertex(fixture.document, { 1.0F, 1.0F, 0.0F });
	const ElementId back2 = add_vertex(fixture.document, { 2.0F, 1.0F, 0.0F });
	const ElementId back3 = add_vertex(fixture.document, { 3.0F, 1.0F, 0.0F });

	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{
	                        front0,
	                        front1,
	                        back1,
	                        back0,
	                    }) != kInvalidElementId);
	fixture.top_mid_face = add_face(
		fixture.document,
		std::array<ElementId, 4>{
			front1,
			front2,
			back2,
			back1,
		});
	EXPECT_TRUE(fixture.top_mid_face != kInvalidElementId);
	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{
	                        front2,
	                        front3,
	                        back3,
	                        back2,
	                    }) != kInvalidElementId);

	const ElementId drop1 = add_vertex(fixture.document, { 1.0F, 0.0F, -1.0F });
	const ElementId drop2 = add_vertex(fixture.document, { 2.0F, 0.0F, -1.0F });
	fixture.drop_face = add_face(
		fixture.document,
		std::array<ElementId, 4>{
			front2,
			front1,
			drop1,
			drop2,
		});
	EXPECT_TRUE(fixture.drop_face != kInvalidElementId);

	const ElementId right_front = add_vertex(fixture.document, { 3.0F, 0.0F, -1.0F });
	const ElementId right_back = add_vertex(fixture.document, { 3.0F, 1.0F, -1.0F });
	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{
	                        front3,
	                        right_front,
	                        right_back,
	                        back3,
	                    }) != kInvalidElementId);

	fixture.seed = make_edge(front1, front2);
	fixture.expected_top_region_loop_edges = {
		make_edge(front0, front1),
		make_edge(front1, front2),
		make_edge(front2, front3),
		make_edge(front3, back3),
		make_edge(back3, back2),
		make_edge(back2, back1),
		make_edge(back1, back0),
		make_edge(back0, front0),
	};
	fixture.excluded_edges = {
		make_edge(front1, back1),
		make_edge(front2, back2),
		make_edge(front1, drop1),
		make_edge(drop1, drop2),
		make_edge(drop2, front2),
		make_edge(front3, right_front),
		make_edge(right_front, right_back),
		make_edge(right_back, back3),
	};
	return fixture;
}

inline OpenBoxBoundaryLoopFixture make_open_box_boundary_loop_document()
{
	OpenBoxBoundaryLoopFixture fixture;
	const ElementId bottom_front_left =
		add_vertex(fixture.document, { -8.0F, 0.0F, -4.0F });
	const ElementId bottom_back_left =
		add_vertex(fixture.document, { -8.0F, 0.0F, 4.0F });
	const ElementId bottom_back_open =
		add_vertex(fixture.document, { 4.0F, 0.0F, 4.0F });
	const ElementId bottom_front_open =
		add_vertex(fixture.document, { 4.0F, 0.0F, -4.0F });
	const ElementId top_front_left =
		add_vertex(fixture.document, { -8.0F, 1.0F, -4.0F });
	const ElementId top_back_left =
		add_vertex(fixture.document, { -8.0F, 1.0F, 4.0F });
	const ElementId top_back_open =
		add_vertex(fixture.document, { 4.0F, 1.0F, 4.0F });
	const ElementId top_front_open =
		add_vertex(fixture.document, { 4.0F, 1.0F, -4.0F });

	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{
	                        bottom_front_open,
	                        bottom_back_open,
	                        bottom_back_left,
	                        bottom_front_left,
	                    }) != kInvalidElementId);
	fixture.top_face = add_face(
		fixture.document,
		std::array<ElementId, 4>{
			top_front_left,
			top_back_left,
			top_back_open,
			top_front_open,
		});
	EXPECT_TRUE(fixture.top_face != kInvalidElementId);
	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{
	                        bottom_back_left,
	                        top_back_left,
	                        top_front_left,
	                        bottom_front_left,
	                    }) != kInvalidElementId);
	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{
	                        bottom_back_open,
	                        top_back_open,
	                        top_back_left,
	                        bottom_back_left,
	                    }) != kInvalidElementId);
	EXPECT_TRUE(add_face(fixture.document,
	                    std::array<ElementId, 4>{
	                        bottom_front_left,
	                        top_front_left,
	                        top_front_open,
	                        bottom_front_open,
	                    }) != kInvalidElementId);

	fixture.seed = make_edge(top_back_open, top_front_open);
	fixture.expected_open_side_loop_edges = {
		make_edge(top_back_open, top_front_open),
		make_edge(bottom_back_open, top_back_open),
		make_edge(bottom_back_open, bottom_front_open),
		make_edge(bottom_front_open, top_front_open),
	};
	fixture.excluded_face_perimeter_edges = {
		make_edge(top_front_left, top_back_left),
		make_edge(top_back_left, top_back_open),
		make_edge(top_front_left, top_front_open),
	};
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

inline bool contains_element_id(const std::vector<ElementId>& ids, ElementId id)
{
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

inline bool contains_edge(const std::vector<Edge>& edges, Edge edge)
{
	const Edge normalized = make_edge(edge.a, edge.b);
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
