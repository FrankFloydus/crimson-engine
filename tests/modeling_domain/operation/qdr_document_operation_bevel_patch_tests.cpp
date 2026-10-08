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
#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

using namespace quader_poly;

/// Applies a positive uniform scale and a rigid transform to the test fixture.
struct PatchTransform {
  float scale = 1.0F;
  float angle = 0.0F;
  quader::QVec3 translation;
};

quader::QVec3 transformed_position(quader::QVec3 position,
                                 const PatchTransform& transform) {
  const quader::QVec3 axis = normalize_or_zero(quader::QVec3{1.0F, 2.0F, 3.0F});
  const float cosine = std::cos(transform.angle);
  const float sine = std::sin(transform.angle);
  const quader::QVec3 rotated = position * cosine +
      cross(axis, position) * sine +
      axis * (dot(axis, position) * (1.0F - cosine));
  return rotated * transform.scale + transform.translation;
}

Selection all_edge_selection(const Document& document) {
  Selection selection;
  selection.mode = SelectionMode::Edge;
  selection.edges = document_edges(document);
  selection.has_active = !selection.edges.empty();
  selection.active_kind = ElementKind::Edge;
  selection.active_edge = selection.edges.front();
  return selection;
}

std::vector<ElementId> oriented_loop_key(std::vector<ElementId> vertices) {
  if (!vertices.empty()) {
    std::rotate(vertices.begin(),
                std::min_element(vertices.begin(), vertices.end()),
                vertices.end());
  }
  return vertices;
}

void expect_closed_renderable_patch(const Document& document,
                                   const Selection& selection,
                                   const Document& source,
                                   float scale) {
  qdr_test::expect_valid_polygon_document(document, "transformed bevel patch");
  ASSERT_EQ(selection.mode, SelectionMode::Face);
  ASSERT_FALSE(selection.faces.empty());
  const std::set<ElementId> selected_faces(selection.faces.begin(),
                                            selection.faces.end());
  EXPECT_EQ(selected_faces.size(), selection.faces.size());
  for (const ElementId face_id : selected_faces) {
    EXPECT_NE(find_face(document, face_id), nullptr);
    EXPECT_EQ(find_face(source, face_id), nullptr);
  }

  std::map<std::pair<ElementId, ElementId>, std::vector<Edge>> edge_uses;
  for (const Face& face : document.faces) {
    SCOPED_TRACE(::testing::Message() << "face=" << face.id);
    ASSERT_GE(face.vertices.size(), 3U);
    EXPECT_FALSE(qdr_test::face_has_repeated_vertices(face));
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      const ElementId from = face.vertices[index];
      const ElementId to = face.vertices[(index + 1U) % face.vertices.size()];
      EXPECT_NE(from, to);
      const auto key = std::pair{std::min(from, to), std::max(from, to)};
      edge_uses[key].push_back(Edge{from, to});
    }
  }
  ASSERT_FALSE(edge_uses.empty());
  for (const auto& [edge, uses] : edge_uses) {
    SCOPED_TRACE(::testing::Message() << "edge=" << edge.first << '-' << edge.second);
    ASSERT_EQ(uses.size(), 2U);
    EXPECT_EQ(uses.front().a, uses.back().b);
    EXPECT_EQ(uses.front().b, uses.back().a);
  }
  const quader::QVec3 document_center = qdr_test::document_vertex_centroid(document);
  for (const Face& face : document.faces) {
    const quader::QVec3 centroid = qdr_test::face_centroid(document, face);
    const quader::QVec3 normal = face_normal(document, face);
    const quader::QVec3 center_delta = centroid - document_center;
    const float facing = dot(normal, center_delta);
    if (facing > 0.0F) {
      continue;
    }
    ::testing::Message details;
    details << "face=" << face.id << " selected=" << selected_faces.contains(face.id)
            << " centroid=(" << centroid.x << ',' << centroid.y << ',' << centroid.z << ')'
            << " normal=(" << normal.x << ',' << normal.y << ',' << normal.z << ')'
            << " document_center=(" << document_center.x << ',' << document_center.y
            << ',' << document_center.z << ')'
            << " center_delta=(" << center_delta.x << ',' << center_delta.y << ','
            << center_delta.z << ") facing=" << facing << " vertices:";
    for (const ElementId vertex_id : face.vertices) {
      const Vertex* vertex = find_vertex(document, vertex_id);
      ASSERT_NE(vertex, nullptr);
      details << ' ' << vertex_id << "=(" << vertex->position.x << ','
              << vertex->position.y << ',' << vertex->position.z << ')';
    }
    EXPECT_GT(facing, 0.0F) << details.GetString();
  }

  const CompiledMesh mesh = compile_document(document);
  ASSERT_EQ(mesh.primitives.size(), document.faces.size());
  const float minimum_cross_squared = 1.0e-16F * scale * scale * scale * scale;
  for (const CompiledPrimitive& primitive : mesh.primitives) {
    ASSERT_GE(primitive.index_count, 3U);
    ASSERT_EQ(primitive.index_count % 3U, 0U);
    const std::size_t end = static_cast<std::size_t>(primitive.index_offset) +
                           primitive.index_count;
    ASSERT_LE(end, mesh.indices.size());
    for (std::size_t index = primitive.index_offset; index < end; index += 3U) {
      const auto first_index = mesh.indices[index];
      const auto second_index = mesh.indices[index + 1U];
      const auto third_index = mesh.indices[index + 2U];
      ASSERT_LT(first_index, mesh.vertices.size());
      ASSERT_LT(second_index, mesh.vertices.size());
      ASSERT_LT(third_index, mesh.vertices.size());
      const quader::QVec3 first = mesh.vertices[first_index].position;
      const quader::QVec3 second = mesh.vertices[second_index].position;
      const quader::QVec3 third = mesh.vertices[third_index].position;
      const float cross_squared = length_squared(cross(second - first, third - first));
      EXPECT_TRUE(std::isfinite(cross_squared));
      EXPECT_GT(cross_squared, minimum_cross_squared);
    }
  }
}

void expect_transformed_topology(const Document& reference,
                                 const Selection& reference_selection,
                                 const Document& candidate,
                                 const Selection& candidate_selection,
                                 const PatchTransform& transform) {
  ASSERT_EQ(reference.vertices.size(), candidate.vertices.size());
  ASSERT_EQ(reference.faces.size(), candidate.faces.size());
  const float tolerance = 0.0001F * transform.scale + 0.00002F;
  std::map<ElementId, ElementId> vertex_mapping;
  std::set<ElementId> matched_vertices;
  for (const Vertex& vertex : reference.vertices) {
    const quader::QVec3 expected = transformed_position(vertex.position, transform);
    const auto match = std::find_if(candidate.vertices.begin(), candidate.vertices.end(),
                                   [&](const Vertex& candidate_vertex) {
      return !matched_vertices.contains(candidate_vertex.id) &&
          length_squared(candidate_vertex.position - expected) <= tolerance * tolerance;
    });
    ASSERT_NE(match, candidate.vertices.end())
        << "No transformed vertex near (" << expected.x << ',' << expected.y << ','
        << expected.z << ')';
    vertex_mapping.emplace(vertex.id, match->id);
    matched_vertices.insert(match->id);
  }

  std::map<std::vector<ElementId>, ElementId> candidate_faces;
  for (const Face& face : candidate.faces) {
    EXPECT_TRUE(candidate_faces.emplace(oriented_loop_key(face.vertices), face.id).second);
  }
  const std::set<ElementId> reference_selected(reference_selection.faces.begin(),
                                               reference_selection.faces.end());
  std::set<ElementId> expected_selected;
  for (const Face& face : reference.faces) {
    std::vector<ElementId> mapped_loop;
    mapped_loop.reserve(face.vertices.size());
    for (const ElementId vertex_id : face.vertices) {
      ASSERT_TRUE(vertex_mapping.contains(vertex_id));
      mapped_loop.push_back(vertex_mapping.at(vertex_id));
    }
    const auto match = candidate_faces.find(oriented_loop_key(std::move(mapped_loop)));
    ASSERT_NE(match, candidate_faces.end()) << "No transformed face for " << face.id;
    if (reference_selected.contains(face.id)) {
      expected_selected.insert(match->second);
    }
  }
  const std::set<ElementId> actual_selected(candidate_selection.faces.begin(),
                                            candidate_selection.faces.end());
  EXPECT_EQ(actual_selected, expected_selected);
}

void expect_cube_patch_equivariance(const PatchTransform& transform) {
  CubePrimitiveSpec spec;
  spec.width = 4.0F;
  spec.height = 4.0F;
  spec.depth = 4.0F;
  const Document source = make_cube(spec);
  const std::array<std::pair<int, float>, 5> cases{{
      {2, 0.0F}, {3, 0.5F}, {8, 0.5F}, {16, 0.0F}, {16, 0.01F}}};
  for (const auto& [segments, profile] : cases) {
    SCOPED_TRACE(::testing::Message() << "segments=" << segments << " profile=" << profile
                                    << " scale=" << transform.scale);
    EdgeBevelSettings settings;
    settings.width = 0.35F;
    settings.profile = profile;
    settings.segments = segments;
    Document reference = source;
    Selection reference_selection = all_edge_selection(reference);
    const OperationResult reference_result =
        bevel_selected_edges(reference, reference_selection, settings);
    ASSERT_TRUE(reference_result.success) << reference_result.message;
    ASSERT_TRUE(reference_result.changed);
    ASSERT_NO_FATAL_FAILURE(expect_closed_renderable_patch(reference, reference_selection,
                                                           source, 1.0F));

    Document candidate = source;
    for (Vertex& vertex : candidate.vertices) {
      vertex.position = transformed_position(vertex.position, transform);
    }
    Selection candidate_selection = all_edge_selection(candidate);
    settings.width *= transform.scale;
    const OperationResult candidate_result =
        bevel_selected_edges(candidate, candidate_selection, settings);
    ASSERT_TRUE(candidate_result.success) << candidate_result.message;
    ASSERT_TRUE(candidate_result.changed);
    ASSERT_NO_FATAL_FAILURE(expect_closed_renderable_patch(candidate, candidate_selection,
                                                           source, transform.scale));
    ASSERT_NO_FATAL_FAILURE(expect_transformed_topology(reference, reference_selection,
                                                        candidate, candidate_selection,
                                                        transform));
  }
}

} // namespace

TEST(QdrDocumentOperationBevelPatchTests, CubeCornerPatchesCommuteWithRigidTransforms) {
  ASSERT_NO_FATAL_FAILURE(expect_cube_patch_equivariance(
      {.angle = 0.71F, .translation = {7.25F, -3.5F, 11.0F}}));
}

TEST(QdrDocumentOperationBevelPatchTests, CubeCornerPatchesCommuteWithUniformScaling) {
  for (const float scale : {0.25F, 3.0F}) {
    ASSERT_NO_FATAL_FAILURE(expect_cube_patch_equivariance({.scale = scale}));
  }
}

TEST(QdrDocumentOperationBevelPatchTests, CubeCornerPatchesRemainValidAcrossProfileAndSegmentControls) {
  CubePrimitiveSpec spec;
  spec.width = 4.0F;
  spec.height = 4.0F;
  spec.depth = 4.0F;
  const Document source = make_cube(spec);
  constexpr std::array<float, 7> kProfiles{0.0F, 0.01F, 0.25F, 0.5F, 0.75F, 0.95F, 1.0F};
  constexpr std::array<int, 5> kSegments{1, 2, 3, 8, 16};
  for (const float profile : kProfiles) {
    for (const int segments : kSegments) {
      SCOPED_TRACE(::testing::Message() << "profile=" << profile << " segments=" << segments);
      Document document = source;
      Selection selection = all_edge_selection(document);
      EdgeBevelSettings settings;
      settings.width = 0.35F;
      settings.profile = profile;
      settings.segments = segments;
      const OperationResult result = bevel_selected_edges(document, selection, settings);
      ASSERT_TRUE(result.success) << result.message;
      ASSERT_TRUE(result.changed);
      ASSERT_NO_FATAL_FAILURE(expect_closed_renderable_patch(document, selection, source, 1.0F));
    }
  }
}
