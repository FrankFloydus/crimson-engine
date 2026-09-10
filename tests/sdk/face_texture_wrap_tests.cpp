////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <quader/modeling/modeling.hpp>

#include <array>

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

} // namespace
