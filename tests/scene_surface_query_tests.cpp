////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/placement/snap_to_floor_service.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/project_session.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <gtest/gtest.h>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

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
  bool backface_culling_enabled = true;
  quader::SceneSurfaceQuery query;

  SceneSurfaceQueryHarness()
      : query(session.scene(), session.assets(),
              session.viewport_local_visibility(),
              [this] { return backface_culling_enabled; }) {}

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
          entity, quader::TransformComponent{.location = position}));
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
        quader::TransformComponent{.location = {-5.0f, 0.0f, 0.0f}}));
    if (add_second_occurrence) {
      result.second_occurrence = session.scene().create_entity(
          "Second Surface Occurrence", quader::EntityKind::Instance);
      EXPECT_TRUE(session.scene().set_instance_occurrence(
          result.second_occurrence,
          quader::InstanceOccurrenceComponent{.definition =
                                                  result.definition}));
      EXPECT_TRUE(session.scene().set_transform(
          result.second_occurrence,
          quader::TransformComponent{.location = {5.0f, 0.0f, 0.0f}}));
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
     RenderSidednessPreservesExplicitRayPoliciesForFrontAndBackFaces) {
  for (const bool double_sided : {false, true}) {
    SCOPED_TRACE(testing::Message() << "double_sided=" << double_sided);
    SceneSurfaceQueryHarness harness;
    const quader::AssetId mesh =
        harness.add_mesh(make_surface_mesh(0.0f, true));
    const quader::EntityId entity =
        harness.add_mesh_entity(mesh, "Sided Surface");
    ASSERT_TRUE(harness.session.scene().set_mesh(
        entity, {.mesh = mesh, .double_sided = !double_sided}));
    harness.backface_culling_enabled = !double_sided;
    const auto snapshot = harness.query.snapshot();
    ASSERT_EQ(snapshot.participants().size(), 1u);
    EXPECT_EQ(snapshot.participants().front().double_sided, double_sided);
    const auto geometry = snapshot.selection_depth_geometry();
    ASSERT_NE(geometry, nullptr);
    ASSERT_EQ(geometry->triangles.size(), 1u);

    for (const bool front_face : {false, true}) {
      SCOPED_TRACE(testing::Message() << "front_face=" << front_face);
      quader::SceneSurfaceRaycastRequest request{
          .world_origin = {0.0f, front_face ? 5.0f : -5.0f, 0.0f},
          .world_direction = {0.0f, front_face ? -1.0f : 1.0f, 0.0f},
      };

      // Placement and snapping retain their explicitly two-sided ray contract.
      const auto unrestricted = snapshot.nearest_hit(request);
      ASSERT_TRUE(unrestricted.has_value());
      EXPECT_EQ(unrestricted->participant.display_entity, entity);
      EXPECT_NEAR(unrestricted->distance, 5.0f, kTestTolerance);

      request.respect_render_sidedness = true;
      const auto rendered = snapshot.nearest_hit(request);
      EXPECT_EQ(rendered.has_value(), front_face || double_sided);
      const auto raster_candidate = snapshot.selection_depth_triangle_distance(
          1u, request.world_origin, request.world_direction);
      EXPECT_EQ(raster_candidate.has_value(), rendered.has_value())
          << "A GPU triangle ID must not turn a culled back face into an occluder.";
      if (rendered.has_value()) {
        EXPECT_EQ(rendered->participant.display_entity, entity);
        EXPECT_NEAR(rendered->distance, 5.0f, kTestTolerance);
        ASSERT_TRUE(raster_candidate.has_value());
        EXPECT_NEAR(*raster_candidate, 5.0f, kTestTolerance);
      }

      // An explicit front-face-only query remains stricter than double-sided
      // rendering, whether the optional render restriction is enabled or not.
      request.include_backfaces = false;
      EXPECT_EQ(snapshot.nearest_hit(request).has_value(), front_face);
      request.respect_render_sidedness = false;
      EXPECT_EQ(snapshot.nearest_hit(request).has_value(), front_face);
    }
  }
}

TEST(SceneSurfaceQueryTests,
     BackFacingSingleSidedRoomWallDoesNotHideFarRenderedSurface) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId room_mesh = harness.add_mesh(make_surface_mesh());
  const quader::AssetId target_mesh =
      harness.add_mesh(make_surface_mesh(0.0f, true));
  const quader::EntityId room = harness.add_mesh_entity(
      room_mesh, "Room Wall Seen From Inside", {0.0f, 2.0f, 0.0f});
  const quader::EntityId target =
      harness.add_mesh_entity(target_mesh, "Visible Target");
  const auto snapshot = harness.query.snapshot();
  const auto geometry = snapshot.selection_depth_geometry();
  ASSERT_NE(geometry, nullptr);
  ASSERT_EQ(geometry->triangles.size(), 2u);

  auto request = downward_ray({0.0f, 5.0f, 0.0f});
  const auto placement_hit = snapshot.nearest_hit(request);
  ASSERT_TRUE(placement_hit.has_value());
  EXPECT_EQ(placement_hit->participant.display_entity, room);
  EXPECT_NEAR(placement_hit->distance, 3.0f, kTestTolerance);

  request.respect_render_sidedness = true;
  const auto visible_hit = snapshot.nearest_hit(request);
  ASSERT_TRUE(visible_hit.has_value());
  EXPECT_EQ(visible_hit->participant.display_entity, target);
  EXPECT_NEAR(visible_hit->distance, 5.0f, kTestTolerance);
  EXPECT_FALSE(snapshot.selection_depth_triangle_distance(
      1u, request.world_origin, request.world_direction).has_value());
  const auto target_distance = snapshot.selection_depth_triangle_distance(
      2u, request.world_origin, request.world_direction);
  ASSERT_TRUE(target_distance.has_value());
  EXPECT_NEAR(*target_distance, 5.0f, kTestTolerance);
}

TEST(SceneSurfaceQueryTests,
     ReflectedOrdinaryMeshesAndFormalOccurrencesUseRendererWindingCorrection) {
  for (const bool formal_occurrence : {false, true}) {
    for (const bool reflected : {false, true}) {
      for (const bool double_sided : {false, true}) {
        SCOPED_TRACE(testing::Message()
                     << "formal_occurrence=" << formal_occurrence
                     << " reflected=" << reflected
                     << " double_sided=" << double_sided);
        SceneSurfaceQueryHarness harness;
        const auto mesh = harness.add_mesh(make_surface_mesh(0.0f, true));
        quader::EntityId display_entity = quader::kInvalidEntityId;
        quader::EntityId geometry_entity = quader::kInvalidEntityId;
        if (formal_occurrence) {
          const auto formal = harness.add_formal_instance(mesh, false);
          display_entity = formal.first_occurrence;
          geometry_entity = formal.member;
        } else {
          display_entity = harness.add_mesh_entity(mesh, "Reflected Surface");
          geometry_entity = display_entity;
        }
        ASSERT_TRUE(harness.session.scene().set_mesh(
            geometry_entity, {.mesh = mesh, .double_sided = !double_sided}));
        harness.backface_culling_enabled = !double_sided;
        ASSERT_TRUE(harness.session.scene().set_transform(
            display_entity,
            {.location = {3.0f, 1.0f, 0.0f},
             .scale = {reflected ? -2.0f : 2.0f, 1.0f, 0.5f}}));
        const auto snapshot = harness.query.snapshot();
        ASSERT_EQ(snapshot.participants().size(), 1u);
        EXPECT_EQ(snapshot.participants().front().id.display_entity,
                  display_entity);
        EXPECT_EQ(snapshot.participants().front().id.geometry_entity,
                  geometry_entity);
        EXPECT_EQ(snapshot.participants().front().double_sided, double_sided);
        ASSERT_EQ(snapshot.selection_depth_geometry()->triangles.size(), 1u);

        for (const bool from_above : {false, true}) {
          SCOPED_TRACE(testing::Message() << "from_above=" << from_above);
          const quader::SceneSurfaceRaycastRequest request{
              .world_origin = {3.0f, from_above ? 6.0f : -4.0f, 0.0f},
              .world_direction = {0.0f, from_above ? -1.0f : 1.0f, 0.0f},
              .respect_render_sidedness = true,
          };
          const bool rendered_face = double_sided || from_above;
          const auto hit = snapshot.nearest_hit(request);
          EXPECT_EQ(hit.has_value(), rendered_face);
          auto explicit_selection = request;
          explicit_selection.include_backfaces = false;
          explicit_selection.respect_render_sidedness = false;
          EXPECT_EQ(snapshot.nearest_hit(explicit_selection).has_value(),
                    from_above != reflected);
          const auto raster_candidate = snapshot.selection_depth_triangle_distance(
              1u, request.world_origin, request.world_direction);
          EXPECT_EQ(raster_candidate.has_value(), rendered_face);
          if (hit.has_value()) {
            EXPECT_EQ(hit->participant.display_entity, display_entity);
            EXPECT_EQ(hit->participant.geometry_entity, geometry_entity);
            EXPECT_NEAR(hit->distance, 5.0f, kTestTolerance);
          }
          if (raster_candidate.has_value()) {
            EXPECT_NEAR(*raster_candidate, 5.0f, kTestTolerance);
          }
        }
      }
    }
  }
}

TEST(SceneSurfaceQueryTests,
     GlobalCullingChangesFreezeOldQueriesInvalidateGpuIdentityAndReuseGeometry) {
  for (const bool formal_occurrence : {false, true}) {
    SCOPED_TRACE(testing::Message() << "formal_occurrence=" << formal_occurrence);
    SceneSurfaceQueryHarness harness;
    const auto mesh = harness.add_mesh(make_surface_mesh());
    quader::EntityId geometry_entity = quader::kInvalidEntityId;
    quader::QVec3 ray_origin{0.0f, 5.0f, 0.0f};
    if (formal_occurrence) {
      const auto formal = harness.add_formal_instance(mesh, false);
      geometry_entity = formal.member;
      ray_origin.x = -5.0f;
    } else {
      geometry_entity = harness.add_mesh_entity(mesh, "Changing Sidedness");
    }
    auto request = downward_ray(ray_origin);
    request.respect_render_sidedness = true;

    const auto single_sided = harness.query.snapshot();
    const auto single_sided_geometry = single_sided.selection_depth_geometry();
    const auto initial_stats = harness.query.stats();
    ASSERT_NE(single_sided_geometry, nullptr);
    ASSERT_EQ(single_sided_geometry->triangles.size(), 1u);
    EXPECT_FALSE(single_sided.nearest_hit(request).has_value());
    EXPECT_FALSE(single_sided.selection_depth_triangle_distance(
        1u, request.world_origin, request.world_direction).has_value());

    const auto scene_revision = harness.session.scene().revision();
    harness.backface_culling_enabled = false;
    const auto double_sided = harness.query.snapshot();
    const auto double_sided_geometry = double_sided.selection_depth_geometry();
    ASSERT_NE(double_sided_geometry, nullptr);
    EXPECT_NE(double_sided.revision(), single_sided.revision());
    EXPECT_NE(double_sided_geometry.get(), single_sided_geometry.get());
    EXPECT_TRUE(double_sided.nearest_hit(request).has_value());
    EXPECT_TRUE(double_sided.selection_depth_triangle_distance(
        1u, request.world_origin, request.world_direction).has_value());
    EXPECT_FALSE(single_sided.nearest_hit(request).has_value());
    EXPECT_FALSE(single_sided.selection_depth_triangle_distance(
        1u, request.world_origin, request.world_direction).has_value());
    EXPECT_EQ(harness.query.stats().blas_build_count, initial_stats.blas_build_count);

    harness.backface_culling_enabled = true;
    EXPECT_EQ(harness.session.scene().revision(), scene_revision);
    EXPECT_FALSE(harness.session.scene().mesh(geometry_entity)->double_sided);
    const auto restored = harness.query.snapshot();
    const auto restored_geometry = restored.selection_depth_geometry();
    EXPECT_NE(restored.revision(), double_sided.revision());
    EXPECT_NE(restored_geometry.get(), double_sided_geometry.get());
    EXPECT_FALSE(restored.nearest_hit(request).has_value());
    EXPECT_FALSE(restored.selection_depth_triangle_distance(
        1u, request.world_origin, request.world_direction).has_value());
    EXPECT_TRUE(double_sided.nearest_hit(request).has_value());
    EXPECT_TRUE(double_sided.selection_depth_triangle_distance(
        1u, request.world_origin, request.world_direction).has_value());

    const auto changed_stats = harness.query.stats();
    EXPECT_EQ(changed_stats.snapshot_build_count,
              initial_stats.snapshot_build_count + 2u);
    EXPECT_EQ(changed_stats.blas_build_count, initial_stats.blas_build_count);
    const auto unchanged = harness.query.snapshot();
    EXPECT_EQ(unchanged.revision(), restored.revision());
    EXPECT_EQ(unchanged.selection_depth_geometry().get(), restored_geometry.get());
    EXPECT_EQ(harness.query.stats().snapshot_build_count,
              changed_stats.snapshot_build_count);
    EXPECT_EQ(harness.query.stats().blas_build_count, changed_stats.blas_build_count);
  }
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
  EXPECT_EQ(with_occurrences.landmarks().size(), 28u);
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
  EXPECT_EQ(cold_stats.landmark_build_count, 0u);
  EXPECT_EQ(cold_stats.landmark_count, 0u);

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
      entity, quader::TransformComponent{.location = {0.0f, 3.0f, 0.0f}}));
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
  EXPECT_EQ(rebuilt_stats.landmark_build_count, 0u);
  EXPECT_EQ(rebuilt_stats.landmark_count, 0u);

  (void)harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().snapshot_build_count,
            rebuilt_stats.snapshot_build_count);
  EXPECT_EQ(harness.query.stats().tlas_build_count,
            rebuilt_stats.tlas_build_count);
  EXPECT_EQ(harness.query.stats().blas_build_count,
            rebuilt_stats.blas_build_count);
}

TEST(SceneSurfaceQueryTests,
     TransformChangesRetainLiveGeometryBeyondTheRecentlyHiddenCache) {
  SceneSurfaceQueryHarness harness;
  constexpr std::size_t kGeometryCount = 128u;
  quader::EntityId moving_entity = quader::kInvalidEntityId;
  quader::AssetId changed_mesh = quader::kInvalidAssetId;
  for (std::size_t index = 0u; index < kGeometryCount; ++index) {
    const quader::AssetId mesh =
        harness.add_mesh(make_surface_mesh(static_cast<float>(index)));
    const quader::EntityId entity = harness.add_mesh_entity(
        mesh, "Retained Surface", {static_cast<float>(index) * 5.0f, 0.0f, 0.0f});
    if (index == 0u) {
      moving_entity = entity;
      changed_mesh = mesh;
    }
  }
  const quader::SceneSurfaceQuerySnapshot initial = harness.query.snapshot();
  EXPECT_EQ(initial.participants().size(), kGeometryCount);
  ASSERT_EQ(harness.query.stats().unique_geometry_count, kGeometryCount);
  ASSERT_EQ(harness.query.stats().blas_build_count, kGeometryCount);
  for (std::size_t step = 1u; step <= 5u; ++step) {
    ASSERT_TRUE(harness.session.scene().set_transform(
        moving_entity, quader::TransformComponent{
                           .location = {0.0f, static_cast<float>(step), 0.0f}}));
    const auto moved = harness.query.snapshot();
    EXPECT_EQ(moved.participants().size(), kGeometryCount);
    EXPECT_EQ(harness.query.stats().blas_build_count, kGeometryCount);
    EXPECT_EQ(harness.query.stats().landmark_build_count, 0u);
  }
  ASSERT_TRUE(harness.session.assets().replace_mesh(
      changed_mesh, make_surface_mesh(static_cast<float>(kGeometryCount))));
  (void)harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().blas_build_count, kGeometryCount + 1u);
}

TEST(SceneSurfaceQueryTests,
     DelayedLandmarksKeepFrozenPayloadsAndTransformsAfterOwnerDestruction) {
  quader::SceneSurfaceQuerySnapshot original;
  quader::SceneSurfaceQuerySnapshot changed;
  quader::EntityId entity = quader::kInvalidEntityId;
  {
    SceneSurfaceQueryHarness harness;
    const quader::AssetId mesh = harness.add_mesh(make_surface_mesh());
    entity = harness.add_mesh_entity(mesh, "Frozen Landmarks", {0.0f, 1.0f, 0.0f});
    original = harness.query.snapshot();
    ASSERT_TRUE(harness.session.scene().set_transform(
        entity, quader::TransformComponent{.location = {0.0f, 3.0f, 0.0f}}));
    ASSERT_TRUE(harness.session.assets().replace_mesh(mesh, make_surface_mesh(2.0f)));
    changed = harness.query.snapshot();
    EXPECT_EQ(harness.query.stats().landmark_build_count, 0u);
    ASSERT_TRUE(harness.session.scene().destroy_entity(entity));
    harness.query.invalidate();
    EXPECT_EQ(harness.query.stats().landmark_count, 0u);
  }

  const auto original_landmarks = original.landmarks();
  const auto changed_landmarks = changed.landmarks();
  ASSERT_EQ(original_landmarks.size(), 7u);
  ASSERT_EQ(changed_landmarks.size(), original_landmarks.size());
  for (std::size_t index = 0u; index < original_landmarks.size(); ++index) {
    const auto &before = original_landmarks[index];
    const auto &after = changed_landmarks[index];
    EXPECT_EQ(before.participant.display_entity, entity);
    EXPECT_EQ(before.participant, after.participant);
    EXPECT_EQ(before.kind, after.kind);
    EXPECT_EQ(before.element_key, after.element_key);
    EXPECT_FLOAT_EQ(before.world_position.y, 1.0f);
    EXPECT_FLOAT_EQ(after.world_position.y, 5.0f);
    EXPECT_FLOAT_EQ(before.world_normal.y, -1.0f);
    EXPECT_FLOAT_EQ(after.world_normal.y, -1.0f);
    if (index < 3u) {
      EXPECT_EQ(before.kind, quader::SceneSurfaceLandmarkKind::Vertex);
      EXPECT_EQ(before.element_key, index);
    } else if (index < 6u) {
      EXPECT_EQ(before.kind, quader::SceneSurfaceLandmarkKind::EdgeMidpoint);
      if (index > 3u) {
        EXPECT_LT(original_landmarks[index - 1u].element_key, before.element_key);
      }
    } else {
      EXPECT_EQ(before.kind, quader::SceneSurfaceLandmarkKind::FaceCenter);
      EXPECT_EQ(before.element_key, 0u);
    }
  }
  EXPECT_EQ(original.landmarks().data(), original_landmarks.data());
  EXPECT_EQ(changed.landmarks().data(), changed_landmarks.data());
}

TEST(SceneSurfaceQueryTests, ConcurrentLandmarkReadersBuildOneFrozenResult) {
  SceneSurfaceQueryHarness harness;
  const quader::AssetId mesh = harness.add_mesh(make_surface_mesh());
  (void)harness.add_mesh_entity(mesh, "Concurrent Landmarks", {0.0f, 1.0f, 0.0f});
  const auto snapshot = harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().landmark_build_count, 0u);
  constexpr std::size_t kReaderCount = 8u;
  std::array<std::span<const quader::SceneSurfaceLandmark>, kReaderCount> results;
  std::barrier start(static_cast<std::ptrdiff_t>(kReaderCount + 1u));
  std::vector<std::jthread> readers;
  readers.reserve(kReaderCount);
  for (std::size_t index = 0u; index < kReaderCount; ++index) {
    readers.emplace_back([&, index] {
      start.arrive_and_wait();
      results[index] = snapshot.landmarks();
    });
  }
  start.arrive_and_wait();
  EXPECT_TRUE(harness.session.assets().replace_mesh(mesh, make_surface_mesh(4.0f)));
  readers.clear();
  for (const auto landmarks : results) {
    ASSERT_EQ(landmarks.size(), 7u);
    EXPECT_EQ(landmarks.data(), results.front().data());
    EXPECT_FLOAT_EQ(landmarks.front().world_position.y, 1.0f);
  }
  EXPECT_EQ(harness.query.stats().landmark_build_count, 1u);
  EXPECT_EQ(harness.query.stats().landmark_count, 7u);
  EXPECT_EQ(snapshot.landmarks().data(), results.front().data());
  EXPECT_EQ(harness.query.stats().landmark_build_count, 1u);
  (void)harness.query.snapshot();
  EXPECT_EQ(harness.query.stats().landmark_count, 0u);
  EXPECT_EQ(harness.query.stats().landmark_build_count, 1u);
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
      quader::TransformComponent{.location = {0.0f, 2.0f, 0.0f}}));
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
      quader::TransformComponent{.location = {1.0f, 0.0f, 0.0f}}));

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
      harness.session.scene().set_selectable(formal.first_occurrence, false));
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
                           quader::SceneSurfaceEligibility::EffectivelySelectable,
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
      quader::TransformComponent{.location = {4.0f, 0.0f, 1.0f}}));

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
      harness.session.scene().set_selectable(formal.first_occurrence, false));
  const quader::SceneSurfaceQuerySnapshot locked =
      harness.session.scene_surface_query().snapshot();
  ASSERT_TRUE(std::ranges::any_of(
      locked.landmarks(), [&](const quader::SceneSurfaceLandmark &landmark) {
        return landmark.participant.display_entity ==
                   formal.first_occurrence &&
               !landmark.effectively_selectable;
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
  ASSERT_TRUE(harness.session.scene().set_selectable(front, false));

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

  ASSERT_TRUE(harness.session.scene().set_selectable(front, true));
  ASSERT_TRUE(harness.session.scene().set_selectability(front, false));
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
  ASSERT_TRUE(harness.session.scene().set_selectable(occluder, false));

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

TEST(SceneSurfaceQueryTests, LargeSceneSingleMeshTransformSnapshotWorkload) {
  const char *source = std::getenv("QUADER_TEST_LARGE_SELECTION_PROJECT");
  if (source == nullptr || *source == '\0') {
    GTEST_SKIP() << "Set QUADER_TEST_LARGE_SELECTION_PROJECT to measure a cloned project.";
  }
  const std::filesystem::path source_project = std::filesystem::u8path(source);
  ASSERT_TRUE(std::filesystem::is_regular_file(source_project));
  const char *source_scene = std::getenv("QUADER_TEST_LARGE_SELECTION_SCENE");
  const std::filesystem::path original_scene =
      source_scene != nullptr && *source_scene != '\0'
          ? std::filesystem::u8path(source_scene)
          : source_project.parent_path() / "Scenes" / "one.qdr";
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path root(temporary.path().toStdWString());
  const std::filesystem::path project_root =
      root / source_project.parent_path().filename();
  std::error_code error;
  std::filesystem::copy(
      source_project.parent_path(), project_root,
      std::filesystem::copy_options::recursive |
          std::filesystem::copy_options::skip_symlinks,
      error);
  ASSERT_FALSE(error) << error.message();
  const std::filesystem::path relative_scene = std::filesystem::relative(
      original_scene, source_project.parent_path(), error);
  ASSERT_FALSE(error) << error.message();
  quader::ProjectSession project(root / "recent-scenes.json");
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                            "resources/modeling_assets");
  quader::QuaderEditorApi editor(project);
  const auto opened_project =
      editor.project().open(project_root / source_project.filename());
  ASSERT_TRUE(opened_project.ok()) << opened_project.error_message();
  const auto opened_scene =
      editor.scene_documents().open_native_path(project_root / relative_scene);
  ASSERT_TRUE(opened_scene.ok()) << opened_scene.error_message();
  const auto entities = editor.query().entities();
  const auto moving_entity = std::ranges::find_if(
      entities, [](const quader::QuaderEntitySnapshot &entity) {
        return entity.has_mesh && entity.visible && entity.selection_eligible &&
               entity.selectable && !entity.is_instance_definition;
      });
  ASSERT_NE(moving_entity, entities.end());
  auto &query = project.active_scene()->session->scene_surface_query();
  const auto elapsed_milliseconds = [](auto start) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
  };
  auto start = std::chrono::steady_clock::now();
  auto snapshot = query.snapshot();
  const double cold_snapshot_milliseconds = elapsed_milliseconds(start);
  ASSERT_FALSE(snapshot.participants().empty());
  const quader::SceneSurfaceQueryStats initial_stats = query.stats();
  std::cout << "SCENE_SURFACE_QUERY cold_snapshot_ms="
            << cold_snapshot_milliseconds
            << " participants=" << initial_stats.participant_count
            << " triangles=" << initial_stats.triangle_count
            << " unique_geometry=" << initial_stats.unique_geometry_count
            << " moving_entity=" << moving_entity->id << std::endl;

  constexpr std::size_t kTransformSteps = 10u;
  double snapshot_milliseconds = 0.0;
  double landmark_milliseconds = 0.0;
  for (std::size_t step = 0u; step < kTransformSteps; ++step) {
    quader::QuaderTransform transform = moving_entity->transform;
    transform.location.x += static_cast<float>(step + 1u) * 0.25f;
    const auto moved = editor.scene().set_transform(moving_entity->id, transform);
    ASSERT_TRUE(moved.ok()) << moved.error_message();
    ASSERT_TRUE(moved.changed());
    start = std::chrono::steady_clock::now();
    snapshot = query.snapshot();
    const double snapshot_step_milliseconds = elapsed_milliseconds(start);
    const quader::SceneSurfaceQueryStats query_stats = query.stats();
    start = std::chrono::steady_clock::now();
    const std::size_t landmark_count = snapshot.landmarks().size();
    const double landmark_step_milliseconds = elapsed_milliseconds(start);
    snapshot_milliseconds += snapshot_step_milliseconds;
    landmark_milliseconds += landmark_step_milliseconds;
    EXPECT_EQ(query_stats.participant_count, initial_stats.participant_count);
    EXPECT_EQ(query_stats.triangle_count, initial_stats.triangle_count);
    EXPECT_GT(landmark_count, 0u);
    std::cout << "SCENE_SURFACE_QUERY_STEP step=" << step
              << " snapshot_ms=" << snapshot_step_milliseconds
              << " landmarks_ms=" << landmark_step_milliseconds
              << " landmarks_before_access=" << query_stats.landmark_count
              << " landmarks=" << landmark_count
              << " landmark_builds=" << query.stats().landmark_build_count
              << " blas_builds=" << query_stats.blas_build_count << std::endl;
  }
  const quader::SceneSurfaceQueryStats final_stats = query.stats();
  EXPECT_EQ(final_stats.snapshot_build_count,
            initial_stats.snapshot_build_count + kTransformSteps);
  EXPECT_EQ(final_stats.blas_build_count, initial_stats.blas_build_count);
  EXPECT_EQ(final_stats.landmark_build_count,
            initial_stats.landmark_build_count + kTransformSteps);
  EXPECT_EQ(query.snapshot().revision(), snapshot.revision());
  std::cout << "SCENE_SURFACE_QUERY_RESULT steps=" << kTransformSteps
            << " snapshot_mean_ms=" << snapshot_milliseconds / kTransformSteps
            << " landmarks_mean_ms=" << landmark_milliseconds / kTransformSteps
            << " snapshot_builds=" << final_stats.snapshot_build_count
            << " landmark_builds=" << final_stats.landmark_build_count
            << " blas_builds=" << final_stats.blas_build_count << std::endl;
}

} // namespace
