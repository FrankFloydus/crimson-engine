////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"
#include "test_support.hpp"
#include <mesh/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace quader_poly;

bool nearly_equal(float left, float right, float epsilon = 0.0001F)
{
    return std::abs(left - right) <= epsilon;
}

bool vec_nearly_equal(quader::QVec3 left, quader::QVec3 right, float epsilon = 0.0001F)
{
    return nearly_equal(left.x, right.x, epsilon) &&
        nearly_equal(left.y, right.y, epsilon) &&
        nearly_equal(left.z, right.z, epsilon);
}

bool face_has_repeated_vertices(const Face& face)
{
    for (auto vertex = face.vertices.begin(); vertex != face.vertices.end(); ++vertex) {
        if (std::find(std::next(vertex), face.vertices.end(), *vertex) != face.vertices.end()) {
            return true;
        }
    }
    return false;
}

std::string open_edge_summary(const Document& document)
{
    std::map<std::pair<ElementId, ElementId>, int> counts;
    for (const Face& face : document.faces) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
            ++counts[{ edge.a, edge.b }];
        }
    }

    std::string summary;
    for (const auto& [edge, count] : counts) {
        if (count == 2) {
            continue;
        }
        if (!summary.empty()) {
            summary += "; ";
        }
        summary += std::to_string(edge.first);
        summary += "-";
        summary += std::to_string(edge.second);
        summary += " count=";
        summary += std::to_string(count);
        const Vertex* first = find_vertex(document, edge.first);
        const Vertex* second = find_vertex(document, edge.second);
        if (first != nullptr && second != nullptr) {
            summary += " [(";
            summary += std::to_string(first->position.x);
            summary += ",";
            summary += std::to_string(first->position.y);
            summary += ",";
            summary += std::to_string(first->position.z);
            summary += ")->(";
            summary += std::to_string(second->position.x);
            summary += ",";
            summary += std::to_string(second->position.y);
            summary += ",";
            summary += std::to_string(second->position.z);
            summary += ")]";
        }
    }
    return summary.empty() ? "no open edges" : summary;
}

std::string doorway_top_region_summary(const Document& document)
{
    std::string summary;
    summary += "vertices:";
    for (const Vertex& vertex : document.vertices) {
        if (vertex.position.x < 0.9F || vertex.position.y < 0.2F ||
            std::abs(vertex.position.z) > 0.55F) {
            continue;
        }
        summary += " ";
        summary += std::to_string(vertex.id);
        summary += "=(";
        summary += std::to_string(vertex.position.x);
        summary += ",";
        summary += std::to_string(vertex.position.y);
        summary += ",";
        summary += std::to_string(vertex.position.z);
        summary += ")";
    }

    summary += " faces:";
    for (const Face& face : document.faces) {
        bool include = false;
        for (const ElementId vertex_id : face.vertices) {
            const Vertex* vertex = find_vertex(document, vertex_id);
            if (vertex != nullptr && vertex->position.x >= 0.9F && vertex->position.y >= 0.2F &&
                std::abs(vertex->position.z) <= 0.55F) {
                include = true;
                break;
            }
        }
        if (!include) {
            continue;
        }
        summary += " f";
        summary += std::to_string(face.id);
        summary += "[";
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            if (index > 0U) {
                summary += ",";
            }
            summary += std::to_string(face.vertices[index]);
        }
        summary += "]";
    }
    return summary;
}

bool every_face_compiles_to_triangles(const Document& document)
{
    const CompiledMesh mesh = compile_document(document);
    return mesh.primitives.size() == document.faces.size() &&
        std::all_of(mesh.primitives.begin(), mesh.primitives.end(), [](const CompiledPrimitive& primitive) {
            return primitive.index_count >= 3 && primitive.index_count % 3U == 0U;
        });
}

bool every_edge_is_closed(const Document& document)
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

bool every_shared_edge_has_opposite_winding(const Document& document)
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

    return !uses_by_edge.empty() && std::all_of(uses_by_edge.begin(), uses_by_edge.end(), [](const auto& entry) {
        const std::vector<EdgeUse>& uses = entry.second;
        return uses.size() == 2U && uses[0].from == uses[1].to && uses[0].to == uses[1].from;
    });
}

bool has_vertex_near_position(const Document& document, quader::QVec3 position, float epsilon = 0.001F)
{
    return std::any_of(document.vertices.begin(), document.vertices.end(), [position, epsilon](const Vertex& vertex) {
        return vec_nearly_equal(vertex.position, position, epsilon);
    });
}

ElementId vertex_near_position(const Document& document, quader::QVec3 position, float epsilon = 0.001F)
{
    const auto vertex = std::find_if(document.vertices.begin(), document.vertices.end(), [position, epsilon](const Vertex& candidate) {
        return vec_nearly_equal(candidate.position, position, epsilon);
    });
    EXPECT_TRUE(vertex != document.vertices.end());
    return vertex->id;
}

quader::QVec3 face_centroid(const Document& document, const Face& face)
{
    quader::QVec3 center;
    std::size_t count = 0;
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex != nullptr) {
            center += vertex->position;
            ++count;
        }
    }
    return count == 0U ? quader::QVec3 {} : center / static_cast<float>(count);
}

quader::QVec3 document_vertex_centroid(const Document& document)
{
    quader::QVec3 center;
    for (const Vertex& vertex : document.vertices) {
        center += vertex.position;
    }
    return document.vertices.empty() ? center : center / static_cast<float>(document.vertices.size());
}

bool all_faces_point_relative_to_center(const Document& document)
{
    const quader::QVec3 center = document_vertex_centroid(document);
    return std::all_of(document.faces.begin(), document.faces.end(), [&document, center](const Face& face) {
        const quader::QVec3 expected = face_centroid(document, face) - center;
        return length_squared(expected) <= 0.000001F || dot(face_normal(document, face), expected) > 0.0F;
    });
}

Selection edge_selection_for_edge(Edge edge)
{
    Selection selection;
    selection.mode = SelectionMode::Edge;
    selection.edges = { edge };
    selection.has_active = true;
    selection.active_kind = ElementKind::Edge;
    selection.active_edge = edge;
    return selection;
}

Selection top_face_loop_selection(const Document& document)
{
    const auto top_face = std::find_if(document.faces.begin(), document.faces.end(), [&document](const Face& face) {
        return vec_nearly_equal(face_normal(document, face), { 0.0F, 1.0F, 0.0F }, 0.001F);
    });
    EXPECT_TRUE(top_face != document.faces.end());

    Selection selection;
    selection.mode = SelectionMode::Edge;
    for (std::size_t index = 0; index < top_face->vertices.size(); ++index) {
        selection.edges.push_back(make_edge(top_face->vertices[index], top_face->vertices[(index + 1U) % top_face->vertices.size()]));
    }
    return selection;
}

std::pair<ElementId, ElementId> edge_pair_key(Edge edge)
{
    return edge.a < edge.b ? std::pair { edge.a, edge.b } : std::pair { edge.b, edge.a };
}

Selection all_document_edge_selection(const Document& document)
{
    std::set<std::pair<ElementId, ElementId>> seen_edges;
    Selection selection;
    selection.mode = SelectionMode::Edge;
    for (const Face& face : document.faces) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
            if (seen_edges.insert(edge_pair_key(edge)).second) {
                selection.edges.push_back(edge);
            }
        }
    }
    if (!selection.edges.empty()) {
        selection.has_active = true;
        selection.active_kind = ElementKind::Edge;
        selection.active_edge = selection.edges.front();
    }
    return selection;
}

int edge_use_count(const Document& document, Edge expected_edge)
{
    const std::pair expected_key = edge_pair_key(expected_edge);
    int count = 0;
    for (const Face& face : document.faces) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
            if (edge_pair_key(edge) == expected_key) {
                ++count;
            }
        }
    }
    return count;
}

int edge_use_count_between_positions(const Document& document, quader::QVec3 first, quader::QVec3 second, float epsilon = 0.001F)
{
    return edge_use_count(
        document,
        make_edge(
            vertex_near_position(document, first, epsilon),
            vertex_near_position(document, second, epsilon)));
}

std::vector<ElementId> neighbor_vertices_for(const Document& document, ElementId vertex_id)
{
    std::set<ElementId> neighbors;
    for (const Face& face : document.faces) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            if (face.vertices[index] != vertex_id) {
                continue;
            }
            neighbors.insert(face.vertices[(index + face.vertices.size() - 1U) % face.vertices.size()]);
            neighbors.insert(face.vertices[(index + 1U) % face.vertices.size()]);
        }
    }
    return { neighbors.begin(), neighbors.end() };
}

std::vector<ElementId> vertices_near_spoke_direction(
    const Document& document,
    std::span<const ElementId> vertex_ids,
    quader::QVec3 origin,
    quader::QVec3 direction,
    float max_perpendicular_distance,
    float max_signed_distance)
{
    std::vector<ElementId> matching;
    const quader::QVec3 axis = normalize_or_zero(direction);
    EXPECT_TRUE(length_squared(axis) > 0.0F);
    for (const ElementId vertex_id : vertex_ids) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        EXPECT_TRUE(vertex != nullptr);
        const quader::QVec3 relative_position = vertex->position - origin;
        const float signed_distance = dot(relative_position, axis);
        const quader::QVec3 perpendicular = relative_position - (axis * signed_distance);
        if (signed_distance > 0.0F &&
            signed_distance <= max_signed_distance &&
            length(perpendicular) <= max_perpendicular_distance) {
            matching.push_back(vertex_id);
        }
    }
    return matching;
}

void add_oriented_face(Document& document, std::vector<ElementId> vertices, quader::QVec3 expected_normal)
{
    Face face;
    face.id = document.next_face_id++;
    face.vertices = std::move(vertices);
    if (dot(face_normal(document, face), expected_normal) < 0.0F) {
        std::ranges::reverse(face.vertices);
    }
    document.faces.push_back(std::move(face));
}

/**
 * Stores the Side Extrusion Fixture data contract used by the automated test suite.
 */
struct SideExtrusionFixture {
    Document document;
    std::array<ElementId, 4> root_corners {};
    std::array<ElementId, 8> extrusion_corners {};
    std::vector<Edge> selected_edges;
};

SideExtrusionFixture make_side_extrusion_fixture()
{
    SideExtrusionFixture fixture;
    constexpr float kHalf = 1.0F;
    constexpr float kInset = 0.35F;
    constexpr float kTipX = 2.0F;
    const std::array<float, 4> coords{-kHalf, -kInset, kInset, kHalf};

    const ElementId back_00 =
        add_vertex(fixture.document, {-kHalf, -kHalf, -kHalf});
    const ElementId back_10 =
        add_vertex(fixture.document, {-kHalf, kHalf, -kHalf});
    const ElementId back_11 =
        add_vertex(fixture.document, {-kHalf, kHalf, kHalf});
    const ElementId back_01 =
        add_vertex(fixture.document, {-kHalf, -kHalf, kHalf});

    std::array<std::array<ElementId, 4>, 4> front {};
    for (std::size_t y = 0; y < coords.size(); ++y) {
        for (std::size_t z = 0; z < coords.size(); ++z) {
          front[y][z] =
              add_vertex(fixture.document, {kHalf, coords[y], coords[z]});
        }
    }

    const ElementId root_00 = front[1][1];
    const ElementId root_10 = front[2][1];
    const ElementId root_11 = front[2][2];
    const ElementId root_01 = front[1][2];
    const ElementId tip_00 =
        add_vertex(fixture.document, {kTipX, -kInset, -kInset});
    const ElementId tip_10 =
        add_vertex(fixture.document, {kTipX, kInset, -kInset});
    const ElementId tip_11 =
        add_vertex(fixture.document, {kTipX, kInset, kInset});
    const ElementId tip_01 =
        add_vertex(fixture.document, {kTipX, -kInset, kInset});

    fixture.root_corners = {
        root_00,
        root_10,
        root_11,
        root_01,
    };
    fixture.extrusion_corners = {
        root_00,
        root_10,
        root_11,
        root_01,
        tip_00,
        tip_10,
        tip_11,
        tip_01,
    };

    add_oriented_face(fixture.document, { back_00, back_01, back_11, back_10 }, { -1.0F, 0.0F, 0.0F });
    add_oriented_face(fixture.document, { back_00, back_10, front[3][0], front[2][0], front[1][0], front[0][0] }, { 0.0F, 0.0F, -1.0F });
    add_oriented_face(fixture.document, { back_01, front[0][3], front[1][3], front[2][3], front[3][3], back_11 }, { 0.0F, 0.0F, 1.0F });
    add_oriented_face(fixture.document, { back_00, front[0][0], front[0][1], front[0][2], front[0][3], back_01 }, { 0.0F, -1.0F, 0.0F });
    add_oriented_face(fixture.document, { back_10, back_11, front[3][3], front[3][2], front[3][1], front[3][0] }, { 0.0F, 1.0F, 0.0F });

    for (std::size_t y = 0; y + 1U < coords.size(); ++y) {
        for (std::size_t z = 0; z + 1U < coords.size(); ++z) {
            if (y == 1U && z == 1U) {
                continue;
            }
            add_oriented_face(
                fixture.document,
                { front[y][z], front[y + 1U][z], front[y + 1U][z + 1U], front[y][z + 1U] },
                { 1.0F, 0.0F, 0.0F });
        }
    }

    add_oriented_face(fixture.document, { tip_00, tip_10, tip_11, tip_01 }, { 1.0F, 0.0F, 0.0F });
    add_oriented_face(fixture.document, { root_00, tip_00, tip_10, root_10 }, { 0.0F, 0.0F, -1.0F });
    add_oriented_face(fixture.document, { root_01, root_11, tip_11, tip_01 }, { 0.0F, 0.0F, 1.0F });
    add_oriented_face(fixture.document, { root_00, root_01, tip_01, tip_00 }, { 0.0F, -1.0F, 0.0F });
    add_oriented_face(fixture.document, { root_10, tip_10, tip_11, root_11 }, { 0.0F, 1.0F, 0.0F });

    fixture.selected_edges = {
        make_edge(root_00, root_10),
        make_edge(root_10, root_11),
        make_edge(root_11, root_01),
        make_edge(root_01, root_00),
        make_edge(root_00, tip_00),
        make_edge(root_10, tip_10),
        make_edge(root_11, tip_11),
        make_edge(root_01, tip_01),
        make_edge(tip_00, tip_10),
        make_edge(tip_10, tip_11),
        make_edge(tip_11, tip_01),
        make_edge(tip_01, tip_00),
    };

    return fixture;
}

Selection side_extrusion_edge_selection(const SideExtrusionFixture& fixture)
{
    Selection selection;
    selection.mode = SelectionMode::Edge;
    selection.edges = fixture.selected_edges;
    selection.has_active = true;
    selection.active_kind = ElementKind::Edge;
    selection.active_edge = fixture.selected_edges.front();
    return selection;
}

std::vector<ElementId> selected_edge_vertices_with_count(const Selection& selection, int expected_count)
{
    std::map<ElementId, int> counts;
    for (const Edge& edge : selection.edges) {
        ++counts[edge.a];
        ++counts[edge.b];
    }

    std::vector<ElementId> vertices;
    for (const auto& [vertex_id, count] : counts) {
        if (count == expected_count) {
            vertices.push_back(vertex_id);
        }
    }
    return vertices;
}

std::map<ElementId, ElementId> support_corner_faces_for_roots(
    const Document& document,
    const Selection& selection,
    std::span<const ElementId> root_corners)
{
    std::set<std::pair<ElementId, ElementId>> selected_edges;
    for (const Edge& edge : selection.edges) {
        selected_edges.insert(edge_pair_key(edge));
    }

    std::map<ElementId, ElementId> corner_faces;
    for (const ElementId root_id : root_corners) {
        for (const Face& face : document.faces) {
            const auto root = std::find(face.vertices.begin(), face.vertices.end(), root_id);
            if (root == face.vertices.end()) {
                continue;
            }
            const std::size_t root_index = static_cast<std::size_t>(std::distance(face.vertices.begin(), root));
            const ElementId previous_id = face.vertices[(root_index + face.vertices.size() - 1U) % face.vertices.size()];
            const ElementId next_id = face.vertices[(root_index + 1U) % face.vertices.size()];
            const bool previous_selected = selected_edges.contains(edge_pair_key(make_edge(previous_id, root_id)));
            const bool next_selected = selected_edges.contains(edge_pair_key(make_edge(root_id, next_id)));
            if (!previous_selected && !next_selected) {
                corner_faces[root_id] = face.id;
                break;
            }
        }
    }
    return corner_faces;
}

void require_single_support_corner_attach(
    const Document& document,
    const std::map<ElementId, quader::QVec3>& source_corner_positions,
    const std::map<ElementId, ElementId>& support_corner_faces,
    float radius)
{
    for (const auto& [source_vertex_id, source_position] : source_corner_positions) {
        const auto support_face_id = support_corner_faces.find(source_vertex_id);
        EXPECT_TRUE(support_face_id != support_corner_faces.end());
        const Face* support_face = find_face(document, support_face_id->second);
        EXPECT_TRUE(support_face != nullptr);

        int near_attach_count = 0;
        for (const ElementId vertex_id : support_face->vertices) {
            const Vertex* vertex = find_vertex(document, vertex_id);
            EXPECT_TRUE(vertex != nullptr);
            const quader::QVec3 delta = vertex->position - source_position;
            if (length_squared(delta) > radius * radius ||
                length_squared(delta) <= 0.00000001F) {
                continue;
            }
            SCOPED_TRACE(::testing::Message() << "support corner face " << support_face->id << " old vertex " << source_vertex_id);
            EXPECT_TRUE(std::abs(delta.y) > 0.0001F);
            EXPECT_TRUE(std::abs(delta.z) > 0.0001F);
            ++near_attach_count;
        }

        SCOPED_TRACE(::testing::Message() << "outer patch miter should meet the old support corner face at one middle vertex near old root " << source_vertex_id);
        EXPECT_TRUE(near_attach_count == 1);
    }
}

void require_original_selected_edges_consumed(const Document& document, const Selection& original_selection)
{
    std::set<std::pair<ElementId, ElementId>> original_edges;
    for (const Edge& edge : original_selection.edges) {
        original_edges.insert(edge_pair_key(edge));
    }

    for (const Face& face : document.faces) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            SCOPED_TRACE(::testing::Message() << "old selected edge should not survive after bevel");
            EXPECT_FALSE(original_edges.contains(edge_pair_key(make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]))));
        }
    }
}

void require_generated_faces_point_away_from_source_corners(
    const Document& document,
    const Selection& selection,
    const std::map<ElementId, quader::QVec3>& source_corner_positions,
    float radius)
{
  EXPECT_TRUE(selection.mode == SelectionMode::Face);
  EXPECT_FALSE(selection.faces.empty());

  for (const auto &[source_vertex_id, source_position] :
       source_corner_positions) {
    int checked_face_count = 0;
    for (const ElementId face_id : selection.faces) {
      const Face *face = find_face(document, face_id);
      EXPECT_TRUE(face != nullptr);
      const quader::QVec3 center = face_centroid(document, *face);
      const quader::QVec3 from_source = center - source_position;
      if (length_squared(from_source) > radius * radius ||
          length_squared(from_source) <= 0.00000001F) {
        continue;
      }

      SCOPED_TRACE(
          ::testing::Message()
          << "generated bevel face should point away from old corner vertex "
          << source_vertex_id << " face=" << face_id);
      const quader::QVec3 normal = face_normal(document, *face);
      const quader::QVec3 direction_from_source =
          from_source / length(from_source);
      EXPECT_TRUE(dot(normal, direction_from_source) > -0.5F);
      ++checked_face_count;
    }
    SCOPED_TRACE(::testing::Message()
                 << "expected generated bevel faces near old corner vertex "
                 << source_vertex_id);
    EXPECT_TRUE(checked_face_count > 0);
  }
}

void require_no_generated_triangles_near_source_corners(
    const Document& document,
    const Selection& selection,
    const std::map<ElementId, quader::QVec3>& source_corner_positions,
    float radius)
{
  EXPECT_TRUE(selection.mode == SelectionMode::Face);
  EXPECT_FALSE(selection.faces.empty());

  for (const auto &[source_vertex_id, source_position] :
       source_corner_positions) {
    int checked_face_count = 0;
    for (const ElementId face_id : selection.faces) {
      const Face *face = find_face(document, face_id);
      EXPECT_TRUE(face != nullptr);
      const quader::QVec3 center = face_centroid(document, *face);
      const quader::QVec3 from_source = center - source_position;
      if (length_squared(from_source) > radius * radius ||
          length_squared(from_source) <= 0.00000001F) {
        continue;
      }

      SCOPED_TRACE(::testing::Message()
                   << "outer miter patch should not collapse to a sharp "
                      "triangle fan at old corner vertex "
                   << source_vertex_id << " face=" << face_id);
      EXPECT_TRUE(face->vertices.size() != 3U);
      ++checked_face_count;
    }
    SCOPED_TRACE(::testing::Message()
                 << "expected generated bevel faces near old corner vertex "
                 << source_vertex_id);
    EXPECT_TRUE(checked_face_count > 0);
  }
}

void require_outer_patch_support_vertices(
    const Document& document,
    const std::map<ElementId, quader::QVec3>& source_corner_positions,
    float width)
{
    for (const auto& [source_vertex_id, source_position] : source_corner_positions) {
        const float y_sign = source_position.y >= 0.0F ? 1.0F : -1.0F;
        const float z_sign = source_position.z >= 0.0F ? 1.0F : -1.0F;
        const std::array<quader::QVec3, 3> expected_positions {
            source_position + quader::QVec3 { 0.0F, y_sign * width, z_sign * width },
            source_position + quader::QVec3 { 0.0F, -y_sign * width, z_sign * width },
            source_position + quader::QVec3 { 0.0F, y_sign * width, -z_sign * width },
        };

        for (const quader::QVec3 expected_position : expected_positions) {
            SCOPED_TRACE(::testing::Message() << "outer PATCH miter boundary should include Blender-style side/support vertex near old root " << source_vertex_id);
            EXPECT_TRUE(has_vertex_near_position(document, expected_position, 0.001F));
        }
    }
}

void require_no_generated_position_poles_near_source_corners(
    const Document& document,
    const Selection& selection,
    const std::map<ElementId, quader::QVec3>& source_corner_positions,
    float radius,
    float minimum_separation)
{
  EXPECT_TRUE(selection.mode == SelectionMode::Face);
  EXPECT_FALSE(selection.faces.empty());

  std::set<ElementId> selected_vertices;
  for (const ElementId face_id : selection.faces) {
    const Face *face = find_face(document, face_id);
    EXPECT_TRUE(face != nullptr);
    selected_vertices.insert(face->vertices.begin(), face->vertices.end());
  }

    for (const auto& [source_vertex_id, source_position] : source_corner_positions) {
        std::vector<std::pair<ElementId, quader::QVec3>> nearby;
        for (const ElementId vertex_id : selected_vertices) {
            const Vertex* vertex = find_vertex(document, vertex_id);
            EXPECT_TRUE(vertex != nullptr);
            if (length(vertex->position - source_position) <= radius) {
                nearby.emplace_back(vertex_id, vertex->position);
            }
        }

        for (auto first = nearby.begin(); first != nearby.end(); ++first) {
            for (auto second = std::next(first); second != nearby.end(); ++second) {
                SCOPED_TRACE(::testing::Message() << "square profile generated vertices should not form a same-position pole near old root " << source_vertex_id);
                EXPECT_TRUE(length(first->second - second->second) > minimum_separation);
            }
        }
    }
}

void require_single_generated_center_ngon_near_source_corners(
    const Document& document,
    const Selection& selection,
    const std::map<ElementId, quader::QVec3>& source_corner_positions,
    float radius,
    std::size_t side_count)
{
  EXPECT_TRUE(selection.mode == SelectionMode::Face);
  EXPECT_FALSE(selection.faces.empty());

  for (const auto &[source_vertex_id, source_position] :
       source_corner_positions) {
    int matching_center_faces = 0;
    for (const ElementId face_id : selection.faces) {
      const Face *face = find_face(document, face_id);
      EXPECT_TRUE(face != nullptr);
      if (face->vertices.size() != side_count) {
        continue;
      }
      const quader::QVec3 center = face_centroid(document, *face);
      if (length(center - source_position) <= radius) {
        ++matching_center_faces;
      }
    }

    SCOPED_TRACE(::testing::Message()
                 << "odd-segment ADJ/PATCH vmesh should emit one unsplit "
                    "center ngon near old corner vertex "
                 << source_vertex_id);
    EXPECT_TRUE(matching_center_faces == 1);
  }
}

void require_patch_miter_center_rows_continue_source_side_lines(
    const Document& document,
    const Selection& selection,
    const std::map<ElementId, quader::QVec3>& source_corner_positions,
    float radius)
{
  EXPECT_TRUE(selection.mode == SelectionMode::Face);
  EXPECT_FALSE(selection.faces.empty());

  for (const auto &[source_vertex_id, source_position] :
       source_corner_positions) {
    int matching_center_faces = 0;
    for (const ElementId face_id : selection.faces) {
      const Face *face = find_face(document, face_id);
      EXPECT_TRUE(face != nullptr);
      if (face->vertices.size() != 5U) {
        continue;
      }
      const quader::QVec3 center = face_centroid(document, *face);
      if (length(center - source_position) > radius) {
        continue;
      }

      int continuation_vertices = 0;
      for (const ElementId vertex_id : face->vertices) {
        const Vertex *vertex = find_vertex(document, vertex_id);
        EXPECT_TRUE(vertex != nullptr);
        if (nearly_equal(vertex->position.y, source_position.y) ||
            nearly_equal(vertex->position.z, source_position.z)) {
          ++continuation_vertices;
        }
      }

      SCOPED_TRACE(::testing::Message()
                   << "PATCH miter center row should continue adjacent "
                      "side-face rows near old corner vertex "
                   << source_vertex_id);
      EXPECT_TRUE(continuation_vertices >= 2);
      ++matching_center_faces;
    }

    SCOPED_TRACE(::testing::Message() << "expected one odd-segment PATCH miter "
                                         "center face near old corner vertex "
                                      << source_vertex_id);
    EXPECT_TRUE(matching_center_faces == 1);
  }
}

} // namespace

TEST(QdrDocumentOperationBevelEdgesTests, TopEdgeLoopBevelSharesSegmentedCornerChains) {
    CubePrimitiveSpec spec;
    spec.width = 128.0F;
    spec.height = 128.0F;
    spec.depth = 128.0F;

    Document document = make_cube(spec);
    Selection selection = top_face_loop_selection(document);

    EdgeBevelSettings settings;
    settings.width = 0.9F;
    settings.profile = 0.5F;
    settings.profile_type = BevelProfileType::Offset;
    settings.segments = 10;

    const OperationResult result = bevel_selected_edges(document, selection, settings);

    EXPECT_TRUE(result.changed);

    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_TRUE(document.vertices.size() == 48U);
    EXPECT_TRUE(document.faces.size() == 46U);
    EXPECT_TRUE(std::all_of(document.faces.begin(), document.faces.end(), [](const Face& face) {
        return face.vertices.size() == 4U;
    }));
    EXPECT_TRUE(selection.mode == SelectionMode::Face);
    EXPECT_TRUE(selection.faces.size() == 40U);
    EXPECT_TRUE(std::all_of(selection.faces.begin(), selection.faces.end(), [&document](ElementId face_id) {
        const Face* face = find_face(document, face_id);
        if (face == nullptr) {
            return false;
        }
        return face->vertices.size() == 4U;
    }));
    EXPECT_TRUE(has_vertex_near_position(document, { 63.736F, 63.736F, 63.736F }, 0.002F));
    EXPECT_TRUE(has_vertex_near_position(document, { -63.736F, 63.736F, 63.736F }, 0.002F));
    EXPECT_TRUE(has_vertex_near_position(document, { 63.736F, 63.736F, -63.736F }, 0.002F));
    EXPECT_TRUE(has_vertex_near_position(document, { -63.736F, 63.736F, -63.736F }, 0.002F));
    SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
    EXPECT_TRUE(every_edge_is_closed(document));
    EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
    EXPECT_TRUE(every_face_compiles_to_triangles(document));
    EXPECT_TRUE(all_faces_point_relative_to_center(document));
}

TEST(QdrDocumentOperationBevelEdgesTests, TopEdgeLoopSquareProfileKeepsClosedOutwardQuads) {
    CubePrimitiveSpec spec;
    spec.width = 128.0F;
    spec.height = 128.0F;
    spec.depth = 128.0F;

    Document document = make_cube(spec);
    Selection selection = top_face_loop_selection(document);
    const Selection original_selection = selection;

    EdgeBevelSettings settings;
    settings.width = 0.9F;
    settings.profile = 1.0F;
    settings.profile_type = BevelProfileType::Offset;
    settings.segments = 3;

    const OperationResult result = bevel_selected_edges(document, selection, settings);

    EXPECT_TRUE(result.changed);

    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_TRUE(selection.mode == SelectionMode::Face);
    EXPECT_FALSE(selection.faces.empty());
    EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), face_has_repeated_vertices));
    EXPECT_TRUE(std::all_of(selection.faces.begin(), selection.faces.end(), [&document](ElementId face_id) {
        const Face* face = find_face(document, face_id);
        return face != nullptr && face->vertices.size() == 4U;
    }));
    SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
    EXPECT_TRUE(every_edge_is_closed(document));
    EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
    EXPECT_TRUE(every_face_compiles_to_triangles(document));
    EXPECT_TRUE(all_faces_point_relative_to_center(document));
    require_original_selected_edges_consumed(document, original_selection);
}

TEST(QdrDocumentOperationBevelEdgesTests, TopPerimeterBevelKeepsUnselectedSpokeEdgesSingle) {
    Document document;
    constexpr float kTopY = 1.0F;
    constexpr float kBottomY = -1.0F;
    constexpr float kRadius = 1.0F;
    const ElementId center = add_vertex(document, {0.0F, kTopY, 0.0F});

    std::array<ElementId, 8> top {};
    std::array<ElementId, 8> bottom {};
    std::array<quader::QVec3, 8> spoke_directions {};
    for (std::size_t index = 0; index < top.size(); ++index) {
        const float angle = (static_cast<float>(index) / static_cast<float>(top.size())) * 6.28318530717958647692F;
        const quader::QVec3 position{std::cos(angle) * kRadius, kTopY,
                                     std::sin(angle) * kRadius};
        spoke_directions[index] = { position.x, 0.0F, position.z };
        top[index] = add_vertex(document, position);
        bottom[index] =
            add_vertex(document, {position.x, kBottomY, position.z});
    }

    for (std::size_t index = 0; index < top.size(); ++index) {
        add_oriented_face(document, { center, top[index], top[(index + 1U) % top.size()] }, { 0.0F, 1.0F, 0.0F });
    }
    for (std::size_t index = 0; index < top.size(); ++index) {
        const Vertex* top_vertex = find_vertex(document, top[index]);
        EXPECT_TRUE(top_vertex != nullptr);
        add_oriented_face(
            document,
            { bottom[index], bottom[(index + 1U) % bottom.size()], top[(index + 1U) % top.size()], top[index] },
            { top_vertex->position.x, 0.0F, top_vertex->position.z });
    }
    std::vector<ElementId> bottom_loop(bottom.rbegin(), bottom.rend());
    add_oriented_face(document, std::move(bottom_loop), { 0.0F, -1.0F, 0.0F });

    Selection selection;
    selection.mode = SelectionMode::Edge;
    for (std::size_t index = 0; index < top.size(); ++index) {
        selection.edges.push_back(make_edge(top[index], top[(index + 1U) % top.size()]));
    }
    selection.has_active = true;
    selection.active_kind = ElementKind::Edge;
    selection.active_edge = selection.edges.front();

    EdgeBevelSettings settings;
    settings.width = 0.2F;
    settings.profile = 0.5F;
    settings.profile_type = BevelProfileType::Offset;
    settings.segments = 3;

    const OperationResult result = bevel_selected_edges(document, selection, settings);

    SCOPED_TRACE(::testing::Message() << result.message);
    EXPECT_TRUE(result.changed);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_TRUE(selection.mode == SelectionMode::Face);
    EXPECT_FALSE(selection.faces.empty());
    EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), face_has_repeated_vertices));
    EXPECT_TRUE(every_face_compiles_to_triangles(document));
    SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
    EXPECT_TRUE(every_edge_is_closed(document));
    EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));

    const std::vector<ElementId> center_neighbors = neighbor_vertices_for(document, center);
    EXPECT_TRUE(center_neighbors.size() == top.size());
    for (std::size_t index = 0; index < top.size(); ++index) {
      const std::vector<ElementId> spoke_neighbors =
          vertices_near_spoke_direction(
              document, center_neighbors, {0.0F, kTopY, 0.0F},
              spoke_directions[index], 0.05F, kRadius);
      for (const ElementId neighbor_id : center_neighbors) {
        const Vertex *neighbor = find_vertex(document, neighbor_id);
        EXPECT_TRUE(neighbor != nullptr);
        SCOPED_TRACE(::testing::Message()
                     << "center neighbor " << neighbor_id << " = ("
                     << neighbor->position.x << ", " << neighbor->position.y
                     << ", " << neighbor->position.z << ")");
      }
        SCOPED_TRACE(::testing::Message() << "unselected spoke at source top vertex " << top[index] << " should remain one rebuilt edge");
        EXPECT_TRUE(spoke_neighbors.size() == 1U);
        EXPECT_TRUE(edge_use_count(document, make_edge(center, spoke_neighbors.front())) == 2);
    }
}

TEST(QdrDocumentOperationBevelEdgesTests, TwoConnectedEdgeBevelsCloseTheSharedCornerPatch) {
    Document document = make_cube({});
    const Face source_face = document.faces.front();
    const ElementId shared_corner = source_face.vertices[1];

    Selection selection;
    selection.mode = SelectionMode::Edge;
    selection.edges = {
        make_edge(source_face.vertices[0], shared_corner),
        make_edge(shared_corner, source_face.vertices[2]),
    };

    EdgeBevelSettings settings;
    settings.width = 0.9F;
    settings.profile = 0.5F;
    settings.profile_type = BevelProfileType::Offset;
    settings.segments = 9;

    const OperationResult result = bevel_selected_edges(document, selection, settings);

    EXPECT_TRUE(result.changed);

    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_TRUE(selection.mode == SelectionMode::Face);
    EXPECT_TRUE(selection.faces.size() == 18U);
    EXPECT_TRUE(find_vertex(document, shared_corner) == nullptr);
    EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), face_has_repeated_vertices));
    EXPECT_TRUE(every_face_compiles_to_triangles(document));
    SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
    EXPECT_TRUE(every_edge_is_closed(document));
    EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
}

TEST(QdrDocumentOperationBevelEdgesTests, SingleEdgeBevelPreservesExactProfileEndpoints) {
    const auto bevel_with_profile = [](float profile) {
        Document document = make_cube({});
        const Face face = document.faces.front();
        Selection selection = edge_selection_for_edge(make_edge(face.vertices[0], face.vertices[1]));

        EdgeBevelSettings settings;
        settings.width = 0.2F;
        settings.profile = profile;
        settings.profile_type = BevelProfileType::Offset;
        settings.segments = 3;

        const OperationResult result = bevel_selected_edges(document, selection, settings);
        EXPECT_TRUE(result.changed);
        qdr_test::expect_valid_polygon_document_after_success(document, result);
        return document;
    };

    const Document square_threshold = bevel_with_profile(0.95F);
    const Document square_endpoint = bevel_with_profile(1.0F);
    EXPECT_TRUE(square_threshold.vertices.size() == square_endpoint.vertices.size());
    EXPECT_TRUE(std::equal(square_threshold.vertices.begin(), square_threshold.vertices.end(), square_endpoint.vertices.begin(), [](const Vertex& left, const Vertex& right) {
        return vec_nearly_equal(left.position, right.position);
    }));

    const Document straight_endpoint = bevel_with_profile(0.0F);
    const Document low_profile = bevel_with_profile(0.01F);
    EXPECT_FALSE(std::equal(straight_endpoint.vertices.begin(), straight_endpoint.vertices.end(), low_profile.vertices.begin(), [](const Vertex& left, const Vertex& right) {
        return vec_nearly_equal(left.position, right.position);
    }));
}

TEST(QdrDocumentOperationBevelEdgesTests, SingleEdgeBevelWidthReachesAvailableFaceSupport) {
    CubePrimitiveSpec spec;
    spec.width = 128.0F;
    spec.height = 64.0F;
    spec.depth = 64.0F;

    Document document = make_cube(spec);
    const ElementId left_front_top = vertex_near_position(document, { -64.0F, 32.0F, 32.0F });
    const ElementId right_front_top = vertex_near_position(document, { 64.0F, 32.0F, 32.0F });
    Selection selection = edge_selection_for_edge(make_edge(left_front_top, right_front_top));

    EdgeBevelSettings settings;
    settings.width = 60.0F;
    settings.profile = 0.5F;
    settings.profile_type = BevelProfileType::Offset;
    settings.segments = 5;

    const OperationResult result = bevel_selected_edges(document, selection, settings);

    SCOPED_TRACE(::testing::Message() << result.message);
    EXPECT_TRUE(result.changed);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_TRUE(has_vertex_near_position(document, { -64.0F, 32.0F, -28.0F }, 0.002F));
    EXPECT_TRUE(has_vertex_near_position(document, { 64.0F, 32.0F, -28.0F }, 0.002F));
    EXPECT_TRUE(has_vertex_near_position(document, { -64.0F, -28.0F, 32.0F }, 0.002F));
    EXPECT_TRUE(has_vertex_near_position(document, { 64.0F, -28.0F, 32.0F }, 0.002F));
    SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
    EXPECT_TRUE(every_edge_is_closed(document));
    EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
    EXPECT_TRUE(every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationBevelEdgesTests, SingleHalfEdgeBevelClosesSplitCubeEdgeTerminals) {
    Document document;
    const ElementId left_front_bottom = add_vertex(document, { -1.0F, -1.0F, -1.0F });
    const ElementId right_front_bottom = add_vertex(document, { 1.0F, -1.0F, -1.0F });
    const ElementId right_back_bottom = add_vertex(document, { 1.0F, -1.0F, 1.0F });
    const ElementId left_back_bottom = add_vertex(document, { -1.0F, -1.0F, 1.0F });
    const ElementId left_front_top = add_vertex(document, { -1.0F, 1.0F, -1.0F });
    const ElementId mid_front_top = add_vertex(document, { 0.0F, 1.0F, -1.0F });
    const ElementId right_front_top = add_vertex(document, { 1.0F, 1.0F, -1.0F });
    const ElementId right_back_top = add_vertex(document, { 1.0F, 1.0F, 1.0F });
    const ElementId left_back_top = add_vertex(document, { -1.0F, 1.0F, 1.0F });

    add_oriented_face(document, { left_front_bottom, left_back_bottom, right_back_bottom, right_front_bottom }, { 0.0F, -1.0F, 0.0F });
    add_oriented_face(document, { left_front_top, mid_front_top, right_front_top, right_back_top, left_back_top }, { 0.0F, 1.0F, 0.0F });
    add_oriented_face(document, { left_front_bottom, right_front_bottom, right_front_top, mid_front_top, left_front_top }, { 0.0F, 0.0F, -1.0F });
    add_oriented_face(document, { right_front_bottom, right_back_bottom, right_back_top, right_front_top }, { 1.0F, 0.0F, 0.0F });
    add_oriented_face(document, { right_back_bottom, left_back_bottom, left_back_top, right_back_top }, { 0.0F, 0.0F, 1.0F });
    add_oriented_face(document, { left_back_bottom, left_front_bottom, left_front_top, left_back_top }, { -1.0F, 0.0F, 0.0F });

    Selection selection = edge_selection_for_edge(make_edge(mid_front_top, right_front_top));

    EdgeBevelSettings settings;
    settings.width = 0.12F;
    settings.profile = 0.5F;
    settings.profile_type = BevelProfileType::Offset;
    settings.segments = 3;

    const OperationResult result = bevel_selected_edges(document, selection, settings);

    SCOPED_TRACE(::testing::Message() << result.message);
    EXPECT_TRUE(result.changed);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_TRUE(selection.mode == SelectionMode::Face);
    EXPECT_FALSE(selection.faces.empty());
    EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), face_has_repeated_vertices));
    EXPECT_TRUE(every_face_compiles_to_triangles(document));
    SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
    EXPECT_TRUE(every_edge_is_closed(document));
    EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
}

TEST(QdrDocumentOperationBevelEdgesTests, SingleSplitCornerHalfEdgeBevelMatchesBlenderPatchTopology) {
    Document document;
    const ElementId v1 = add_vertex(document, { -1.0F, -1.0F, 1.0F });
    const ElementId v2 = add_vertex(document, { -1.0F, 1.0F, 1.0F });
    const ElementId v3 = add_vertex(document, { -1.0F, -1.0F, -1.0F });
    const ElementId v4 = add_vertex(document, { -1.0F, 1.0F, -1.0F });
    const ElementId v5 = add_vertex(document, { 1.0F, -1.0F, 1.0F });
    const ElementId v6 = add_vertex(document, { 1.0F, 1.0F, 1.0F });
    const ElementId v7 = add_vertex(document, { 1.0F, -1.0F, -1.0F });
    const ElementId v8 = add_vertex(document, { -1.0F, -1.0F, 0.0F });
    const ElementId v9 = add_vertex(document, { -1.0F, 1.0F, 0.0F });
    const ElementId v10 = add_vertex(document, { 1.0F, -1.0F, 0.0F });
    const ElementId lower_split_corner = add_vertex(document, { 1.0F, 1.0F, -1.0F });
    const ElementId upper_split_corner = add_vertex(document, { 1.0F, 1.0F, 0.0F });

    add_oriented_face(document, { v8, v9, v4, v3 }, { -1.0F, 0.0F, 0.0F });
    add_oriented_face(document, { v5, v6, v2, v1 }, { 0.0F, 0.0F, 1.0F });
    add_oriented_face(document, { v8, v10, v5, v1 }, { 0.0F, -1.0F, 0.0F });
    add_oriented_face(document, { v3, v7, v10, v8 }, { 0.0F, -1.0F, 0.0F });
    add_oriented_face(document, { v1, v2, v9, v8 }, { -1.0F, 0.0F, 0.0F });
    add_oriented_face(document, { lower_split_corner, v4, v9, upper_split_corner }, { 0.0F, 1.0F, 0.0F });
    add_oriented_face(document, { v3, v4, lower_split_corner, v7 }, { 0.0F, 0.0F, -1.0F });
    add_oriented_face(document, { v7, lower_split_corner, upper_split_corner, v10 }, { 1.0F, 0.0F, 0.0F });
    add_oriented_face(document, { upper_split_corner, v9, v2, v6 }, { 0.0F, 1.0F, 0.0F });
    add_oriented_face(document, { v10, upper_split_corner, v6, v5 }, { 1.0F, 0.0F, 0.0F });

    Selection selection = edge_selection_for_edge(make_edge(lower_split_corner, upper_split_corner));

    EdgeBevelSettings settings;
    settings.width = 0.12F;
    settings.profile = 0.5F;
    settings.profile_type = BevelProfileType::Offset;
    settings.segments = 3;

    const OperationResult result = bevel_selected_edges(document, selection, settings);

    SCOPED_TRACE(::testing::Message() << result.message);
    EXPECT_TRUE(result.changed);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_TRUE(selection.mode == SelectionMode::Face);
    EXPECT_FALSE(selection.faces.empty());
    EXPECT_TRUE(document.vertices.size() == 20U);
    EXPECT_TRUE(document.faces.size() == 16U);
    std::map<std::size_t, int> face_valence_counts;
    for (const Face& face : document.faces) {
        ++face_valence_counts[face.vertices.size()];
    }
    EXPECT_TRUE(face_valence_counts[3U] == 3);
    EXPECT_TRUE(face_valence_counts[4U] == 10);
    EXPECT_TRUE(face_valence_counts[5U] == 0);
    EXPECT_TRUE(face_valence_counts[6U] == 2);
    EXPECT_TRUE(face_valence_counts[7U] == 1);
    EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), face_has_repeated_vertices));
    EXPECT_TRUE(every_face_compiles_to_triangles(document));
    SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
    EXPECT_TRUE(every_edge_is_closed(document));
    EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
}

TEST(QdrDocumentOperationBevelEdgesTests, EdgeBevelClampsSegmentCountAtTheDocumentOperationBoundary) {
    const auto bevel_with_segments = [](int segments) {
        Document document = make_cube({});
        const Face face = document.faces.front();
        Selection selection = edge_selection_for_edge(make_edge(face.vertices[0], face.vertices[1]));

        EdgeBevelSettings settings;
        settings.width = 0.2F;
        settings.profile = 0.5F;
        settings.profile_type = BevelProfileType::Offset;
        settings.segments = segments;

        const OperationResult result = bevel_selected_edges(document, selection, settings);
        EXPECT_TRUE(result.changed);
        qdr_test::expect_valid_polygon_document_after_success(document, result);
        return document;
    };

    const Document clamped = bevel_with_segments(1000);
    const Document explicit_max = bevel_with_segments(16);

    EXPECT_TRUE(clamped.vertices.size() == explicit_max.vertices.size());
    EXPECT_TRUE(clamped.faces.size() == explicit_max.faces.size());
    EXPECT_TRUE(std::equal(clamped.vertices.begin(), clamped.vertices.end(), explicit_max.vertices.begin(), [](const Vertex& left, const Vertex& right) {
        return vec_nearly_equal(left.position, right.position);
    }));
    EXPECT_TRUE(std::equal(clamped.faces.begin(), clamped.faces.end(), explicit_max.faces.begin(), [](const Face& left, const Face& right) {
        return left.vertices == right.vertices;
    }));
}

TEST(QdrDocumentOperationBevelEdgesTests, ConvexValenceThreeCubeCornersKeepValidVmeshPatches) {
    /**
     * Stores the Expected Case data contract used by the automated test suite.
     */
    struct ExpectedCase {
        int segments = 0;
        float profile = 0.5F;
        bool square_in_corner = false;
    };

    const std::array<ExpectedCase, 3> cases {
        ExpectedCase { 2, 0.0F, true },
        ExpectedCase { 3, 0.5F, false },
        ExpectedCase { 8, 0.5F, false },
    };

    for (const ExpectedCase& expected : cases) {
        SCOPED_TRACE(::testing::Message() << "segments=" << expected.segments << " profile=" << expected.profile);
        Document document = make_cube({});
        Selection selection = all_document_edge_selection(document);
        const Selection original_selection = selection;

        const std::vector<ElementId> three_edge_corners = selected_edge_vertices_with_count(selection, 3);
        EXPECT_TRUE(three_edge_corners.size() == 8U);
        std::map<ElementId, quader::QVec3> source_corner_positions;
        for (const ElementId vertex_id : three_edge_corners) {
            const Vertex* vertex = find_vertex(document, vertex_id);
            EXPECT_TRUE(vertex != nullptr);
            source_corner_positions.emplace(vertex_id, vertex->position);
        }

        EdgeBevelSettings settings;
        settings.width = 0.2F;
        settings.profile = expected.profile;
        settings.profile_type = BevelProfileType::Offset;
        settings.segments = expected.segments;

        const OperationResult result = bevel_selected_edges(document, selection, settings);

        SCOPED_TRACE(::testing::Message() << result.message);
        EXPECT_TRUE(result.changed);
        qdr_test::expect_valid_polygon_document_after_success(document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_FALSE(selection.faces.empty());
        EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), face_has_repeated_vertices));
        EXPECT_TRUE(every_face_compiles_to_triangles(document));
        SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
        EXPECT_TRUE(every_edge_is_closed(document));
        EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
        EXPECT_TRUE(all_faces_point_relative_to_center(document));
        require_original_selected_edges_consumed(document, original_selection);
        if (expected.square_in_corner) {
            EXPECT_TRUE(std::all_of(selection.faces.begin(), selection.faces.end(), [&document](ElementId face_id) {
                const Face* face = find_face(document, face_id);
                return face != nullptr && face->vertices.size() == 4U;
            }));
            require_no_generated_position_poles_near_source_corners(
                document,
                selection,
                source_corner_positions,
                0.45F,
                settings.width * 0.05F);
        }
        for (const ElementId vertex_id : three_edge_corners) {
            EXPECT_TRUE(find_vertex(document, vertex_id) == nullptr);
        }
    }
}

TEST(QdrDocumentOperationBevelEdgesTests, SideExtrusionBevelClosesAndOrientsAllExtrusionRootCorners) {
    /**
     * Stores the Expected Case data contract used by the automated test suite.
     */
    struct ExpectedCase {
        int segments = 0;
        float profile = 0.5F;
        std::size_t min_selected_faces = 0;
    };

    const std::array<ExpectedCase, 4> cases {
        ExpectedCase { 3, 0.0F, 36U },
        ExpectedCase { 3, 0.5F, 36U },
        ExpectedCase { 3, 1.0F, 36U },
        ExpectedCase { 8, 0.5F, 96U },
    };

    for (const ExpectedCase& expected : cases) {
        SCOPED_TRACE(::testing::Message() << "segments=" << expected.segments << " profile=" << expected.profile);
        SideExtrusionFixture fixture = make_side_extrusion_fixture();
        Document document = fixture.document;
        Selection selection = side_extrusion_edge_selection(fixture);
        const Selection original_selection = selection;
        EXPECT_TRUE(document.vertices.size() == 24U);
        EXPECT_TRUE(document.faces.size() == 18U);
        SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
        EXPECT_TRUE(every_edge_is_closed(document));

        const std::vector<ElementId> three_edge_corners = selected_edge_vertices_with_count(selection, 3);
        EXPECT_TRUE(three_edge_corners.size() == fixture.extrusion_corners.size());
        const std::map<ElementId, ElementId> support_corner_faces =
            support_corner_faces_for_roots(document, selection, fixture.root_corners);
        EXPECT_TRUE(support_corner_faces.size() == fixture.root_corners.size());
        std::map<ElementId, quader::QVec3> source_corner_positions;
        for (const ElementId vertex_id : fixture.root_corners) {
            const Vertex* vertex = find_vertex(document, vertex_id);
            EXPECT_TRUE(vertex != nullptr);
            source_corner_positions.emplace(vertex_id, vertex->position);
        }

        EdgeBevelSettings settings;
        settings.width = 0.12F;
        settings.profile = expected.profile;
        settings.profile_type = BevelProfileType::Offset;
        settings.segments = expected.segments;

        const OperationResult result = bevel_selected_edges(document, selection, settings);

        SCOPED_TRACE(::testing::Message() << result.message);
        EXPECT_TRUE(result.changed);
        qdr_test::expect_valid_polygon_document_after_success(document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() >= expected.min_selected_faces);
        EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), face_has_repeated_vertices));
        EXPECT_TRUE(every_face_compiles_to_triangles(document));
        SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
        EXPECT_TRUE(every_edge_is_closed(document));
        EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
        require_original_selected_edges_consumed(document, original_selection);
        require_single_support_corner_attach(document, source_corner_positions, support_corner_faces, 0.25F);
        require_outer_patch_support_vertices(document, source_corner_positions, settings.width);
        if ((settings.segments % 2) == 1) {
            require_single_generated_center_ngon_near_source_corners(
                document,
                selection,
                source_corner_positions,
                0.35F,
                5U);
        }
        if (expected.profile >= 0.95F) {
            require_no_generated_position_poles_near_source_corners(
                document,
                selection,
                source_corner_positions,
                0.35F,
                settings.width * 0.05F);
        }
        require_generated_faces_point_away_from_source_corners(document, selection, source_corner_positions, 0.35F);
        require_no_generated_triangles_near_source_corners(document, selection, source_corner_positions, 0.35F);
        if (expected.segments == 3 && expected.profile > 0.45F && expected.profile < 0.55F) {
            require_patch_miter_center_rows_continue_source_side_lines(
                document,
                selection,
                source_corner_positions,
                0.35F);
        }
        for (const ElementId vertex_id : three_edge_corners) {
            EXPECT_TRUE(find_vertex(document, vertex_id) == nullptr);
        }
    }
}

TEST(QdrDocumentOperationBevelEdgesTests, ParallelTopOpeningEdgeBevelsCloseDoorwayTerminalPatches) {
    SideExtrusionFixture fixture = make_side_extrusion_fixture();
    Document document = fixture.document;

    Selection selection;
    selection.mode = SelectionMode::Edge;
    selection.edges = {
        make_edge(fixture.root_corners[1], fixture.extrusion_corners[5]),
        make_edge(fixture.root_corners[2], fixture.extrusion_corners[6]),
    };
    selection.has_active = true;
    selection.active_kind = ElementKind::Edge;
    selection.active_edge = selection.edges.front();

    EdgeBevelSettings settings;
    settings.width = 0.12F;
    settings.profile = 0.5F;
    settings.profile_type = BevelProfileType::Offset;
    settings.segments = 3;

    const OperationResult result = bevel_selected_edges(document, selection, settings);

    SCOPED_TRACE(::testing::Message() << result.message);
    EXPECT_TRUE(result.changed);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_TRUE(selection.mode == SelectionMode::Face);
    EXPECT_FALSE(selection.faces.empty());
    EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), face_has_repeated_vertices));
    EXPECT_TRUE(every_face_compiles_to_triangles(document));
    SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
    SCOPED_TRACE(::testing::Message() << doorway_top_region_summary(document));
    EXPECT_TRUE(every_edge_is_closed(document));
    EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
    const std::array<std::pair<ElementId, quader::QVec3>, 4> forbidden_outer_corner_edges {{
        { fixture.root_corners[1], { 1.0F, 0.35F, -1.0F } },
        { fixture.root_corners[1], { 1.0F, 1.0F, -0.35F } },
        { fixture.root_corners[2], { 1.0F, 0.35F, 1.0F } },
        { fixture.root_corners[2], { 1.0F, 1.0F, 0.35F } },
    }};
    for (const auto& [terminal_vertex_id, neighbor_position] : forbidden_outer_corner_edges) {
        const ElementId neighbor_id = vertex_near_position(document, neighbor_position);
        EXPECT_TRUE(edge_use_count(document, make_edge(terminal_vertex_id, neighbor_id)) == 0);
    }
    const std::array<quader::QVec3, 4> expected_terminal_boundary_vertices {{
        { 1.0F, 0.47F, -0.35F },
        { 1.0F, 0.35F, -0.47F },
        { 1.0F, 0.47F, 0.35F },
        { 1.0F, 0.35F, 0.47F },
    }};
    for (const quader::QVec3 boundary_position : expected_terminal_boundary_vertices) {
        EXPECT_TRUE(has_vertex_near_position(document, boundary_position));
    }
    const std::array<std::pair<quader::QVec3, quader::QVec3>, 4> forbidden_long_terminal_diagonals {{
        { { 1.0F, 1.0F, -0.35F }, { 1.0F, 0.35F, -0.23F } },
        { { 1.0F, 0.35F, -1.0F }, { 1.0F, 0.23F, -0.35F } },
        { { 1.0F, 1.0F, 0.35F }, { 1.0F, 0.35F, 0.23F } },
        { { 1.0F, 0.35F, 1.0F }, { 1.0F, 0.23F, 0.35F } },
    }};
    for (const auto& [source_face_neighbor, bevel_profile_position] : forbidden_long_terminal_diagonals) {
        EXPECT_TRUE(edge_use_count_between_positions(document, source_face_neighbor, bevel_profile_position) == 0);
    }
    for (const ElementId terminal_vertex_id : {
             fixture.root_corners[1],
             fixture.root_corners[2],
             fixture.extrusion_corners[5],
             fixture.extrusion_corners[6],
         }) {
        EXPECT_TRUE(find_vertex(document, terminal_vertex_id) == nullptr);
    }
}

TEST(QdrDocumentOperationBevelEdgesTests, AdjacentOpeningEdgeBevelClosesOppositeTerminalPatch) {
    SideExtrusionFixture fixture = make_side_extrusion_fixture();
    Document document = fixture.document;

    Selection selection;
    selection.mode = SelectionMode::Edge;
    selection.edges = {
        make_edge(fixture.root_corners[0], fixture.root_corners[1]),
        make_edge(fixture.root_corners[1], fixture.root_corners[2]),
    };
    selection.has_active = true;
    selection.active_kind = ElementKind::Edge;
    selection.active_edge = selection.edges.front();

    EdgeBevelSettings settings;
    settings.width = 0.12F;
    settings.profile = 0.5F;
    settings.profile_type = BevelProfileType::Offset;
    settings.segments = 3;

    const OperationResult result = bevel_selected_edges(document, selection, settings);

    SCOPED_TRACE(::testing::Message() << result.message);
    EXPECT_TRUE(result.changed);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_TRUE(selection.mode == SelectionMode::Face);
    EXPECT_FALSE(selection.faces.empty());
    EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), face_has_repeated_vertices));
    EXPECT_TRUE(every_face_compiles_to_triangles(document));
    SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
    SCOPED_TRACE(::testing::Message() << doorway_top_region_summary(document));
    EXPECT_TRUE(every_edge_is_closed(document));
    EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
}

TEST(QdrDocumentOperationBevelEdgesTests, OppositeSideAndBottomOpeningBevelClosesBottomTerminalPatches) {
    SideExtrusionFixture fixture = make_side_extrusion_fixture();
    Document document = fixture.document;

    Selection selection;
    selection.mode = SelectionMode::Edge;
    selection.edges = {
        make_edge(fixture.root_corners[0], fixture.root_corners[1]),
        make_edge(fixture.root_corners[3], fixture.root_corners[2]),
        make_edge(fixture.root_corners[0], fixture.extrusion_corners[4]),
        make_edge(fixture.root_corners[3], fixture.extrusion_corners[7]),
    };
    selection.has_active = true;
    selection.active_kind = ElementKind::Edge;
    selection.active_edge = selection.edges.front();

    EdgeBevelSettings settings;
    settings.width = 0.12F;
    settings.profile = 0.5F;
    settings.profile_type = BevelProfileType::Offset;
    settings.segments = 3;

    const OperationResult result = bevel_selected_edges(document, selection, settings);

    SCOPED_TRACE(::testing::Message() << result.message);
    EXPECT_TRUE(result.changed);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_TRUE(selection.mode == SelectionMode::Face);
    EXPECT_FALSE(selection.faces.empty());
    EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), face_has_repeated_vertices));
    EXPECT_TRUE(every_face_compiles_to_triangles(document));
    SCOPED_TRACE(::testing::Message() << open_edge_summary(document));
    SCOPED_TRACE(::testing::Message() << doorway_top_region_summary(document));
    EXPECT_TRUE(every_edge_is_closed(document));
    EXPECT_TRUE(every_shared_edge_has_opposite_winding(document));
}
