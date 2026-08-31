////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/placement/snap_to_floor_service.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>

namespace {

constexpr float kTestTolerance = 0.00001f;

quader::MeshAsset make_surface_mesh(float local_y = 0.0f,
                                    bool reverse_winding = false,
                                    bool prepend_degenerate = false,
                                    bool duplicate_surface = false) {
  quader::MeshAsset mesh;
  mesh.name = "Scene Surface";
  quader::MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = {
      {.position = {-2.0f, local_y, -2.0f},
       .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {2.0f, local_y, -2.0f},
       .normal = {0.0f, 1.0f, 0.0f}},
      {.position = {0.0f, local_y, 2.0f},
       .normal = {0.0f, 1.0f, 0.0f}},
  };
  if (prepend_degenerate) {
    geometry.indices.insert(geometry.indices.end(), {0u, 0u, 0u});
  }
  if (reverse_winding) {
    geometry.indices.insert(geometry.indices.end(), {2u, 1u, 0u});
  } else {
    geometry.indices.insert(geometry.indices.end(), {0u, 1u, 2u});
  }
  if (duplicate_surface) {
    geometry.indices.insert(geometry.indices.end(), {2u, 1u, 0u});
  }
  return mesh;
}

/// Owns the minimum editor state needed by native surface-query tests.
struct SceneSurfaceQueryHarness final {
  quader::SceneSession session;
  quader::SceneSurfaceQuery query;

  SceneSurfaceQueryHarness()
      : query(session.scene(), session.assets(),
              session.viewport_local_visibility()) {}

  quader::AssetId add_mesh(quader::MeshAsset mesh) {
    return session.assets().add_mesh(std::move(mesh));
  }

  quader::EntityId add_mesh_entity(
      quader::AssetId mesh, const char *name,
      quader::QVec3 position = {}) {
    const quader::EntityId entity =
        session.scene().create_entity(name, quader::EntityKind::Mesh);
    EXPECT_TRUE(session.scene().set_mesh(
        entity, quader::MeshComponent{.mesh = mesh}));
    if (position.x != 0.0f || position.y != 0.0f || position.z != 0.0f) {
      EXPECT_TRUE(session.scene().set_transform(
          entity, quader::TransformComponent{.position = position}));
    }
    return entity;
  }

  /// Identifies the canonical definition member and its visible occurrence.
  struct FormalInstance final {
    quader::EntityId definition = quader::kInvalidEntityId;
    quader::EntityId member = quader::kInvalidEntityId;
    quader::EntityId first_occurrence = quader::kInvalidEntityId;
    quader::EntityId second_occurrence = quader::kInvalidEntityId;
  };

  FormalInstance add_formal_instance(quader::AssetId mesh,
                                     bool add_second_occurrence = true) {
    FormalInstance result;
    result.definition = session.scene().create_entity(
        "Surface Definition", quader::EntityKind::Empty);
    EXPECT_TRUE(session.scene().set_instance_definition(result.definition));
    EXPECT_TRUE(session.scene().set_visible(result.definition, false));
    result.member = session.scene().create_entity(
        "Surface Definition Member", quader::EntityKind::Mesh);
    EXPECT_TRUE(session.scene().set_mesh(
        result.member, quader::MeshComponent{.mesh = mesh}));
    EXPECT_TRUE(
        session.scene().set_parent(result.member, result.definition, false));
    result.first_occurrence = session.scene().create_entity(
        "First Surface Occurrence", quader::EntityKind::Instance);
    EXPECT_TRUE(session.scene().set_instance_occurrence(
        result.first_occurrence,
        quader::InstanceOccurrenceComponent{.definition =
                                                result.definition}));
    EXPECT_TRUE(session.scene().set_transform(
        result.first_occurrence,
        quader::TransformComponent{.position = {-5.0f, 0.0f, 0.0f}}));
    if (add_second_occurrence) {
      result.second_occurrence = session.scene().create_entity(
          "Second Surface Occurrence", quader::EntityKind::Instance);
      EXPECT_TRUE(session.scene().set_instance_occurrence(
          result.second_occurrence,
          quader::InstanceOccurrenceComponent{.definition =
                                                  result.definition}));
      EXPECT_TRUE(session.scene().set_transform(
          result.second_occurrence,
          quader::TransformComponent{.position = {5.0f, 0.0f, 0.0f}}));
    }
    return result;
  }
};

quader::SceneSurfaceRaycastRequest downward_ray(quader::QVec3 origin) {
  return {
      .world_origin = origin,
      .world_direction = {0.0f, -1.0f, 0.0f},
      .min_distance = 0.0f,
      .max_distance = std::numeric_limits<float>::infinity(),
  };
}

TEST(SceneSurfaceQueryTests,
     EqualDistanceTiesUseStableParticipantThenPrimitiveIdentity) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh =
      harness.add_mesh(make_surface_mesh(0.0f, false, false, true));
  ASSERT_NE(mesh, quader::kInvalidAssetId);
  const quader::EntityId first =
      harness.add_mesh_entity(mesh, "First", {0.0f, 2.0f, 0.0f});
  const quader::EntityId second =
      harness.add_mesh_entity(mesh, "Second", {0.0f, 2.0f, 0.0f});

  const quader::SceneSurfaceQuerySnapshot snapshot = harness.query.snapshot();
  const std::optional<quader::SceneSurfaceHit> hit =
      snapshot.nearest_hit(downward_ray({0.0f, 5.0f, 0.0f}));
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->participant.display_entity, first);
  EXPECT_EQ(hit->primitive_index, 0u);
  EXPECT_NEAR(hit->distance, 3.0f, kTestTolerance);

  const std::array exclusions{quader::SceneSurfaceParticipantId{
      .display_entity = first,
      .geometry_entity = first,
      .definition_entity = quader::kInvalidEntityId}};
  quader::SceneSurfaceRaycastRequest request =
      downward_ray({0.0f, 5.0f, 0.0f});
  request.excluded_participants = exclusions;
  const std::optional<quader::SceneSurfaceHit> second_hit =
      snapshot.nearest_hit(request);
  ASSERT_TRUE(second_hit.has_value());
  EXPECT_EQ(second_hit->participant.display_entity, second);
  EXPECT_EQ(second_hit->primitive_index, 0u);
}

TEST(SceneSurfaceQueryTests,
     ExactZeroContactIsTwoSidedAndDegenerateFacesAreIgnored) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh =
      harness.add_mesh(make_surface_mesh(0.0f, true, true));
  ASSERT_NE(mesh, quader::kInvalidAssetId);
  const quader::EntityId entity =
      harness.add_mesh_entity(mesh, "Two-Sided Surface", {0.0f, 2.0f, 0.0f});

  const quader::SceneSurfaceQuerySnapshot snapshot = harness.query.snapshot();
  ASSERT_EQ(snapshot.participants().size(), 1u);
  EXPECT_EQ(snapshot.participants().front().id.display_entity, entity);
  EXPECT_EQ(snapshot.participants().front().triangle_count, 1u);

  const std::optional<quader::SceneSurfaceHit> contact =
      snapshot.nearest_hit(downward_ray({0.0f, 2.0f, 0.0f}));
  ASSERT_TRUE(contact.has_value());
  EXPECT_NEAR(contact->distance, 0.0f, kTestTolerance);
  EXPECT_EQ(contact->primitive_index, 1u);

  quader::SceneSurfaceRaycastRequest upward_request;
  upward_request.world_origin = {0.0f, 0.0f, 0.0f};
  upward_request.world_direction = {0.0f, 1.0f, 0.0f};
  const std::optional<quader::SceneSurfaceHit> backside =
      snapshot.nearest_hit(upward_request);
  ASSERT_TRUE(backside.has_value());
  EXPECT_NEAR(backside->distance, 2.0f, kTestTolerance);
  EXPECT_EQ(backside->primitive_index, 1u);
}

TEST(SceneSurfaceQueryTests,
     IdenticalPayloadsAndFormalOccurrencesShareOneLocalBvh) {
  SceneSurfaceQueryHarness harness;
  const quader::MeshAsset source = make_surface_mesh();
  const quader::AssetId first_mesh = harness.add_mesh(source);
  const quader::AssetId second_mesh = harness.add_mesh(source);
  ASSERT_NE(first_mesh, quader::kInvalidAssetId);
  ASSERT_NE(second_mesh, quader::kInvalidAssetId);
  (void)harness.add_mesh_entity(first_mesh, "First Logical Mesh",
                                {-10.0f, 0.0f, 0.0f});
  (void)harness.add_mesh_entity(second_mesh, "Second Logical Mesh",
                                {10.0f, 0.0f, 0.0f});

  const quader::SceneSurfaceQuerySnapshot ordinary = harness.query.snapshot();
  const quader::SceneSurfaceQueryStats ordinary_stats = harness.query.stats();
  ASSERT_TRUE(ordinary.valid());
  EXPECT_EQ(ordinary_stats.participant_count, 2u);
  EXPECT_EQ(ordinary_stats.unique_geometry_count, 1u);
  EXPECT_EQ(ordinary_stats.blas_build_count, 1u);

  const SceneSurfaceQueryHarness::FormalInstance formal =
      harness.add_formal_instance(first_mesh);
  const quader::SceneSurfaceQuerySnapshot with_occurrences =
      harness.query.snapshot();
  const quader::SceneSurfaceQueryStats occurrence_stats =
      harness.query.stats();
  EXPECT_EQ(occurrence_stats.participant_count, 4u);
  EXPECT_EQ(occurrence_stats.unique_geometry_count, 1u);
  EXPECT_EQ(occurrence_stats.blas_build_count, 1u);
  EXPECT_EQ(occurrence_stats.landmark_count, 28u);

  const auto participants = with_occurrences.participants();
  const auto first_occurrence = std::ranges::find_if(
      participants, [&](const quader::SceneSurfaceParticipant &participant) {
        return participant.id.display_entity == formal.first_occurrence;
      });
  const auto second_occurrence = std::ranges::find_if(
      participants, [&](const quader::SceneSurfaceParticipant &participant) {
        return participant.id.display_entity == formal.second_occurrence;
      });
  ASSERT_NE(first_occurrence, participants.end());
  ASSERT_NE(second_occurrence, participants.end());
  EXPECT_EQ(first_occurrence->id.geometry_entity, formal.member);
  EXPECT_EQ(first_occurrence->id.definition_entity, formal.definition);
  EXPECT_EQ(second_occurrence->id.geometry_entity, formal.member);
  EXPECT_EQ(second_occurrence->id.definition_entity, formal.definition);
}

TEST(SceneSurfaceQueryTests,
     SnapshotsAreFrozenAndUnchangedWarmAccessRebuildsNothing) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh = harness.add_mesh(make_surface_mesh());
  ASSERT_NE(mesh, quader::kInvalidAssetId);
  const quader::EntityId entity =
      harness.add_mesh_entity(mesh, "Moving Surface", {0.0f, 1.0f, 0.0f});

  const quader::SceneSurfaceQuerySnapshot frozen = harness.query.snapshot();
  const quader::SceneSurfaceQueryStats cold_stats = harness.query.stats();
  ASSERT_EQ(cold_stats.snapshot_build_count, 1u);
  ASSERT_EQ(cold_stats.tlas_build_count, 1u);
  ASSERT_EQ(cold_stats.blas_build_count, 1u);

  const quader::SceneSurfaceQuerySnapshot warm = harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().snapshot_build_count,
            cold_stats.snapshot_build_count);
  EXPECT_EQ(harness.query.stats().tlas_build_count,
            cold_stats.tlas_build_count);
  EXPECT_EQ(harness.query.stats().blas_build_count,
            cold_stats.blas_build_count);
  quader::SceneSurfaceQueryScratch scratch;
  for (const float x : std::array{-0.5f, 0.0f, 0.5f}) {
    const std::optional<quader::SceneSurfaceHit> warm_hit =
        warm.nearest_hit(downward_ray({x, 5.0f, 0.0f}), scratch);
    ASSERT_TRUE(warm_hit.has_value());
    EXPECT_NEAR(warm_hit->world_position.y, 1.0f, kTestTolerance);
  }
  EXPECT_EQ(harness.query.stats().snapshot_build_count,
            cold_stats.snapshot_build_count);

  ASSERT_TRUE(harness.session.scene().set_transform(
      entity, quader::TransformComponent{.position = {0.0f, 3.0f, 0.0f}}));
  const std::optional<quader::SceneSurfaceHit> frozen_hit =
      frozen.nearest_hit(downward_ray({0.0f, 5.0f, 0.0f}));
  ASSERT_TRUE(frozen_hit.has_value());
  EXPECT_NEAR(frozen_hit->world_position.y, 1.0f, kTestTolerance);

  const quader::SceneSurfaceQuerySnapshot rebuilt = harness.query.snapshot();
  const std::optional<quader::SceneSurfaceHit> rebuilt_hit =
      rebuilt.nearest_hit(downward_ray({0.0f, 5.0f, 0.0f}));
  ASSERT_TRUE(rebuilt_hit.has_value());
  EXPECT_NEAR(rebuilt_hit->world_position.y, 3.0f, kTestTolerance);
  const quader::SceneSurfaceQueryStats rebuilt_stats = harness.query.stats();
  EXPECT_EQ(rebuilt_stats.snapshot_build_count, 2u);
  EXPECT_EQ(rebuilt_stats.tlas_build_count, 2u);
  EXPECT_EQ(rebuilt_stats.blas_build_count, 1u);

  (void)harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().snapshot_build_count,
            rebuilt_stats.snapshot_build_count);
  EXPECT_EQ(harness.query.stats().tlas_build_count,
            rebuilt_stats.tlas_build_count);
  EXPECT_EQ(harness.query.stats().blas_build_count,
            rebuilt_stats.blas_build_count);
}

TEST(SceneSurfaceQueryTests,
     RecentlyHiddenGeometryRetainsItsBlasAfterFrozenSnapshotsAreReleased) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh = harness.add_mesh(make_surface_mesh());
  ASSERT_NE(mesh, quader::kInvalidAssetId);
  const quader::EntityId entity =
      harness.add_mesh_entity(mesh, "Temporarily Hidden Surface");

  quader::SceneSurfaceQuerySnapshot visible = harness.query.snapshot();
  ASSERT_EQ(visible.participants().size(), 1u);
  ASSERT_EQ(harness.query.stats().blas_build_count, 1u);

  ASSERT_TRUE(harness.session.scene().set_visible(entity, false));
  const quader::SceneSurfaceQuerySnapshot hidden = harness.query.snapshot();
  ASSERT_TRUE(hidden.participants().empty());
  const quader::SceneSurfaceQueryStats hidden_stats = harness.query.stats();
  EXPECT_EQ(hidden_stats.snapshot_build_count, 2u);
  EXPECT_EQ(hidden_stats.tlas_build_count, 2u);
  EXPECT_EQ(hidden_stats.blas_build_count, 1u);

  // Neither the current hidden snapshot nor any caller-held snapshot now
  // pins the cached surface geometry.
  visible = {};

  ASSERT_TRUE(harness.session.scene().set_visible(entity, true));
  const quader::SceneSurfaceQuerySnapshot restored = harness.query.snapshot();
  ASSERT_EQ(restored.participants().size(), 1u);
  const quader::SceneSurfaceQueryStats restored_stats = harness.query.stats();
  EXPECT_EQ(restored_stats.snapshot_build_count, 3u);
  EXPECT_EQ(restored_stats.tlas_build_count, 3u);
  EXPECT_EQ(restored_stats.blas_build_count, 1u);
}

TEST(SceneSurfaceQueryTests,
     OwnerRevisionsInvalidateSnapshotsAndOnlyGeometryRebuildsBlas) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh = harness.add_mesh(make_surface_mesh());
  ASSERT_NE(mesh, quader::kInvalidAssetId);
  const quader::EntityId ordinary =
      harness.add_mesh_entity(mesh, "Ordinary Surface");
  const SceneSurfaceQueryHarness::FormalInstance formal =
      harness.add_formal_instance(mesh, false);

  // Retaining every frozen snapshot also verifies that invalidation never
  // mutates an in-flight operation's receiver set.
  const quader::SceneSurfaceQuerySnapshot initial = harness.query.snapshot();
  ASSERT_EQ(harness.query.stats().snapshot_build_count, 1u);
  ASSERT_EQ(harness.query.stats().tlas_build_count, 1u);
  ASSERT_EQ(harness.query.stats().blas_build_count, 1u);

  ASSERT_TRUE(
      harness.session.assets().replace_mesh(mesh, make_surface_mesh(1.0f)));
  const quader::SceneSurfaceQuerySnapshot after_asset =
      harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().snapshot_build_count, 2u);
  EXPECT_EQ(harness.query.stats().tlas_build_count, 2u);
  EXPECT_EQ(harness.query.stats().blas_build_count, 2u);

  ASSERT_TRUE(harness.session.scene().set_transform(
      ordinary,
      quader::TransformComponent{.position = {0.0f, 2.0f, 0.0f}}));
  const quader::SceneSurfaceQuerySnapshot after_transform =
      harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().snapshot_build_count, 3u);
  EXPECT_EQ(harness.query.stats().tlas_build_count, 3u);
  EXPECT_EQ(harness.query.stats().blas_build_count, 2u);

  ASSERT_TRUE(harness.session.scene().set_visible(ordinary, false));
  const quader::SceneSurfaceQuerySnapshot after_visibility =
      harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().snapshot_build_count, 4u);
  EXPECT_EQ(harness.query.stats().tlas_build_count, 4u);
  EXPECT_EQ(harness.query.stats().blas_build_count, 2u);

  ASSERT_TRUE(harness.session.scene().clear_instance_occurrence(
      formal.first_occurrence));
  const quader::SceneSurfaceQuerySnapshot after_occurrence =
      harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().snapshot_build_count, 5u);
  EXPECT_EQ(harness.query.stats().tlas_build_count, 5u);
  EXPECT_EQ(harness.query.stats().blas_build_count, 2u);

  const std::array<quader::ViewportLocalVisibilityEntityId, 4> baseline{
      ordinary, formal.definition, formal.member, formal.first_occurrence};
  const std::array<quader::ViewportLocalVisibilityEntityId, 1> members{
      ordinary};
  ASSERT_TRUE(harness.session.viewport_workspace().enter_local_visibility(
      baseline, members));
  const quader::SceneSurfaceQuerySnapshot after_local_visibility =
      harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().snapshot_build_count, 6u);
  EXPECT_EQ(harness.query.stats().tlas_build_count, 6u);
  EXPECT_EQ(harness.query.stats().blas_build_count, 2u);

  (void)harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().snapshot_build_count, 6u);
  EXPECT_EQ(harness.query.stats().tlas_build_count, 6u);
  EXPECT_EQ(harness.query.stats().blas_build_count, 2u);

  EXPECT_TRUE(initial.valid());
  EXPECT_TRUE(after_asset.valid());
  EXPECT_TRUE(after_transform.valid());
  EXPECT_TRUE(after_visibility.valid());
  EXPECT_TRUE(after_occurrence.valid());
  EXPECT_TRUE(after_local_visibility.valid());
}

TEST(SceneSurfaceQueryTests,
     SessionSharesOneWarmSurfaceIndexAcrossPlacementAndSnapToFloor) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh = harness.add_mesh(make_surface_mesh());
  ASSERT_NE(mesh, quader::kInvalidAssetId);
  (void)harness.add_mesh_entity(mesh, "Shared Surface");

  quader::ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(quader::ViewportViewAxis::Y, 1);
  quader::ViewportPointPlacementPolicy policy;
  policy.vertex = false;
  policy.edge_midpoint = false;
  policy.face_center = false;
  policy.surface = true;
  policy.grid = false;
  const quader::ViewportPointPlacementResult placement =
      harness.session.point_placement().resolve(
          harness.session.scene(), harness.session.assets(), camera,
          {.mouse_local = {400.0f, 300.0f}, .policy = policy});
  ASSERT_TRUE(placement.valid);

  const quader::SceneSurfaceQueryStats placement_stats =
      harness.session.point_placement().surface_query_stats();
  const quader::SceneSurfaceQueryStats floor_stats =
      harness.session.snap_to_floor().surface_query_stats();
  EXPECT_EQ(placement_stats.snapshot_build_count, 1u);
  EXPECT_EQ(floor_stats.snapshot_build_count,
            placement_stats.snapshot_build_count);
  EXPECT_EQ(floor_stats.blas_build_count, placement_stats.blas_build_count);
  (void)harness.session.scene_surface_query().snapshot();
  EXPECT_EQ(harness.session.scene_surface_query().stats().snapshot_build_count,
            placement_stats.snapshot_build_count);
}

TEST(SceneSurfaceQueryTests,
     PointPlacementExpandsFormalOccurrencesAndReturnsOccurrenceIdentity) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh = harness.add_mesh(make_surface_mesh());
  ASSERT_NE(mesh, quader::kInvalidAssetId);
  const SceneSurfaceQueryHarness::FormalInstance formal =
      harness.add_formal_instance(mesh, false);
  ASSERT_TRUE(harness.session.scene().set_transform(
      formal.first_occurrence,
      quader::TransformComponent{.position = {1.0f, 0.0f, 0.0f}}));

  quader::ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(quader::ViewportViewAxis::Y, 1);
  const quader::ViewportScreenPoint screen =
      camera.project_world_point({1.0f, 0.0f, 0.0f});
  ASSERT_TRUE(screen.visible);
  quader::ViewportPointPlacementPolicy policy;
  policy.vertex = false;
  policy.edge_midpoint = false;
  policy.face_center = false;
  policy.surface = true;
  policy.grid = false;

  const quader::ViewportPointPlacementResult result =
      harness.session.point_placement().resolve(
          harness.session.scene(), harness.session.assets(), camera,
          {.mouse_local = screen.position, .policy = policy});
  ASSERT_TRUE(result.valid);
  EXPECT_EQ(result.snap_kind, quader::ViewportPointSnapKind::Surface);
  EXPECT_EQ(result.entity, formal.first_occurrence);
  EXPECT_NEAR(result.world_position.x, 1.0f, kTestTolerance);
  EXPECT_NEAR(result.world_position.y, 0.0f, kTestTolerance);

  ASSERT_TRUE(
      harness.session.scene().set_locked(formal.first_occurrence, true));
  const quader::SceneSurfaceQuerySnapshot locked_snapshot =
      harness.session.scene_surface_query().snapshot();
  const quader::ViewportRay ray =
      camera.ray_from_viewport_point(screen.position);
  const std::optional<quader::SceneSurfaceHit> locked_occluder =
      locked_snapshot.nearest_hit({
          .world_origin = ray.origin,
          .world_direction = ray.direction,
          .eligibility = quader::SceneSurfaceEligibility::RenderVisible,
      });
  ASSERT_TRUE(locked_occluder.has_value());
  EXPECT_EQ(locked_occluder->participant.display_entity,
            formal.first_occurrence);
  EXPECT_FALSE(locked_snapshot
                   .nearest_hit({
                       .world_origin = ray.origin,
                       .world_direction = ray.direction,
                       .eligibility =
                           quader::SceneSurfaceEligibility::SelectableUnlocked,
                   })
                   .has_value());
  EXPECT_FALSE(harness.session.point_placement()
                   .resolve(harness.session.scene(), harness.session.assets(),
                            camera,
                            {.mouse_local = screen.position, .policy = policy})
                   .valid);
}

TEST(SceneSurfaceQueryTests,
     PivotLandmarksUseFormalOccurrenceTransformAndRejectLockedTargets) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh = harness.add_mesh(make_surface_mesh());
  ASSERT_NE(mesh, quader::kInvalidAssetId);
  const SceneSurfaceQueryHarness::FormalInstance formal =
      harness.add_formal_instance(mesh, false);
  ASSERT_TRUE(harness.session.scene().set_transform(
      formal.first_occurrence,
      quader::TransformComponent{.position = {4.0f, 0.0f, 1.0f}}));

  const quader::QVec3 occurrence_vertex{2.0f, 0.0f, -1.0f};
  quader::ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(quader::ViewportViewAxis::Y, 1);
  const quader::ViewportScreenPoint screen =
      camera.project_world_point(occurrence_vertex);
  ASSERT_TRUE(screen.visible);

  quader::ViewportPointPlacementPolicy policy =
      quader::ViewportPointPlacementPolicy::pivot({
          .vertex = true,
          .edge_midpoint = false,
          .face_center = false,
          .grid = false,
      });
  const auto resolve = [&]() {
    return harness.session.point_placement().resolve(
        harness.session.scene(), harness.session.assets(), camera,
        {.mouse_local = screen.position, .policy = policy});
  };

  const quader::ViewportPointPlacementResult visible = resolve();
  ASSERT_TRUE(visible.valid);
  EXPECT_EQ(visible.snap_kind, quader::ViewportPointSnapKind::Vertex);
  EXPECT_EQ(visible.entity, formal.first_occurrence);
  EXPECT_NEAR(visible.world_position.x, occurrence_vertex.x, kTestTolerance);
  EXPECT_NEAR(visible.world_position.y, occurrence_vertex.y, kTestTolerance);
  EXPECT_NEAR(visible.world_position.z, occurrence_vertex.z, kTestTolerance);

  ASSERT_TRUE(
      harness.session.scene().set_locked(formal.first_occurrence, true));
  const quader::SceneSurfaceQuerySnapshot locked =
      harness.session.scene_surface_query().snapshot();
  ASSERT_TRUE(std::ranges::any_of(
      locked.landmarks(), [&](const quader::SceneSurfaceLandmark &landmark) {
        return landmark.participant.display_entity ==
                   formal.first_occurrence &&
               landmark.effectively_locked;
      }));
  EXPECT_FALSE(resolve().valid)
      << "Locked formal geometry stays in render occlusion but cannot become "
         "a Pivot landmark target.";
}

TEST(SceneSurfaceQueryTests,
     LockedAndNonselectableFrontSurfacesOccludeEligiblePlacementTargets) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh = harness.add_mesh(make_surface_mesh());
  ASSERT_NE(mesh, quader::kInvalidAssetId);
  const quader::EntityId back =
      harness.add_mesh_entity(mesh, "Eligible Back Surface");
  const quader::EntityId front = harness.add_mesh_entity(
      mesh, "Locked Front Occluder", {0.0f, 1.0f, 0.0f});
  ASSERT_TRUE(harness.session.scene().set_locked(front, true));

  quader::ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(quader::ViewportViewAxis::Y, 1);
  const quader::ViewportScreenPoint screen =
      camera.project_world_point({0.0f, 1.0f, 0.0f});
  ASSERT_TRUE(screen.visible);
  quader::ViewportPointPlacementPolicy policy;
  policy.vertex = false;
  policy.edge_midpoint = false;
  policy.face_center = false;
  policy.surface = true;
  policy.grid = false;
  const auto resolve = [&]() {
    return harness.session.point_placement().resolve(
        harness.session.scene(), harness.session.assets(), camera,
        {.mouse_local = screen.position, .policy = policy});
  };

  const quader::ViewportRay ray =
      camera.ray_from_viewport_point(screen.position);
  const std::optional<quader::SceneSurfaceHit> visible_hit =
      harness.session.scene_surface_query().snapshot().nearest_hit({
          .world_origin = ray.origin,
          .world_direction = ray.direction,
      });
  ASSERT_TRUE(visible_hit.has_value());
  ASSERT_EQ(visible_hit->participant.display_entity, front);
  EXPECT_FALSE(resolve().valid);

  ASSERT_TRUE(harness.session.scene().set_locked(front, false));
  ASSERT_TRUE(harness.session.scene().set_selectable(front, false));
  EXPECT_FALSE(resolve().valid);

  ASSERT_TRUE(harness.session.scene().set_visible(front, false));
  const quader::ViewportPointPlacementResult revealed = resolve();
  ASSERT_TRUE(revealed.valid);
  EXPECT_EQ(revealed.entity, back);
}

TEST(SceneSurfaceQueryTests,
     RenderVisibleSurfaceOverloadOccludesCurveControlsBehindLockedMeshes) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh = harness.add_mesh(make_surface_mesh());
  ASSERT_NE(mesh, quader::kInvalidAssetId);
  const quader::EntityId occluder = harness.add_mesh_entity(
      mesh, "Locked Curve Occluder", {0.0f, 1.0f, 0.0f});
  ASSERT_TRUE(harness.session.scene().set_locked(occluder, true));

  quader::ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.view_axis(quader::ViewportViewAxis::Y, 1);
  const quader::ViewportScreenPoint screen =
      camera.project_world_point({0.0f, 0.0f, 0.0f});
  ASSERT_TRUE(screen.visible);
  const std::array<quader::modeling::CurvePointSpec, 2u> points{{
      {.position = {0.0f, 0.0f, 0.0f}},
      {.position = {1.0f, 0.0f, 0.0f}},
  }};
  quader::CurveToolPreviewState curve{
      .active = true,
      .document =
          quader::modeling::CurveDocument::make_bezier(points).or_throw(),
  };

  EXPECT_FALSE(quader::pick_unoccluded_viewport_curve_control(
                   camera, curve, screen.position,
                   harness.session.scene_surface_query().snapshot(), true)
                   .has_value());
  ASSERT_TRUE(harness.session.scene().set_visible(occluder, false));
  EXPECT_TRUE(quader::pick_unoccluded_viewport_curve_control(
                  camera, curve, screen.position,
                  harness.session.scene_surface_query().snapshot(), true)
                  .has_value());
}

} // namespace
