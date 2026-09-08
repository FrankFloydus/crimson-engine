////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace {

using namespace quader::modeling;

/** Creates candidate vertices with separate, distant supporting triangles. */
PolygonDocument make_merge_fans(std::span<const Vec3> positions) {
  AuthoredTopology topology;
  const auto count = static_cast<std::uint32_t>(positions.size());
  for (std::uint32_t index = 0; index < count; ++index) {
    topology.vertices.push_back({make_id<VertexTag>(index + 1U),
                                 positions[index]});
  }
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto first = make_id<VertexTag>(count + index * 2U + 1U);
    const auto second = make_id<VertexTag>(count + index * 2U + 2U);
    const float support_y = 10.0F + static_cast<float>(index) * 10.0F;
    topology.vertices.push_back(
        {first, {positions[index].x, support_y, 0.0F}});
    topology.vertices.push_back(
        {second, {positions[index].x, support_y, 2.0F}});
    topology.faces.push_back(
        {.id = make_id<FaceTag>(index + 1U),
         .vertices = {make_id<VertexTag>(index + 1U), first, second},
         .uvs = {{0.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}},
         .material_slot = index + 3U,
         .shading = AuthoredSurfaceShading::Smooth,
         .texture_transform = {.scale = {2.0F, 0.5F},
                               .shift_uv = {0.125F, -0.25F},
                               .rotation_degrees = 45.0F}});
  }
  topology.next_vertex_id = count * 3U + 1U;
  topology.next_face_id = count + 1U;
  if (count > 0U) {
    topology.hard_normal_edges = {
        make_edge_key(make_id<VertexTag>(1U),
                      make_id<VertexTag>(count + 1U))};
  }
  return PolygonDocument::from_authored_topology(topology).or_throw();
}

/** Checks exact target coordinates instead of permitting a centroid move. */
void expect_vertex_position(const PolygonDocument &document, VertexId id,
                            Vec3 expected) {
  const auto topology = document.authored_topology();
  ASSERT_TRUE(topology.ok()) << topology.error_message();
  const auto found = std::ranges::find(topology.value().vertices, id,
                                      &AuthoredTopologyVertex::id);
  ASSERT_NE(found, topology.value().vertices.end());
  EXPECT_EQ(found->position.x, expected.x);
  EXPECT_EQ(found->position.y, expected.y);
  EXPECT_EQ(found->position.z, expected.z);
}

/** Unchanged edits preserve both immutable publications and revision values. */
void expect_unchanged(const PolygonDocument &before,
                      const PolygonDocument &after) {
  EXPECT_TRUE(after.shares_topology_with(before));
  EXPECT_TRUE(after.shares_selection_with(before));
  EXPECT_EQ(after.content_identity(), before.content_identity());
  EXPECT_EQ(after.content_revision(), before.content_revision());
  EXPECT_EQ(after.selection_revision(), before.selection_revision());
}

TEST(ModelingAutoMergeTests, ChoosesNearestUnselectedTargetAndRemapsActiveVertex) {
  const std::array positions{Vec3{0.0F, 0.0F, 0.0F},
                             Vec3{0.0007F, 0.0F, 0.0F},
                             Vec3{0.0003F, 0.0F, 0.0F},
                             Vec3{10.0F, 0.0F, 0.0F},
                             Vec3{10.0001F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  const auto moved = make_id<VertexTag>(1U);
  const auto target = make_id<VertexTag>(3U);
  ASSERT_TRUE(document.apply_selection({.kind = SelectionKind::Vertex,
                                        .vertices = {moved},
                                        .has_active = true,
                                        .active_vertex = moved}).ok());
  const auto before_count = document.vertex_count();

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().success);
  ASSERT_TRUE(result.value().changed);
  EXPECT_TRUE(result.value().dirty.topology);
  EXPECT_TRUE(result.value().dirty.geometry);
  EXPECT_TRUE(result.value().dirty.selection);
  EXPECT_EQ(result.value().deleted.vertices, std::vector<VertexId>{moved});
  EXPECT_TRUE(result.value().created.vertices.empty());
  EXPECT_TRUE(result.value().created.faces.empty());
  EXPECT_EQ(result.value().created.edges.size(), 2U);
  EXPECT_EQ(result.value().selection_remap.source.vertices,
            std::vector<VertexId>{moved});
  EXPECT_EQ(result.value().selection_remap.destination.vertices,
            std::vector<VertexId>{target});
  EXPECT_EQ(document.vertex_count(), before_count - 1U);
  EXPECT_EQ(document.selection().kind, SelectionKind::Vertex);
  EXPECT_EQ(document.selection().vertices, std::vector<VertexId>{target});
  EXPECT_TRUE(document.selection().has_active);
  EXPECT_EQ(document.selection().active_vertex, target);
  for (std::uint32_t index = 1U; index < positions.size(); ++index) {
    expect_vertex_position(document, make_id<VertexTag>(index + 1U),
                           positions[index]);
  }
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingAutoMergeTests, LooseVerticesRetainTargetsAndUnrelatedClosePairs) {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {0.0005F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {10.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(4U), {10.0001F, 0.0F, 0.0F}},
  };
  topology.next_vertex_id = 5U;
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto source = make_id<VertexTag>(1U);
  const auto target = make_id<VertexTag>(2U);
  ASSERT_TRUE(document.apply_selection({.kind = SelectionKind::Vertex,
                                        .vertices = {source},
                                        .has_active = true,
                                        .active_vertex = source}).ok());

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  EXPECT_EQ(document.vertex_ids(),
            (std::vector<VertexId>{target, make_id<VertexTag>(3U),
                                    make_id<VertexTag>(4U)}));
  EXPECT_EQ(document.face_count(), 0U);
  for (std::size_t index = 1U; index < topology.vertices.size(); ++index) {
    expect_vertex_position(document, topology.vertices[index].id,
                           topology.vertices[index].position);
  }
  EXPECT_EQ(document.selection().vertices, std::vector<VertexId>{target});
  EXPECT_TRUE(document.selection().has_active);
  EXPECT_EQ(document.selection().active_vertex, target);
  EXPECT_EQ(result.value().deleted.vertices, std::vector<VertexId>{source});
  EXPECT_EQ(result.value().selection_remap.source.vertices,
            std::vector<VertexId>{source});
  EXPECT_EQ(result.value().selection_remap.destination.vertices,
            std::vector<VertexId>{target});
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingAutoMergeTests, UnselectedTargetsTakePriorityOverSelectedClusters) {
  const std::array positions{Vec3{0.0F, 0.0F, 0.0F},
                             Vec3{0.0005F, 0.0F, 0.0F},
                             Vec3{0.0006F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  const std::array selected{make_id<VertexTag>(1U), make_id<VertexTag>(2U)};
  ASSERT_TRUE(document.select_vertices(selected).ok());

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  EXPECT_EQ(document.vertex_count(), positions.size() * 3U - 2U);
  EXPECT_EQ(document.selection().vertices,
            std::vector<VertexId>{make_id<VertexTag>(3U)});
  expect_vertex_position(document, make_id<VertexTag>(3U), positions[2]);
}

TEST(ModelingAutoMergeTests, ThresholdIsInclusiveWithoutAnAddedEpsilon) {
  constexpr float kTolerance = 0.001F;
  const std::array distances{
      kTolerance, std::nextafter(kTolerance,
                                std::numeric_limits<float>::infinity())};
  for (const float distance : distances) {
    SCOPED_TRACE(distance);
    const std::array positions{Vec3{0.0F, 0.0F, 0.0F},
                               Vec3{distance, 0.0F, 0.0F}};
    auto document = make_merge_fans(positions);
    ASSERT_TRUE(document.select_vertex(make_id<VertexTag>(1U)).ok());
    const auto before = document;

    const auto result = document.auto_merge_selected_vertices(kTolerance);

    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_EQ(result.value().changed, distance == kTolerance);
    if (distance == kTolerance) {
      EXPECT_EQ(document.vertex_count(), before.vertex_count() - 1U);
      expect_vertex_position(document, make_id<VertexTag>(2U), positions[1]);
    } else {
      expect_unchanged(before, document);
    }
  }
}

TEST(ModelingAutoMergeTests, ZeroToleranceMergesExactlyCoincidentVertices) {
  const std::array positions{Vec3{}, Vec3{}};
  auto document = make_merge_fans(positions);
  ASSERT_TRUE(document.select_vertex(make_id<VertexTag>(1U)).ok());

  const auto result = document.auto_merge_selected_vertices(0.0F);

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  EXPECT_EQ(document.vertex_count(), 5U);
  EXPECT_EQ(document.selection().vertices,
            std::vector<VertexId>{make_id<VertexTag>(2U)});
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingAutoMergeTests, SelectedPairKeepsFirstDocumentVertexAtItsPosition) {
  const std::array positions{Vec3{0.0F, 0.0F, 0.0F},
                             Vec3{0.0005F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  const std::array reversed{make_id<VertexTag>(2U), make_id<VertexTag>(1U)};
  ASSERT_TRUE(document.select_vertices(reversed).ok());

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  EXPECT_EQ(document.selection().vertices,
            std::vector<VertexId>{make_id<VertexTag>(1U)});
  expect_vertex_position(document, make_id<VertexTag>(1U), positions[0]);
}

TEST(ModelingAutoMergeTests, LargerSelectedClusterKeepsMostCentralExistingVertex) {
  const std::array positions{Vec3{0.0F, 0.0F, 0.0F},
                             Vec3{0.0004F, 0.0F, 0.0F},
                             Vec3{0.0009F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  const std::array selected{make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                            make_id<VertexTag>(3U)};
  ASSERT_TRUE(document.select_vertices(selected).ok());

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  EXPECT_EQ(document.selection().vertices,
            std::vector<VertexId>{make_id<VertexTag>(2U)});
  expect_vertex_position(document, make_id<VertexTag>(2U), positions[1]);
}

TEST(ModelingAutoMergeTests, EqualCentralDistancesChooseFirstDocumentVertex) {
  // Binary fractions make the two middle distances an exact tie.
  constexpr float kStep = 1.0F / 4096.0F;
  const std::array positions{Vec3{}, Vec3{kStep, 0.0F, 0.0F},
                             Vec3{kStep * 2.0F, 0.0F, 0.0F},
                             Vec3{kStep * 3.0F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  const std::array selected{make_id<VertexTag>(4U), make_id<VertexTag>(3U),
                            make_id<VertexTag>(2U), make_id<VertexTag>(1U)};
  ASSERT_TRUE(document.select_vertices(selected).ok());

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  EXPECT_EQ(document.selection().vertices,
            std::vector<VertexId>{make_id<VertexTag>(2U)});
  expect_vertex_position(document, make_id<VertexTag>(2U), positions[1]);
}

TEST(ModelingAutoMergeTests, SelectedClustersUseSeedRadiusWithoutTransitiveGrowth) {
  const std::array positions{Vec3{0.0F, 0.0F, 0.0F},
                             Vec3{0.0009F, 0.0F, 0.0F},
                             Vec3{0.0018F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  const std::array selected{make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                            make_id<VertexTag>(3U)};
  ASSERT_TRUE(document.select_vertices(selected).ok());

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  EXPECT_EQ(document.vertex_count(), 8U);
  EXPECT_EQ(document.selection().vertices,
            (std::vector<VertexId>{make_id<VertexTag>(1U),
                                    make_id<VertexTag>(3U)}));
  expect_vertex_position(document, make_id<VertexTag>(1U), positions[0]);
  expect_vertex_position(document, make_id<VertexTag>(3U), positions[2]);
}

TEST(ModelingAutoMergeTests, EdgeSelectionRemapsActiveEdgeAndPreservesAttributes) {
  const std::array positions{Vec3{}, Vec3{0.0005F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  const auto source_edge =
      make_edge_key(make_id<VertexTag>(1U), make_id<VertexTag>(3U));
  const auto target_edge =
      make_edge_key(make_id<VertexTag>(2U), make_id<VertexTag>(3U));
  ASSERT_TRUE(document.apply_selection({.kind = SelectionKind::Edge,
                                        .edges = {source_edge},
                                        .has_active = true,
                                        .active_edge = source_edge}).ok());
  const auto before = document.authored_topology().or_throw();

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  const auto selected = document.selection();
  EXPECT_EQ(selected.kind, SelectionKind::Edge);
  EXPECT_EQ(selected.edges, std::vector<EdgeKey>{target_edge});
  EXPECT_TRUE(selected.has_active);
  EXPECT_EQ(selected.active_edge, target_edge);
  EXPECT_EQ(result.value().selection_remap.source.edges,
            std::vector<EdgeKey>{source_edge});
  EXPECT_EQ(result.value().selection_remap.destination.edges,
            std::vector<EdgeKey>{target_edge});
  EXPECT_EQ(document.hard_edge_ids(), std::vector<EdgeKey>{target_edge});
  const auto after = document.authored_topology().or_throw();
  EXPECT_EQ(after.next_vertex_id, before.next_vertex_id);
  EXPECT_EQ(after.next_face_id, before.next_face_id);
  ASSERT_EQ(after.faces.size(), before.faces.size());
  for (std::size_t index = 0; index < before.faces.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_EQ(after.faces[index].id, before.faces[index].id);
    EXPECT_EQ(after.faces[index].material_slot,
              before.faces[index].material_slot);
    EXPECT_EQ(after.faces[index].shading, before.faces[index].shading);
    EXPECT_EQ(after.faces[index].texture_transform,
              before.faces[index].texture_transform);
    ASSERT_EQ(after.faces[index].uvs.size(), before.faces[index].uvs.size());
    for (std::size_t corner = 0; corner < before.faces[index].uvs.size(); ++corner) {
      EXPECT_EQ(after.faces[index].uvs[corner].x,
                before.faces[index].uvs[corner].x);
      EXPECT_EQ(after.faces[index].uvs[corner].y,
                before.faces[index].uvs[corner].y);
    }
  }
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingAutoMergeTests, FaceSelectionDerivesVerticesAndRetainsActiveFace) {
  const std::array positions{Vec3{}, Vec3{0.0005F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  const auto face = make_id<FaceTag>(1U);
  ASSERT_TRUE(document.apply_selection({.kind = SelectionKind::Face,
                                        .faces = {face},
                                        .has_active = true,
                                        .active_face = face}).ok());

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  EXPECT_EQ(result.value().deleted.vertices,
            std::vector<VertexId>{make_id<VertexTag>(1U)});
  EXPECT_EQ(document.selection().kind, SelectionKind::Face);
  EXPECT_EQ(document.selection().faces, std::vector<FaceId>{face});
  EXPECT_TRUE(document.selection().has_active);
  EXPECT_EQ(document.selection().active_face, face);
  const auto after = document.authored_topology().or_throw();
  ASSERT_EQ(after.faces.size(), 2U);
  EXPECT_EQ(after.faces.front().vertices,
            (std::vector<VertexId>{make_id<VertexTag>(2U),
                                    make_id<VertexTag>(3U),
                                    make_id<VertexTag>(4U)}));
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingAutoMergeTests, SplittingSelectedFaceSelectsEveryDescendant) {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {-2.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {-1.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(4U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(5U), {1.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(6U), {2.0F, 0.0F, 0.0F}},
  };
  const auto source_face = make_id<FaceTag>(1U);
  topology.faces = {
      {.id = source_face,
       .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                    make_id<VertexTag>(3U), make_id<VertexTag>(4U),
                    make_id<VertexTag>(5U), make_id<VertexTag>(6U)}}};
  topology.next_vertex_id = 7U;
  topology.next_face_id = 2U;
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  ASSERT_TRUE(document.apply_selection({.kind = SelectionKind::Face,
                                        .faces = {source_face},
                                        .has_active = true,
                                        .active_face = source_face}).ok());

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  const auto after = document.authored_topology().or_throw();
  ASSERT_EQ(after.faces.size(), 2U);
  EXPECT_EQ(after.vertices.size(), 5U);
  for (const auto &face : after.faces) {
    EXPECT_EQ(face.vertices.size(), 3U);
    EXPECT_NE(std::ranges::find(face.vertices, make_id<VertexTag>(1U)),
              face.vertices.end());
  }
  const auto selected = document.selection();
  EXPECT_EQ(selected.kind, SelectionKind::Face);
  EXPECT_EQ(selected.faces.size(), 2U);
  for (const auto &face : after.faces) {
    EXPECT_NE(std::ranges::find(selected.faces, face.id), selected.faces.end());
  }
  EXPECT_TRUE(selected.has_active);
  EXPECT_EQ(selected.active_face, source_face);
  EXPECT_EQ(result.value().created.faces.size(), 1U);
  EXPECT_EQ(result.value().selection_remap.source.faces,
            std::vector<FaceId>{source_face});
  EXPECT_EQ(result.value().selection_remap.destination.faces, selected.faces);
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingAutoMergeTests, UnrelatedFiniteCollinearFaceRemainsAuthored) {
  const std::array positions{Vec3{}, Vec3{}};
  auto topology = make_merge_fans(positions).authored_topology().or_throw();
  topology.vertices.push_back(
      {make_id<VertexTag>(7U), {100.0F, 0.0F, 0.0F}});
  topology.vertices.push_back(
      {make_id<VertexTag>(8U), {101.0F, 0.0F, 0.0F}});
  topology.vertices.push_back(
      {make_id<VertexTag>(9U), {102.0F, 0.0F, 0.0F}});
  const AuthoredTopologyFace collinear{
      .id = make_id<FaceTag>(3U),
      .vertices = {make_id<VertexTag>(7U), make_id<VertexTag>(8U),
                   make_id<VertexTag>(9U)},
      .uvs = {{0.0F, 0.0F}, {0.5F, 1.0F}, {1.0F, 0.0F}},
      .material_slot = 9U,
      .shading = AuthoredSurfaceShading::Smooth,
      .texture_transform = {.scale = {2.0F, 0.5F},
                            .shift_uv = {0.125F, -0.25F},
                            .rotation_degrees = 45.0F}};
  topology.faces.push_back(collinear);
  topology.next_vertex_id = 10U;
  topology.next_face_id = 4U;
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  ASSERT_TRUE(document.select_vertex(make_id<VertexTag>(1U)).ok());
  EXPECT_FALSE(document.validate().ok());

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  EXPECT_EQ(document.vertex_count(), 8U);
  EXPECT_EQ(document.face_count(), 3U);
  EXPECT_TRUE(result.value().deleted.faces.empty());
  const auto after = document.authored_topology().or_throw();
  const auto retained = std::ranges::find(after.faces, collinear.id,
                                         &AuthoredTopologyFace::id);
  ASSERT_NE(retained, after.faces.end());
  EXPECT_EQ(retained->vertices, collinear.vertices);
  EXPECT_EQ(retained->material_slot, collinear.material_slot);
  EXPECT_EQ(retained->shading, collinear.shading);
  EXPECT_EQ(retained->texture_transform, collinear.texture_transform);
  ASSERT_EQ(retained->uvs.size(), collinear.uvs.size());
  for (std::size_t index = 0; index < collinear.uvs.size(); ++index) {
    EXPECT_EQ(retained->uvs[index].x, collinear.uvs[index].x);
    EXPECT_EQ(retained->uvs[index].y, collinear.uvs[index].y);
    expect_vertex_position(document, topology.vertices[index + 6U].id,
                           topology.vertices[index + 6U].position);
  }
  EXPECT_EQ(document.selection().vertices,
            std::vector<VertexId>{make_id<VertexTag>(2U)});
  EXPECT_FALSE(document.validate().ok());
}

TEST(ModelingAutoMergeTests, EmptySelectionAndDistantVerticesAreSuccessfulNoOps) {
  const std::array positions{Vec3{}, Vec3{1.0F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  for (const bool select_vertex : {false, true}) {
    SCOPED_TRACE(select_vertex);
    if (select_vertex) {
      ASSERT_TRUE(document.select_vertex(make_id<VertexTag>(1U)).ok());
    }
    const auto before = document;

    const auto result = document.auto_merge_selected_vertices();

    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.value().success);
    EXPECT_FALSE(result.value().changed);
    EXPECT_FALSE(result.value().dirty.topology);
    EXPECT_FALSE(result.value().dirty.geometry);
    EXPECT_FALSE(result.value().dirty.selection);
    EXPECT_TRUE(result.value().created.empty());
    EXPECT_TRUE(result.value().deleted.empty());
    expect_unchanged(before, document);
  }
}

TEST(ModelingAutoMergeTests, InvalidToleranceRejectsAtomically) {
  const std::array positions{Vec3{}, Vec3{0.0005F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  ASSERT_TRUE(document.select_vertex(make_id<VertexTag>(1U)).ok());
  const auto before = document;
  const std::array tolerances{-0.001F,
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()};
  for (const float tolerance : tolerances) {
    SCOPED_TRACE(tolerance);

    const auto result = document.auto_merge_selected_vertices(tolerance);

    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.error_message().empty());
    expect_unchanged(before, document);
  }
}

TEST(ModelingAutoMergeTests, PreservingIdentityHighWaterOnlyAdvancesAllocationCursors) {
  const std::array positions{Vec3{}, Vec3{1.0F, 0.0F, 0.0F}};
  auto document = make_merge_fans(positions);
  ASSERT_TRUE(document.select_vertex(make_id<VertexTag>(1U)).ok());
  const auto before = document;
  auto observed_topology = before.authored_topology().or_throw();
  observed_topology.next_vertex_id += 10U;
  observed_topology.next_face_id += 20U;
  const auto observed = PolygonDocument::from_authored_topology(observed_topology).or_throw();

  document.preserve_identity_high_water(observed);

  EXPECT_TRUE(document.has_same_geometry(before));
  EXPECT_TRUE(document.shares_selection_with(before));
  EXPECT_EQ(document.content_revision(), before.content_revision());
  EXPECT_EQ(document.selection_revision(), before.selection_revision());
  const auto after = document.authored_topology().or_throw();
  EXPECT_EQ(after.next_vertex_id, observed_topology.next_vertex_id);
  EXPECT_EQ(after.next_face_id, observed_topology.next_face_id);
  EXPECT_NE(before.authored_topology().or_throw().next_vertex_id, after.next_vertex_id);
  const auto advanced = document;
  document.preserve_identity_high_water(before);
  EXPECT_TRUE(document.shares_topology_with(advanced));
}

TEST(ModelingAutoMergeTests, ExtrudedCapTransfersSelectionToSurvivingDuplicateSidewall) {
  auto document = PolygonDocument::make_box({}).or_throw();
  const auto front = document.face_ids_by_normal({0.0F, 0.0F, 1.0F});
  ASSERT_EQ(front.size(), 1U);
  ASSERT_TRUE(document.select_face(front.front()).ok());
  ASSERT_TRUE(document.extrude_selected_elements({2.0F, 0.0F, 0.0F}).ok());
  const auto cap = document.selection();
  ASSERT_EQ(cap.faces.size(), 1U);
  ASSERT_TRUE(cap.has_active);

  const auto result = document.auto_merge_selected_vertices();

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  const auto after = document.selection();
  EXPECT_EQ(after.kind, SelectionKind::Face);
  ASSERT_EQ(after.faces.size(), 1U);
  EXPECT_TRUE(after.has_active);
  EXPECT_EQ(after.active_face, after.faces.front());
  EXPECT_NE(after.active_face, cap.active_face);
  EXPECT_EQ(result.value().selection_remap.source.faces, cap.faces);
  EXPECT_EQ(result.value().selection_remap.destination.faces, after.faces);
  const auto topology = document.authored_topology().or_throw();
  const auto surviving = std::ranges::find(topology.faces, after.active_face,
                                          &AuthoredTopologyFace::id);
  ASSERT_NE(surviving, topology.faces.end());
  ASSERT_EQ(surviving->vertices.size(), 4U);
  for (const auto id : surviving->vertices) {
    const auto vertex = std::ranges::find(topology.vertices, id,
                                         &AuthoredTopologyVertex::id);
    ASSERT_NE(vertex, topology.vertices.end());
    EXPECT_EQ(vertex->position.z, 1.0F);
    EXPECT_TRUE(vertex->position.x == 1.0F || vertex->position.x == 3.0F);
  }
}

} // namespace
