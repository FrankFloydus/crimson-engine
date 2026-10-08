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

TEST(FaceTextureWrapTests, ConcavePlanesFollowTheSelectedBoundaryEdgeOrientation) {
  AuthoredTopology topology;
  constexpr std::array<Vec3, 10> kPositions{{
      {0.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F},
      {4.0F, 4.0F, 0.0F}, {3.0F, 4.0F, 0.0F},
      {3.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 0.0F},
      {1.0F, 4.0F, 0.0F}, {0.0F, 4.0F, 0.0F},
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
  topology.next_vertex_id = 11;
  topology.next_face_id = 3;
  const auto shared_document = PolygonDocument::from_authored_topology(topology).or_throw();
  for (const bool disconnected : {false, true}) {
    SCOPED_TRACE(disconnected);
    auto source_topology = topology;
    source_topology.faces.resize(1U);
    auto target_topology = topology;
    target_topology.faces.erase(target_topology.faces.begin());
    const auto source_document =
        PolygonDocument::from_authored_topology(source_topology).or_throw();
    const auto target_document =
        PolygonDocument::from_authored_topology(target_topology).or_throw();
    const FaceTextureTransferFace source{
        .key = 1, .document = disconnected ? &source_document : &shared_document,
        .face = make_id<FaceTag>(1)};
    const FaceTextureTransferFace target{
        .key = disconnected ? 2U : 1U,
        .document = disconnected ? &target_document : &shared_document,
        .face = make_id<FaceTag>(2)};
    const auto result = prepare_face_texture_transfer(source, target, 7);
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto &prepared = result.value().documents.front().document;
    EXPECT_TRUE(prepared.has_same_geometry(*target.document));
    const auto output = prepared.authored_topology().or_throw();
    const auto &face = output.faces.back();
    // The reflex edge points down Y; its farthest source corner lies at X=0,
    // so Hammer uses -Z here despite the source polygon's +Z area normal.
    constexpr std::array<Vec2, 4> kExpected{{
        {3.0F, 1.0F}, {3.0F, 4.0F}, {5.0F, 4.0F}, {5.0F, 1.0F}}};
    ASSERT_EQ(face.uvs.size(), kExpected.size());
    for (std::size_t corner = 0U; corner < kExpected.size(); ++corner) {
      EXPECT_NEAR(face.uvs[corner].x, kExpected[corner].x, 1.0e-5F);
      EXPECT_NEAR(face.uvs[corner].y, kExpected[corner].y, 1.0e-5F);
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

TEST(FaceTextureWrapTests, DegenerateSelectedEdgePlaneRetainsTheSourceChart) {
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
  // Below the edge-length gate Hammer measures from the first endpoint. The
  // farthest source corner is collinear, so its zero plane prevents rotation.
  ASSERT_EQ(output.faces[1].uvs.size(), 4U);
  for (const auto uv : output.faces[1].uvs) {
    EXPECT_NEAR(uv.y, 0.0F, 1.0e-6F);
  }
  EXPECT_NEAR(output.faces[1].uvs[0].x, 1.0e-4F, 1.0e-7F);
  EXPECT_NEAR(output.faces[1].uvs[1].x, 0.0F, 1.0e-7F);
}

} // namespace
