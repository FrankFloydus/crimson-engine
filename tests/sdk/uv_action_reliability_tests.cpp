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
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using namespace quader::modeling;

/** A valid authored quad optionally shares a document with a collapsed face. */
AuthoredTopology quad_topology(bool collapsed_neighbor = false) {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {0.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(2U), {2.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(3U), {2.0F, 2.0F, 0.0F}},
      {make_id<VertexTag>(4U), {0.0F, 2.0F, 0.0F}}};
  topology.faces = {{.id = make_id<FaceTag>(1U),
      .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                   make_id<VertexTag>(3U), make_id<VertexTag>(4U)},
      .uvs = {{0.25F, 0.0F}, {1.25F, 0.0F}, {1.25F, -1.0F}, {0.25F, -1.0F}}}};
  topology.next_vertex_id = 5U;
  topology.next_face_id = 2U;
  if (collapsed_neighbor) {
    topology.vertices.insert(topology.vertices.end(), {
        {make_id<VertexTag>(5U), {4.0F, 0.0F, 0.0F}},
        {make_id<VertexTag>(6U), {5.0F, 0.0F, 0.0F}},
        {make_id<VertexTag>(7U), {6.0F, 0.0F, 0.0F}}});
    topology.faces.push_back({.id = make_id<FaceTag>(2U),
        .vertices = {make_id<VertexTag>(5U), make_id<VertexTag>(6U),
                     make_id<VertexTag>(7U)},
        .uvs = {{0.2F, 0.8F}, {0.7F, 0.4F}, {0.9F, 0.1F}},
        .material_slot = 3U});
    topology.next_vertex_id = 8U;
    topology.next_face_id = 3U;
  }
  return topology;
}

/** Targets one local face without making occurrence placement a UV input. */
FaceTextureActionTarget quad_target(const PolygonDocument &document) {
  return {.key = 1U, .document = &document, .faces = {{.face = make_id<FaceTag>(1U)}}};
}

/** The untouched collapsed face must retain every authored attribute. */
void expect_collapsed_face_preserved(const PolygonDocument &before,
                                     const PolygonDocument &after) {
  EXPECT_TRUE(after.has_same_geometry(before));
  EXPECT_TRUE(after.shares_selection_with(before));
  EXPECT_EQ(after.selection_revision(), before.selection_revision());
  const auto original = before.authored_topology().or_throw();
  const auto result = after.authored_topology().or_throw();
  ASSERT_EQ(result.faces.size(), original.faces.size());
  const auto &first = original.faces.back();
  const auto &second = result.faces.back();
  EXPECT_EQ(second.id, first.id);
  EXPECT_EQ(second.vertices, first.vertices);
  EXPECT_EQ(second.material_slot, first.material_slot);
  EXPECT_EQ(second.texture_transform, first.texture_transform);
  ASSERT_EQ(second.uvs.size(), first.uvs.size());
  for (std::size_t corner = 0U; corner < first.uvs.size(); ++corner) {
    EXPECT_EQ(second.uvs[corner].x, first.uvs[corner].x);
    EXPECT_EQ(second.uvs[corner].y, first.uvs[corner].y);
  }
  EXPECT_FALSE(after.validate().ok());
}

TEST(UvActionReliabilityTests, LocalUvActionsPreserveUnrelatedCollapsedAuthoredFaces) {
  auto document = PolygonDocument::from_authored_topology(quad_topology(true)).or_throw();
  const std::array face{make_id<FaceTag>(1U)};
  ASSERT_TRUE(document.select_faces(face, SelectionEdit::Replace).ok());
  EXPECT_FALSE(document.validate().ok());
  const auto before = serialize_qdr_authored_topology(document).or_throw();
  const std::array target{quad_target(document)};
  for (const auto action : {FaceTextureAction::ShiftLeft, FaceTextureAction::FitBoth,
                            FaceTextureAction::AlignToFace, FaceTextureAction::ProjectCube,
                            FaceTextureAction::FlipFaceHorizontal, FaceTextureAction::FlipFaceVertical}) {
    SCOPED_TRACE(static_cast<int>(action));
    const auto prepared = prepare_face_texture_action(target, {.action = action});
    ASSERT_TRUE(prepared.ok()) << prepared.error_message();
    ASSERT_TRUE(prepared.value().changed);
    ASSERT_EQ(prepared.value().documents.size(), 1U);
    expect_collapsed_face_preserved(document, prepared.value().documents.front().document);
  }
  EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), before);
}

TEST(UvActionReliabilityTests, PeelValidPatchPreservesUnrelatedCollapsedAuthoredFace) {
  auto document = PolygonDocument::from_authored_topology(quad_topology(true)).or_throw();
  const auto edge = make_edge_key(make_id<VertexTag>(1U), make_id<VertexTag>(2U));
  const std::array target{UvPeelTarget{.key = 1U, .document = &document,
      .edges = {edge}, .active_edge = edge}};
  const auto before = serialize_qdr_authored_topology(document).or_throw();
  const auto prepared = prepare_uv_peel(target, {.u_offset = 0.75});
  ASSERT_TRUE(prepared.ok()) << prepared.error_message();
  ASSERT_TRUE(prepared.value().changed);
  ASSERT_EQ(prepared.value().documents.size(), 1U);
  expect_collapsed_face_preserved(document, prepared.value().documents.front().document);
  EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), before);
}

TEST(UvActionReliabilityTests, TextureTransferPreservesUnrelatedCollapsedAuthoredFace) {
  auto source_topology = quad_topology();
  for (auto &uv : source_topology.faces.front().uvs) {
    uv.x += 0.5F;
  }
  auto source = PolygonDocument::from_authored_topology(source_topology).or_throw();
  auto target = PolygonDocument::from_authored_topology(quad_topology(true)).or_throw();
  const auto source_before = serialize_qdr_authored_topology(source).or_throw();
  const auto target_before = serialize_qdr_authored_topology(target).or_throw();
  const auto result = prepare_face_texture_transfer(
      {.key = 1U, .document = &source, .face = make_id<FaceTag>(1U)},
      {.key = 2U, .document = &target, .face = make_id<FaceTag>(1U)}, 7U);
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.value().changed);
  ASSERT_EQ(result.value().documents.size(), 1U);
  const auto &prepared = result.value().documents.front();
  EXPECT_EQ(prepared.key, 2U);
  EXPECT_TRUE(prepared.receipt.dirty.materials);
  expect_collapsed_face_preserved(target, prepared.document);
  const auto topology = prepared.document.authored_topology().or_throw();
  EXPECT_EQ(topology.faces.front().material_slot, 7U);
  ASSERT_EQ(topology.faces.front().uvs.size(), source_topology.faces.front().uvs.size());
  for (std::size_t corner = 0U; corner < topology.faces.front().uvs.size(); ++corner) {
    EXPECT_EQ(topology.faces.front().uvs[corner].x,
              source_topology.faces.front().uvs[corner].x);
    EXPECT_EQ(topology.faces.front().uvs[corner].y,
              source_topology.faces.front().uvs[corner].y);
  }
  EXPECT_EQ(serialize_qdr_authored_topology(source).or_throw(), source_before);
  EXPECT_EQ(serialize_qdr_authored_topology(target).or_throw(), target_before);
}

TEST(UvActionReliabilityTests, PlaneDependentActionsRejectCollapsedFacesAndAllActionsRejectStaleIds) {
  auto document = PolygonDocument::from_authored_topology(quad_topology(true)).or_throw();
  const auto before = serialize_qdr_authored_topology(document).or_throw();
  for (const auto action : {FaceTextureAction::ShiftLeft, FaceTextureAction::AlignToFace}) {
    auto target = quad_target(document);
    target.faces.front().face = make_id<FaceTag>(99U);
    const auto result = prepare_face_texture_action(
        std::array{target}, {.action = action});
    EXPECT_FALSE(result.ok());
  }
  auto collapsed_target = quad_target(document);
  collapsed_target.faces.front().face = make_id<FaceTag>(2U);
  const auto alignment = prepare_face_texture_action(
      std::array{collapsed_target}, {.action = FaceTextureAction::AlignToFace});
  EXPECT_FALSE(alignment.ok());
  // A stored texture shift needs no geometric plane and remains meaningful
  // if this authored face is expanded again by a later modeling operation.
  const auto shifted = prepare_face_texture_action(
      std::array{collapsed_target}, {.action = FaceTextureAction::ShiftLeft});
  ASSERT_TRUE(shifted.ok()) << shifted.error_message();
  ASSERT_TRUE(shifted.value().changed);
  const auto &shifted_document = shifted.value().documents.front().document;
  EXPECT_TRUE(shifted_document.has_same_geometry(document));
  EXPECT_TRUE(shifted_document.shares_selection_with(document));
  const auto shifted_topology = shifted_document.authored_topology().or_throw();
  const auto original_topology = document.authored_topology().or_throw();
  const auto &original_uvs = original_topology.faces.back().uvs;
  const auto &shifted_uvs = shifted_topology.faces.back().uvs;
  ASSERT_EQ(shifted_uvs.size(), original_uvs.size());
  for (std::size_t corner = 0U; corner < original_uvs.size(); ++corner) {
    EXPECT_EQ(shifted_uvs[corner].x, original_uvs[corner].x);
    EXPECT_EQ(shifted_uvs[corner].y, original_uvs[corner].y);
  }
  EXPECT_EQ(shifted_topology.faces.back().texture_transform.shift_uv.x, 1.0F / 64.0F);
  EXPECT_FALSE(shifted_document.validate().ok());
  const auto edge = make_edge_key(make_id<VertexTag>(5U), make_id<VertexTag>(6U));
  const auto peel = prepare_uv_peel(std::array{UvPeelTarget{.key = 1U,
      .document = &document, .edges = {edge}, .active_edge = edge}}, {});
  EXPECT_FALSE(peel.ok());
  EXPECT_EQ(serialize_qdr_authored_topology(document).or_throw(), before);
}

TEST(UvActionReliabilityTests, AlignmentDoesNotDiscardQuarterTileAtMillionTileOffset) {
  for (const float offset : {-1000000.0F, 1000000.0F}) {
    auto topology = quad_topology();
    topology.faces.front().texture_transform.shift_uv.x = offset;
    auto document = PolygonDocument::from_authored_topology(topology).or_throw();
    const auto prepared = prepare_face_texture_action(
        std::array{quad_target(document)}, {.action = FaceTextureAction::AlignToView});
    ASSERT_TRUE(prepared.ok()) << prepared.error_message();
    ASSERT_TRUE(prepared.value().changed);
    const auto &result = prepared.value().documents.front().document;
    const auto compiled = result.compile_mesh().or_throw();
    ASSERT_FALSE(compiled.vertices.empty());
    for (const auto &vertex : compiled.vertices) {
      EXPECT_EQ(vertex.uv0.x, offset + vertex.position.x * 0.5F);
      EXPECT_EQ(vertex.uv0.y, -vertex.position.y * 0.5F);
    }
    const auto repeated = prepare_face_texture_action(
        std::array{quad_target(result)}, {.action = FaceTextureAction::AlignToView});
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.value().changed);
    EXPECT_EQ(repeated.value().documents.front().document.content_revision(),
              result.content_revision());
  }
}

TEST(UvActionReliabilityTests, LocalUvActionsDoNotQuantizeFarOccurrenceGeometry) {
  auto document = PolygonDocument::from_authored_topology(quad_topology()).or_throw();
  for (const auto action : {
      FaceTextureAction::ScaleXUp, FaceTextureAction::ScaleXDown,
      FaceTextureAction::ScaleYUp, FaceTextureAction::ScaleYDown,
      FaceTextureAction::ShiftLeft, FaceTextureAction::ShiftRight,
      FaceTextureAction::ShiftUp, FaceTextureAction::ShiftDown,
      FaceTextureAction::RotateClockwise, FaceTextureAction::RotateCounterClockwise,
      FaceTextureAction::FlipTextureHorizontal, FaceTextureAction::FlipTextureVertical,
      FaceTextureAction::FitBoth, FaceTextureAction::FitX, FaceTextureAction::FitY,
      FaceTextureAction::JustifyLeft, FaceTextureAction::JustifyRight,
      FaceTextureAction::JustifyTop, FaceTextureAction::JustifyBottom,
      FaceTextureAction::JustifyCenter, FaceTextureAction::ProjectCube,
      FaceTextureAction::ProjectCylinder, FaceTextureAction::ProjectSphere}) {
    SCOPED_TRACE(static_cast<int>(action));
    const auto local = prepare_face_texture_action(
        std::array{quad_target(document)}, {.action = action});
    ASSERT_TRUE(local.ok()) << local.error_message();
    auto distant_target = quad_target(document);
    distant_target.world_from_local.origin = {1.0e8F, -1.0e8F, 1.0e8F};
    const auto distant = prepare_face_texture_action(
        std::array{distant_target}, {.action = action});
    ASSERT_TRUE(distant.ok()) << distant.error_message();
    EXPECT_EQ(distant.value().changed, local.value().changed);
    EXPECT_EQ(serialize_qdr_authored_topology(distant.value().documents.front().document).or_throw(),
              serialize_qdr_authored_topology(local.value().documents.front().document).or_throw());
  }
}

TEST(UvActionReliabilityTests, TreatAsOneFitsIndependentAnisotropicUvAxes) {
  for (const float height : {1.0e-7F, 1.0e-12F}) {
    auto topology = quad_topology();
    topology.faces.front().uvs = {{0.0F, 0.0F}, {1.0F, 0.0F},
                                 {1.0F, height}, {0.0F, height}};
    auto first = PolygonDocument::from_authored_topology(topology).or_throw();
    auto second = PolygonDocument::from_authored_topology(topology).or_throw();
    auto second_target = quad_target(second);
    second_target.key = 2U;
    second_target.world_from_local.origin.x = 2.0F;
    const std::array targets{quad_target(first), second_target};
    const auto result = prepare_face_texture_action(targets,
        {.action = FaceTextureAction::FitBoth, .treat_as_one = true,
         .active_target_key = 1U, .active_face = make_id<FaceTag>(1U)});
    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    ASSERT_EQ(result.value().documents.size(), 2U);
    for (std::size_t index = 0U; index < 2U; ++index) {
      const auto mesh = result.value().documents[index].document.compile_mesh().or_throw();
      ASSERT_FALSE(mesh.vertices.empty());
      for (const auto &vertex : mesh.vertices) {
        EXPECT_NEAR(vertex.uv0.x, vertex.position.x * 0.25F +
                                      static_cast<float>(index) * 0.5F, 1.0e-5F);
        EXPECT_NEAR(vertex.uv0.y, vertex.position.y * 0.5F, 1.0e-5F);
      }
    }
  }
}

TEST(UvActionReliabilityTests, TreatAsOneStillRejectsCollinearUvCharts) {
  auto topology = quad_topology();
  topology.faces.front().uvs = {{0.0F, 0.0F}, {1.0F, 2.0F},
                               {2.0F, 4.0F}, {1.0F, 2.0F}};
  auto first = PolygonDocument::from_authored_topology(topology).or_throw();
  auto second = PolygonDocument::from_authored_topology(topology).or_throw();
  auto second_target = quad_target(second);
  second_target.key = 2U;
  const auto result = prepare_face_texture_action(
      std::array{quad_target(first), second_target},
      {.action = FaceTextureAction::FitBoth, .treat_as_one = true});
  EXPECT_FALSE(result.ok());
}

} // namespace
