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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace quader::modeling;

/** Builds a selected open quad carrying authored face and edge attributes. */
PolygonDocument make_extrude_quad(bool collapsed = false) {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {-1.0F, -1.0F, 0.0F}},
      {make_id<VertexTag>(2U), {1.0F, -1.0F, 0.0F}},
      {make_id<VertexTag>(3U), {1.0F, 1.0F, 0.0F}},
      {make_id<VertexTag>(4U), {-1.0F, 1.0F, 0.0F}},
  };
  if (collapsed) {
    for (std::size_t index = 0; index < topology.vertices.size(); ++index) {
      topology.vertices[index].position = {static_cast<float>(index), 0.0F, 0.0F};
    }
  }
  topology.faces.push_back(
      {.id = make_id<FaceTag>(11U),
       .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                    make_id<VertexTag>(3U), make_id<VertexTag>(4U)},
       .uvs = {{0.25F, -1.0F}, {2.0F, 0.5F}, {3.0F, 1.75F}, {-0.5F, 2.0F}},
       .material_slot = 7U,
       .shading = AuthoredSurfaceShading::Smooth,
       .texture_transform = {.scale = {-2.0F, 0.5F},
                             .shift_uv = {0.125F, -0.25F},
                             .rotation_degrees = 45.0F}});
  topology.hard_normal_edges = {
      make_edge_key(make_id<VertexTag>(1U), make_id<VertexTag>(2U))};
  topology.soft_normal_edges = {
      make_edge_key(make_id<VertexTag>(3U), make_id<VertexTag>(4U))};
  topology.next_vertex_id = 5U;
  topology.next_face_id = 12U;
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  static_cast<void>(document.apply_selection(
      {.kind = SelectionKind::Face,
       .faces = {make_id<FaceTag>(11U)},
       .has_active = true,
       .active_face = make_id<FaceTag>(11U)}).or_throw());
  return document;
}

/** Rejected and zero-distance edits must preserve immutable publications. */
void expect_extrude_unchanged(const PolygonDocument& before,
                             const PolygonDocument& after) {
  EXPECT_TRUE(after.shares_topology_with(before));
  EXPECT_TRUE(after.shares_selection_with(before));
  EXPECT_EQ(after.content_identity(), before.content_identity());
  EXPECT_EQ(after.content_revision(), before.content_revision());
  EXPECT_EQ(after.selection_revision(), before.selection_revision());
  EXPECT_EQ(after.vertex_count(), before.vertex_count());
  EXPECT_EQ(after.face_count(), before.face_count());
}

/** Builds two perpendicular selected faces with distinct authored attributes. */
PolygonDocument make_extrude_corner(float scale = 1.0F, bool reverse = false) {
  auto topology = make_extrude_quad().authored_topology().or_throw();
  const std::array positions{
      Vec3{0.0F, 0.0F, 0.0F}, Vec3{2.0F, 0.0F, 0.0F},
      Vec3{2.0F, 2.0F, 0.0F}, Vec3{0.0F, 2.0F, 0.0F},
      Vec3{2.0F, 0.0F, -2.0F}, Vec3{2.0F, 2.0F, -2.0F}};
  topology.vertices.clear();
  for (std::size_t index = 0; index < positions.size(); ++index) {
    const auto position = positions[index];
    topology.vertices.push_back({
        make_id<VertexTag>(static_cast<std::uint32_t>(index + 1U)),
        {position.x * scale, position.y * scale, position.z * scale}});
  }
  auto second = topology.faces.front();
  second.id = make_id<FaceTag>(12U);
  second.vertices = {make_id<VertexTag>(2U), make_id<VertexTag>(5U),
                     make_id<VertexTag>(6U), make_id<VertexTag>(3U)};
  second.material_slot = 13U;
  second.shading = AuthoredSurfaceShading::Flat;
  second.texture_transform.rotation_degrees = -25.0F;
  topology.faces.push_back(second);
  if (reverse) {
    for (auto& face : topology.faces) {
      std::ranges::reverse(face.vertices);
      std::ranges::reverse(face.uvs);
    }
  }
  topology.next_vertex_id = 7U;
  topology.next_face_id = 13U;
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  static_cast<void>(document.apply_selection({
      .kind = SelectionKind::Face,
      .faces = {make_id<FaceTag>(11U), make_id<FaceTag>(12U)},
      .has_active = true,
      .active_face = make_id<FaceTag>(11U)}).or_throw());
  return document;
}

/** Builds an open inset panel whose outer boundary lies in the YZ plane. */
PolygonDocument make_extrude_inset_panel(bool include_loose_vertex = false) {
  AuthoredTopology topology;
  topology.vertices = {
      {make_id<VertexTag>(1U), {0.0F, -5.5F, -5.5F}},
      {make_id<VertexTag>(2U), {0.0F, 5.5F, -5.5F}},
      {make_id<VertexTag>(3U), {0.0F, 5.5F, 5.5F}},
      {make_id<VertexTag>(4U), {0.0F, -5.5F, 5.5F}},
      {make_id<VertexTag>(5U), {-0.5F, -4.5F, -4.5F}},
      {make_id<VertexTag>(6U), {-0.5F, 4.5F, -4.5F}},
      {make_id<VertexTag>(7U), {-0.5F, 4.5F, 4.5F}},
      {make_id<VertexTag>(8U), {-0.5F, -4.5F, 4.5F}},
  };
  const std::array<std::array<std::uint32_t, 4>, 5> face_vertices{{
      {5U, 6U, 7U, 8U}, {1U, 2U, 6U, 5U}, {2U, 3U, 7U, 6U},
      {3U, 4U, 8U, 7U}, {4U, 1U, 5U, 8U}}};
  for (std::size_t index = 0; index < face_vertices.size(); ++index) {
    AuthoredTopologyFace face;
    face.id = make_id<FaceTag>(static_cast<std::uint32_t>(11U + index));
    for (const auto vertex_id : face_vertices[index]) {
      face.vertices.push_back(make_id<VertexTag>(vertex_id));
    }
    topology.faces.push_back(face);
  }
  topology.next_vertex_id = 9U;
  topology.next_face_id = 16U;
  if (include_loose_vertex) {
    topology.vertices.push_back({make_id<VertexTag>(9U), {20.0F, 30.0F, 40.0F}});
    topology.next_vertex_id = 10U;
  }
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  static_cast<void>(document.apply_selection({
      .kind = SelectionKind::Face,
      .faces = document.face_ids(),
      .has_active = true,
      .active_face = make_id<FaceTag>(11U)}).or_throw());
  return document;
}

/** Compares authored output while allowing normal floating-point rounding. */
void expect_extrude_equivalent(const PolygonDocument& expected,
                              const PolygonDocument& actual) {
  const auto left = expected.authored_topology().or_throw();
  const auto right = actual.authored_topology().or_throw();
  ASSERT_EQ(right.vertices.size(), left.vertices.size());
  ASSERT_EQ(right.faces.size(), left.faces.size());
  EXPECT_EQ(right.next_vertex_id, left.next_vertex_id);
  EXPECT_EQ(right.next_face_id, left.next_face_id);
  EXPECT_EQ(right.hard_normal_edges, left.hard_normal_edges);
  EXPECT_EQ(right.soft_normal_edges, left.soft_normal_edges);
  for (std::size_t index = 0; index < left.vertices.size(); ++index) {
    EXPECT_EQ(right.vertices[index].id, left.vertices[index].id);
    EXPECT_FLOAT_EQ(right.vertices[index].position.x, left.vertices[index].position.x);
    EXPECT_FLOAT_EQ(right.vertices[index].position.y, left.vertices[index].position.y);
    EXPECT_FLOAT_EQ(right.vertices[index].position.z, left.vertices[index].position.z);
  }
  for (std::size_t index = 0; index < left.faces.size(); ++index) {
    const auto& source = left.faces[index];
    const auto& output = right.faces[index];
    EXPECT_EQ(output.id, source.id);
    EXPECT_EQ(output.vertices, source.vertices);
    EXPECT_EQ(output.material_slot, source.material_slot);
    EXPECT_EQ(output.shading, source.shading);
    EXPECT_EQ(output.texture_transform, source.texture_transform);
    ASSERT_EQ(output.uvs.size(), source.uvs.size());
    for (std::size_t corner = 0; corner < source.uvs.size(); ++corner) {
      EXPECT_FLOAT_EQ(output.uvs[corner].x, source.uvs[corner].x);
      EXPECT_FLOAT_EQ(output.uvs[corner].y, source.uvs[corner].y);
    }
  }
  EXPECT_EQ(actual.selection().kind, expected.selection().kind);
  EXPECT_EQ(actual.selection().faces, expected.selection().faces);
  EXPECT_EQ(actual.selection().has_active, expected.selection().has_active);
  EXPECT_EQ(actual.selection().active_face, expected.selection().active_face);
}

/** Checks that each evaluation retains the complete source-relative receipt. */
void expect_extrude_receipt(const OperationResult& expected,
                           const OperationResult& actual) {
  EXPECT_EQ(actual.success, expected.success);
  EXPECT_EQ(actual.changed, expected.changed);
  EXPECT_EQ(actual.error_code, expected.error_code);
  EXPECT_EQ(actual.dirty.topology, expected.dirty.topology);
  EXPECT_EQ(actual.dirty.geometry, expected.dirty.geometry);
  EXPECT_EQ(actual.dirty.selection, expected.dirty.selection);
  EXPECT_EQ(actual.dirty.materials, expected.dirty.materials);
  EXPECT_EQ(actual.dirty.overlays, expected.dirty.overlays);
  const auto expect_delta = [](const ElementDelta& left, const ElementDelta& right) {
    EXPECT_EQ(right.vertices, left.vertices);
    EXPECT_EQ(right.edges, left.edges);
    EXPECT_EQ(right.faces, left.faces);
  };
  expect_delta(expected.created, actual.created);
  expect_delta(expected.deleted, actual.deleted);
  expect_delta(expected.affected, actual.affected);
  expect_delta(expected.modified, actual.modified);
  expect_delta(expected.selection_remap.source, actual.selection_remap.source);
  expect_delta(expected.selection_remap.destination, actual.selection_remap.destination);
}

TEST(ModelingSdkFaceExtrudeTests, DefaultOptionsCreateOneUnitRegionWithSelectedCap) {
  auto document = make_extrude_quad();
  const auto before = document;

  const auto result = document.extrude_selected_faces(FaceExtrudeOptions{});

  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.value().success);
  EXPECT_TRUE(result.value().changed);
  EXPECT_TRUE(result.value().dirty.topology);
  EXPECT_TRUE(result.value().dirty.geometry);
  EXPECT_EQ(document.vertex_count(), 8U);
  EXPECT_EQ(document.face_count(), 5U);
  EXPECT_FALSE(document.shares_topology_with(before));
  EXPECT_EQ(document.selection().kind, SelectionKind::Face);
  EXPECT_EQ(document.selection().faces, (std::vector<FaceId>{make_id<FaceTag>(11U)}));
  EXPECT_TRUE(document.selection().has_active);
  EXPECT_EQ(document.selection().active_face, make_id<FaceTag>(11U));
  const auto topology = document.authored_topology().or_throw();
  const auto cap = std::ranges::find(topology.faces, make_id<FaceTag>(11U),
                                    &AuthoredTopologyFace::id);
  ASSERT_NE(cap, topology.faces.end());
  for (const auto vertex_id : cap->vertices) {
    const auto vertex = std::ranges::find(topology.vertices, vertex_id,
                                         &AuthoredTopologyVertex::id);
    ASSERT_NE(vertex, topology.vertices.end());
    EXPECT_FLOAT_EQ(vertex->position.z, 1.0F);
  }
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingSdkFaceExtrudeTests, EveryModePreservesAuthoredCapAttributesAndEdgeModes) {
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto document = make_extrude_quad();
    const auto before = document.authored_topology().or_throw();
    const auto& source = before.faces.front();

    const auto result = document.extrude_selected_faces(
        FaceExtrudeOptions{.mode = mode, .amount = 2.15F});

    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    const auto topology = document.authored_topology().or_throw();
    const auto cap = std::ranges::find(topology.faces, source.id,
                                      &AuthoredTopologyFace::id);
    ASSERT_NE(cap, topology.faces.end());
    ASSERT_EQ(cap->vertices.size(), source.vertices.size());
    ASSERT_EQ(cap->uvs.size(), source.uvs.size());
    EXPECT_EQ(cap->material_slot, source.material_slot);
    EXPECT_EQ(cap->shading, source.shading);
    EXPECT_EQ(cap->texture_transform, source.texture_transform);
    for (std::size_t corner = 0; corner < source.uvs.size(); ++corner) {
      EXPECT_FLOAT_EQ(cap->uvs[corner].x, source.uvs[corner].x);
      EXPECT_FLOAT_EQ(cap->uvs[corner].y, source.uvs[corner].y);
    }
    const auto hard_cap = make_edge_key(cap->vertices[0], cap->vertices[1]);
    const auto soft_cap = make_edge_key(cap->vertices[2], cap->vertices[3]);
    EXPECT_NE(std::ranges::find(topology.hard_normal_edges, hard_cap),
              topology.hard_normal_edges.end());
    EXPECT_NE(std::ranges::find(topology.soft_normal_edges, soft_cap),
              topology.soft_normal_edges.end());
    for (std::size_t corner = 0; corner < source.vertices.size(); ++corner) {
      const auto connector = make_edge_key(source.vertices[corner], cap->vertices[corner]);
      EXPECT_EQ(std::ranges::find(topology.hard_normal_edges, connector),
                topology.hard_normal_edges.end());
      EXPECT_EQ(std::ranges::find(topology.soft_normal_edges, connector),
                topology.soft_normal_edges.end());
    }
    for (const auto& face : topology.faces) {
      EXPECT_EQ(face.material_slot, source.material_slot);
      EXPECT_EQ(face.texture_transform, source.texture_transform);
    }
    EXPECT_TRUE(document.validate().ok());
  }
}

TEST(ModelingSdkFaceExtrudeTests, ZeroAmountPreservesTopologySelectionAndAllocationState) {
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto document = make_extrude_quad();
    const auto before = document;
    const auto original = before.authored_topology().or_throw();

    const auto result = document.extrude_selected_faces(
        FaceExtrudeOptions{.mode = mode, .amount = 0.0F});

    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.value().success);
    EXPECT_FALSE(result.value().changed);
    EXPECT_FALSE(result.value().dirty.topology);
    EXPECT_FALSE(result.value().dirty.geometry);
    EXPECT_FALSE(result.value().dirty.selection);
    EXPECT_TRUE(result.value().created.empty());
    EXPECT_TRUE(result.value().deleted.empty());
    expect_extrude_unchanged(before, document);
    const auto after = document.authored_topology().or_throw();
    EXPECT_EQ(after.next_vertex_id, original.next_vertex_id);
    EXPECT_EQ(after.next_face_id, original.next_face_id);
  }
}

TEST(ModelingSdkFaceExtrudeTests, InvalidNumbersAndTransformsRejectWithoutPublication) {
  const float infinity = std::numeric_limits<float>::infinity();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  std::vector<FaceExtrudeOptions> invalid_options;
  for (const float amount : {infinity, -infinity, nan}) {
    invalid_options.push_back({.amount = amount});
  }
  FaceExtrudeOptions singular;
  singular.extrusion_from_local.z_axis = {};
  invalid_options.push_back(singular);
  FaceExtrudeOptions nonfinite_axis;
  nonfinite_axis.extrusion_from_local.x_axis.x = nan;
  invalid_options.push_back(nonfinite_axis);
  FaceExtrudeOptions nonfinite_origin;
  nonfinite_origin.extrusion_from_local.origin.y = infinity;
  invalid_options.push_back(nonfinite_origin);
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    for (auto options : invalid_options) {
      SCOPED_TRACE(static_cast<int>(mode));
      options.mode = mode;
      auto document = make_extrude_quad();
      const auto before = document;

      const auto result = document.extrude_selected_faces(options);

      ASSERT_FALSE(result.ok());
      EXPECT_FALSE(result.error_message().empty());
      expect_extrude_unchanged(before, document);
    }
  }
}

TEST(ModelingSdkFaceExtrudeTests, InvalidRegionDirectionRejectsWithoutPublication) {
  for (const auto direction : {Vec3{}, Vec3{0.0F, 0.0F,
                                           std::numeric_limits<float>::infinity()}}) {
    auto document = make_extrude_quad();
    const auto before = document;
    FaceExtrudeOptions options;
    options.region_direction = direction;

    const auto result = document.extrude_selected_faces(options);

    ASSERT_FALSE(result.ok());
    EXPECT_FALSE(result.error_message().empty());
    expect_extrude_unchanged(before, document);
  }
}

TEST(ModelingSdkFaceExtrudeTests, FiniteOffsetThatCollapsesDestinationCapsRejectsAtomically) {
  auto document = make_extrude_quad();
  const auto before = document;
  FaceExtrudeOptions options{.mode = FaceExtrudeMode::Region, .amount = 1.0e20F};
  options.region_direction = Vec3{1.0F, 1.0F, 1.0F};

  const auto result = document.extrude_selected_faces(options);

  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.error_message().empty());
  expect_extrude_unchanged(before, document);
}

TEST(ModelingSdkFaceExtrudeTests, UnusableSourceNormalsRejectAllModesAtomically) {
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto document = make_extrude_quad(true);
    const auto before = document;

    const auto result = document.extrude_selected_faces(
        FaceExtrudeOptions{.mode = mode, .amount = 1.0F});

    ASSERT_FALSE(result.ok());
    EXPECT_FALSE(result.error_message().empty());
    expect_extrude_unchanged(before, document);
  }
}

TEST(ModelingSdkFaceExtrudeTests, InvalidSecondSelectedFaceNeverPublishesTheFirstExtrusion) {
  auto topology = make_extrude_quad().authored_topology().or_throw();
  topology.vertices.insert(topology.vertices.end(), {
      {make_id<VertexTag>(5U), {10.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(6U), {11.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(7U), {12.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(8U), {13.0F, 0.0F, 0.0F}},
  });
  topology.faces.push_back(
      {.id = make_id<FaceTag>(12U),
       .vertices = {make_id<VertexTag>(5U), make_id<VertexTag>(6U),
                    make_id<VertexTag>(7U), make_id<VertexTag>(8U)}});
  topology.next_vertex_id = 9U;
  topology.next_face_id = 13U;
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto document = PolygonDocument::from_authored_topology(topology).or_throw();
    ASSERT_TRUE(document.apply_selection(
        {.kind = SelectionKind::Face,
         .faces = {make_id<FaceTag>(11U), make_id<FaceTag>(12U)},
         .has_active = true,
         .active_face = make_id<FaceTag>(11U)}).ok());
    const auto before = document;

    const auto result = document.extrude_selected_faces(
        FaceExtrudeOptions{.mode = mode, .amount = 1.0F});

    ASSERT_FALSE(result.ok());
    expect_extrude_unchanged(before, document);
  }
}

TEST(ModelingSdkFaceExtrudeTests, CrossedNormalExtrusionWallRejectsWithoutPublishingUsableCaps) {
  for (const float scale : {0.001F, 1.0F, 1000.0F}) {
    for (const float winding : {-1.0F, 1.0F}) {
      SCOPED_TRACE(scale);
      SCOPED_TRACE(winding);
      AuthoredTopology topology;
      const std::array positions{
          Vec3{0.0F, 0.0F, 0.0F}, Vec3{2.0F, 0.0F, 0.0F},
          Vec3{2.0F, 2.0F, 0.0F}, Vec3{0.0F, 2.0F, 0.0F},
          Vec3{2.0F, 0.0F, -2.0F}, Vec3{2.0F, 2.0F, -2.0F}};
      for (std::size_t index = 0; index < positions.size(); ++index) {
        const auto position = positions[index];
        topology.vertices.push_back({
            make_id<VertexTag>(static_cast<std::uint32_t>(index + 1U)),
            {position.x * scale, position.y * scale, position.z * scale}});
      }
      topology.faces = {
          {.id = make_id<FaceTag>(11U),
           .vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(2U),
                        make_id<VertexTag>(3U), make_id<VertexTag>(4U)}},
          {.id = make_id<FaceTag>(12U),
           .vertices = {make_id<VertexTag>(2U), make_id<VertexTag>(5U),
                        make_id<VertexTag>(6U), make_id<VertexTag>(3U)}}};
      if (winding < 0.0F) {
        for (auto& face : topology.faces) {
          std::ranges::reverse(face.vertices);
        }
      }
      topology.next_vertex_id = 7U;
      topology.next_face_id = 13U;
      auto document = PolygonDocument::from_authored_topology(topology).or_throw();
      ASSERT_TRUE(document.apply_selection({
          .kind = SelectionKind::Face,
          .faces = {make_id<FaceTag>(11U), make_id<FaceTag>(12U)},
          .has_active = true,
          .active_face = make_id<FaceTag>(11U)}).ok());
      const auto before = document;

      const auto result = document.extrude_selected_faces({
          .mode = FaceExtrudeMode::AlongNormals, .amount = -4.0F * scale * winding});

      // The top cap remains a rectangle, but its front wall's two connectors
      // cross at (0, 0, -2) in the unscaled source coordinates.
      ASSERT_FALSE(result.ok());
      EXPECT_NE(result.error_message().find("side wall boundary"), std::string::npos);
      expect_extrude_unchanged(before, document);
      const auto after = document.authored_topology().or_throw();
      EXPECT_EQ(after.next_vertex_id, topology.next_vertex_id);
      EXPECT_EQ(after.next_face_id, topology.next_face_id);
      EXPECT_EQ(document.selection().faces, before.selection().faces);
    }
  }
}

TEST(ModelingSdkFaceExtrudeTests, LegacyFloatExtrusionKeepsItsOpenBaseBehavior) {
  auto document = make_extrude_quad();

  const auto result = document.extrude_selected_faces(0.5F);

  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.value().changed);
  EXPECT_EQ(document.vertex_count(), 8U);
  EXPECT_EQ(document.face_count(), 5U);
  EXPECT_EQ(document.selection().active_face, make_id<FaceTag>(11U));
  EXPECT_TRUE(document.validate().ok());
}

TEST(ModelingSdkFaceExtrudeTests, FaceFrameReportsUniqueVertexCenterUnitNormalSumAndActiveNormal) {
  auto topology = make_extrude_quad().authored_topology().or_throw();
  topology.vertices.push_back({make_id<VertexTag>(5U), {-1.0F, -1.0F, -1.0F}});
  topology.vertices.push_back({make_id<VertexTag>(6U), {1.0F, -1.0F, -1.0F}});
  topology.faces.push_back(
      {.id = make_id<FaceTag>(12U),
       .vertices = {make_id<VertexTag>(2U), make_id<VertexTag>(1U),
                    make_id<VertexTag>(5U), make_id<VertexTag>(6U)}});
  topology.next_vertex_id = 7U;
  topology.next_face_id = 13U;
  auto document = PolygonDocument::from_authored_topology(topology).or_throw();
  ASSERT_TRUE(document.apply_selection(
      {.kind = SelectionKind::Face,
       .faces = {make_id<FaceTag>(11U), make_id<FaceTag>(12U)},
       .has_active = true,
       .active_face = make_id<FaceTag>(12U)}).ok());
  const auto before = document;

  const auto local = document.selected_face_extrude_frame();

  ASSERT_TRUE(local.ok()) << local.error_message();
  EXPECT_EQ(local.value().face_count, 2U);
  EXPECT_EQ(local.value().vertex_count, 6U);
  EXPECT_FLOAT_EQ(local.value().center.x, 0.0F);
  EXPECT_FLOAT_EQ(local.value().center.y, -1.0F / 3.0F);
  EXPECT_FLOAT_EQ(local.value().center.z, -1.0F / 3.0F);
  EXPECT_FLOAT_EQ(local.value().summed_normal.x, 0.0F);
  EXPECT_FLOAT_EQ(local.value().summed_normal.y, -1.0F);
  EXPECT_FLOAT_EQ(local.value().summed_normal.z, 1.0F);
  EXPECT_FLOAT_EQ(local.value().active_normal.x, 0.0F);
  EXPECT_FLOAT_EQ(local.value().active_normal.y, -1.0F);
  EXPECT_FLOAT_EQ(local.value().active_normal.z, 0.0F);

  for (const float handedness : {-1.0F, 1.0F}) {
    SCOPED_TRACE(handedness);
    const Transform3 transform{
        .x_axis = {0.0F, 2.0F * handedness, 0.0F},
        .y_axis = {-3.0F, 0.0F, 0.0F},
        .z_axis = {0.0F, 0.0F, 4.0F},
        .origin = {7.0F, -13.0F, 21.0F}};

    const auto frame = document.selected_face_extrude_frame(transform);

    ASSERT_TRUE(frame.ok()) << frame.error_message();
    EXPECT_EQ(frame.value().face_count, 2U);
    EXPECT_EQ(frame.value().vertex_count, 6U);
    EXPECT_FLOAT_EQ(frame.value().center.x, 8.0F);
    EXPECT_FLOAT_EQ(frame.value().center.y, -13.0F);
    EXPECT_FLOAT_EQ(frame.value().center.z, 21.0F - 4.0F / 3.0F);
    EXPECT_FLOAT_EQ(frame.value().summed_normal.x, handedness);
    EXPECT_FLOAT_EQ(frame.value().summed_normal.y, 0.0F);
    EXPECT_FLOAT_EQ(frame.value().summed_normal.z, handedness);
    EXPECT_FLOAT_EQ(frame.value().active_normal.x, handedness);
    EXPECT_FLOAT_EQ(frame.value().active_normal.y, 0.0F);
    EXPECT_FLOAT_EQ(frame.value().active_normal.z, 0.0F);
    expect_extrude_unchanged(before, document);
  }

  Transform3 shear{
      .x_axis = {2.0F, 0.0F, 0.0F},
      .y_axis = {1.0F, 3.0F, 0.0F},
      .z_axis = {0.5F, 0.25F, 4.0F}};
  const auto sheared = document.selected_face_extrude_frame(shear);
  ASSERT_TRUE(sheared.ok()) << sheared.error_message();
  EXPECT_EQ(sheared.value().vertex_count, 6U);
  EXPECT_FLOAT_EQ(sheared.value().center.x, -0.5F);
  EXPECT_FLOAT_EQ(sheared.value().center.y, -13.0F / 12.0F);
  EXPECT_FLOAT_EQ(sheared.value().center.z, -4.0F / 3.0F);
  const float normal_scale = 1.0F / std::sqrt(257.0F);
  EXPECT_NEAR(sheared.value().active_normal.x, 0.0F, 0.00001F);
  EXPECT_NEAR(sheared.value().active_normal.y, -16.0F * normal_scale, 0.00001F);
  EXPECT_NEAR(sheared.value().active_normal.z, normal_scale, 0.00001F);
  EXPECT_NEAR(sheared.value().summed_normal.z, 1.0F + normal_scale, 0.00001F);

  shear.origin = {100000000.0F, -200000000.0F, 300000000.0F};
  const auto translated = document.selected_face_extrude_frame(shear);
  ASSERT_TRUE(translated.ok()) << translated.error_message();
  EXPECT_EQ(translated.value().vertex_count, 6U);
  EXPECT_FLOAT_EQ(translated.value().center.x, shear.origin.x + sheared.value().center.x);
  EXPECT_FLOAT_EQ(translated.value().center.y, shear.origin.y + sheared.value().center.y);
  EXPECT_FLOAT_EQ(translated.value().center.z, shear.origin.z + sheared.value().center.z);
  EXPECT_FLOAT_EQ(translated.value().active_normal.x, sheared.value().active_normal.x);
  EXPECT_FLOAT_EQ(translated.value().active_normal.y, sheared.value().active_normal.y);
  EXPECT_FLOAT_EQ(translated.value().active_normal.z, sheared.value().active_normal.z);
  EXPECT_FLOAT_EQ(translated.value().summed_normal.x, sheared.value().summed_normal.x);
  EXPECT_FLOAT_EQ(translated.value().summed_normal.y, sheared.value().summed_normal.y);
  EXPECT_FLOAT_EQ(translated.value().summed_normal.z, sheared.value().summed_normal.z);
  expect_extrude_unchanged(before, document);
}

TEST(ModelingSdkFaceExtrudeTests, FaceFrameRejectsInvalidSelectionAndGeometryWithoutPublication) {
  auto document = make_extrude_quad();
  const auto before = document;
  Transform3 singular;
  singular.z_axis = {};

  const auto singular_frame = document.selected_face_extrude_frame(singular);

  EXPECT_FALSE(singular_frame.ok());
  expect_extrude_unchanged(before, document);
  ASSERT_TRUE(document.clear_selection().ok());
  const auto deselected = document;
  EXPECT_FALSE(document.selected_face_extrude_frame().ok());
  expect_extrude_unchanged(deselected, document);
  auto collapsed = make_extrude_quad(true);
  const auto collapsed_before = collapsed;
  EXPECT_FALSE(collapsed.selected_face_extrude_frame().ok());
  expect_extrude_unchanged(collapsed_before, collapsed);
}

TEST(ModelingSdkFaceExtrudeTests, SelectionOperationsAndCheckedFacadesForwardTypedOptionsAndHistory) {
  for (const int entrypoint : {0, 1, 2}) {
    for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                            FaceExtrudeMode::Individual}) {
      SCOPED_TRACE(entrypoint);
      SCOPED_TRACE(static_cast<int>(mode));
      auto api = ModelingApi::create();
      auto mesh = api.add_document(make_extrude_quad(), "Extrude facade");
      ASSERT_TRUE(mesh.valid());
      ASSERT_TRUE(mesh.faces().all().replace().success);
      FaceExtrudeOptions options{.mode = mode, .amount = 2.15F};
      options.extrusion_from_local.z_axis = {0.0F, 0.0F, 4.0F};
      OperationReceipt receipt;

      if (entrypoint == 0) {
        receipt = mesh.faces().selected().extrude(options);
      } else if (entrypoint == 1) {
        receipt = api.operations().extrude(options);
      } else {
        const auto result = api.checked().operations().extrude(options);
        ASSERT_TRUE(result.ok()) << result.error_message();
        receipt = result.value();
      }

      ASSERT_TRUE(receipt.success) << receipt.error.message;
      ASSERT_TRUE(receipt.changed);
      const auto payload = mesh.payloads().authored_polygon();
      EXPECT_EQ(payload.vertices.size(), 8U);
      EXPECT_EQ(payload.faces.size(), 5U);
      const auto cap = std::ranges::find(payload.faces, make_id<FaceTag>(11U),
                                        &AuthoredPolygonFacePayload::id);
      ASSERT_NE(cap, payload.faces.end());
      for (const auto vertex_id : cap->vertices) {
        const auto vertex = std::ranges::find(payload.vertices, vertex_id);
        ASSERT_NE(vertex, payload.vertices.end());
        const auto index = static_cast<std::size_t>(vertex - payload.vertices.begin());
        EXPECT_NEAR(payload.positions[index].z, 2.15F / 4.0F, 0.00001F);
      }
      ASSERT_EQ(mesh.faces().selected().faces().size(), 1U);
      EXPECT_EQ(mesh.faces().selected().faces().front(), make_id<FaceTag>(11U));
      ASSERT_TRUE(api.undo().success);
      EXPECT_EQ(mesh.payloads().authored_polygon().faces.size(), 1U);
      EXPECT_EQ(mesh.payloads().authored_polygon().vertices.size(), 4U);
      ASSERT_TRUE(api.redo().success);
      EXPECT_EQ(mesh.payloads().authored_polygon().faces.size(), payload.faces.size());
      EXPECT_EQ(mesh.payloads().authored_polygon().vertices.size(), payload.vertices.size());
    }
  }
}

TEST(ModelingSdkFaceExtrudeTests, SmallSignedAmountsKeepEveryWallAtSmallAndDistantCoordinates) {
  struct ScaleCase {
    float scale;
    float origin;
  };
  for (const ScaleCase coordinates : {ScaleCase{1.0F, 0.0F},
                                      ScaleCase{0.0001F, 0.0F},
                                      ScaleCase{1.0F, 10000.0F}}) {
    for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                            FaceExtrudeMode::Individual}) {
      for (const float amount : {-0.000001F, 0.0F, 0.000001F}) {
        SCOPED_TRACE(coordinates.scale);
        SCOPED_TRACE(coordinates.origin);
        SCOPED_TRACE(static_cast<int>(mode));
        SCOPED_TRACE(amount);
        auto topology = make_extrude_quad().authored_topology().or_throw();
        for (auto& vertex : topology.vertices) {
          vertex.position.x = vertex.position.x * coordinates.scale + coordinates.origin;
          vertex.position.y = vertex.position.y * coordinates.scale + coordinates.origin;
        }
        auto document = PolygonDocument::from_authored_topology(topology).or_throw();
        ASSERT_TRUE(document.apply_selection(
            {.kind = SelectionKind::Face,
             .faces = {make_id<FaceTag>(11U)},
             .has_active = true,
             .active_face = make_id<FaceTag>(11U)}).ok());
        const auto before = document;

        const auto result = document.extrude_selected_faces(
            FaceExtrudeOptions{.mode = mode, .amount = amount});

        ASSERT_TRUE(result.ok()) << result.error_message();
        EXPECT_TRUE(result.value().success);
        EXPECT_EQ(result.value().changed, amount != 0.0F);
        if (amount == 0.0F) {
          expect_extrude_unchanged(before, document);
          continue;
        }
        EXPECT_EQ(document.vertex_count(), 8U);
        EXPECT_EQ(document.face_count(), 5U);
        EXPECT_EQ(result.value().created.vertices.size(), 4U);
        EXPECT_EQ(result.value().created.faces.size(), 4U);
        EXPECT_EQ(document.selection().faces, (std::vector<FaceId>{make_id<FaceTag>(11U)}));
        EXPECT_EQ(document.selection().active_face, make_id<FaceTag>(11U));
        const auto after = document.authored_topology().or_throw();
        const auto cap = std::ranges::find(after.faces, make_id<FaceTag>(11U),
                                          &AuthoredTopologyFace::id);
        ASSERT_NE(cap, after.faces.end());
        ASSERT_EQ(cap->vertices.size(), 4U);
        for (std::size_t corner = 0; corner < cap->vertices.size(); ++corner) {
          const auto vertex = std::ranges::find(after.vertices, cap->vertices[corner],
                                               &AuthoredTopologyVertex::id);
          ASSERT_NE(vertex, after.vertices.end());
          EXPECT_FLOAT_EQ(vertex->position.x, topology.vertices[corner].position.x);
          EXPECT_FLOAT_EQ(vertex->position.y, topology.vertices[corner].position.y);
          EXPECT_FLOAT_EQ(vertex->position.z, amount);
        }
        // Import validates authored connectivity without rejecting thin side walls.
        EXPECT_TRUE(PolygonDocument::from_authored_topology(after).ok());
      }
    }
  }
}

TEST(ModelingSdkFaceExtrudeTests, DistantUnitQuadExtrudesWithoutGlobalAreaCancellation) {
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto topology = make_extrude_quad().authored_topology().or_throw();
    for (auto& vertex : topology.vertices) {
      vertex.position.x = vertex.position.x * 0.5F + 10000.0F;
      vertex.position.y = vertex.position.y * 0.5F + 10000.0F;
    }
    auto document = PolygonDocument::from_authored_topology(topology).or_throw();
    ASSERT_TRUE(document.apply_selection(
        {.kind = SelectionKind::Face,
         .faces = {make_id<FaceTag>(11U)},
         .has_active = true,
         .active_face = make_id<FaceTag>(11U)}).ok());
    const auto frame = document.selected_face_extrude_frame();
    ASSERT_TRUE(frame.ok()) << frame.error_message();
    EXPECT_FLOAT_EQ(frame.value().center.x, 10000.0F);
    EXPECT_FLOAT_EQ(frame.value().center.y, 10000.0F);
    EXPECT_FLOAT_EQ(frame.value().center.z, 0.0F);
    EXPECT_FLOAT_EQ(frame.value().active_normal.z, 1.0F);

    const auto result = document.extrude_selected_faces(
        FaceExtrudeOptions{.mode = mode, .amount = 0.5F});

    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    const auto after = document.authored_topology().or_throw();
    EXPECT_EQ(after.faces.size(), 5U);
    const auto cap = std::ranges::find(after.faces, make_id<FaceTag>(11U),
                                      &AuthoredTopologyFace::id);
    ASSERT_NE(cap, after.faces.end());
    for (const auto vertex_id : cap->vertices) {
      const auto vertex = std::ranges::find(after.vertices, vertex_id,
                                           &AuthoredTopologyVertex::id);
      ASSERT_NE(vertex, after.vertices.end());
      EXPECT_FLOAT_EQ(vertex->position.z, 0.5F);
    }
    EXPECT_EQ(document.selection().active_face, make_id<FaceTag>(11U));
    EXPECT_TRUE(PolygonDocument::from_authored_topology(after).ok());
  }
}

TEST(ModelingSdkFaceExtrudeTests, UnselectedCollapsedFaceAndItsAttributesSurviveAllModes) {
  auto topology = make_extrude_quad().authored_topology().or_throw();
  topology.vertices.insert(topology.vertices.end(), {
      {make_id<VertexTag>(5U), {10.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(6U), {11.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(7U), {12.0F, 0.0F, 0.0F}},
      {make_id<VertexTag>(8U), {13.0F, 0.0F, 0.0F}},
  });
  const AuthoredTopologyFace collapsed{
      .id = make_id<FaceTag>(12U),
      .vertices = {make_id<VertexTag>(5U), make_id<VertexTag>(6U),
                   make_id<VertexTag>(7U), make_id<VertexTag>(8U)},
      .uvs = {{0.0F, 0.0F}, {2.0F, 0.0F}, {3.0F, 1.0F}, {-1.0F, 2.0F}},
      .material_slot = 13U,
      .shading = AuthoredSurfaceShading::Flat,
      .texture_transform = {.scale = {-3.0F, 2.0F},
                            .shift_uv = {0.75F, -0.125F},
                            .rotation_degrees = 90.0F}};
  topology.faces.push_back(collapsed);
  const auto hard = make_edge_key(make_id<VertexTag>(5U), make_id<VertexTag>(6U));
  const auto soft = make_edge_key(make_id<VertexTag>(7U), make_id<VertexTag>(8U));
  topology.hard_normal_edges.push_back(hard);
  topology.soft_normal_edges.push_back(soft);
  topology.next_vertex_id = 9U;
  topology.next_face_id = 13U;
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto document = PolygonDocument::from_authored_topology(topology).or_throw();
    ASSERT_TRUE(document.apply_selection(
        {.kind = SelectionKind::Face,
         .faces = {make_id<FaceTag>(11U)},
         .has_active = true,
         .active_face = make_id<FaceTag>(11U)}).ok());

    const auto result = document.extrude_selected_faces(
        FaceExtrudeOptions{.mode = mode, .amount = 0.5F});

    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    const auto after = document.authored_topology().or_throw();
    const auto retained = std::ranges::find(after.faces, collapsed.id,
                                           &AuthoredTopologyFace::id);
    ASSERT_NE(retained, after.faces.end());
    EXPECT_EQ(retained->vertices, collapsed.vertices);
    EXPECT_EQ(retained->material_slot, collapsed.material_slot);
    EXPECT_EQ(retained->shading, collapsed.shading);
    EXPECT_EQ(retained->texture_transform, collapsed.texture_transform);
    ASSERT_EQ(retained->uvs.size(), collapsed.uvs.size());
    for (std::size_t corner = 0; corner < collapsed.uvs.size(); ++corner) {
      EXPECT_FLOAT_EQ(retained->uvs[corner].x, collapsed.uvs[corner].x);
      EXPECT_FLOAT_EQ(retained->uvs[corner].y, collapsed.uvs[corner].y);
      const auto vertex = std::ranges::find(after.vertices, collapsed.vertices[corner],
                                           &AuthoredTopologyVertex::id);
      ASSERT_NE(vertex, after.vertices.end());
      EXPECT_FLOAT_EQ(vertex->position.x, topology.vertices[corner + 4U].position.x);
      EXPECT_FLOAT_EQ(vertex->position.y, 0.0F);
      EXPECT_FLOAT_EQ(vertex->position.z, 0.0F);
    }
    EXPECT_NE(std::ranges::find(after.hard_normal_edges, hard), after.hard_normal_edges.end());
    EXPECT_NE(std::ranges::find(after.soft_normal_edges, soft), after.soft_normal_edges.end());
    EXPECT_EQ(document.selection().faces, (std::vector<FaceId>{make_id<FaceTag>(11U)}));
    EXPECT_EQ(document.face_count(), 6U);
    EXPECT_TRUE(PolygonDocument::from_authored_topology(after).ok());
    EXPECT_FALSE(document.validate().ok());
  }
}

TEST(ModelingSdkFaceExtrudeTests, OpenInsetPanelExtrusionReplacesSourceAndOnlyRemovesOrphanedSourceVertices) {
  for (const bool include_loose_vertex : {false, true}) {
    SCOPED_TRACE(include_loose_vertex);
    const auto source = make_extrude_inset_panel(include_loose_vertex);
    const auto source_topology = source.authored_topology().or_throw();
    const auto prepared = source.prepare_face_extrusion(FaceExtrudeMode::Region).or_throw();
    auto document = source;

    const auto result = document.extrude_selected_faces({
        .mode = FaceExtrudeMode::Region, .amount = 11.0F,
        .region_direction = Vec3{0.0F, 0.0F, 1.0F}});

    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.value().changed);
    EXPECT_EQ(document.face_count(), 9U);
    EXPECT_EQ(document.vertex_count(), include_loose_vertex ? 13U : 12U);
    EXPECT_EQ(document.selection().faces, source.selection().faces);
    EXPECT_EQ(document.selection().active_face, source.selection().active_face);
    EXPECT_EQ(result.value().created.vertices.size(), 8U);
    EXPECT_EQ(result.value().created.faces.size(), 4U);
    EXPECT_TRUE(result.value().deleted.faces.empty());
    EXPECT_EQ(result.value().deleted.vertices,
              (std::vector<VertexId>{make_id<VertexTag>(5U), make_id<VertexTag>(6U),
                                     make_id<VertexTag>(7U), make_id<VertexTag>(8U)}));
    const auto topology = document.authored_topology().or_throw();
    for (const auto& face : topology.faces) {
      // Every surviving face uses at least one new vertex: no source panel remains.
      EXPECT_TRUE(std::ranges::any_of(face.vertices, [&](const auto vertex_id) {
        return std::ranges::find(result.value().created.vertices, vertex_id) !=
               result.value().created.vertices.end();
      }));
    }
    for (const auto& source_face : source_topology.faces) {
      const auto cap = std::ranges::find(topology.faces, source_face.id,
                                         &AuthoredTopologyFace::id);
      ASSERT_NE(cap, topology.faces.end());
      ASSERT_EQ(cap->vertices.size(), source_face.vertices.size());
      for (std::size_t index = 0; index < cap->vertices.size(); ++index) {
        const auto source_vertex = std::ranges::find(
            source_topology.vertices, source_face.vertices[index], &AuthoredTopologyVertex::id);
        const auto cap_vertex = std::ranges::find(
            topology.vertices, cap->vertices[index], &AuthoredTopologyVertex::id);
        ASSERT_NE(source_vertex, source_topology.vertices.end());
        ASSERT_NE(cap_vertex, topology.vertices.end());
        EXPECT_FLOAT_EQ(cap_vertex->position.x, source_vertex->position.x);
        EXPECT_FLOAT_EQ(cap_vertex->position.y, source_vertex->position.y);
        EXPECT_FLOAT_EQ(cap_vertex->position.z, source_vertex->position.z + 11.0F);
      }
    }
    if (include_loose_vertex) {
      const auto loose = std::ranges::find(topology.vertices, make_id<VertexTag>(9U),
                                           &AuthoredTopologyVertex::id);
      ASSERT_NE(loose, topology.vertices.end());
      EXPECT_FLOAT_EQ(loose->position.x, 20.0F);
      EXPECT_FLOAT_EQ(loose->position.y, 30.0F);
      EXPECT_FLOAT_EQ(loose->position.z, 40.0F);
    }
    const auto evaluated = prepared.evaluate(11.0F, Vec3{0.0F, 0.0F, 1.0F});
    ASSERT_TRUE(evaluated.ok()) << evaluated.error_message();
    expect_extrude_equivalent(document, evaluated.value().document);
    expect_extrude_receipt(result.value(), evaluated.value().receipt);
  }
}

TEST(ModelingSdkFaceExtrudeTests, PreparedInsetPanelSamplesKeepSourceRelativeReceiptsAndRestoreZero) {
  const auto source = make_extrude_inset_panel();
  const auto before = source;
  const auto prepared = source.prepare_face_extrusion(FaceExtrudeMode::Region).or_throw();
  const Vec3 direction{0.0F, 0.0F, 1.0F};
  const auto first = prepared.evaluate(11.0F, direction).or_throw();
  for (const float amount : {5.5F, -11.0F, 0.0F, 11.0F}) {
    SCOPED_TRACE(amount);
    auto expected = source;
    const auto fresh = expected.extrude_selected_faces({
        .mode = FaceExtrudeMode::Region, .amount = amount,
        .region_direction = direction}).or_throw();
    const auto sample = prepared.evaluate(amount, direction).or_throw();
    expect_extrude_equivalent(expected, sample.document);
    expect_extrude_receipt(fresh, sample.receipt);
    if (amount == 0.0F) {
      expect_extrude_unchanged(before, sample.document);
      EXPECT_FALSE(sample.receipt.changed);
      EXPECT_TRUE(sample.receipt.created.empty());
      EXPECT_TRUE(sample.receipt.deleted.empty());
    } else {
      EXPECT_EQ(sample.document.vertex_ids(), first.document.vertex_ids());
      EXPECT_EQ(sample.document.face_ids(), first.document.face_ids());
      EXPECT_EQ(sample.receipt.created.vertices, first.receipt.created.vertices);
      EXPECT_EQ(sample.receipt.created.faces, first.receipt.created.faces);
      EXPECT_EQ(sample.receipt.deleted.vertices, first.receipt.deleted.vertices);
    }
    expect_extrude_unchanged(before, source);
  }
}

TEST(ModelingSdkFaceExtrudeTests, InsetPanelExtrusionUndoRedoRestoresSourceAndGeneratedTopology) {
  auto api = ModelingApi::create();
  auto mesh = api.add_document(make_extrude_inset_panel(), "Inset panel");
  ASSERT_TRUE(mesh.valid());
  ASSERT_TRUE(mesh.faces().all().replace().success);
  const auto source = mesh.payloads().authored_polygon();
  const auto selected_region = mesh.faces().selected();
  const std::vector<FaceId> selected(selected_region.faces().begin(), selected_region.faces().end());

  const auto result = api.operations().extrude({
      .mode = FaceExtrudeMode::Region, .amount = 11.0F,
      .region_direction = Vec3{0.0F, 0.0F, 1.0F}});

  ASSERT_TRUE(result.success) << result.error.message;
  ASSERT_TRUE(result.changed);
  const auto extruded = mesh.payloads().authored_polygon();
  EXPECT_EQ(extruded.faces.size(), 9U);
  EXPECT_EQ(extruded.vertices.size(), 12U);
  EXPECT_TRUE(std::ranges::equal(mesh.faces().selected().faces(), selected));
  const auto expect_payload = [](const AuthoredPolygonPayload& expected,
                                 const AuthoredPolygonPayload& actual) {
    EXPECT_EQ(actual.vertices, expected.vertices);
    ASSERT_EQ(actual.positions.size(), expected.positions.size());
    for (std::size_t index = 0; index < expected.positions.size(); ++index) {
      EXPECT_FLOAT_EQ(actual.positions[index].x, expected.positions[index].x);
      EXPECT_FLOAT_EQ(actual.positions[index].y, expected.positions[index].y);
      EXPECT_FLOAT_EQ(actual.positions[index].z, expected.positions[index].z);
    }
    ASSERT_EQ(actual.faces.size(), expected.faces.size());
    for (std::size_t index = 0; index < expected.faces.size(); ++index) {
      EXPECT_EQ(actual.faces[index].id, expected.faces[index].id);
      EXPECT_EQ(actual.faces[index].vertices, expected.faces[index].vertices);
    }
  };
  ASSERT_TRUE(api.undo().success);
  expect_payload(source, mesh.payloads().authored_polygon());
  EXPECT_TRUE(std::ranges::equal(mesh.faces().selected().faces(), selected));
  ASSERT_TRUE(api.redo().success);
  expect_payload(extruded, mesh.payloads().authored_polygon());
  EXPECT_TRUE(std::ranges::equal(mesh.faces().selected().faces(), selected));
}

TEST(ModelingSdkFaceExtrudeTests, PreparedEvaluationsMatchFreshOperationsAcrossModesAndAffineFrames) {
  const std::array frames{
      Transform3{},
      Transform3{.x_axis = {-2.0F, 0.25F, 0.0F},
                 .y_axis = {0.5F, 3.0F, 0.125F},
                 .z_axis = {0.25F, -0.5F, 0.75F},
                 .origin = {17.0F, -9.0F, 4.0F}}};
  for (const auto mode : {FaceExtrudeMode::Region, FaceExtrudeMode::AlongNormals,
                          FaceExtrudeMode::Individual}) {
    for (std::size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
      SCOPED_TRACE(static_cast<int>(mode));
      SCOPED_TRACE(frame_index);
      const auto source = make_extrude_corner();
      const auto prepared = source.prepare_face_extrusion(mode, frames[frame_index]);
      ASSERT_TRUE(prepared.ok()) << prepared.error_message();
      for (const float amount : {0.000001F, 0.2F, -0.3F, 0.0F, 0.15F}) {
        const std::array<std::optional<Vec3>, 2> directions{
            std::nullopt, Vec3{0.25F, 0.5F, 2.0F}};
        for (const auto& direction : directions) {
          if (mode != FaceExtrudeMode::Region && direction.has_value()) {
            continue;
          }
          SCOPED_TRACE(amount);
          SCOPED_TRACE(direction.has_value());
          auto expected = source;
          const auto fresh = expected.extrude_selected_faces({
              .mode = mode, .amount = amount, .region_direction = direction,
              .extrusion_from_local = frames[frame_index]});
          ASSERT_TRUE(fresh.ok()) << fresh.error_message();
          const auto evaluated = prepared.value().evaluate(amount, direction);
          ASSERT_TRUE(evaluated.ok()) << evaluated.error_message();
          expect_extrude_equivalent(expected, evaluated.value().document);
          expect_extrude_receipt(fresh.value(), evaluated.value().receipt);
          if (amount == 0.0F) {
            expect_extrude_unchanged(source, evaluated.value().document);
          }
        }
      }
    }
  }
}

TEST(ModelingSdkFaceExtrudeTests, PreparedSamplesReuseIdsUntilSourceHighWaterRetiresThem) {
  auto source = make_extrude_quad();
  const auto before = source;
  const auto prepared = source.prepare_face_extrusion(FaceExtrudeMode::Region).or_throw();
  const auto first = prepared.evaluate(2.15F).or_throw();
  const auto first_snapshot = first.document;
  const auto first_topology = first.document.authored_topology().or_throw();
  for (const float amount : {0.5F, -0.75F, 0.0F, 1.25F}) {
    const auto sample = prepared.evaluate(amount).or_throw();
    if (amount == 0.0F) {
      expect_extrude_unchanged(before, sample.document);
      EXPECT_FALSE(sample.receipt.changed);
      EXPECT_TRUE(sample.receipt.created.empty());
      continue;
    }
    EXPECT_EQ(sample.document.vertex_ids(), first.document.vertex_ids());
    EXPECT_EQ(sample.document.face_ids(), first.document.face_ids());
    EXPECT_EQ(sample.receipt.created.vertices, first.receipt.created.vertices);
    EXPECT_EQ(sample.receipt.created.faces, first.receipt.created.faces);
    EXPECT_TRUE(sample.receipt.dirty.topology);
    EXPECT_EQ(sample.document.authored_topology().or_throw().next_vertex_id,
              first_topology.next_vertex_id);
    EXPECT_EQ(sample.document.authored_topology().or_throw().next_face_id,
              first_topology.next_face_id);
  }
  expect_extrude_unchanged(first_snapshot, first.document);
  expect_extrude_unchanged(before, source);

  // A host retiring preview topology reserves its IDs before preparing again.
  source.preserve_identity_high_water(first.document);
  const auto replacement = source.prepare_face_extrusion(FaceExtrudeMode::Region)
                               .or_throw().evaluate(0.5F).or_throw();
  ASSERT_EQ(replacement.receipt.created.vertices.size(), first.receipt.created.vertices.size());
  ASSERT_EQ(replacement.receipt.created.faces.size(), first.receipt.created.faces.size());
  for (const auto id : replacement.receipt.created.vertices) {
    EXPECT_EQ(std::ranges::find(first.receipt.created.vertices, id),
              first.receipt.created.vertices.end());
  }
  for (const auto id : replacement.receipt.created.faces) {
    EXPECT_EQ(std::ranges::find(first.receipt.created.faces, id),
              first.receipt.created.faces.end());
  }
  expect_extrude_equivalent(first_snapshot, prepared.evaluate(2.15F).or_throw().document);
}

TEST(ModelingSdkFaceExtrudeTests, PreparedFailuresPreserveSourceAndEarlierEvaluations) {
  const auto source = make_extrude_corner();
  const auto before = source;
  const auto prepared = source.prepare_face_extrusion(FaceExtrudeMode::AlongNormals).or_throw();
  const auto accepted = prepared.evaluate(-0.25F).or_throw();
  const auto accepted_snapshot = accepted.document;
  for (const float amount : {-4.0F, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
    const auto rejected = prepared.evaluate(amount);
    EXPECT_FALSE(rejected.ok());
    expect_extrude_unchanged(before, source);
    expect_extrude_unchanged(accepted_snapshot, accepted.document);
  }
  auto expected = source;
  const auto fresh = expected.extrude_selected_faces({
      .mode = FaceExtrudeMode::AlongNormals, .amount = 0.5F}).or_throw();
  const auto recovered = prepared.evaluate(0.5F).or_throw();
  expect_extrude_equivalent(expected, recovered.document);
  expect_extrude_receipt(fresh, recovered.receipt);
  EXPECT_EQ(recovered.document.vertex_ids(), accepted.document.vertex_ids());
  EXPECT_EQ(recovered.document.face_ids(), accepted.document.face_ids());

  const auto region = source.prepare_face_extrusion(FaceExtrudeMode::Region).or_throw();
  EXPECT_FALSE(region.evaluate(0.5F, Vec3{}).ok());
  EXPECT_FALSE(region.evaluate(0.5F, Vec3{0.0F, 0.0F,
      std::numeric_limits<float>::infinity()}).ok());
  EXPECT_TRUE(region.evaluate(0.5F).ok());
}

TEST(ModelingSdkFaceExtrudeTests, PreparationAllowsSmallValidSampleWhenUnitSampleWouldCrossWall) {
  const auto source = make_extrude_corner(0.1F, true);
  auto rejected_unit = source;
  ASSERT_FALSE(rejected_unit.extrude_selected_faces({
      .mode = FaceExtrudeMode::AlongNormals, .amount = 1.0F}).ok());
  const auto prepared = source.prepare_face_extrusion(FaceExtrudeMode::AlongNormals);
  ASSERT_TRUE(prepared.ok()) << prepared.error_message();
  auto expected = source;
  const auto fresh = expected.extrude_selected_faces({
      .mode = FaceExtrudeMode::AlongNormals, .amount = 0.001F}).or_throw();
  const auto small = prepared.value().evaluate(0.001F);
  ASSERT_TRUE(small.ok()) << small.error_message();
  expect_extrude_equivalent(expected, small.value().document);
  expect_extrude_receipt(fresh, small.value().receipt);
  EXPECT_FALSE(prepared.value().evaluate(1.0F).ok());
  expect_extrude_unchanged(source, prepared.value().evaluate(0.0F).or_throw().document);
}

TEST(ModelingSdkFaceExtrudeTests, PreparedZeroPreservesSourceWhenOnlyNonzeroExtrusionIsInvalid) {
  for (const bool canceled_normals : {true, false}) {
    SCOPED_TRACE(canceled_normals);
    auto topology = make_extrude_quad().authored_topology().or_throw();
    auto second = topology.faces.front();
    second.id = make_id<FaceTag>(12U);
    if (canceled_normals) {
      // Opposing faces are valid source topology but cancel every vertex normal.
      std::ranges::reverse(second.vertices);
      std::ranges::reverse(second.uvs);
    } else {
      // A vertex-only connection gives the extrusion connector four incident walls.
      topology.vertices.push_back({make_id<VertexTag>(5U), {-1.0F, -1.0F, -2.0F}});
      topology.vertices.push_back({make_id<VertexTag>(6U), {-3.0F, -1.0F, 0.0F}});
      second.vertices = {make_id<VertexTag>(1U), make_id<VertexTag>(5U),
                         make_id<VertexTag>(6U)};
      second.uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
      topology.next_vertex_id = 7U;
    }
    topology.faces.push_back(second);
    topology.next_face_id = 13U;
    auto source = PolygonDocument::from_authored_topology(topology).or_throw();
    static_cast<void>(source.apply_selection({
        .kind = SelectionKind::Face,
        .faces = {make_id<FaceTag>(11U), make_id<FaceTag>(12U)},
        .has_active = true,
        .active_face = make_id<FaceTag>(11U)}).or_throw());
    const auto before = source;
    const auto fresh_zero = source.extrude_selected_faces({
        .mode = FaceExtrudeMode::AlongNormals, .amount = 0.0F});
    ASSERT_TRUE(fresh_zero.ok()) << fresh_zero.error_message();
    expect_extrude_unchanged(before, source);
    const auto prepared = source.prepare_face_extrusion(FaceExtrudeMode::AlongNormals);
    ASSERT_TRUE(prepared.ok()) << prepared.error_message();
    for (const float amount : {0.0F, 0.5F, -0.5F, 0.0F}) {
      SCOPED_TRACE(amount);
      const auto evaluated = prepared.value().evaluate(amount);
      if (amount == 0.0F) {
        ASSERT_TRUE(evaluated.ok()) << evaluated.error_message();
        expect_extrude_unchanged(before, evaluated.value().document);
        expect_extrude_receipt(fresh_zero.value(), evaluated.value().receipt);
      } else {
        auto fresh = source;
        EXPECT_FALSE(fresh.extrude_selected_faces({
            .mode = FaceExtrudeMode::AlongNormals, .amount = amount}).ok());
        EXPECT_FALSE(evaluated.ok());
        expect_extrude_unchanged(before, fresh);
      }
      expect_extrude_unchanged(before, source);
    }
  }
}

TEST(ModelingSdkFaceExtrudeTests, PreparationCapturesSourceAndRejectsInvalidInputs) {
  auto source = make_extrude_quad();
  const auto before = source;
  const auto prepared = source.prepare_face_extrusion(FaceExtrudeMode::Individual).or_throw();
  static_cast<void>(source.extrude_selected_faces({
      .mode = FaceExtrudeMode::Region, .amount = 3.0F}).or_throw());
  auto expected = before;
  const auto fresh = expected.extrude_selected_faces({
      .mode = FaceExtrudeMode::Individual, .amount = -0.5F}).or_throw();
  const auto evaluated = prepared.evaluate(-0.5F).or_throw();
  expect_extrude_equivalent(expected, evaluated.document);
  expect_extrude_receipt(fresh, evaluated.receipt);

  Transform3 singular;
  singular.z_axis = {};
  EXPECT_FALSE(before.prepare_face_extrusion(FaceExtrudeMode::Region, singular).ok());
  EXPECT_FALSE(make_extrude_quad(true).prepare_face_extrusion(FaceExtrudeMode::Region).ok());
  auto unselected = before;
  static_cast<void>(unselected.apply_selection({.kind = SelectionKind::Face}).or_throw());
  EXPECT_FALSE(unselected.prepare_face_extrusion(FaceExtrudeMode::Region).ok());
}

// Opt in with --gtest_also_run_disabled_tests and this exact test filter.
TEST(ModelingSdkFaceExtrudeTests, DISABLED_PreparedExtrusionSameMeshBenchmark) {
  constexpr std::size_t kSampleCount = 64U;
  auto source = PolygonDocument::make_box({
      .x_subdivisions = 24, .y_subdivisions = 24, .z_subdivisions = 24}).or_throw();
  auto selected = source.face_ids_by_normal({0.0F, 1.0F, 0.0F});
  const auto adjoining = source.face_ids_by_normal({1.0F, 0.0F, 0.0F});
  selected.insert(selected.end(), adjoining.begin(), adjoining.end());
  ASSERT_FALSE(selected.empty());
  static_cast<void>(source.apply_selection({
      .kind = SelectionKind::Face, .faces = selected, .has_active = true,
      .active_face = selected.front()}).or_throw());
  const auto preparation_start = std::chrono::steady_clock::now();
  const auto prepared = source.prepare_face_extrusion(FaceExtrudeMode::AlongNormals).or_throw();
  const double preparation_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - preparation_start).count();
  const auto amount_for = [](std::size_t index) {
    return 0.001F + static_cast<float>(index) * 0.00025F;
  };
  // Warm both paths and check parity before measuring either one.
  auto warm = source;
  static_cast<void>(warm.extrude_selected_faces({
      .mode = FaceExtrudeMode::AlongNormals, .amount = amount_for(0U)}).or_throw());
  expect_extrude_equivalent(warm, prepared.evaluate(amount_for(0U)).or_throw().document);
  std::uint64_t fresh_elements = 0U;
  const auto fresh_start = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < kSampleCount; ++index) {
    auto document = source;
    static_cast<void>(document.extrude_selected_faces({
        .mode = FaceExtrudeMode::AlongNormals, .amount = amount_for(index)}).or_throw());
    fresh_elements += document.vertex_count() + document.face_count();
  }
  const double fresh_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - fresh_start).count();
  std::uint64_t prepared_elements = 0U;
  const auto prepared_start = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < kSampleCount; ++index) {
    const auto evaluated = prepared.evaluate(amount_for(index)).or_throw();
    prepared_elements += evaluated.document.vertex_count() + evaluated.document.face_count();
  }
  const double prepared_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - prepared_start).count();
  EXPECT_EQ(prepared_elements, fresh_elements);
  std::cout << "extrusion_benchmark source_vertices=" << source.vertex_count()
            << " source_faces=" << source.face_count() << " selected_faces=" << selected.size()
            << " samples=" << kSampleCount << " output_elements=" << fresh_elements
            << " preparation_ms=" << preparation_ms << " fresh_ms=" << fresh_ms
            << " prepared_ms=" << prepared_ms << '\n';
}

} // namespace
