////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_selection_depth.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/session/scene_session.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace quader {
namespace {

ViewportSelectionDepthSnapshot depth_snapshot() {
  ViewportSelectionDepthSnapshot result;
  auto geometry = std::make_shared<ViewportSelectionDepthGeometry>();
  geometry->triangles.resize(8u);
  result.geometry = std::move(geometry);
  result.camera.viewport_size = {8.0f, 8.0f};
  result.width = result.height = 8u;
  result.conservative_coverage = true;
  for (auto &layer : result.layers) layer.resize(64u);
  return result;
}

void set_layer(ViewportSelectionDepthSnapshot &snapshot, std::size_t layer,
               std::uint32_t triangle_id, float lower_depth) {
  snapshot.layers[layer][3u * 8u + 4u] =
      {static_cast<float>(triangle_id), 1.0f / (1.0f + lower_depth), 1.0f, 1.0f};
}

ViewportSelectionDepthIntersection known_intersections(const std::array<float, 8u> &distances) {
  return {.owner = &distances,
          .intersect = [](const void *owner, std::uint32_t triangle_id, QVec3, QVec3) -> std::optional<float> {
            const float distance = (*static_cast<const std::array<float, 8u> *>(owner))[triangle_id - 1u];
            return distance >= 0.0f ? std::optional<float>{distance} : std::nullopt;
          }};
}

TEST(ComponentSelectionDepthTests, ExhaustionAcceptsOnlyCompleteConservativeCoverage) {
  auto snapshot = depth_snapshot();
  const std::array<float, 8u> distances{};
  const auto classify = [&] {
    return classify_viewport_selection_depth(snapshot, {4, 4}, {}, {0, 0, -1}, 10, 0.001,
                                             known_intersections(distances));
  };
  EXPECT_EQ(classify(), ViewportSelectionDepthDecision::Accepted);
  snapshot.conservative_coverage = false;
  EXPECT_EQ(classify(), ViewportSelectionDepthDecision::Overflow);
}

TEST(ComponentSelectionDepthTests, RasterCandidatesRequireExactIntersectionAndPreserveDepthTies) {
  auto snapshot = depth_snapshot();
  set_layer(snapshot, 0u, 1u, 1.0f);
  std::array<float, 8u> distances{-1, -1, -1, -1, -1, -1, -1, -1};
  const auto classify = [&] {
    return classify_viewport_selection_depth(snapshot, {4, 4}, {}, {0, 0, -1}, 10, 0.125,
                                             known_intersections(distances));
  };
  EXPECT_EQ(classify(), ViewportSelectionDepthDecision::Accepted);
  distances[0] = 9.0f;
  EXPECT_EQ(classify(), ViewportSelectionDepthDecision::Rejected);
  distances[0] = 9.875f;
  EXPECT_EQ(classify(), ViewportSelectionDepthDecision::Accepted);
  distances[0] = std::nextafter(9.875f, 0.0f);
  EXPECT_EQ(classify(), ViewportSelectionDepthDecision::Overflow);
}

TEST(ComponentSelectionDepthTests, FourLayersUseConservativeTailOrFallBackForHiddenFifthTriangle) {
  auto snapshot = depth_snapshot();
  const std::array<float, 8u> distances{-1, -1, -1, -1, 2, -1, -1, -1};
  for (std::size_t index = 0u; index < 4u; ++index) set_layer(snapshot, index, static_cast<std::uint32_t>(index + 1u), 1.0f);
  EXPECT_EQ(classify_viewport_selection_depth(snapshot, {4, 4}, {}, {0, 0, -1}, 10, 0.001,
      known_intersections(distances)), ViewportSelectionDepthDecision::Overflow);
  for (std::size_t index = 0u; index < 4u; ++index) set_layer(snapshot, index, static_cast<std::uint32_t>(index + 1u), 20.0f);
  EXPECT_EQ(classify_viewport_selection_depth(snapshot, {4, 4}, {}, {0, 0, -1}, 10, 0.001,
      known_intersections(distances)), ViewportSelectionDepthDecision::Accepted);
}

TEST(ComponentSelectionDepthTests, TailBoundUsesWitnessRaySlopeAndOriginAndOutwardDepthBucket) {
  auto snapshot = depth_snapshot();
  const std::array<float, 8u> distances{-1, -1, -1, -1, -1, -1, -1, -1};
  for (std::size_t index = 0u; index < 4u; ++index) set_layer(snapshot, index, static_cast<std::uint32_t>(index + 1u), 10.0f);
  EXPECT_EQ(classify_viewport_selection_depth(snapshot, {4, 4}, {0, 0, -2}, {0.6f, 0, -0.8f}, 9, 0.001,
      known_intersections(distances)), ViewportSelectionDepthDecision::Accepted);
  EXPECT_EQ(classify_viewport_selection_depth(snapshot, {4, 4}, {0, 0, -2}, {0.6f, 0, -0.8f}, 11, 0.001,
      known_intersections(distances)), ViewportSelectionDepthDecision::Overflow);
  EXPECT_EQ(classify_viewport_selection_depth(snapshot, {4, 4}, {}, {0, 0, -1}, 10, 0,
      known_intersections(distances)), ViewportSelectionDepthDecision::Overflow);
}

TEST(ComponentSelectionDepthTests, InvalidIdsDuplicateLayersBordersAndStaleProjectionCannotCertify) {
  auto snapshot = depth_snapshot();
  const std::array<float, 8u> distances{-1, -1, -1, -1, -1, -1, -1, -1};
  set_layer(snapshot, 0u, 1u, 10);
  set_layer(snapshot, 1u, 1u, 10);
  EXPECT_EQ(classify_viewport_selection_depth(snapshot, {4, 4}, {}, {0, 0, -1}, 10, 0.001,
      known_intersections(distances)), ViewportSelectionDepthDecision::Unavailable);
  EXPECT_EQ(classify_viewport_selection_depth(snapshot, {1, 4}, {}, {0, 0, -1}, 10, 0.001,
      known_intersections(distances)), ViewportSelectionDepthDecision::Unavailable);
  snapshot.layers[0u][28u][0u] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(classify_viewport_selection_depth(snapshot, {4, 4}, {}, {0, 0, -1}, 10, 0.001,
      known_intersections(distances)), ViewportSelectionDepthDecision::Unavailable);
  EXPECT_TRUE(snapshot.matches(snapshot.geometry.get(), snapshot.camera));
  auto camera = snapshot.camera;
  camera.origin.x = 1.0f;
  EXPECT_FALSE(snapshot.matches(snapshot.geometry.get(), camera));
  const ViewportSelectionDepthGeometry replacement;
  EXPECT_FALSE(snapshot.matches(&replacement, snapshot.camera));
}

TEST(ComponentSelectionDepthTests, SurfaceExportRetainsRigidUniformAndReflectedTransformsButGuardsConditioning) {
  SceneSession session;
  MeshAsset mesh;
  auto &payload = mesh.edit_geometry();
  payload.vertices = {{.position = {-1, -1, 0}}, {.position = {1, -1, 0}}, {.position = {0, 1, 0}}};
  payload.indices = {0u, 1u, 2u};
  const auto asset = session.assets().add_mesh(std::move(mesh));
  const auto entity = session.scene().create_entity("GPU certificate transform", EntityKind::Mesh);
  ASSERT_TRUE(session.scene().set_mesh(entity, {.mesh = asset}));
  SceneSurfaceQuery surfaces(session.scene(), session.assets(), session.viewport_local_visibility());
  ASSERT_TRUE(session.scene().set_transform(entity, {.location = {3, 4, -10}, .rotation_euler = {0.2f, 0.4f, 0.8f}, .scale = {-2, 2, 2}}));
  const auto rigid = surfaces.snapshot().selection_depth_geometry();
  ASSERT_TRUE(rigid);
  EXPECT_TRUE(rigid->conservative_transforms);
  ASSERT_TRUE(session.scene().set_transform(entity, {.location = {3, 4, -10}, .scale = {10000, 0.0001f, 1}}));
  const auto ill_conditioned = surfaces.snapshot().selection_depth_geometry();
  ASSERT_TRUE(ill_conditioned);
  EXPECT_FALSE(ill_conditioned->conservative_transforms);
  EXPECT_NE(rigid, ill_conditioned);
  EXPECT_TRUE(rigid->conservative_transforms);
}

TEST(ComponentSelectionDepthTests, SurfaceExportBoundsLargeTermsEvenWhenWorldCoordinatesCancel) {
  SceneSession session;
  MeshAsset mesh;
  auto &payload = mesh.edit_geometry();
  payload.vertices = {{.position = {1000000, -1, 0}}, {.position = {1000002, -1, 0}}, {.position = {1000001, 1, 0}}};
  payload.indices = {0u, 1u, 2u};
  const auto asset = session.assets().add_mesh(std::move(mesh));
  const auto entity = session.scene().create_entity("GPU cancellation bound", EntityKind::Mesh);
  ASSERT_TRUE(session.scene().set_mesh(entity, {.mesh = asset}));
  ASSERT_TRUE(session.scene().set_transform(entity, {.location = {-1000000, 0, -10}}));
  SceneSurfaceQuery surfaces(session.scene(), session.assets(), session.viewport_local_visibility());
  const auto geometry = surfaces.snapshot().selection_depth_geometry();
  ASSERT_TRUE(geometry);
  ASSERT_EQ(geometry->triangles.size(), 1u);
  EXPECT_LT(std::abs(geometry->triangles[0u].a.x), 3.0f);
  EXPECT_GE(geometry->transform_arithmetic_magnitude, 2000000.0);
}

} // namespace
} // namespace quader
