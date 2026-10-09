////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/curve_asset_payload.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/interactions/selection_lasso.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/viewport/viewport_interaction_frame.hpp"
#include "editor/viewport/viewport_scene_hit_policy.hpp"
#include "scene/scene_document.hpp"

#include <gtest/gtest.h>

#include <array>
#include <set>

namespace quader {
namespace {

ModelingViewportCamera lasso_camera() {
  return {.orthographic = true, .origin = {0.0f, 0.0f, 10.0f},
          .viewport_size = {100.0f, 100.0f}, .orthographic_size = 4.0};
}

constexpr std::array<QVec2, 4> kSquare{{{20.0f, 20.0f}, {80.0f, 20.0f},
                                      {80.0f, 80.0f}, {20.0f, 80.0f}}};

TEST(LassoSelectionGeometryTests, BoundaryInclusiveAndPartialSegmentCrossing) {
  const quader_editor_core::SelectionLassoRegion region(kSquare);
  EXPECT_TRUE(region.valid());
  EXPECT_TRUE(region.contains_point({20.0f, 50.0f}));
  EXPECT_TRUE(region.contains_segment({20.0f, 20.0f}, {80.0f, 20.0f}));
  EXPECT_FALSE(region.contains_segment({0.0f, 50.0f}, {100.0f, 50.0f}));
  EXPECT_TRUE(region.intersects_segment({0.0f, 50.0f}, {100.0f, 50.0f}));
  EXPECT_FALSE(region.intersects_segment({0.0f, 0.0f}, {10.0f, 10.0f}));
}

TEST(LassoSelectionGeometryTests, ConcaveNotchRejectsContainedEndpointsAndFaceVertices) {
  const std::array<QVec2, 8> path{{{0, 0}, {10, 0}, {10, 10}, {7, 10},
                                  {7, 3}, {3, 3}, {3, 10}, {0, 10}}};
  const quader_editor_core::SelectionLassoRegion region(path);
  EXPECT_TRUE(region.contains_point({1, 8}));
  EXPECT_TRUE(region.contains_point({9, 8}));
  EXPECT_FALSE(region.contains_segment({1, 8}, {9, 8}));
  const std::array<QVec2, 3> face{{{1, 8}, {9, 8}, {5, 1}}};
  EXPECT_FALSE(region.contains_polygon(face));
  EXPECT_TRUE(region.intersects_polygon(face));
}

TEST(LassoSelectionGeometryTests, SelfIntersectionUsesEvenOddInterior) {
  const std::array<QVec2, 4> path{{{0, 0}, {10, 10}, {0, 10}, {10, 0}}};
  const quader_editor_core::SelectionLassoRegion region(path);
  EXPECT_TRUE(region.valid());
  EXPECT_TRUE(region.contains_point({5, 1}));
  EXPECT_TRUE(region.contains_point({5, 9}));
  EXPECT_TRUE(region.contains_point({5, 5}));
  EXPECT_FALSE(region.contains_point({1, 5}));
}

TEST(LassoSelectionGeometryTests, RetracedAndCollinearPathsHaveNoInterior) {
  const std::array<QVec2, 4> line{{{0, 0}, {2, 0}, {4, 0}, {6, 0}}};
  EXPECT_FALSE(quader_editor_core::SelectionLassoRegion(line).valid());
  const std::array<QVec2, 9> twice{{{0, 0}, {10, 0}, {10, 10}, {0, 10},
      {0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}}};
  EXPECT_FALSE(quader_editor_core::SelectionLassoRegion(twice).valid());
}

TEST(LassoSelectionGeometryTests, FaceEnclosingEvenOddHoleIsNotFullyContained) {
  const std::array<QVec2, 11> path{{{0, 0}, {10, 0}, {10, 10}, {0, 10},
      {0, 0}, {3, 3}, {7, 3}, {7, 7}, {3, 7}, {3, 3}, {0, 0}}};
  const quader_editor_core::SelectionLassoRegion region(path);
  const std::array<QVec2, 4> face{{{1, 1}, {9, 1}, {9, 9}, {1, 9}}};
  EXPECT_TRUE(region.contains_segment(face[0], face[1]));
  EXPECT_FALSE(region.contains_point({5, 5}));
  EXPECT_FALSE(region.contains_polygon(face));
  EXPECT_TRUE(region.intersects_polygon(face));
}

TEST(LassoSelectionGeometryTests, PartialIntersectionAcceptsLassoInsideFace) {
  const quader_editor_core::SelectionLassoRegion region(kSquare);
  const std::array<QVec2, 4> face{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
  EXPECT_FALSE(region.contains_polygon(face));
  EXPECT_TRUE(region.intersects_polygon(face));
}

TEST(LassoSelectionGeometryTests, CoincidentVertexIdentitiesAndAuthoredBackfacesArePreserved) {
  quader_editor_core::RectangleProjectedHitsRequest candidates;
  candidates.mode = quader_editor_core::SelectionMode::Vertex;
  candidates.vertices = {
      {.object_id = 1, .vertex_index = 4, .has_screen_position = true,
       .screen_position = {50, 50}, .front_facing = true},
      {.object_id = 1, .vertex_index = 9, .has_screen_position = true,
       .screen_position = {50, 50}, .front_facing = false},
      {.object_id = 2, .vertex_index = 4, .has_screen_position = true,
       .screen_position = {50, 50}, .front_facing = true}};
  const quader_editor_core::SelectionLassoRegion region(kSquare);
  EXPECT_EQ(quader_editor_core::lasso_hits_from_projected_candidates(
      candidates, region, false).vertices.size(), 3u);
  candidates.select_backfaces = false;
  EXPECT_EQ(quader_editor_core::lasso_hits_from_projected_candidates(
      candidates, region, false).vertices.size(), 2u);
}

TEST(LassoSelectionGeometryTests, QuerySelectsThroughMeshAndReusesPathIndependentProjection) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto box = service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}}, .name = "Lasso cube"});
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  ModelingLassoSelectRequest request{.camera = lasso_camera(), .points = kSquare};
  auto hits = service.query_selection_lasso_hits(request);
  ASSERT_EQ(hits.size(), 8u);
  std::set<int> ids;
  for (const auto &hit : hits) {
    ids.insert(hit.vertex_id);
  }
  EXPECT_EQ(ids.size(), 8u);
  const auto builds = service.rectangle_picking_projection_build_count();
  auto moved = kSquare;
  moved[0].x -= 1.0f;
  request.points = moved;
  EXPECT_EQ(service.query_selection_lasso_hits(request).size(), 8u);
  EXPECT_EQ(service.rectangle_picking_projection_build_count(), builds);
  request.select_backfaces = false;
  EXPECT_EQ(service.query_selection_lasso_hits(request).size(), 4u);
  EXPECT_EQ(service.rectangle_picking_projection_build_count(), builds);
  ASSERT_TRUE(scene.set_selectability(box.object.entity, false));
  EXPECT_TRUE(service.query_selection_lasso_hits(request).empty());
}

TEST(LassoSelectionGeometryTests, CameraDepthClipsPartialGeometryAndRejectsFullOrBehind) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  ASSERT_TRUE(service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}}, .name = "Depth cube"}).ok);
  ModelingLassoSelectRequest request{.camera = lasso_camera(), .points = kSquare,
      .near_plane = 10.0, .far_plane = 20.0};
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Object).ok);
  EXPECT_TRUE(service.query_selection_lasso_hits(request).empty());
  request.partial_intersection = true;
  EXPECT_EQ(service.query_selection_lasso_hits(request).size(), 1u);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  EXPECT_EQ(service.query_selection_lasso_hits(request).size(), 4u);
  request.near_plane = 0.1;
  request.far_plane = 10.0;
  EXPECT_EQ(service.query_selection_lasso_hits(request).size(), 4u);
  request.camera.origin.z = -10.0f;
  EXPECT_TRUE(service.query_selection_lasso_hits(request).empty());
}

TEST(LassoSelectionGeometryTests, GroupFullContainmentIncludesNestedAndOccludedMembers) {
  SceneDocument scene;
  AssetRegistry assets;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history, &assets);
  ModelingSessionService modeling(scene, assets);
  EditorSelectionService selection(scene, commands, modeling);
  const auto group = scene.create_empty("Group");
  const auto nested = scene.create_empty("Nested");
  const auto first = scene.create_entity("First", EntityKind::Generic);
  const auto second = scene.create_entity("Occluded", EntityKind::Generic);
  ASSERT_TRUE(scene.set_parent(nested, group));
  ASSERT_TRUE(scene.set_parent(first, group));
  ASSERT_TRUE(scene.set_parent(second, nested));
  ASSERT_TRUE(modeling.set_selection_mode(ModelingSelectionMode::Group).ok);
  const std::array eligible{first, second};
  const std::array<ResolvedSelectionTarget, 1> one{{
      {.domain = ResolvedSelectionDomain::SceneEntity, .entity = first}}};
  EXPECT_TRUE(selection.filter_lasso_group_targets(one, eligible, false).empty());
  const auto partial = selection.filter_lasso_group_targets(one, eligible, true);
  ASSERT_EQ(partial.size(), 1u);
  EXPECT_EQ(partial[0].entity, group);
  const std::array<ResolvedSelectionTarget, 2> both{{one[0],
      {.domain = ResolvedSelectionDomain::SceneEntity, .entity = second}}};
  const auto full = selection.filter_lasso_group_targets(both, eligible, false);
  EXPECT_EQ(full.size(), 2u);
}

TEST(LassoSelectionGeometryTests, SceneProvidersSelectThroughDepthAndKeepUnmatchedEligibility) {
  const std::array<ViewportSceneBillboardPickCandidate, 4> billboards{{
      {.entity = 10, .position = {0, 0, 0}, .size_pixels = 6},
      {.entity = 11, .position = {0, 0, -4}, .size_pixels = 6},
      {.entity = 12, .position = {0, 0, 20}, .size_pixels = 6},
      {.entity = 13, .position = {0, 0, 0}, .effectively_selectable = false}}};
  std::vector<EntityId> eligible;
  ViewportSceneLassoProjectionCache cache;
  ViewportSceneLassoHitQuery query{.camera = lasso_camera(), .points = kSquare,
      .billboards = billboards, .eligible_entities = &eligible,
      .projection_cache = &cache};
  EXPECT_EQ(collect_viewport_scene_lasso_hits(query).size(), 2u);
  EXPECT_EQ(eligible, (std::vector<EntityId>{10, 11, 12}));
  EXPECT_EQ(cache.projection_build_count, 1u);
  auto moved = kSquare;
  moved[0].x += 1.0f;
  query.points = moved;
  query.partial_intersection = true;
  EXPECT_EQ(collect_viewport_scene_lasso_hits(query).size(), 2u);
  EXPECT_EQ(cache.projection_build_count, 1u);
}

TEST(LassoSelectionGeometryTests, CurvesRequireWholeSegmentsForFullAndAcceptPartialCrossing) {
  auto payload = std::make_shared<CurveSamplePayload>();
  payload->samples = {{.position = {-2, 0, 0}}, {.position = {2, 0, 0}}};
  const std::array<ViewportSceneCurvePickCandidate, 1> curves{{
      {.entity = 20, .sample_payload = payload}}};
  ViewportSceneLassoHitQuery query{.camera = lasso_camera(), .points = kSquare,
      .curves = curves};
  EXPECT_TRUE(collect_viewport_scene_lasso_hits(query).empty());
  query.partial_intersection = true;
  EXPECT_EQ(collect_viewport_scene_lasso_hits(query).size(), 1u);
}

TEST(LassoSelectionGeometryTests, PartialTubeLassoInsideDepthClippedTriangleHitsInterior) {
  // Use the explicit modeling projection camera defaults to avoid relying on
  // controller navigation state: triangle is clipped at depth 0.01.
  auto projection = lasso_camera();
  projection.origin = {0, 0, 0};
  const auto payload = ViewportCurveTubePickPayload::build({
      {.a = {-2, -2, -1}, .b = {2, -2, -1}, .c = {0, 2, 1}}});
  ASSERT_NE(payload, nullptr);
  const std::array<ViewportSceneCurvePickCandidate, 1> curves{{
      {.entity = 21, .solidified = true, .tube_payload = payload}}};
  const std::array<QVec2, 4> inside{{{45, 70}, {55, 70}, {55, 80}, {45, 80}}};
  ViewportSceneLassoHitQuery query{.camera = projection, .points = inside,
      .partial_intersection = true, .curves = curves};
  EXPECT_EQ(collect_viewport_scene_lasso_hits(query).size(), 1u);
  query.points = kSquare;
  query.partial_intersection = false;
  EXPECT_TRUE(collect_viewport_scene_lasso_hits(query).empty());
}

} // namespace
} // namespace quader
