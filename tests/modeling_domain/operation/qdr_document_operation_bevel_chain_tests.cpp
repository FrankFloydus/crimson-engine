////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

using namespace quader_poly;

/// Captures the authored support before a second bevel crosses existing bands.
struct BevelChainFixture {
  Document document;
  Selection selection;
  std::set<ElementId> chain_vertices;
};

void make_top_beveled_cube_chain(BevelChainFixture& fixture, int first_segments,
                                int corner_count = 1) {
  ASSERT_TRUE(corner_count == 1 || corner_count == 2 || corner_count == 4);
  CubePrimitiveSpec spec;
  spec.width = 4.0F;
  spec.height = 4.0F;
  spec.depth = 4.0F;
  fixture.document = make_cube(spec);
  fixture.selection.mode = SelectionMode::Edge;
  for (const Edge edge : document_edges(fixture.document)) {
    const Vertex* first = find_vertex(fixture.document, edge.a);
    const Vertex* second = find_vertex(fixture.document, edge.b);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    if (std::abs(first->position.y - 2.0F) < 0.0001F &&
        std::abs(second->position.y - 2.0F) < 0.0001F) {
      fixture.selection.edges.push_back(edge);
    }
  }
  ASSERT_EQ(fixture.selection.edges.size(), 4U);
  fixture.selection.has_active = true;
  fixture.selection.active_kind = ElementKind::Edge;
  fixture.selection.active_edge = fixture.selection.edges.front();

  EdgeBevelSettings settings;
  settings.width = 0.5F;
  settings.profile = 0.5F;
  settings.segments = first_segments;
  const OperationResult result =
      bevel_selected_edges(fixture.document, fixture.selection, settings);
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  qdr_test::expect_valid_polygon_document(fixture.document, "first top perimeter bevel");

  fixture.selection = {};
  fixture.selection.mode = SelectionMode::Edge;
  for (const Edge edge : document_edges(fixture.document)) {
    const Vertex* first = find_vertex(fixture.document, edge.a);
    const Vertex* second = find_vertex(fixture.document, edge.b);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    const auto on_corner = [corner_count](quader::QVec3 position) {
      return (corner_count >= 2 || position.x > 0.0F) &&
             (corner_count >= 4 || position.z > 0.0F) &&
             std::abs(std::abs(position.x) - std::abs(position.z)) < 0.0001F;
    };
    if (on_corner(first->position) && on_corner(second->position) &&
        std::abs(first->position.y - second->position.y) > 0.0001F) {
      fixture.selection.edges.push_back(edge);
      fixture.chain_vertices.insert(edge.a);
      fixture.chain_vertices.insert(edge.b);
    }
  }
  ASSERT_EQ(fixture.selection.edges.size(),
            static_cast<std::size_t>((first_segments + 1) * corner_count));
  ASSERT_EQ(fixture.chain_vertices.size(),
            static_cast<std::size_t>((first_segments + 2) * corner_count));
  fixture.selection.has_active = true;
  fixture.selection.active_kind = ElementKind::Edge;
  fixture.selection.active_edge = fixture.selection.edges.front();
}

bool point_on_segment(quader::QVec3 point, quader::QVec3 first,
                      quader::QVec3 second) {
  constexpr float kTolerance = 0.0001F;
  const quader::QVec3 direction = second - first;
  const float squared_length = length_squared(direction);
  if (squared_length <= kTolerance * kTolerance) {
    return false;
  }
  const float fraction = dot(point - first, direction) / squared_length;
  return fraction > kTolerance && fraction < 1.0F - kTolerance &&
      length_squared(point - (first + direction * fraction)) <=
          kTolerance * kTolerance;
}

void expect_original_support_retained(const BevelChainFixture& fixture,
                                     const Document& result,
                                     const Selection* square_profile_selection = nullptr) {
  std::set<ElementId> square_profile_vertices;
  if (square_profile_selection != nullptr) {
    for (const ElementId face_id : square_profile_selection->faces) {
      const Face* face = find_face(result, face_id);
      ASSERT_NE(face, nullptr);
      square_profile_vertices.insert(face->vertices.begin(), face->vertices.end());
    }
  }
  for (const Vertex& source_vertex : fixture.document.vertices) {
    if (fixture.chain_vertices.contains(source_vertex.id)) {
      continue;
    }
    SCOPED_TRACE(::testing::Message() << "remote vertex=" << source_vertex.id);
    const Vertex* retained = find_vertex(result, source_vertex.id);
    ASSERT_NE(retained, nullptr);
    EXPECT_FLOAT_EQ(retained->position.x, source_vertex.position.x);
    EXPECT_FLOAT_EQ(retained->position.y, source_vertex.position.y);
    EXPECT_FLOAT_EQ(retained->position.z, source_vertex.position.z);
  }

  for (const Face& source_face : fixture.document.faces) {
    SCOPED_TRACE(::testing::Message() << "retained face=" << source_face.id);
    const Face* retained = find_face(result, source_face.id);
    ASSERT_NE(retained, nullptr);
    const Vertex* origin = find_vertex(fixture.document, source_face.vertices.front());
    ASSERT_NE(origin, nullptr);
    const quader::QVec3 normal = face_normal(fixture.document, source_face);
    for (const ElementId vertex_id : retained->vertices) {
      const Vertex* vertex = find_vertex(result, vertex_id);
      ASSERT_NE(vertex, nullptr);
      EXPECT_NEAR(dot(normal, vertex->position - origin->position), 0.0F, 0.0001F)
          << "vertex=" << vertex_id << " position=(" << vertex->position.x << ','
          << vertex->position.y << ',' << vertex->position.z << ')';
    }
  }

  const std::vector<Edge> result_edges = document_edges(result);
  std::size_t checked_support_ends = 0U;
  for (const Edge source_edge : document_edges(fixture.document)) {
    const bool first_on_chain = fixture.chain_vertices.contains(source_edge.a);
    const bool second_on_chain = fixture.chain_vertices.contains(source_edge.b);
    if ((!first_on_chain && !second_on_chain) ||
        std::find(fixture.selection.edges.begin(), fixture.selection.edges.end(), source_edge) !=
            fixture.selection.edges.end()) {
      continue;
    }
    const std::size_t cut_ends = static_cast<std::size_t>(first_on_chain) +
                                 static_cast<std::size_t>(second_on_chain);
    checked_support_ends += cut_ends;
    const ElementId root_id = first_on_chain ? source_edge.a : source_edge.b;
    const ElementId remote_id = first_on_chain ? source_edge.b : source_edge.a;
    const Vertex* root = find_vertex(fixture.document, root_id);
    const Vertex* remote = find_vertex(fixture.document, remote_id);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(remote, nullptr);
    std::size_t interior_vertices = 0U;
    for (const Vertex& vertex : result.vertices) {
      if (point_on_segment(vertex.position, root->position, remote->position)) {
        ++interior_vertices;
        if (square_profile_selection != nullptr) {
          EXPECT_TRUE(square_profile_vertices.contains(vertex.id))
              << "Square-profile support samples must belong to the generated strip, vertex="
              << vertex.id;
        }
      }
    }
    if (square_profile_selection != nullptr) {
      // A square profile follows the trimmed-away support rays, so its authored
      // intermediate profile samples legitimately lie on those original edges.
      EXPECT_GE(interior_vertices, cut_ends);
    } else {
      EXPECT_EQ(interior_vertices, cut_ends)
          << "Support edge " << source_edge.a << '-' << source_edge.b
          << " must contain one cut vertex per selected endpoint, without retraced slivers";
    }

    std::size_t retained_rails = 0U;
    for (const Edge edge : result_edges) {
      if (cut_ends == 2U) {
        const Vertex* first = find_vertex(result, edge.a);
        const Vertex* second = find_vertex(result, edge.b);
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        if (point_on_segment(first->position, root->position, remote->position) &&
            point_on_segment(second->position, root->position, remote->position)) {
          ++retained_rails;
        }
        continue;
      }
      if (edge.a != remote_id && edge.b != remote_id) {
        continue;
      }
      const ElementId rail_id = edge.a == remote_id ? edge.b : edge.a;
      const Vertex* rail = find_vertex(result, rail_id);
      ASSERT_NE(rail, nullptr);
      if (find_vertex(fixture.document, rail_id) == nullptr &&
          point_on_segment(rail->position, root->position, remote->position)) {
        ++retained_rails;
      }
    }
    EXPECT_EQ(retained_rails, 1U)
        << "Unselected band edge " << root_id << '-' << remote_id
        << " must terminate once on its original support line";
  }
  EXPECT_EQ(checked_support_ends, 2U * fixture.chain_vertices.size());
}

void expect_shared_profile_rows(const BevelChainFixture& fixture,
                                const Document& result,
                                const Selection& selection,
                                int second_segments) {
  std::map<Edge, std::size_t, decltype([](const Edge& left, const Edge& right) {
    return std::pair{left.a, left.b} < std::pair{right.a, right.b};
  })> selected_edge_uses;
  for (const ElementId face_id : selection.faces) {
    const Face* face = find_face(result, face_id);
    ASSERT_NE(face, nullptr);
    for (std::size_t index = 0U; index < face->vertices.size(); ++index) {
      ++selected_edge_uses[make_edge(face->vertices[index],
          face->vertices[(index + 1U) % face->vertices.size()])];
    }
  }
  std::map<ElementId, std::vector<ElementId>> shared_neighbors;
  std::set<ElementId> strip_boundary_vertices;
  for (const auto& [edge, count] : selected_edge_uses) {
    if (count == 2U) {
      shared_neighbors[edge.a].push_back(edge.b);
      shared_neighbors[edge.b].push_back(edge.a);
    } else if (count == 1U) {
      strip_boundary_vertices.insert(edge.a);
      strip_boundary_vertices.insert(edge.b);
    }
  }
  const std::vector<Edge> source_edges = document_edges(fixture.document);
  for (const ElementId root_id : fixture.chain_vertices) {
    const auto selected_degree = std::count_if(
        fixture.selection.edges.begin(), fixture.selection.edges.end(),
        [root_id](const Edge edge) { return edge.a == root_id || edge.b == root_id; });
    if (selected_degree != 2) {
      continue;
    }
    const Vertex* root = find_vertex(fixture.document, root_id);
    ASSERT_NE(root, nullptr);
    std::vector<ElementId> rails;
    for (const Edge edge : source_edges) {
      if ((edge.a != root_id && edge.b != root_id) ||
          std::find(fixture.selection.edges.begin(), fixture.selection.edges.end(), edge) !=
              fixture.selection.edges.end()) {
        continue;
      }
      const Vertex* remote = find_vertex(fixture.document, edge.a == root_id ? edge.b : edge.a);
      ASSERT_NE(remote, nullptr);
      ElementId closest_id = kInvalidElementId;
      float closest_squared = std::numeric_limits<float>::max();
      for (const Vertex& vertex : result.vertices) {
        const float squared = length_squared(vertex.position - root->position);
        // Square-profile interior samples can also lie on this original edge.
        // Only the interface with retained faces is a profile-row endpoint.
        if (strip_boundary_vertices.contains(vertex.id) && squared < closest_squared &&
            point_on_segment(vertex.position, root->position, remote->position)) {
          closest_id = vertex.id;
          closest_squared = squared;
        }
      }
      ASSERT_NE(closest_id, kInvalidElementId) << "root=" << root_id;
      rails.push_back(closest_id);
    }
    ASSERT_EQ(rails.size(), 2U) << "root=" << root_id;
    std::map<ElementId, int> distances{{rails.front(), 0}};
    std::vector<ElementId> pending{rails.front()};
    for (std::size_t index = 0U; index < pending.size(); ++index) {
      const ElementId vertex_id = pending[index];
      for (const ElementId neighbor : shared_neighbors[vertex_id]) {
        if (distances.emplace(neighbor, distances.at(vertex_id) + 1).second) {
          pending.push_back(neighbor);
        }
      }
    }
    ASSERT_TRUE(distances.contains(rails.back())) << "root=" << root_id;
    EXPECT_EQ(distances.at(rails.back()), second_segments)
        << "Adjacent strips must share a full profile row by vertex identity at root=" << root_id;
  }
}

void expect_closed_quad_strip(const BevelChainFixture& fixture,
                              const Document& result,
                              const Selection& selection,
                              int second_segments) {
  qdr_test::expect_valid_polygon_document(result, "second bevel across existing bands");
  ASSERT_EQ(selection.mode, SelectionMode::Face);
  ASSERT_EQ(selection.faces.size(),
            fixture.selection.edges.size() * static_cast<std::size_t>(second_segments));
  const std::set<ElementId> selected_faces(selection.faces.begin(), selection.faces.end());
  EXPECT_EQ(selected_faces.size(), selection.faces.size());
  for (const ElementId face_id : selection.faces) {
    const Face* face = find_face(result, face_id);
    ASSERT_NE(face, nullptr);
    EXPECT_EQ(find_face(fixture.document, face_id), nullptr);
    EXPECT_EQ(face->vertices.size(), 4U) << "strip face=" << face_id;
  }

  std::map<std::pair<ElementId, ElementId>, std::vector<Edge>> edge_uses;
  for (const Face& face : result.faces) {
    ASSERT_GE(face.vertices.size(), 3U);
    EXPECT_FALSE(qdr_test::face_has_repeated_vertices(face));
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      const ElementId first = face.vertices[index];
      const ElementId second = face.vertices[(index + 1U) % face.vertices.size()];
      edge_uses[{std::min(first, second), std::max(first, second)}].push_back({first, second});
    }
  }
  for (const auto& [edge, uses] : edge_uses) {
    SCOPED_TRACE(::testing::Message() << "edge=" << edge.first << '-' << edge.second);
    ASSERT_EQ(uses.size(), 2U);
    EXPECT_EQ(uses.front().a, uses.back().b);
    EXPECT_EQ(uses.front().b, uses.back().a);
  }
  ASSERT_NO_FATAL_FAILURE(expect_shared_profile_rows(fixture, result, selection, second_segments));

  const CompiledMesh mesh = compile_document(result);
  ASSERT_EQ(mesh.primitives.size(), result.faces.size());
  for (const CompiledPrimitive& primitive : mesh.primitives) {
    ASSERT_GE(primitive.index_count, 3U);
    ASSERT_EQ(primitive.index_count % 3U, 0U);
    const std::size_t end = static_cast<std::size_t>(primitive.index_offset) +
                            primitive.index_count;
    ASSERT_LE(end, mesh.indices.size());
    for (std::size_t index = primitive.index_offset; index < end; index += 3U) {
      ASSERT_LT(mesh.indices[index], mesh.vertices.size());
      ASSERT_LT(mesh.indices[index + 1U], mesh.vertices.size());
      ASSERT_LT(mesh.indices[index + 2U], mesh.vertices.size());
      const quader::QVec3 first = mesh.vertices[mesh.indices[index]].position;
      const quader::QVec3 second = mesh.vertices[mesh.indices[index + 1U]].position;
      const quader::QVec3 third = mesh.vertices[mesh.indices[index + 2U]].position;
      EXPECT_GT(length_squared(cross(second - first, third - first)), 1.0e-12F);
    }
  }
}

void expect_successive_corner_bevel(int first_segments, int second_segments,
                                   float second_width, bool rotate_fixture = false,
                                   float profile = 0.5F, int corner_count = 1) {
  BevelChainFixture fixture;
  ASSERT_NO_FATAL_FAILURE(make_top_beveled_cube_chain(fixture, first_segments, corner_count));
  if (rotate_fixture) {
    const quader::QVec3 axis = normalize_or_zero(quader::QVec3{1.0F, 2.0F, 3.0F});
    const float cosine = std::cos(0.71F);
    const float sine = std::sin(0.71F);
    for (Vertex& vertex : fixture.document.vertices) {
      const quader::QVec3 position = vertex.position;
      vertex.position = position * cosine + cross(axis, position) * sine +
          axis * (dot(axis, position) * (1.0F - cosine)) +
          quader::QVec3{7.25F, -3.5F, 11.0F};
    }
  }
  Document candidate = fixture.document;
  Selection selection = fixture.selection;
  EdgeBevelSettings settings;
  settings.width = second_width;
  settings.profile = profile;
  settings.segments = second_segments;
  const OperationResult result = bevel_selected_edges(candidate, selection, settings);
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_TRUE(result.changed);
  ASSERT_NO_FATAL_FAILURE(expect_original_support_retained(
      fixture, candidate, profile == 1.0F ? &selection : nullptr));
  ASSERT_NO_FATAL_FAILURE(expect_closed_quad_strip(fixture, candidate, selection, second_segments));
}

void expect_document_unchanged(const Document& expected, const Document& actual) {
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  ASSERT_EQ(actual.faces.size(), expected.faces.size());
  EXPECT_EQ(actual.hard_normal_edges, expected.hard_normal_edges);
  EXPECT_EQ(actual.soft_normal_edges, expected.soft_normal_edges);
  EXPECT_EQ(actual.next_vertex_id, expected.next_vertex_id);
  EXPECT_EQ(actual.next_face_id, expected.next_face_id);
  for (std::size_t index = 0U; index < expected.vertices.size(); ++index) {
    const Vertex& source = expected.vertices[index];
    const Vertex& result = actual.vertices[index];
    EXPECT_EQ(result.id, source.id);
    EXPECT_FLOAT_EQ(result.position.x, source.position.x);
    EXPECT_FLOAT_EQ(result.position.y, source.position.y);
    EXPECT_FLOAT_EQ(result.position.z, source.position.z);
  }
  for (std::size_t index = 0U; index < expected.faces.size(); ++index) {
    const Face& source = expected.faces[index];
    const Face& result = actual.faces[index];
    EXPECT_EQ(result.id, source.id);
    EXPECT_EQ(result.vertices, source.vertices);
    EXPECT_EQ(result.material_slot, source.material_slot);
    EXPECT_EQ(result.normal_shading, source.normal_shading);
    EXPECT_EQ(result.texture_transform, source.texture_transform);
    ASSERT_EQ(result.uvs.size(), source.uvs.size());
    for (std::size_t corner = 0U; corner < source.uvs.size(); ++corner) {
      EXPECT_FLOAT_EQ(result.uvs[corner].x, source.uvs[corner].x);
      EXPECT_FLOAT_EQ(result.uvs[corner].y, source.uvs[corner].y);
    }
  }
}

void expect_selection_unchanged(const Selection& expected, const Selection& actual) {
  EXPECT_EQ(actual.mode, expected.mode);
  EXPECT_EQ(actual.vertices, expected.vertices);
  EXPECT_EQ(actual.edges, expected.edges);
  EXPECT_EQ(actual.faces, expected.faces);
  EXPECT_EQ(actual.has_active, expected.has_active);
  EXPECT_EQ(actual.active_kind, expected.active_kind);
  EXPECT_EQ(actual.active_vertex, expected.active_vertex);
  EXPECT_EQ(actual.active_edge, expected.active_edge);
  EXPECT_EQ(actual.active_face, expected.active_face);
}

std::vector<ElementId> oriented_face_key(std::vector<ElementId> vertices) {
  if (!vertices.empty()) {
    std::rotate(vertices.begin(), std::min_element(vertices.begin(), vertices.end()), vertices.end());
  }
  return vertices;
}

void expect_same_selected_geometry(const Document& expected, const Selection& expected_selection,
                                  const Document& actual, const Selection& actual_selection) {
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  ASSERT_EQ(actual.faces.size(), expected.faces.size());
  std::map<ElementId, ElementId> vertex_ids;
  std::set<ElementId> matched_ids;
  for (const Vertex& vertex : expected.vertices) {
    const auto found = std::find_if(actual.vertices.begin(), actual.vertices.end(),
        [&](const Vertex& candidate) {
          return !matched_ids.contains(candidate.id) &&
              length_squared(candidate.position - vertex.position) <= 1.0e-8F;
        });
    ASSERT_NE(found, actual.vertices.end()) << "No equivalent position for vertex=" << vertex.id;
    vertex_ids.emplace(vertex.id, found->id);
    matched_ids.insert(found->id);
  }
  std::map<std::vector<ElementId>, ElementId> actual_faces;
  for (const Face& face : actual.faces) {
    ASSERT_TRUE(actual_faces.emplace(oriented_face_key(face.vertices), face.id).second);
  }
  const std::set<ElementId> expected_selected(expected_selection.faces.begin(),
                                               expected_selection.faces.end());
  std::set<ElementId> mapped_selection;
  for (const Face& face : expected.faces) {
    std::vector<ElementId> mapped_vertices;
    for (const ElementId vertex_id : face.vertices) {
      mapped_vertices.push_back(vertex_ids.at(vertex_id));
    }
    const auto found = actual_faces.find(oriented_face_key(std::move(mapped_vertices)));
    ASSERT_NE(found, actual_faces.end()) << "No equivalently oriented face=" << face.id;
    if (expected_selected.contains(face.id)) {
      mapped_selection.insert(found->second);
    }
  }
  EXPECT_EQ(actual_selection.mode, expected_selection.mode);
  EXPECT_EQ(std::set<ElementId>(actual_selection.faces.begin(), actual_selection.faces.end()),
            mapped_selection);
}

} // namespace

TEST(QdrDocumentOperationBevelChainTests, CornerBevelPreservesTheExistingTwoBandTopBevel) {
  ASSERT_NO_FATAL_FAILURE(expect_successive_corner_bevel(2, 2, 0.2F));
}

TEST(QdrDocumentOperationBevelChainTests, CornerChainKeepsSupportAcrossSegmentAndWidthChoices) {
  constexpr std::array<int, 3> kFirstSegments{1, 3, 5};
  constexpr std::array<int, 3> kSecondSegments{1, 3, 5};
  for (const int first_segments : kFirstSegments) {
    for (const int second_segments : kSecondSegments) {
      for (const float width : {0.1F, 0.35F}) {
        SCOPED_TRACE(::testing::Message() << "first_segments=" << first_segments
                                        << " second_segments=" << second_segments
                                        << " second_width=" << width);
        ASSERT_NO_FATAL_FAILURE(expect_successive_corner_bevel(first_segments, second_segments, width));
      }
    }
  }
}

TEST(QdrDocumentOperationBevelChainTests, CornerChainSupportDoesNotDependOnWorldAxes) {
  ASSERT_NO_FATAL_FAILURE(expect_successive_corner_bevel(2, 3, 0.2F, true));
}

TEST(QdrDocumentOperationBevelChainTests, ProfilesPreserveSharedRowsAndOriginalSupport) {
  for (const float profile : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
    SCOPED_TRACE(::testing::Message() << "profile=" << profile);
    ASSERT_NO_FATAL_FAILURE(expect_successive_corner_bevel(2, 3, 0.2F, false, profile));
  }
}

TEST(QdrDocumentOperationBevelChainTests, AdjacentAndAllCornerChainsTrimSharedBandsCleanly) {
  for (const int corner_count : {2, 4}) {
    for (const int second_segments : {1, 3}) {
      SCOPED_TRACE(::testing::Message() << "corners=" << corner_count
                                      << " second_segments=" << second_segments);
      ASSERT_NO_FATAL_FAILURE(expect_successive_corner_bevel(
          2, second_segments, 0.2F, false, 0.5F, corner_count));
    }
  }
}

TEST(QdrDocumentOperationBevelChainTests, SelectedEdgeOrderDoesNotChangeConnectedStripGeometry) {
  for (const int corner_count : {1, 4}) {
    SCOPED_TRACE(::testing::Message() << "corners=" << corner_count);
    BevelChainFixture fixture;
    ASSERT_NO_FATAL_FAILURE(make_top_beveled_cube_chain(fixture, 2, corner_count));
    EdgeBevelSettings settings;
    settings.width = 0.2F;
    settings.profile = 0.25F;
    settings.segments = 3;
    Document forward = fixture.document;
    Selection forward_selection = fixture.selection;
    const OperationResult forward_result = bevel_selected_edges(forward, forward_selection, settings);
    ASSERT_TRUE(forward_result.success) << forward_result.message;
    ASSERT_TRUE(forward_result.changed);
    Document reversed = fixture.document;
    Selection reversed_selection = fixture.selection;
    std::reverse(reversed_selection.edges.begin(), reversed_selection.edges.end());
    const OperationResult reversed_result = bevel_selected_edges(reversed, reversed_selection, settings);
    ASSERT_TRUE(reversed_result.success) << reversed_result.message;
    ASSERT_TRUE(reversed_result.changed);
    ASSERT_NO_FATAL_FAILURE(expect_same_selected_geometry(
        forward, forward_selection, reversed, reversed_selection));
    ASSERT_NO_FATAL_FAILURE(expect_original_support_retained(fixture, reversed));
    ASSERT_NO_FATAL_FAILURE(expect_closed_quad_strip(fixture, reversed, reversed_selection, 3));
  }
}

TEST(QdrDocumentOperationBevelChainTests, OverwideNarrowChainsClampReplayablyOrRejectWithoutMutation) {
  for (const int corner_count : {2, 4}) {
    SCOPED_TRACE(::testing::Message() << "corners=" << corner_count);
    BevelChainFixture fixture;
    ASSERT_NO_FATAL_FAILURE(make_top_beveled_cube_chain(fixture, 2, corner_count));
    for (Vertex& vertex : fixture.document.vertices) {
      vertex.position = vertex.position * 0.125F;
    }
    Document candidate = fixture.document;
    Selection selection = fixture.selection;
    EdgeBevelSettings settings;
    settings.width = 1.25F;
    settings.profile = 0.5F;
    settings.segments = 3;
    const OperationResult result = bevel_selected_edges(candidate, selection, settings);
    if (!result.success) {
      EXPECT_FALSE(result.changed);
      ASSERT_NO_FATAL_FAILURE(expect_document_unchanged(fixture.document, candidate));
      ASSERT_NO_FATAL_FAILURE(expect_selection_unchanged(fixture.selection, selection));
      continue;
    }
    ASSERT_TRUE(result.changed);
    ASSERT_TRUE(result.edge_bevel_width.has_value());
    EXPECT_TRUE(result.edge_bevel_width->support_limited);
    EXPECT_GT(result.edge_bevel_width->applied_width, 0.0F);
    EXPECT_LT(result.edge_bevel_width->applied_width, settings.width);
    ASSERT_NO_FATAL_FAILURE(expect_original_support_retained(fixture, candidate));
    ASSERT_NO_FATAL_FAILURE(expect_closed_quad_strip(fixture, candidate, selection, 3));
    Document replay = fixture.document;
    Selection replay_selection = fixture.selection;
    settings.width = result.edge_bevel_width->applied_width;
    const OperationResult replay_result = bevel_selected_edges(replay, replay_selection, settings);
    ASSERT_TRUE(replay_result.success) << replay_result.message;
    ASSERT_TRUE(replay_result.changed);
    ASSERT_NO_FATAL_FAILURE(expect_document_unchanged(candidate, replay));
    ASSERT_NO_FATAL_FAILURE(expect_selection_unchanged(selection, replay_selection));
  }
}

TEST(QdrDocumentOperationBevelChainTests, SkewSingleEdgeKeepsExactTerminalCutsAtSmallScale) {
  for (const float scale : {1.0F, 0.02F}) {
    SCOPED_TRACE(::testing::Message() << "scale=" << scale);
    BevelChainFixture fixture;
    CubePrimitiveSpec spec;
    spec.width = 4.0F;
    spec.height = 4.0F;
    spec.depth = 4.0F;
    fixture.document = make_cube(spec);
    fixture.selection.mode = SelectionMode::Edge;
    for (const Edge edge : document_edges(fixture.document)) {
      const Vertex* first = find_vertex(fixture.document, edge.a);
      const Vertex* second = find_vertex(fixture.document, edge.b);
      ASSERT_NE(first, nullptr);
      ASSERT_NE(second, nullptr);
      if (first->position.x == 2.0F && second->position.x == 2.0F &&
          first->position.z == 2.0F && second->position.z == 2.0F) {
        fixture.selection.edges.push_back(edge);
        fixture.chain_vertices.insert(edge.a);
        fixture.chain_vertices.insert(edge.b);
      }
    }
    ASSERT_EQ(fixture.selection.edges.size(), 1U);
    fixture.selection.has_active = true;
    fixture.selection.active_kind = ElementKind::Edge;
    fixture.selection.active_edge = fixture.selection.edges.front();
    for (Vertex& vertex : fixture.document.vertices) {
      const quader::QVec3 position = vertex.position;
      vertex.position = quader::QVec3{
          position.x + 0.45F * position.y + 0.25F * position.z,
          position.y,
          position.z + 0.3F * position.y} * scale;
    }

    Document candidate = fixture.document;
    Selection selection = fixture.selection;
    EdgeBevelSettings settings;
    settings.width = 0.2F * scale;
    settings.profile = 0.5F;
    settings.segments = 3;
    const OperationResult result = bevel_selected_edges(candidate, selection, settings);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_NO_FATAL_FAILURE(expect_original_support_retained(fixture, candidate));
    ASSERT_NO_FATAL_FAILURE(expect_closed_quad_strip(fixture, candidate, selection, 3));
  }
}

TEST(QdrDocumentOperationBevelChainTests, CornerStripKeepsTheSameRailSpacingAcrossEveryBandAndTerminal) {
  for (const int first_segments : {1, 2, 4}) {
    for (const float width : {0.1F, 0.2F}) {
      std::vector<std::pair<float, bool>> profile_cases{{0.5F, false}};
      if (first_segments == 2 && width == 0.2F) {
        profile_cases.insert(profile_cases.end(), {
            {0.0F, false}, {0.25F, false}, {0.75F, false}, {1.0F, false}, {0.5F, true}});
      }
      for (const auto& [profile, rotated] : profile_cases) {
        SCOPED_TRACE(::testing::Message() << "first_segments=" << first_segments
                                        << " second_width=" << width << " profile=" << profile
                                        << " rotated=" << rotated);
        BevelChainFixture fixture;
        ASSERT_NO_FATAL_FAILURE(make_top_beveled_cube_chain(fixture, first_segments));
        quader::QVec3 row_normal{0.0F, 1.0F, 0.0F};
        quader::QVec3 row_sort_axis{1.0F, 0.0F, -1.0F};
        if (rotated) {
          const auto rotate = [](quader::QVec3 position) {
            const quader::QVec3 axis = normalize_or_zero(quader::QVec3{1.0F, 2.0F, 3.0F});
            const float cosine = std::cos(0.71F);
            const float sine = std::sin(0.71F);
            return position * cosine + cross(axis, position) * sine +
                axis * (dot(axis, position) * (1.0F - cosine));
          };
          for (Vertex& vertex : fixture.document.vertices) {
            vertex.position = rotate(vertex.position) + quader::QVec3{7.25F, -3.5F, 11.0F};
          }
          row_normal = rotate(row_normal);
          row_sort_axis = rotate(row_sort_axis);
        }
        Document candidate = fixture.document;
        Selection selection = fixture.selection;
        EdgeBevelSettings settings;
        settings.width = width;
        settings.profile = profile;
        settings.segments = profile == 0.5F ? 2 : 3;
        const OperationResult result = bevel_selected_edges(candidate, selection, settings);
        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        ASSERT_NO_FATAL_FAILURE(expect_original_support_retained(
            fixture, candidate, profile == 1.0F ? &selection : nullptr));
        ASSERT_NO_FATAL_FAILURE(expect_closed_quad_strip(
            fixture, candidate, selection, settings.segments));

        std::set<ElementId> strip_vertices;
        for (const ElementId face_id : selection.faces) {
          const Face* face = find_face(candidate, face_id);
          ASSERT_NE(face, nullptr);
          strip_vertices.insert(face->vertices.begin(), face->vertices.end());
        }
        const std::vector<Edge> source_edges = document_edges(fixture.document);
        const std::vector<Edge> result_edges = document_edges(candidate);
        std::vector<quader::QVec3> reference_row_offsets;
        for (const ElementId root_id : fixture.chain_vertices) {
          const Vertex* root = find_vertex(fixture.document, root_id);
          ASSERT_NE(root, nullptr);
          SCOPED_TRACE(::testing::Message() << "root=" << root_id << " position=("
              << root->position.x << ',' << root->position.y << ',' << root->position.z << ')');
          std::vector<quader::QVec3> rail_positions;
          for (const Edge edge : source_edges) {
            if ((edge.a != root_id && edge.b != root_id) ||
                std::find(fixture.selection.edges.begin(), fixture.selection.edges.end(), edge) !=
                    fixture.selection.edges.end()) {
              continue;
            }
            const ElementId remote_id = edge.a == root_id ? edge.b : edge.a;
            const Vertex* remote = find_vertex(fixture.document, remote_id);
            ASSERT_NE(remote, nullptr);
            std::vector<quader::QVec3> support_cuts;
            for (const Edge result_edge : result_edges) {
              if (result_edge.a != remote_id && result_edge.b != remote_id) {
                continue;
              }
              const ElementId cut_id = result_edge.a == remote_id ? result_edge.b : result_edge.a;
              const Vertex* cut = find_vertex(candidate, cut_id);
              ASSERT_NE(cut, nullptr);
              if (point_on_segment(cut->position, root->position, remote->position)) {
                support_cuts.push_back(cut->position);
              }
            }
            ASSERT_EQ(support_cuts.size(), 1U) << "support=" << root_id << '-' << remote_id;
            const quader::QVec3 rail_position = support_cuts.front();
            const float cut_distance = std::sqrt(length_squared(rail_position - root->position));
            EXPECT_NEAR(cut_distance, width, 0.00001F)
                << "support=" << root_id << '-' << remote_id << " measured cut=" << cut_distance
                << " rail=(" << rail_position.x << ',' << rail_position.y << ',' << rail_position.z << ')';
            rail_positions.push_back(rail_position);
          }
          ASSERT_EQ(rail_positions.size(), 2U);
          // The cube's two support rays are orthogonal at every band and terminal.
          // Equal along-ray cuts must therefore retain the same transverse span.
          const float span = std::sqrt(length_squared(rail_positions[1] - rail_positions[0]));
          EXPECT_NEAR(span, std::sqrt(2.0F) * width, 0.00001F)
              << "The rounded corner strip must not widen toward the top cap";

          std::vector<quader::QVec3> row_offsets;
          for (const ElementId vertex_id : strip_vertices) {
            const Vertex* vertex = find_vertex(candidate, vertex_id);
            ASSERT_NE(vertex, nullptr);
            const quader::QVec3 offset = vertex->position - root->position;
            if (std::abs(dot(offset, row_normal)) <= 0.00001F) {
              row_offsets.push_back(offset);
            }
          }
          ASSERT_EQ(row_offsets.size(), static_cast<std::size_t>(settings.segments + 1))
              << "Every profile sample must remain at the original band's height";
          std::sort(row_offsets.begin(), row_offsets.end(),
              [row_sort_axis](quader::QVec3 left, quader::QVec3 right) {
                return dot(left, row_sort_axis) < dot(right, row_sort_axis);
              });
          if (reference_row_offsets.empty()) {
            reference_row_offsets = row_offsets;
          } else {
            ASSERT_EQ(row_offsets.size(), reference_row_offsets.size());
            for (std::size_t index = 0U; index < row_offsets.size(); ++index) {
              SCOPED_TRACE(::testing::Message() << "profile_sample=" << index);
              EXPECT_NEAR(row_offsets[index].x, reference_row_offsets[index].x, 0.00001F);
              EXPECT_NEAR(row_offsets[index].y, reference_row_offsets[index].y, 0.00001F);
              EXPECT_NEAR(row_offsets[index].z, reference_row_offsets[index].z, 0.00001F);
            }
          }
        }
      }
    }
  }
}

TEST(QdrDocumentOperationBevelChainTests, SkewSupportDistanceFeedbackMatchesFinalCutsAndReplays) {
  BevelChainFixture fixture;
  CubePrimitiveSpec spec;
  spec.width = 1.0F;
  spec.height = 1.0F;
  spec.depth = 1.0F;
  fixture.document = make_cube(spec);
  fixture.selection.mode = SelectionMode::Edge;
  for (const Edge edge : document_edges(fixture.document)) {
    const Vertex* first = find_vertex(fixture.document, edge.a);
    const Vertex* second = find_vertex(fixture.document, edge.b);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    if (first->position.x == 0.5F && second->position.x == 0.5F &&
        first->position.y == 0.5F && second->position.y == 0.5F) {
      fixture.selection.edges.push_back(edge);
      fixture.chain_vertices.insert(edge.a);
      fixture.chain_vertices.insert(edge.b);
    }
  }
  ASSERT_EQ(fixture.selection.edges.size(), 1U);
  fixture.selection.has_active = true;
  fixture.selection.active_kind = ElementKind::Edge;
  fixture.selection.active_edge = fixture.selection.edges.front();
  for (Vertex& vertex : fixture.document.vertices) {
    vertex.position.x += std::sqrt(3.0F) * vertex.position.z;
  }

  for (const float requested_width : {0.75F, 1.25F}) {
    SCOPED_TRACE(::testing::Message() << "requested_width=" << requested_width);
    Document candidate = fixture.document;
    Selection selection = fixture.selection;
    EdgeBevelSettings settings;
    settings.width = requested_width;
    settings.profile = 0.5F;
    settings.segments = 3;
    const OperationResult result = bevel_selected_edges(candidate, selection, settings);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_TRUE(result.edge_bevel_width.has_value());
    const bool expected_saturation = requested_width > 1.0F;
    const float expected_distance = expected_saturation ? 0.999F : requested_width;
    EXPECT_EQ(result.edge_bevel_width->support_limited, expected_saturation);
    EXPECT_FLOAT_EQ(result.edge_bevel_width->requested_width, requested_width);
    EXPECT_NEAR(result.edge_bevel_width->applied_width, expected_distance, 0.00001F);
    ASSERT_NO_FATAL_FAILURE(expect_original_support_retained(fixture, candidate));
    ASSERT_NO_FATAL_FAILURE(expect_closed_quad_strip(fixture, candidate, selection, 3));

    const std::vector<Edge> result_edges = document_edges(candidate);
    std::size_t checked_cuts = 0U;
    for (const Edge edge : document_edges(fixture.document)) {
      const bool first_selected = fixture.chain_vertices.contains(edge.a);
      const bool second_selected = fixture.chain_vertices.contains(edge.b);
      if (first_selected == second_selected) {
        continue;
      }
      const ElementId root_id = first_selected ? edge.a : edge.b;
      const ElementId remote_id = first_selected ? edge.b : edge.a;
      const Vertex* root = find_vertex(fixture.document, root_id);
      const Vertex* remote = find_vertex(fixture.document, remote_id);
      ASSERT_NE(root, nullptr);
      ASSERT_NE(remote, nullptr);
      EXPECT_NEAR(std::sqrt(length_squared(remote->position - root->position)), 1.0F, 0.00001F);
      for (const Edge result_edge : result_edges) {
        if (result_edge.a != remote_id && result_edge.b != remote_id) {
          continue;
        }
        const ElementId cut_id = result_edge.a == remote_id ? result_edge.b : result_edge.a;
        const Vertex* cut = find_vertex(candidate, cut_id);
        ASSERT_NE(cut, nullptr);
        if (point_on_segment(cut->position, root->position, remote->position)) {
          ++checked_cuts;
          EXPECT_NEAR(std::sqrt(length_squared(cut->position - root->position)),
                      expected_distance, 0.00001F);
        }
      }
    }
    EXPECT_EQ(checked_cuts, 4U);

    Document replay = fixture.document;
    Selection replay_selection = fixture.selection;
    settings.width = result.edge_bevel_width->applied_width;
    const OperationResult replay_result = bevel_selected_edges(replay, replay_selection, settings);
    ASSERT_TRUE(replay_result.success) << replay_result.message;
    ASSERT_TRUE(replay_result.changed);
    ASSERT_NO_FATAL_FAILURE(expect_document_unchanged(candidate, replay));
    ASSERT_NO_FATAL_FAILURE(expect_selection_unchanged(selection, replay_selection));
  }
}
