////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <quader/modeling/modeling.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace {

using namespace quader::modeling;

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
  constexpr std::array<Vec2, 4> expected{{
      {1.0F, 0.0F}, {1.5F, -0.5F}, {1.5F, 0.5F}, {1.0F, 1.0F}}};
  for (std::size_t corner = 0; corner < expected.size(); ++corner) {
    EXPECT_NEAR(output.faces[1].uvs[corner].x, expected[corner].x, 1.0e-6F);
    EXPECT_NEAR(output.faces[1].uvs[corner].y, expected[corner].y, 1.0e-6F);
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
  for (const float epsilon_factor : {0.5F, 2.0F}) {
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
    const double phase = epsilon_factor < 1.0F ? 0.0 :
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
