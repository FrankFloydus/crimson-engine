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
#include <limits>
#include <vector>

namespace {

using namespace quader::modeling;

/// Gives the shared edge one uniquely farthest source corner with non-affine UVs.
AuthoredTopology asymmetric_wrap_topology(bool relabeled) {
  constexpr std::array<Vec3, 6> kPositions{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 3.0F, 0.0F}, {-1.0F, 3.0F, 0.0F},
      {4.0F, 0.0F, -2.0F}, {4.0F, 3.0F, -2.0F}}};
  AuthoredTopology topology;
  std::array<VertexId, kPositions.size()> vertices{};
  for (std::size_t index = 0U; index < kPositions.size(); ++index) {
    vertices[index] = make_id<VertexTag>(relabeled ? kPositions.size() - index : index + 1U);
    topology.vertices.push_back({vertices[index], kPositions[index]});
  }
  topology.faces = {
      {.id = make_id<FaceTag>(1U),
       .vertices = {vertices[0], vertices[1], vertices[2], vertices[3]},
       .uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 2.0F}},
       .material_slot = 3U},
      {.id = make_id<FaceTag>(2U),
       .vertices = {vertices[1], vertices[4], vertices[5], vertices[2]},
       .material_slot = 5U}};
  topology.faces.front().texture_transform = {
      .scale = {-2.0F, 0.5F}, .shift_uv = {0.31F, -0.37F}, .rotation_degrees = 37.0F};
  topology.next_vertex_id = 7U;
  topology.next_face_id = 3U;
  return topology;
}

std::array<Vec2, 4> compiled_face_uvs(const PolygonDocument& document, FaceId face,
    const std::array<VertexId, 4>& corners) {
  auto topology = document.authored_topology().or_throw();
  const auto found_face = std::ranges::find(topology.faces, face, &AuthoredTopologyFace::id);
  EXPECT_NE(found_face, topology.faces.end());
  if (found_face == topology.faces.end()) {
    return {};
  }
  const auto isolated_face = *found_face;
  topology.faces = {isolated_face};
  const auto isolated = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto compiled = isolated.compile_mesh().or_throw();
  std::array<Vec2, 4> uvs{};
  std::array<bool, 4> sampled{};
  for (const auto& vertex : compiled.vertices) {
    const auto found = std::ranges::find(corners, vertex.source_vertex);
    EXPECT_NE(found, corners.end());
    if (found != corners.end()) {
      const auto corner = static_cast<std::size_t>(found - corners.begin());
      uvs[corner] = vertex.uv0;
      sampled[corner] = true;
    }
  }
  EXPECT_TRUE(std::ranges::all_of(sampled, [](bool present) { return present; }));
  return uvs;
}

TEST(FaceTextureWrapTests, SharedEdgeContinuesTheLocalChartOfANonAffineSource) {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2), {4.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3), {4.0F, 3.0F, 0.0F}},
      {make_id<VertexTag>(4), {0.0F, 3.0F, 0.0F}},
      {make_id<VertexTag>(5), {4.0F, 0.0F, -2.0F}},
      {make_id<VertexTag>(6), {4.0F, 3.0F, -2.0F}}};
  topology.faces = {
      {.id = make_id<FaceTag>(1),
       .vertices = {make_id<VertexTag>(1), make_id<VertexTag>(2),
                    make_id<VertexTag>(3), make_id<VertexTag>(4)},
       .uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 2.0F}}},
      {.id = make_id<FaceTag>(2),
       .vertices = {make_id<VertexTag>(2), make_id<VertexTag>(5),
                    make_id<VertexTag>(6), make_id<VertexTag>(3)}}};
  topology.next_vertex_id = 7;
  topology.next_face_id = 3;
  const auto imported = PolygonDocument::from_authored_topology(topology);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  const auto &document = imported.value();
  const auto before = serialize_qdr_authored_topology(document).or_throw();
  const FaceTextureTransferFace source{
      .key = 1, .document = &document, .face = make_id<FaceTag>(1)};
  const FaceTextureTransferFace target{
      .key = 1, .document = &document, .face = make_id<FaceTag>(2)};
  const auto self = prepare_face_texture_transfer(source, source, 0);
  ASSERT_TRUE(self.ok()) << self.error_message();
  EXPECT_FALSE(self.value().changed);
  EXPECT_EQ(serialize_qdr_authored_topology(self.value().documents.front().document).or_throw(),
            before);
  const auto transferred = prepare_face_texture_transfer(source, target, 7);
  ASSERT_TRUE(transferred.ok()) << transferred.error_message();
  ASSERT_TRUE(transferred.value().changed);
  const auto &prepared = transferred.value().documents.front().document;
  const auto output = prepared.authored_topology().or_throw();
  ASSERT_EQ(output.faces.size(), 2U);
  ASSERT_EQ(output.faces[0].uvs.size(), topology.faces[0].uvs.size());
  for (std::size_t corner = 0; corner < topology.faces[0].uvs.size(); ++corner) {
    EXPECT_EQ(output.faces[0].uvs[corner].x, topology.faces[0].uvs[corner].x);
    EXPECT_EQ(output.faces[0].uvs[corner].y, topology.faces[0].uvs[corner].y);
  }
  EXPECT_EQ(output.faces[0].texture_transform, topology.faces[0].texture_transform);
  EXPECT_EQ(output.faces[1].material_slot, 7U);
  ASSERT_EQ(output.faces[1].uvs.size(), 4U);
  // The common edge's two samples and source vertex 4 define the local chart.
  // It preserves the whole seam while unfolding U=1-z/4, V=y/3+z/4 onto +X.
  constexpr std::array<Vec2, 4> kExpected{{
      {1.0F, 0.0F}, {1.5F, -0.5F}, {1.5F, 0.5F}, {1.0F, 1.0F}}};
  for (std::size_t corner = 0; corner < kExpected.size(); ++corner) {
    EXPECT_NEAR(output.faces[1].uvs[corner].x, kExpected[corner].x, 1.0e-6F);
    EXPECT_NEAR(output.faces[1].uvs[corner].y, kExpected[corner].y, 1.0e-6F);
  }
  EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), before);
  const FaceTextureTransferFace repeated_source{
      .key = 1, .document = &prepared, .face = make_id<FaceTag>(1)};
  const FaceTextureTransferFace repeated_target{
      .key = 1, .document = &prepared, .face = make_id<FaceTag>(2)};
  const auto repeated = prepare_face_texture_transfer(repeated_source, repeated_target, 7);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
}

TEST(FaceTextureWrapTests,
     UniqueLocalChartRetainsItsSeamUnderCyclicStartsRelabelingAndObliquePlacement) {
  for (const bool relabeled : {false, true}) {
    for (const bool placed : {false, true}) {
      for (std::size_t source_start = 0U; source_start < 4U; ++source_start) {
        for (std::size_t target_start = 0U; target_start < 4U; ++target_start) {
          SCOPED_TRACE(::testing::Message()
              << "relabeled=" << relabeled << ", placed=" << placed
              << ", source start=" << source_start << ", target start=" << target_start);
          auto topology = asymmetric_wrap_topology(relabeled);
          const auto target_vertices = topology.faces[1].vertices;
          auto& source_face = topology.faces[0];
          auto& target_face = topology.faces[1];
          std::rotate(source_face.vertices.begin(),
              source_face.vertices.begin() + static_cast<std::ptrdiff_t>(source_start),
              source_face.vertices.end());
          std::rotate(source_face.uvs.begin(),
              source_face.uvs.begin() + static_cast<std::ptrdiff_t>(source_start),
              source_face.uvs.end());
          std::rotate(target_face.vertices.begin(),
              target_face.vertices.begin() + static_cast<std::ptrdiff_t>(target_start),
              target_face.vertices.end());
          auto document = PolygonDocument::from_authored_topology(topology).or_throw();
          static_cast<void>(document.select_face(source_face.id).or_throw());
          const auto before = serialize_qdr_authored_topology(document).or_throw();
          const auto identity = document.content_identity();
          const auto revision = document.content_revision();
          FaceTextureTransferFace source{
              .key = 1U, .document = &document, .face = source_face.id};
          FaceTextureTransferFace target{
              .key = 1U, .document = &document, .face = target_face.id};
          if (placed) {
            // The orthonormal columns rotate the seam away from every world axis.
            source.world_from_local = {
                .x_axis = {0.6F, 0.8F, 0.0F},
                .y_axis = {-0.48F, 0.36F, 0.8F},
                .z_axis = {0.64F, -0.48F, 0.6F},
                .origin = {17.0F, -9.0F, 23.0F}};
            target.world_from_local = source.world_from_local;
          }

          const auto result = prepare_face_texture_transfer(source, target, 7U);

          ASSERT_TRUE(result.ok()) << result.error_message();
          ASSERT_TRUE(result.value().changed);
          ASSERT_EQ(result.value().documents.size(), 1U);
          const auto& replacement = result.value().documents.front();
          const auto& prepared = replacement.document;
          EXPECT_EQ(replacement.key, 1U);
          EXPECT_TRUE(prepared.has_same_geometry(document));
          EXPECT_TRUE(prepared.shares_selection_with(document));
          EXPECT_EQ(prepared.selection_revision(), document.selection_revision());
          EXPECT_EQ(replacement.receipt.modified.faces, (std::vector<FaceId>{target_face.id}));
          const auto output = prepared.authored_topology().or_throw();
          ASSERT_EQ(output.faces.size(), 2U);
          EXPECT_EQ(output.faces[1].texture_transform, source_face.texture_transform);
          EXPECT_EQ(output.faces[1].material_slot, 7U);
          ASSERT_EQ(output.faces[1].uvs.size(), target_vertices.size());
          // The endpoint samples and (-1, 3, 0) give U=1-z/5, V=y/3+z/5.
          constexpr std::array<Vec2, 4> kExpected{{
              {1.0F, 0.0F}, {1.4F, -0.4F}, {1.4F, 0.6F}, {1.0F, 1.0F}}};
          for (std::size_t corner = 0U; corner < output.faces[1].vertices.size(); ++corner) {
            const auto found = std::ranges::find(target_vertices, output.faces[1].vertices[corner]);
            ASSERT_NE(found, target_vertices.end());
            const auto index = static_cast<std::size_t>(found - target_vertices.begin());
            EXPECT_NEAR(output.faces[1].uvs[corner].x, kExpected[index].x, 2.0e-5F);
            EXPECT_NEAR(output.faces[1].uvs[corner].y, kExpected[index].y, 2.0e-5F);
          }
          auto expected_topology = topology;
          expected_topology.faces[1].uvs = output.faces[1].uvs;
          expected_topology.faces[1].texture_transform = source_face.texture_transform;
          expected_topology.faces[1].material_slot = 7U;
          const auto expected_document = PolygonDocument::from_authored_topology(expected_topology).or_throw();
          EXPECT_EQ(serialize_qdr_authored_topology(prepared).or_throw(),
              serialize_qdr_authored_topology(expected_document).or_throw());
          const auto compiled = prepared.compile_mesh().or_throw();
          std::array<bool, 4> sampled{};
          const auto settings = source_face.texture_transform;
          const double angle = settings.rotation_degrees * 0.017453292519943295;
          for (const auto& vertex : compiled.vertices) {
            const auto found = std::ranges::find(target_vertices, vertex.source_vertex);
            if (found == target_vertices.end()) {
              continue;
            }
            const auto index = static_cast<std::size_t>(found - target_vertices.begin());
            sampled[index] = true;
            const auto base = kExpected[index];
            const double expected_u = (base.x * std::cos(angle) - base.y * std::sin(angle)) /
                settings.scale.x + settings.shift_uv.x;
            const double expected_v = (base.x * std::sin(angle) + base.y * std::cos(angle)) /
                settings.scale.y + settings.shift_uv.y;
            EXPECT_NEAR(vertex.uv0.x, expected_u, 4.0e-5);
            EXPECT_NEAR(vertex.uv0.y, expected_v, 4.0e-5);
          }
          EXPECT_TRUE(std::ranges::all_of(sampled, [](bool present) { return present; }));
          EXPECT_EQ(document.content_identity(), identity);
          EXPECT_EQ(document.content_revision(), revision);
          EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), before);
          source.document = &prepared;
          target.document = &prepared;
          const auto repeated = prepare_face_texture_transfer(source, target, 7U);
          ASSERT_TRUE(repeated.ok()) << repeated.error_message();
          EXPECT_FALSE(repeated.value().changed);
          const auto& unchanged = repeated.value().documents.front().document;
          EXPECT_EQ(unchanged.content_identity(), prepared.content_identity());
          EXPECT_EQ(unchanged.content_revision(), prepared.content_revision());
          EXPECT_EQ(serialize_qdr_authored_topology(unchanged).or_throw(),
              serialize_qdr_authored_topology(prepared).or_throw());
        }
      }
    }
  }
}

TEST(FaceTextureWrapTests,
     SharedVertexIdsDoNotContinueANonAffineChartAcrossDisplacedOccurrences) {
  const auto topology = asymmetric_wrap_topology(false);
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  static_cast<void>(document.select_face(topology.faces[0].id).or_throw());
  const auto before = serialize_qdr_authored_topology(document).or_throw();
  const auto identity = document.content_identity();
  const auto revision = document.content_revision();
  const auto selection = document.selection();
  const auto selection_revision = document.selection_revision();
  const FaceTextureTransferFace source{
      .key = 1U, .document = &document, .face = topology.faces[0].id};
  for (const auto target_key : {1U, 2U}) {
    SCOPED_TRACE(target_key);
    FaceTextureTransferFace target{
        .key = target_key, .document = &document, .face = topology.faces[1].id};
    target.world_from_local.origin = {0.0F, 0.0F, 10.0F};

    const auto result = prepare_face_texture_transfer(source, target, 7U);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    EXPECT_FALSE(result.error_message().empty());
    EXPECT_EQ(document.content_identity(), identity);
    EXPECT_EQ(document.content_revision(), revision);
    EXPECT_EQ(document.selection_revision(), selection_revision);
    EXPECT_EQ(document.selection().faces, selection.faces);
    EXPECT_EQ(document.selection().active_face, selection.active_face);
    EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), before);
  }
}

TEST(FaceTextureWrapTests,
     EquallyConditionedSharedEdgeCornersUseTheFirstCyclicCandidate) {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {-2.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {1.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(4U), {-1.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(5U), {-2.0F, 0.0F, -1.0F}},
      {make_id<VertexTag>(6U), {0.0F, 0.0F, -1.0F}}};
  topology.faces = {
      {.id = make_id<FaceTag>(1U),
       .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                    make_id<VertexTag>(3U), make_id<VertexTag>(4U)},
       .uvs = {{-1.0F, 0.0F}, {0.0F, 0.0F}, {0.5F, 1.0F}, {-0.5F, 2.0F}}},
      {.id = make_id<FaceTag>(2U),
       .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(5U),
                    make_id<VertexTag>(6U), make_id<VertexTag>(2U)}}};
  topology.next_vertex_id = 7U;
  topology.next_face_id = 3U;
  const auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto before = serialize_qdr_authored_topology(document).or_throw();
  const auto identity = document.content_identity();
  const auto revision = document.content_revision();
  FaceTextureTransferFace source{
      .key = 1U, .document = &document, .face = make_id<FaceTag>(1U)};
  FaceTextureTransferFace target{
      .key = 1U, .document = &document, .face = make_id<FaceTag>(2U)};

  const auto result = prepare_face_texture_transfer(source, target, 7U);

  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  ASSERT_EQ(result.value().documents.size(), 1U);
  const auto& prepared = result.value().documents.front().document;
  const auto output = prepared.authored_topology().or_throw();
  ASSERT_EQ(output.faces.size(), 2U);
  // The tied corners carry different V gradients; the first gives V=z here.
  constexpr std::array<Vec2, 4> kExpected{{
      {-1.0F, 0.0F}, {-1.0F, -1.0F}, {0.0F, -1.0F}, {0.0F, 0.0F}}};
  ASSERT_EQ(output.faces[1].uvs.size(), kExpected.size());
  for (std::size_t corner = 0U; corner < kExpected.size(); ++corner) {
    EXPECT_NEAR(output.faces[1].uvs[corner].x, kExpected[corner].x, 1.0e-6F);
    EXPECT_NEAR(output.faces[1].uvs[corner].y, kExpected[corner].y, 1.0e-6F);
  }
  auto expected_topology = topology;
  expected_topology.faces[1].uvs = output.faces[1].uvs;
  expected_topology.faces[1].material_slot = 7U;
  const auto expected = PolygonDocument::from_authored_topology(expected_topology).or_throw();
  EXPECT_EQ(serialize_qdr_authored_topology(prepared).or_throw(),
      serialize_qdr_authored_topology(expected).or_throw());
  EXPECT_EQ(document.content_identity(), identity);
  EXPECT_EQ(document.content_revision(), revision);
  EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), before);
  source.document = &prepared;
  target.document = &prepared;
  const auto repeated = prepare_face_texture_transfer(source, target, 7U);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
  EXPECT_EQ(repeated.value().documents.front().document.content_identity(),
      prepared.content_identity());
}

TEST(FaceTextureWrapTests, ConcavePlanesFollowSurfaceWindingAcrossCyclicStartsAndBothDirections) {
  AuthoredTopology topology;
  constexpr std::array<Vec3, 10> kPositions{{
      {0.0F, 0.0F, 0.0F}, {6.0F, 0.0F, 0.0F},
      {6.0F, 4.0F, 0.0F}, {3.0F, 4.0F, 0.0F},
      {3.0F, 1.0F, 0.0F}, {2.0F, 1.0F, 0.0F},
      {2.0F, 4.0F, 0.0F}, {0.0F, 4.0F, 0.0F},
      {3.0F, 4.0F, -2.0F}, {3.0F, 1.0F, -2.0F}}};
  for (std::size_t corner = 0U; corner < kPositions.size(); ++corner) {
    topology.vertices.push_back({make_id<VertexTag>(corner + 1U), kPositions[corner]});
  }
  topology.faces = {{.id = make_id<FaceTag>(1)}, {.id = make_id<FaceTag>(2)}};
  for (std::size_t corner = 0U; corner < 8U; ++corner) {
    topology.faces[0].vertices.push_back(make_id<VertexTag>(corner + 1U));
    topology.faces[0].uvs.push_back({kPositions[corner].x, kPositions[corner].y});
  }
  topology.faces[1].vertices = {make_id<VertexTag>(5), make_id<VertexTag>(4),
      make_id<VertexTag>(9), make_id<VertexTag>(10)};
  topology.faces[1].uvs = {{3.0F, 1.0F}, {3.0F, 4.0F}, {1.0F, 4.0F}, {1.0F, 1.0F}};
  topology.next_vertex_id = 11;
  topology.next_face_id = 3;
  for (const bool reverse : {false, true}) {
    const std::size_t source_index = reverse ? 1U : 0U;
    const std::size_t target_index = reverse ? 0U : 1U;
    for (const bool disconnected : {false, true}) {
      for (std::size_t source_start = 0U;
          source_start < topology.faces[source_index].vertices.size(); ++source_start) {
        for (std::size_t target_start = 0U;
            target_start < topology.faces[target_index].vertices.size(); ++target_start) {
          SCOPED_TRACE(::testing::Message() << "reverse=" << reverse
              << ", disconnected=" << disconnected << ", source start=" << source_start
              << ", target start=" << target_start);
          auto rotated = topology;
          auto& source_face = rotated.faces[source_index];
          auto& target_face = rotated.faces[target_index];
          std::rotate(source_face.vertices.begin(),
              source_face.vertices.begin() + static_cast<std::ptrdiff_t>(source_start),
              source_face.vertices.end());
          std::rotate(source_face.uvs.begin(),
              source_face.uvs.begin() + static_cast<std::ptrdiff_t>(source_start),
              source_face.uvs.end());
          std::rotate(target_face.vertices.begin(),
              target_face.vertices.begin() + static_cast<std::ptrdiff_t>(target_start),
              target_face.vertices.end());
          target_face.uvs.clear();
          const auto shared_document = PolygonDocument::from_authored_topology(rotated).or_throw();
          auto source_topology = rotated;
          source_topology.faces = {source_face};
          auto target_topology = rotated;
          target_topology.faces = {target_face};
          const auto source_document =
              PolygonDocument::from_authored_topology(source_topology).or_throw();
          const auto target_document =
              PolygonDocument::from_authored_topology(target_topology).or_throw();
          const FaceTextureTransferFace source{
              .key = 1, .document = disconnected ? &source_document : &shared_document,
              .face = source_face.id};
          const FaceTextureTransferFace target{
              .key = disconnected ? 2U : 1U,
              .document = disconnected ? &target_document : &shared_document,
              .face = target_face.id};
          const auto result = prepare_face_texture_transfer(source, target, 7);
          ASSERT_TRUE(result.ok()) << result.error_message();
          const auto& prepared = result.value().documents.front().document;
          EXPECT_TRUE(prepared.has_same_geometry(*target.document));
          const auto output = prepared.authored_topology().or_throw();
          const auto face = std::ranges::find(output.faces, target.face, &AuthoredTopologyFace::id);
          ASSERT_NE(face, output.faces.end());
          ASSERT_EQ(face->uvs.size(), target_face.vertices.size());
          // Opposite arms are equally far from the reflex edge. Choosing either
          // must retain the surface winding, U=x on the top and U=3+z on the side.
          for (std::size_t corner = 0U; corner < face->uvs.size(); ++corner) {
            const auto vertex = std::ranges::find(
                topology.vertices, face->vertices[corner], &AuthoredTopologyVertex::id);
            ASSERT_NE(vertex, topology.vertices.end());
            EXPECT_NEAR(face->uvs[corner].x,
                reverse ? vertex->position.x : 3.0F + vertex->position.z, 1.0e-5F);
            EXPECT_NEAR(face->uvs[corner].y, vertex->position.y, 1.0e-5F);
          }
          auto repeated_source = source;
          auto repeated_target = target;
          if (!disconnected) {
            repeated_source.document = &prepared;
          }
          repeated_target.document = &prepared;
          const auto repeated = prepare_face_texture_transfer(repeated_source, repeated_target, 7);
          ASSERT_TRUE(repeated.ok()) << repeated.error_message();
          EXPECT_FALSE(repeated.value().changed);
        }
      }
    }
  }
}

TEST(FaceTextureWrapTests,
     ChainedObtuseFoldsPreserveCompiledDensityShearAndSeamsInBothDirections) {
  constexpr std::array<float, 4> kDistances{{0.0F, 4.0F, 6.0F, 8.0F}};
  constexpr std::array<std::size_t, 5> kRoute{{0U, 1U, 2U, 1U, 0U}};
  const FaceTextureTransform settings{
      .scale = {-2.0F, 0.5F}, .shift_uv = {0.31F, -0.37F}, .rotation_degrees = 37.0F};
  for (const double degrees : {-150.0, -135.0, -120.0, -60.0, 60.0, 120.0, 135.0, 150.0}) {
    const double angle = degrees * 0.017453292519943295;
    const float folded_x = 4.0F + static_cast<float>(2.0 * std::cos(angle));
    const float folded_z = static_cast<float>(-2.0 * std::sin(angle));
    const std::array<Vec3, 4> stations{{
        {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
        {folded_x, 0.0F, folded_z}, {folded_x + 2.0F, 0.0F, folded_z}}};
    for (const bool disconnected : {false, true}) {
      for (std::size_t cyclic_start = 0U; cyclic_start < 4U; ++cyclic_start) {
        SCOPED_TRACE(::testing::Message() << "degrees=" << degrees
            << ", disconnected=" << disconnected << ", cyclic start=" << cyclic_start);
        AuthoredTopology topology;
        for (std::size_t station = 0U; station < stations.size(); ++station) {
          const auto point = stations[station];
          topology.vertices.push_back({make_id<VertexTag>(2U * station + 1U), point});
          topology.vertices.push_back({make_id<VertexTag>(2U * station + 2U),
              {point.x, 3.0F, point.z}});
        }
        std::array<std::array<VertexId, 4>, 3> corners{};
        for (std::size_t face_index = 0U; face_index < corners.size(); ++face_index) {
          corners[face_index] = {make_id<VertexTag>(2U * face_index + 1U),
              make_id<VertexTag>(2U * face_index + 3U),
              make_id<VertexTag>(2U * face_index + 4U),
              make_id<VertexTag>(2U * face_index + 2U)};
          AuthoredTopologyFace face{.id = make_id<FaceTag>(face_index + 1U),
              .vertices = {corners[face_index].begin(), corners[face_index].end()}};
          if (face_index == 0U) {
            face.uvs = {{0.3F, -0.7F}, {1.9F, -0.3F}, {2.5F, -2.1F}, {0.9F, -2.5F}};
            face.texture_transform = settings;
          }
          const auto start = static_cast<std::ptrdiff_t>((cyclic_start + face_index) % 4U);
          std::rotate(face.vertices.begin(), face.vertices.begin() + start, face.vertices.end());
          if (!face.uvs.empty()) {
            std::rotate(face.uvs.begin(), face.uvs.begin() + start, face.uvs.end());
          }
          topology.faces.push_back(face);
        }
        topology.next_vertex_id = 9U;
        topology.next_face_id = 4U;
        auto shared_document = PolygonDocument::from_authored_topology(topology).or_throw();
        ASSERT_TRUE(shared_document.select_edge(
            make_edge_key(corners[0][1], corners[0][2])).ok());
        std::array<PolygonDocument, 3> documents;
        for (std::size_t face_index = 0U; face_index < documents.size(); ++face_index) {
          auto isolated = topology;
          isolated.faces = {topology.faces[face_index]};
          documents[face_index] = PolygonDocument::from_authored_topology(isolated).or_throw();
        }
        const auto source_uvs = compiled_face_uvs(documents[0], make_id<FaceTag>(1U), corners[0]);
        const Vec2 along{(source_uvs[1].x - source_uvs[0].x) / 4.0F,
            (source_uvs[1].y - source_uvs[0].y) / 4.0F};
        const Vec2 across{(source_uvs[3].x - source_uvs[0].x) / 3.0F,
            (source_uvs[3].y - source_uvs[0].y) / 3.0F};
        const float determinant = along.x * across.y - along.y * across.x;
        ASSERT_GT(std::abs(determinant), 0.1F);
        for (std::size_t step = 1U; step < kRoute.size(); ++step) {
          const auto source_index = kRoute[step - 1U];
          const auto target_index = kRoute[step];
          SCOPED_TRACE(::testing::Message() << "source=" << source_index
              << ", target=" << target_index);
          const FaceTextureTransferFace source{
              .key = disconnected ? source_index + 1U : 1U,
              .document = disconnected ? &documents[source_index] : &shared_document,
              .face = make_id<FaceTag>(source_index + 1U)};
          const FaceTextureTransferFace target{
              .key = disconnected ? target_index + 1U : 1U,
              .document = disconnected ? &documents[target_index] : &shared_document,
              .face = make_id<FaceTag>(target_index + 1U)};
          const auto before = compiled_face_uvs(*source.document, source.face, corners[source_index]);
          const auto result = prepare_face_texture_transfer(source, target, 7U);
          ASSERT_TRUE(result.ok()) << result.error_message();
          ASSERT_EQ(result.value().documents.size(), 1U);
          const auto& prepared = result.value().documents.front().document;
          EXPECT_TRUE(prepared.has_same_geometry(*target.document));
          EXPECT_TRUE(prepared.shares_selection_with(*target.document));
          const auto actual = compiled_face_uvs(prepared, target.face, corners[target_index]);
          const float width = kDistances[target_index + 1U] - kDistances[target_index];
          const Vec2 target_along{(actual[1].x - actual[0].x) / width,
              (actual[1].y - actual[0].y) / width};
          const Vec2 target_across{(actual[3].x - actual[0].x) / 3.0F,
              (actual[3].y - actual[0].y) / 3.0F};
          EXPECT_NEAR(target_along.x, along.x, 2.0e-5F);
          EXPECT_NEAR(target_along.y, along.y, 2.0e-5F);
          EXPECT_NEAR(target_across.x, across.x, 2.0e-5F);
          EXPECT_NEAR(target_across.y, across.y, 2.0e-5F);
          EXPECT_NEAR(target_along.x * target_across.y - target_along.y * target_across.x,
              determinant, 2.0e-5F);
          for (std::size_t corner = 0U; corner < actual.size(); ++corner) {
            const float distance = kDistances[target_index + (corner == 1U || corner == 2U)];
            const float height = corner >= 2U ? 3.0F : 0.0F;
            EXPECT_NEAR(actual[corner].x,
                source_uvs[0].x + distance * along.x + height * across.x, 4.0e-5F);
            EXPECT_NEAR(actual[corner].y,
                source_uvs[0].y + distance * along.y + height * across.y, 4.0e-5F);
            const auto found = std::ranges::find(corners[source_index], corners[target_index][corner]);
            if (found != corners[source_index].end()) {
              const auto source_corner = static_cast<std::size_t>(found - corners[source_index].begin());
              EXPECT_NEAR(actual[corner].x, before[source_corner].x, 2.0e-5F);
              EXPECT_NEAR(actual[corner].y, before[source_corner].y, 2.0e-5F);
            }
          }
          if (disconnected) {
            documents[target_index] = prepared;
          } else {
            shared_document = prepared;
          }
        }
      }
    }
  }
}

TEST(FaceTextureWrapTests, JunctionRoutesPreserveEachSeamAndCloseWhenDevelopable) {
  constexpr std::array<std::array<std::size_t, 4>, 4> kFaceCorners{{
      {3U, 4U, 7U, 6U}, {4U, 5U, 8U, 7U},
      {0U, 1U, 4U, 3U}, {1U, 2U, 5U, 4U}}};
  constexpr std::array<std::array<std::size_t, 3>, 2> kRoutes{{
      {0U, 1U, 3U}, {0U, 2U, 3U}}};
  for (const float rise : {0.0F, 1.0F}) {
    SCOPED_TRACE(rise);
    // Four faces meet at (0,0,2): ramp top, flat top, and two coplanar fronts.
    const std::array<Vec3, 9> positions{{
        {-2.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F},
        {-2.0F, 0.0F, 2.0F - rise}, {0.0F, 0.0F, 2.0F}, {2.0F, 0.0F, 2.0F},
        {-2.0F, 2.0F, 2.0F - rise}, {0.0F, 2.0F, 2.0F}, {2.0F, 2.0F, 2.0F}}};
    AuthoredTopology topology;
    for (std::size_t vertex = 0U; vertex < positions.size(); ++vertex) {
      topology.vertices.push_back({make_id<VertexTag>(vertex + 1U), positions[vertex]});
    }
    std::array<std::array<VertexId, 4>, 4> face_vertices{};
    for (std::size_t face_index = 0U; face_index < kFaceCorners.size(); ++face_index) {
      for (std::size_t corner = 0U; corner < kFaceCorners[face_index].size(); ++corner) {
        face_vertices[face_index][corner] = make_id<VertexTag>(kFaceCorners[face_index][corner] + 1U);
      }
      topology.faces.push_back({.id = make_id<FaceTag>(face_index + 1U),
          .vertices = {face_vertices[face_index].begin(), face_vertices[face_index].end()}});
    }
    const float ramp_width = std::hypot(2.0F, rise);
    topology.faces[0].uvs = {{-ramp_width, 0.0F}, {0.0F, 0.0F},
        {0.0F, 2.0F}, {-ramp_width, 2.0F}};
    topology.next_vertex_id = 10U;
    topology.next_face_id = 5U;
    const auto initial = PolygonDocument::from_authored_topology(topology).or_throw();
    std::array<std::array<Vec2, 4>, 2> results{};
    for (std::size_t route_index = 0U; route_index < kRoutes.size(); ++route_index) {
      SCOPED_TRACE(route_index);
      auto document = initial;
      const auto& route = kRoutes[route_index];
      for (std::size_t step = 1U; step < route.size(); ++step) {
        SCOPED_TRACE(step);
        const auto source_index = route[step - 1U];
        const auto target_index = route[step];
        const FaceTextureTransferFace source{
            .key = 1U, .document = &document, .face = make_id<FaceTag>(source_index + 1U)};
        const FaceTextureTransferFace target{
            .key = 1U, .document = &document, .face = make_id<FaceTag>(target_index + 1U)};
        const auto source_uvs = compiled_face_uvs(document, source.face, face_vertices[source_index]);
        const auto transferred = prepare_face_texture_transfer(source, target, 7U);
        ASSERT_TRUE(transferred.ok()) << transferred.error_message();
        ASSERT_EQ(transferred.value().documents.size(), 1U);
        const auto& prepared = transferred.value().documents.front().document;
        EXPECT_TRUE(prepared.has_same_geometry(document));
        const auto target_uvs = compiled_face_uvs(prepared, target.face, face_vertices[target_index]);
        std::size_t seam_samples = 0U;
        for (std::size_t corner = 0U; corner < target_uvs.size(); ++corner) {
          const auto found = std::ranges::find(
              face_vertices[source_index], face_vertices[target_index][corner]);
          if (found != face_vertices[source_index].end()) {
            const auto source_corner = static_cast<std::size_t>(found - face_vertices[source_index].begin());
            EXPECT_NEAR(target_uvs[corner].x, source_uvs[source_corner].x, 2.0e-5F);
            EXPECT_NEAR(target_uvs[corner].y, source_uvs[source_corner].y, 2.0e-5F);
            ++seam_samples;
          }
          // A unit-density chart preserves every in-face distance, including
          // diagonals: this also catches shear introduced during a transfer.
          for (std::size_t other = corner + 1U; other < target_uvs.size(); ++other) {
            const auto first = positions[kFaceCorners[target_index][corner]];
            const auto second = positions[kFaceCorners[target_index][other]];
            const float world_distance_squared = (first.x - second.x) * (first.x - second.x) +
                (first.y - second.y) * (first.y - second.y) +
                (first.z - second.z) * (first.z - second.z);
            const float du = target_uvs[corner].x - target_uvs[other].x;
            const float dv = target_uvs[corner].y - target_uvs[other].y;
            EXPECT_NEAR(du * du + dv * dv, world_distance_squared, 2.0e-5F);
          }
        }
        EXPECT_EQ(seam_samples, 2U);
        document = prepared;
      }
      results[route_index] = compiled_face_uvs(document, make_id<FaceTag>(4U), face_vertices[3]);
    }
    if (rise == 0.0F) {
      for (std::size_t corner = 0U; corner < results[0].size(); ++corner) {
        EXPECT_NEAR(results[0][corner].x, results[1][corner].x, 2.0e-5F);
        EXPECT_NEAR(results[0][corner].y, results[1][corner].y, 2.0e-5F);
      }
    }
    const Vec2 first_direction{results[0][2].x - results[0][3].x,
        results[0][2].y - results[0][3].y};
    const Vec2 second_direction{results[1][2].x - results[1][3].x,
        results[1][2].y - results[1][3].y};
    const double route_rotation = std::atan2(
        first_direction.x * second_direction.y - first_direction.y * second_direction.x,
        first_direction.x * second_direction.x + first_direction.y * second_direction.y) *
        57.29577951308232;
    // Three corners are right angles; the ramp-front corner is atan2(2,rise).
    // Their deficit is real surface curvature, so a sloped ring cannot close
    // both routes while preserving all seam samples and in-face distances.
    constexpr double kPi = 3.14159265358979323846;
    const double corner_sum = 1.5 * kPi + std::atan2(2.0, static_cast<double>(rise));
    const double expected_rotation = (corner_sum - 2.0 * kPi) * 57.29577951308232;
    EXPECT_NEAR(route_rotation, expected_rotation, 1.0e-4)
        << "measured route rotation=" << route_rotation
        << ", signed angle deficit=" << expected_rotation;
  }
}

TEST(FaceTextureWrapTests,
     ToleratedWarpKeepsTheSharedSeamFixedUnderNearlyOpposedFolds) {
  const std::array<FaceTextureTransform, 2> settings{{{},
      {.scale = {-0.25F, 0.5F}, .shift_uv = {0.31F, -0.37F}, .rotation_degrees = 37.0F}}};
  constexpr std::array<VertexId, 4> kSourceCorners{{make_id<VertexTag>(1U),
      make_id<VertexTag>(2U), make_id<VertexTag>(3U), make_id<VertexTag>(4U)}};
  constexpr std::array<VertexId, 4> kTargetCorners{{make_id<VertexTag>(3U),
      make_id<VertexTag>(2U), make_id<VertexTag>(5U), make_id<VertexTag>(6U)}};
  for (const float warp : {1.0e-6F, 1.0e-5F}) {
    for (const auto& transform : settings) {
      for (const bool distant : {false, true}) {
        for (std::size_t source_start = 0U; source_start < 4U; ++source_start) {
          for (std::size_t target_start = 0U; target_start < 4U; ++target_start) {
            SCOPED_TRACE(::testing::Message() << "warp=" << warp << ", distant=" << distant
                << ", rotation=" << transform.rotation_degrees
                << ", source start=" << source_start << ", target start=" << target_start);
            AuthoredTopology topology;
            topology.vertices = {
                {kSourceCorners[0], {0.0F, 1.0F, 0.0F}},
                {kSourceCorners[1], {0.0F, 0.0F, 0.0F}},
                {kSourceCorners[2], {1.0F, 0.0F, 0.0F}},
                {kSourceCorners[3], {1.0F, 1.0F, warp}},
                {kTargetCorners[2], {0.0F, 1.0F, 2.0F * warp}},
                {kTargetCorners[3], {1.0F, 1.0F, 2.0F * warp}}};
            topology.faces = {
                {.id = make_id<FaceTag>(1U),
                 .vertices = {kSourceCorners.begin(), kSourceCorners.end()},
                 .uvs = {{0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}},
                 .texture_transform = transform},
                {.id = make_id<FaceTag>(2U),
                 .vertices = {kTargetCorners.begin(), kTargetCorners.end()}}};
            auto& source_face = topology.faces[0];
            auto& target_face = topology.faces[1];
            std::rotate(source_face.vertices.begin(),
                source_face.vertices.begin() + static_cast<std::ptrdiff_t>(source_start),
                source_face.vertices.end());
            std::rotate(source_face.uvs.begin(),
                source_face.uvs.begin() + static_cast<std::ptrdiff_t>(source_start),
                source_face.uvs.end());
            std::rotate(target_face.vertices.begin(),
                target_face.vertices.begin() + static_cast<std::ptrdiff_t>(target_start),
                target_face.vertices.end());
            topology.next_vertex_id = 7U;
            topology.next_face_id = 3U;
            const auto document = PolygonDocument::from_authored_topology(topology).or_throw();
            const auto before = serialize_qdr_authored_topology(document).or_throw();
            FaceTextureTransferFace source{
                .key = 1U, .document = &document, .face = source_face.id};
            FaceTextureTransferFace target{
                .key = 1U, .document = &document, .face = target_face.id};
            if (distant) {
              source.world_from_local.origin = {1.0e8F, -1.0e8F, 1.0e8F};
              target.world_from_local = source.world_from_local;
            }
            const auto source_uvs = compiled_face_uvs(document, source.face, kSourceCorners);
            const auto result = prepare_face_texture_transfer(source, target, 7U);
            ASSERT_TRUE(result.ok()) << result.error_message();
            ASSERT_EQ(result.value().documents.size(), 1U);
            const auto& prepared = result.value().documents.front().document;
            EXPECT_TRUE(prepared.has_same_geometry(document));
            EXPECT_TRUE(prepared.shares_selection_with(document));
            const auto actual = compiled_face_uvs(prepared, target.face, kTargetCorners);
            // The tiny warp is within the face planarity tolerance. Its almost
            // opposed fold must stay anchored to A-B, never to an offset plane.
            const Vec2 transverse{source_uvs[3].x - source_uvs[2].x,
                source_uvs[3].y - source_uvs[2].y};
            const std::array<Vec2, 4> expected{{source_uvs[2], source_uvs[1],
                {source_uvs[1].x - transverse.x, source_uvs[1].y - transverse.y},
                {source_uvs[2].x - transverse.x, source_uvs[2].y - transverse.y}}};
            for (std::size_t corner = 0U; corner < actual.size(); ++corner) {
              EXPECT_NEAR(actual[corner].x, expected[corner].x, 2.0e-5F);
              EXPECT_NEAR(actual[corner].y, expected[corner].y, 2.0e-5F);
            }
            EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), before);
            source.document = &prepared;
            target.document = &prepared;
            const auto repeated = prepare_face_texture_transfer(source, target, 7U);
            ASSERT_TRUE(repeated.ok()) << repeated.error_message();
            EXPECT_FALSE(repeated.value().changed);
          }
        }
      }
    }
  }
}

TEST(FaceTextureWrapTests, CoincidentSeparateBoundaryTakesPriorityOverIntersectingRemoteLines) {
  constexpr std::array<VertexId, 4> kSourceCorners{{make_id<VertexTag>(1U),
      make_id<VertexTag>(2U), make_id<VertexTag>(3U), make_id<VertexTag>(4U)}};
  constexpr std::array<VertexId, 4> kTargetCorners{{make_id<VertexTag>(11U),
      make_id<VertexTag>(12U), make_id<VertexTag>(13U), make_id<VertexTag>(14U)}};
  for (std::size_t source_start = 0U; source_start < kSourceCorners.size(); ++source_start) {
    SCOPED_TRACE(source_start);
    AuthoredTopology source_topology;
    source_topology.vertices = {
        {kSourceCorners[0], {0.0F, 0.0F, 0.0F}},
        {kSourceCorners[1], {4.0F, 0.0F, 0.0F}},
        {kSourceCorners[2], {4.0F, 3.0F, 1.0F}},
        {kSourceCorners[3], {0.0F, 3.0F, 0.0F}}};
    source_topology.faces = {{.id = make_id<FaceTag>(1U),
        .vertices = {kSourceCorners.begin(), kSourceCorners.end()},
        .uvs = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}, {0.0F, 3.0F}}}};
    auto& source_face = source_topology.faces.front();
    std::rotate(source_face.vertices.begin(),
        source_face.vertices.begin() + static_cast<std::ptrdiff_t>(source_start),
        source_face.vertices.end());
    std::rotate(source_face.uvs.begin(),
        source_face.uvs.begin() + static_cast<std::ptrdiff_t>(source_start),
        source_face.uvs.end());
    source_topology.next_vertex_id = 5U;
    source_topology.next_face_id = 2U;
    AuthoredTopology target_topology;
    target_topology.vertices = {
        {kTargetCorners[0], {100.0F, 0.0F, 0.0F}},
        {kTargetCorners[1], {104.0F, 1.0F, 0.0F}},
        {kTargetCorners[2], {4.0F, 3.0F, 1.0F}},
        {kTargetCorners[3], {4.0F, 0.0F, 0.0F}}};
    target_topology.faces = {{.id = make_id<FaceTag>(2U),
        .vertices = {kTargetCorners.begin(), kTargetCorners.end()}}};
    target_topology.next_vertex_id = 15U;
    target_topology.next_face_id = 3U;
    const auto source_document = PolygonDocument::from_authored_topology(source_topology).or_throw();
    const auto target_document = PolygonDocument::from_authored_topology(target_topology).or_throw();
    const FaceTextureTransferFace source{
        .key = 1U, .document = &source_document, .face = make_id<FaceTag>(1U)};
    const FaceTextureTransferFace target{
        .key = 2U, .document = &target_document, .face = make_id<FaceTag>(2U)};
    const auto result = prepare_face_texture_transfer(source, target, 7U);
    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_EQ(result.value().documents.size(), 1U);
    const auto& prepared = result.value().documents.front().document;
    EXPECT_TRUE(prepared.has_same_geometry(target_document));
    const auto actual = compiled_face_uvs(prepared, target.face, kTargetCorners);
    // The first source and target supporting lines intersect remotely, but only
    // the B-C boundary actually coincides and must keep both endpoint samples.
    EXPECT_NEAR(actual[2].x, 4.0F, 1.0e-5F);
    EXPECT_NEAR(actual[2].y, 3.0F, 1.0e-5F);
    EXPECT_NEAR(actual[3].x, 4.0F, 1.0e-5F);
    EXPECT_NEAR(actual[3].y, 0.0F, 1.0e-5F);
  }
}

TEST(FaceTextureWrapTests, CoincidentOpposedFacesUnfoldAroundTheirExactBoundary) {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {4.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {4.0F, 3.0F, 0.0F}},
      {make_id<VertexTag>(4U), {0.0F, 3.0F, 0.0F}},
      {make_id<VertexTag>(5U), {2.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(6U), {2.0F, 3.0F, 0.0F}}};
  topology.faces = {
      {.id = make_id<FaceTag>(1U),
       .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
           make_id<VertexTag>(3U), make_id<VertexTag>(4U)},
       .uvs = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}, {0.0F, 3.0F}}},
      {.id = make_id<FaceTag>(2U),
       .vertices = {make_id<VertexTag>(2U), make_id<VertexTag>(5U),
           make_id<VertexTag>(6U), make_id<VertexTag>(3U)}}};
  topology.next_vertex_id = 7U;
  topology.next_face_id = 3U;
  const auto shared_document = PolygonDocument::from_authored_topology(topology).or_throw();
  auto source_topology = topology;
  source_topology.faces = {topology.faces[0]};
  auto target_topology = topology;
  target_topology.faces = {topology.faces[1]};
  const auto source_document = PolygonDocument::from_authored_topology(source_topology).or_throw();
  const auto target_document = PolygonDocument::from_authored_topology(target_topology).or_throw();
  for (const bool disconnected : {false, true}) {
    SCOPED_TRACE(disconnected);
    const FaceTextureTransferFace source{
        .key = 1U, .document = disconnected ? &source_document : &shared_document,
        .face = make_id<FaceTag>(1U)};
    const FaceTextureTransferFace target{
        .key = disconnected ? 2U : 1U,
        .document = disconnected ? &target_document : &shared_document,
        .face = make_id<FaceTag>(2U)};
    const auto result = prepare_face_texture_transfer(source, target, 7U);
    ASSERT_TRUE(result.ok()) << result.error_message();
    constexpr std::array<VertexId, 4> kCorners{{make_id<VertexTag>(2U),
        make_id<VertexTag>(5U), make_id<VertexTag>(6U), make_id<VertexTag>(3U)}};
    const auto actual = compiled_face_uvs(result.value().documents.front().document, target.face, kCorners);
    constexpr std::array<Vec2, 4> kExpected{{
        {4.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 3.0F}, {4.0F, 3.0F}}};
    for (std::size_t corner = 0U; corner < actual.size(); ++corner) {
      EXPECT_NEAR(actual[corner].x, kExpected[corner].x, 1.0e-5F);
      EXPECT_NEAR(actual[corner].y, kExpected[corner].y, 1.0e-5F);
    }
  }
}

TEST(FaceTextureWrapTests, LongThinSharedFacesRetainTheirConditionedLocalChart) {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {1.0e8F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {0.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(4U), {-1.0e8F, 1.0F, 0.0F}},
      {make_id<VertexTag>(5U), {0.0F, 0.0F, -2.0F}},
      {make_id<VertexTag>(6U), {1.0e8F, 0.0F, -2.0F}}};
  topology.faces = {
      {.id = make_id<FaceTag>(1U),
       .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
           make_id<VertexTag>(3U), make_id<VertexTag>(4U)},
       .uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 2.0F}}},
      {.id = make_id<FaceTag>(2U),
       .vertices = {make_id<VertexTag>(2U), make_id<VertexTag>(1U),
           make_id<VertexTag>(5U), make_id<VertexTag>(6U)}}};
  topology.next_vertex_id = 7U;
  topology.next_face_id = 3U;
  const auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  const FaceTextureTransferFace source{
      .key = 1U, .document = &document, .face = make_id<FaceTag>(1U)};
  const FaceTextureTransferFace target{
      .key = 1U, .document = &document, .face = make_id<FaceTag>(2U)};
  const auto result = prepare_face_texture_transfer(source, target, 7U);
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto output = result.value().documents.front().document.authored_topology().or_throw();
  // The seam and first corner off its line define U=x/1e8, V=y, even though
  // both candidate directions round to parallel in a cosine-based score.
  constexpr std::array<Vec2, 4> kExpected{{
      {1.0F, 0.0F}, {0.0F, 0.0F}, {0.0F, -2.0F}, {1.0F, -2.0F}}};
  ASSERT_EQ(output.faces[1].uvs.size(), kExpected.size());
  for (std::size_t corner = 0U; corner < kExpected.size(); ++corner) {
    EXPECT_NEAR(output.faces[1].uvs[corner].x, kExpected[corner].x, 1.0e-5F);
    EXPECT_NEAR(output.faces[1].uvs[corner].y, kExpected[corner].y, 1.0e-5F);
  }
}

TEST(FaceTextureWrapTests, NearParallelPlanesUseTheFloatEpsilonIntersectionBoundary) {
  AuthoredTopology topology;
  topology.vertices = {{make_id<VertexTag>(1), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2), {1.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3), {1.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(4), {0.0F, 1.0F, 0.0F}}};
  topology.faces = {{.id = make_id<FaceTag>(1),
      .vertices = {make_id<VertexTag>(1), make_id<VertexTag>(2),
          make_id<VertexTag>(3), make_id<VertexTag>(4)},
      .uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}}};
  topology.next_vertex_id = 5;
  topology.next_face_id = 2;
  const auto source_document = PolygonDocument::from_authored_topology(topology).or_throw();
  const auto target_document = PolygonDocument::from_authored_topology(topology).or_throw();
  const FaceTextureTransferFace source{
      .key = 1, .document = &source_document, .face = make_id<FaceTag>(1)};
  for (const float epsilon_factor : {-2.0F, -0.5F, 0.5F, 2.0F}) {
    SCOPED_TRACE(epsilon_factor);
    const float slope = epsilon_factor * std::numeric_limits<float>::epsilon();
    FaceTextureTransferFace target{
        .key = 2, .document = &target_document, .face = make_id<FaceTag>(1)};
    target.world_from_local.origin.z = 1.0e6F;
    target.world_from_local.x_axis.z = -slope;
    const auto result = prepare_face_texture_transfer(source, target, 7);
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto &prepared = result.value().documents.front().document;
    const auto output = prepared.authored_topology().or_throw();
    const double phase = std::abs(epsilon_factor) < 1.0F ? 0.0 :
        -1.0e6 * slope / (std::sqrt(1.0 + static_cast<double>(slope) * slope) + 1.0);
    ASSERT_EQ(output.faces.front().uvs.size(), 4U);
    EXPECT_NEAR(output.faces.front().uvs[0].x, phase, 0.005);
    EXPECT_NEAR(output.faces.front().uvs[1].x, 1.0 + phase, 0.005);
    EXPECT_NEAR(output.faces.front().uvs[2].y, 1.0F, 1.0e-6F);
    target.document = &prepared;
    const auto repeated = prepare_face_texture_transfer(source, target, 7);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.value().changed);
  }
}

TEST(FaceTextureWrapTests, ShortSelectedEdgeUsesAUsablePlaneWhenTheFarthestCornerIsCollinear) {
  AuthoredTopology topology;
  topology.vertices = {{make_id<VertexTag>(1), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2), {1.0e-4F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3), {100.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(4), {99.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(5), {0.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(6), {0.0F, 0.0F, -2.0F}},
      {make_id<VertexTag>(7), {1.0e-4F, 0.0F, -2.0F}}};
  topology.faces = {{.id = make_id<FaceTag>(1),
      .vertices = {make_id<VertexTag>(1), make_id<VertexTag>(2),
          make_id<VertexTag>(3), make_id<VertexTag>(4), make_id<VertexTag>(5)},
      .uvs = {{0.0F, 0.0F}, {1.0e-4F, 0.0F}, {100.0F, 0.0F},
          {99.0F, 1.0F}, {0.0F, 1.0F}}},
      {.id = make_id<FaceTag>(2),
      .vertices = {make_id<VertexTag>(2), make_id<VertexTag>(1),
          make_id<VertexTag>(6), make_id<VertexTag>(7)}}};
  topology.next_vertex_id = 8;
  topology.next_face_id = 3;
  const auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  const FaceTextureTransferFace source{
      .key = 1, .document = &document, .face = make_id<FaceTag>(1)};
  const FaceTextureTransferFace target{
      .key = 1, .document = &document, .face = make_id<FaceTag>(2)};
  const auto result = prepare_face_texture_transfer(source, target, 7);
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto &prepared = result.value().documents.front().document;
  EXPECT_TRUE(prepared.has_same_geometry(document));
  const auto output = prepared.authored_topology().or_throw();
  // The endpoint's farthest corner is collinear with this short edge, but
  // the face still has a usable seam plane and must unfold its full width.
  ASSERT_EQ(output.faces[1].uvs.size(), 4U);
  for (std::size_t corner = 0U; corner < output.faces[1].uvs.size(); ++corner) {
    EXPECT_NEAR(output.faces[1].uvs[corner].y, corner < 2U ? 0.0F : -2.0F, 1.0e-6F);
  }
  EXPECT_NEAR(output.faces[1].uvs[0].x, 1.0e-4F, 1.0e-7F);
  EXPECT_NEAR(output.faces[1].uvs[1].x, 0.0F, 1.0e-7F);
}

} // namespace
