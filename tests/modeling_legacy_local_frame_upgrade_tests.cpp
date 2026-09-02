////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "editor/modeling/services/modeling_legacy_local_frame_upgrade.hpp"
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/session/scene_session.hpp"
#include "math/glm_bridge.hpp"
#include "quader/modeling/neutral/polygon/document.hpp"
#include "scene/scene_document.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace quader {
namespace {

constexpr float kTestTolerance = 0.0002f;

bool near_point(QVec3 left, QVec3 right,
                float tolerance = kTestTolerance) {
  return length_squared(left - right) <= tolerance * tolerance;
}

void expect_matrix_near(const QMat4 &actual, const QMat4 &expected,
                        float tolerance = kTestTolerance) {
  for (std::size_t index = 0u; index < actual.values.size(); ++index) {
    EXPECT_NEAR(actual.values[index], expected.values[index], tolerance)
        << "matrix element " << index;
  }
}

struct SceneRevisionSnapshot final {
  std::uint64_t revision = 0u;
  std::uint64_t content = 0u;
  std::uint64_t selection = 0u;
  std::uint64_t hierarchy = 0u;
  std::uint64_t visibility = 0u;
  std::uint64_t transform = 0u;
  std::uint64_t texture_mapping = 0u;
  std::uint64_t mesh_binding = 0u;
  std::uint64_t curve_binding = 0u;
  std::uint64_t material_binding = 0u;
  std::uint64_t render_component = 0u;
  std::uint64_t layer = 0u;

  [[nodiscard]] bool operator==(
      const SceneRevisionSnapshot &) const = default;
};

SceneRevisionSnapshot scene_revisions(const SceneDocument &scene) {
  return {
      .revision = scene.revision(),
      .content = scene.content_revision(),
      .selection = scene.selection_revision(),
      .hierarchy = scene.hierarchy_presentation_revision(),
      .visibility = scene.visibility_revision(),
      .transform = scene.transform_revision(),
      .texture_mapping = scene.texture_mapping_revision(),
      .mesh_binding = scene.mesh_binding_revision(),
      .curve_binding = scene.curve_binding_revision(),
      .material_binding = scene.material_binding_revision(),
      .render_component = scene.render_component_revision(),
      .layer = scene.layer_revision(),
  };
}

struct AssetRevisionSnapshot final {
  std::uint64_t revision = 0u;
  std::uint64_t material_catalog = 0u;
  std::uint64_t mesh_material_usage = 0u;

  [[nodiscard]] bool operator==(
      const AssetRevisionSnapshot &) const = default;
};

AssetRevisionSnapshot asset_revisions(const AssetRegistry &assets) {
  return {
      .revision = assets.revision(),
      .material_catalog = assets.material_catalog_revision(),
      .mesh_material_usage = assets.mesh_material_usage_revision(),
  };
}

void expect_transform_exact(const TransformComponent &actual,
                            const TransformComponent &expected) {
  EXPECT_EQ(actual.position.x, expected.position.x);
  EXPECT_EQ(actual.position.y, expected.position.y);
  EXPECT_EQ(actual.position.z, expected.position.z);
  EXPECT_EQ(actual.rotation_euler.x, expected.rotation_euler.x);
  EXPECT_EQ(actual.rotation_euler.y, expected.rotation_euler.y);
  EXPECT_EQ(actual.rotation_euler.z, expected.rotation_euler.z);
  EXPECT_EQ(actual.scale.x, expected.scale.x);
  EXPECT_EQ(actual.scale.y, expected.scale.y);
  EXPECT_EQ(actual.scale.z, expected.scale.z);
  EXPECT_EQ(actual.custom_pivot, expected.custom_pivot);
}

void expect_modeling_object_exact(const ModelingObjectComponent &actual,
                                  const ModelingObjectComponent &expected) {
  EXPECT_EQ(actual.object_id, expected.object_id);
  EXPECT_EQ(actual.material_slot, expected.material_slot);
  EXPECT_EQ(actual.sdk_owned, expected.sdk_owned);
  EXPECT_EQ(actual.local_frame_version, expected.local_frame_version);
}

class ScopedLegacyUpgradeFailure final {
public:
  explicit ScopedLegacyUpgradeFailure(
      ModelingLegacyLocalFrameUpgradeFailurePoint point) {
    ModelingLegacyLocalFrameUpgradeService::set_failure_point_for_tests(point);
  }

  ~ScopedLegacyUpgradeFailure() {
    ModelingLegacyLocalFrameUpgradeService::set_failure_point_for_tests(
        ModelingLegacyLocalFrameUpgradeFailurePoint::None);
  }

  ScopedLegacyUpgradeFailure(const ScopedLegacyUpgradeFailure &) = delete;
  ScopedLegacyUpgradeFailure &
  operator=(const ScopedLegacyUpgradeFailure &) = delete;
};

quader_poly::Document baked_cube_document(QVec3 origin, QVec3 x_axis,
                                          QVec3 y_axis, float width,
                                          float height, float depth) {
  const QVec3 z_axis = normalize(cross(x_axis, y_axis));
  quader_poly::Document document = quader_poly::make_cube({
      .width = width,
      .height = height,
      .depth = depth,
      .width_segments = 1,
      .height_segments = 1,
      .depth_segments = 1,
      .flip_faces = false,
  });
  for (quader_poly::Vertex &vertex : document.vertices) {
    const QVec3 local = vertex.position;
    vertex.position = origin + x_axis * local.x + y_axis * local.y +
                      z_axis * local.z;
  }
  return document;
}

MeshAsset authored_mesh(const quader_poly::Document &document,
                        const char *name) {
  MeshAsset mesh =
      MeshProjectionService::compile_polygon_document_mesh(document);
  mesh.name = name;
  mesh.replace_authored_polygon_document(document);
  return mesh;
}

std::array<QVec3, 8> authored_world_positions(const SceneDocument &scene,
                                               const MeshAsset &mesh,
                                               EntityId entity) {
  std::array<QVec3, 8> result{};
  const QMat4 world = scene.world_transform_matrix(entity);
  for (std::size_t index = 0u; index < result.size(); ++index) {
    result[index] = transform_point(
        world, mesh.authored_polygon_document->vertices[index].position);
  }
  return result;
}

std::vector<std::vector<QVec2>> face_uvs(
    const quader_poly::Document &document) {
  std::vector<std::vector<QVec2>> result;
  result.reserve(document.faces.size());
  for (const quader_poly::Face &face : document.faces) {
    result.push_back(face.uvs);
  }
  return result;
}

TEST(ModelingLegacyLocalFrameUpgradeTests,
     RebasesBakedCubeWithoutChangingWorldGeometryTextureOrChildren) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingMeshSyncService mesh_sync;

  constexpr float kBakedAngle = 0.2617993878f;
  const QVec3 baked_x{std::cos(kBakedAngle), 0.0f,
                      -std::sin(kBakedAngle)};
  const QVec3 baked_y{0.0f, 1.0f, 0.0f};
  const quader_poly::Document baked = baked_cube_document(
      {3.0f, 1.5f, -2.0f}, baked_x, baked_y, 4.0f, 2.0f, 6.0f);
  ASSERT_TRUE(std::ranges::all_of(
      baked.faces,
      [](const quader_poly::Face &face) { return face.uvs.empty(); }));

  quader_poly::Document generated_uv_document = baked;
  quader_poly::ensure_face_uvs(generated_uv_document);
  const std::vector<std::vector<QVec2>> generated_uvs =
      face_uvs(generated_uv_document);

  const AssetId baked_mesh =
      assets.add_mesh(authored_mesh(baked, "Legacy Baked Box"));
  ASSERT_NE(baked_mesh, kInvalidAssetId);

  const EntityId target =
      scene.create_entity("Legacy Box", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_transform(
      target, TransformComponent{
                  .position = {-1.0f, 0.5f, 4.0f},
                  .rotation_euler = {0.0f, -kBakedAngle, 0.0f},
                  .scale = {1.0f, 1.0f, 1.0f},
              }));
  ASSERT_TRUE(scene.set_mesh(target, MeshComponent{.mesh = baked_mesh}));
  ASSERT_TRUE(scene.set_modeling_object(
      target, ModelingObjectComponent{
                  .object_id = 19,
                  .sdk_owned = true,
                  .local_frame_version =
                      kLegacyUnknownModelingObjectLocalFrameVersion,
              }));

  const EntityId child = scene.create_empty("Child");
  ASSERT_TRUE(scene.set_transform(
      child, TransformComponent{
                 .position = {0.5f, 2.0f, -1.0f},
                 .rotation_euler = {0.2f, -0.1f, 0.3f},
                 .scale = {1.0f, 1.0f, 1.0f},
                 .custom_pivot = true,
             }));
  ASSERT_TRUE(scene.set_parent(child, target, false));

  // Force clone-on-write and prove that migration cannot rotate an unrelated
  // consumer of the same immutable authored mesh.
  const EntityId passive =
      scene.create_entity("Passive Mesh Consumer", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(passive, MeshComponent{.mesh = baked_mesh}));

  const MeshAsset *mesh_before = assets.mesh(baked_mesh);
  ASSERT_NE(mesh_before, nullptr);
  const std::array<QVec3, 8> world_before =
      authored_world_positions(scene, *mesh_before, target);
  const QMat4 child_world_before = scene.world_transform_matrix(child);
  const std::array<QVec3, 8> original_authored_positions = [&] {
    std::array<QVec3, 8> positions{};
    for (std::size_t index = 0u; index < positions.size(); ++index) {
      positions[index] =
          mesh_before->authored_polygon_document->vertices[index].position;
    }
    return positions;
  }();

  const ModelingLegacyLocalFrameUpgradeStats stats =
      ModelingLegacyLocalFrameUpgradeService::upgrade_scene(scene, assets,
                                                             mesh_sync);
  EXPECT_EQ(stats.legacy_candidate_count, 1u);
  EXPECT_EQ(stats.upgraded_object_count, 1u);
  EXPECT_EQ(stats.cloned_mesh_asset_count, 1u);
  EXPECT_EQ(stats.failed_object_count, 0u);

  const ModelingObjectComponent *modeling = scene.modeling_object(target);
  ASSERT_NE(modeling, nullptr);
  EXPECT_EQ(modeling->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);
  const MeshComponent *target_component = scene.mesh(target);
  const MeshComponent *passive_component = scene.mesh(passive);
  ASSERT_NE(target_component, nullptr);
  ASSERT_NE(passive_component, nullptr);
  EXPECT_NE(target_component->mesh, baked_mesh);
  EXPECT_EQ(passive_component->mesh, baked_mesh);

  const MeshAsset *canonical_mesh = assets.mesh(target_component->mesh);
  const MeshAsset *untouched_shared_mesh = assets.mesh(baked_mesh);
  ASSERT_NE(canonical_mesh, nullptr);
  ASSERT_NE(canonical_mesh->authored_polygon_document, nullptr);
  ASSERT_NE(untouched_shared_mesh, nullptr);
  ASSERT_NE(untouched_shared_mesh->authored_polygon_document, nullptr);

  const quader_poly::Document replay = quader_poly::make_cube({
      .width = 4.0f,
      .height = 2.0f,
      .depth = 6.0f,
      .width_segments = 1,
      .height_segments = 1,
      .depth_segments = 1,
      .flip_faces = false,
  });
  for (std::size_t index = 0u; index < replay.vertices.size(); ++index) {
    EXPECT_TRUE(near_point(
        canonical_mesh->authored_polygon_document->vertices[index].position,
        replay.vertices[index].position));
    EXPECT_TRUE(near_point(
        untouched_shared_mesh->authored_polygon_document->vertices[index]
            .position,
        original_authored_positions[index]));
  }

  const std::array<QVec3, 8> world_after =
      authored_world_positions(scene, *canonical_mesh, target);
  for (std::size_t index = 0u; index < world_before.size(); ++index) {
    EXPECT_TRUE(near_point(world_after[index], world_before[index]));
  }
  expect_matrix_near(scene.world_transform_matrix(child), child_world_before);
  ASSERT_NE(scene.transform(child), nullptr);
  EXPECT_TRUE(scene.transform(child)->custom_pivot);

  const std::vector<std::vector<QVec2>> persisted_uvs =
      face_uvs(*canonical_mesh->authored_polygon_document);
  ASSERT_EQ(persisted_uvs.size(), generated_uvs.size());
  for (std::size_t face = 0u; face < generated_uvs.size(); ++face) {
    ASSERT_EQ(persisted_uvs[face].size(), generated_uvs[face].size());
    for (std::size_t corner = 0u; corner < generated_uvs[face].size();
         ++corner) {
      EXPECT_FLOAT_EQ(persisted_uvs[face][corner].x,
                      generated_uvs[face][corner].x);
      EXPECT_FLOAT_EQ(persisted_uvs[face][corner].y,
                      generated_uvs[face][corner].y);
    }
  }

  // The local gizmo X axis is now the same world direction as the visible
  // vertex-1 -> vertex-2 box edge.
  const QVec3 visible_edge = normalize(world_after[1] - world_after[0]);
  const QVec3 local_gizmo_x = normalize(transform_vector(
      scene.world_transform_matrix(target), {1.0f, 0.0f, 0.0f}));
  EXPECT_TRUE(near_point(visible_edge, local_gizmo_x));

  const std::size_t mesh_count_after_upgrade = assets.mesh_count();
  const ModelingLegacyLocalFrameUpgradeStats repeated =
      ModelingLegacyLocalFrameUpgradeService::upgrade_scene(scene, assets,
                                                             mesh_sync);
  EXPECT_EQ(repeated.legacy_candidate_count, 0u);
  EXPECT_EQ(repeated.upgraded_object_count, 0u);
  EXPECT_EQ(assets.mesh_count(), mesh_count_after_upgrade);
}

TEST(ModelingLegacyLocalFrameUpgradeTests,
     LeavesADeformedCubeTopologyUnknownAndUnchanged) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingMeshSyncService mesh_sync;

  quader_poly::Document arbitrary = quader_poly::make_cube({});
  arbitrary.vertices[2].position += QVec3{0.3f, 0.1f, -0.2f};
  const AssetId mesh_id =
      assets.add_mesh(authored_mesh(arbitrary, "Arbitrary Mesh"));
  ASSERT_NE(mesh_id, kInvalidAssetId);
  const EntityId entity =
      scene.create_entity("Arbitrary Mesh", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(entity, MeshComponent{.mesh = mesh_id}));
  ASSERT_TRUE(scene.set_modeling_object(
      entity, ModelingObjectComponent{
                  .object_id = 7,
                  .sdk_owned = true,
                  .local_frame_version =
                      kLegacyUnknownModelingObjectLocalFrameVersion,
              }));

  const QMat4 world_before = scene.world_transform_matrix(entity);
  const MeshAsset *before = assets.mesh(mesh_id);
  ASSERT_NE(before, nullptr);
  const std::array<QVec3, 8> positions_before = [&] {
    std::array<QVec3, 8> positions{};
    for (std::size_t index = 0u; index < positions.size(); ++index) {
      positions[index] =
          before->authored_polygon_document->vertices[index].position;
    }
    return positions;
  }();

  const ModelingLegacyLocalFrameUpgradeStats stats =
      ModelingLegacyLocalFrameUpgradeService::upgrade_scene(scene, assets,
                                                             mesh_sync);
  EXPECT_EQ(stats.legacy_candidate_count, 1u);
  EXPECT_EQ(stats.unrecognized_object_count, 1u);
  EXPECT_EQ(stats.upgraded_object_count, 0u);
  EXPECT_EQ(stats.failed_object_count, 0u);

  const ModelingObjectComponent *modeling = scene.modeling_object(entity);
  ASSERT_NE(modeling, nullptr);
  EXPECT_EQ(modeling->local_frame_version,
            kLegacyUnknownModelingObjectLocalFrameVersion);
  ASSERT_NE(scene.mesh(entity), nullptr);
  EXPECT_EQ(scene.mesh(entity)->mesh, mesh_id);
  expect_matrix_near(scene.world_transform_matrix(entity), world_before);
  const MeshAsset *after = assets.mesh(mesh_id);
  ASSERT_NE(after, nullptr);
  for (std::size_t index = 0u; index < positions_before.size(); ++index) {
    EXPECT_TRUE(near_point(
        after->authored_polygon_document->vertices[index].position,
        positions_before[index]));
  }
}

TEST(ModelingLegacyLocalFrameUpgradeTests,
     SequentialSharedCandidatesCloneOnlyWhileTheAssetIsShared) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingMeshSyncService mesh_sync;

  constexpr float kBakedAngle = 0.2617993878f;
  const QVec3 baked_x{std::cos(kBakedAngle), 0.0f,
                      -std::sin(kBakedAngle)};
  const quader_poly::Document baked = baked_cube_document(
      {3.0f, 1.5f, -2.0f}, baked_x, {0.0f, 1.0f, 0.0f}, 4.0f,
      2.0f, 6.0f);
  const AssetId baked_mesh =
      assets.add_mesh(authored_mesh(baked, "Sequential Legacy Boxes"));
  ASSERT_NE(baked_mesh, kInvalidAssetId);

  std::array<EntityId, 2> targets{};
  for (std::size_t index = 0u; index < targets.size(); ++index) {
    targets[index] =
        scene.create_entity("Sequential Legacy Box", EntityKind::Mesh);
    ASSERT_NE(targets[index], kInvalidEntityId);
    ASSERT_TRUE(scene.set_transform(
        targets[index],
        TransformComponent{
            .position = {static_cast<float>(index) * 8.0f, 0.0f, 0.0f},
            .rotation_euler = {0.0f, -kBakedAngle, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
        }));
    ASSERT_TRUE(scene.set_mesh(targets[index], {.mesh = baked_mesh}));
    ASSERT_TRUE(scene.set_modeling_object(
        targets[index],
        ModelingObjectComponent{
            .object_id = 101 + static_cast<int>(index),
            .sdk_owned = true,
            .local_frame_version =
                kLegacyUnknownModelingObjectLocalFrameVersion,
        }));
  }
  ASSERT_EQ(scene.mesh_binding_reference_count(baked_mesh), 2u);

  const ModelingLegacyLocalFrameUpgradeStats stats =
      ModelingLegacyLocalFrameUpgradeService::upgrade_scene(scene, assets,
                                                             mesh_sync);

  EXPECT_EQ(stats.legacy_candidate_count, 2u);
  EXPECT_EQ(stats.upgraded_object_count, 2u);
  EXPECT_EQ(stats.cloned_mesh_asset_count, 1u);
  EXPECT_EQ(stats.failed_object_count, 0u);
  EXPECT_EQ(assets.mesh_count(), 2u);

  std::array<AssetId, 2> upgraded_meshes{};
  const quader_poly::Document canonical = quader_poly::make_cube({
      .width = 4.0f,
      .height = 2.0f,
      .depth = 6.0f,
  });
  for (std::size_t index = 0u; index < targets.size(); ++index) {
    const MeshComponent *mesh_component = scene.mesh(targets[index]);
    const ModelingObjectComponent *modeling =
        scene.modeling_object(targets[index]);
    ASSERT_NE(mesh_component, nullptr);
    ASSERT_NE(modeling, nullptr);
    upgraded_meshes[index] = mesh_component->mesh;
    EXPECT_EQ(modeling->local_frame_version,
              kCanonicalModelingObjectLocalFrameVersion);
    EXPECT_EQ(scene.mesh_binding_reference_count(mesh_component->mesh), 1u);

    const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
    ASSERT_NE(mesh, nullptr);
    ASSERT_NE(mesh->authored_polygon_document, nullptr);
    ASSERT_EQ(mesh->authored_polygon_document->vertices.size(),
              canonical.vertices.size());
    for (std::size_t vertex = 0u; vertex < canonical.vertices.size();
         ++vertex) {
      EXPECT_TRUE(near_point(
          mesh->authored_polygon_document->vertices[vertex].position,
          canonical.vertices[vertex].position));
    }
  }
  EXPECT_NE(upgraded_meshes[0], upgraded_meshes[1]);
  EXPECT_TRUE(upgraded_meshes[0] == baked_mesh ||
              upgraded_meshes[1] == baked_mesh);
}

TEST(ModelingLegacyLocalFrameUpgradeTests,
     MarksAnAlreadyCanonicalLegacyCubeWithoutReplacingItsMesh) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingMeshSyncService mesh_sync;

  const quader_poly::Document canonical = quader_poly::make_cube({});
  const AssetId mesh_id =
      assets.add_mesh(authored_mesh(canonical, "Canonical Legacy Box"));
  ASSERT_NE(mesh_id, kInvalidAssetId);
  const EntityId entity =
      scene.create_entity("Canonical Legacy Box", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(entity, MeshComponent{.mesh = mesh_id}));
  ASSERT_TRUE(scene.set_modeling_object(
      entity, ModelingObjectComponent{
                  .object_id = 8,
                  .sdk_owned = false,
                  .local_frame_version =
                      kLegacyUnknownModelingObjectLocalFrameVersion,
              }));

  const std::size_t mesh_count_before = assets.mesh_count();
  const ModelingLegacyLocalFrameUpgradeStats stats =
      ModelingLegacyLocalFrameUpgradeService::upgrade_scene(scene, assets,
                                                             mesh_sync);

  EXPECT_EQ(stats.upgraded_object_count, 1u);
  EXPECT_EQ(stats.marker_only_object_count, 1u);
  EXPECT_EQ(stats.cloned_mesh_asset_count, 0u);
  EXPECT_EQ(assets.mesh_count(), mesh_count_before);
  ASSERT_NE(scene.mesh(entity), nullptr);
  EXPECT_EQ(scene.mesh(entity)->mesh, mesh_id);
  ASSERT_NE(scene.modeling_object(entity), nullptr);
  EXPECT_EQ(scene.modeling_object(entity)->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);
}

TEST(ModelingLegacyLocalFrameUpgradeTests,
     PublicationFailuresRestoreExactNativeOwnerState) {
  constexpr std::array kFailurePoints{
      ModelingLegacyLocalFrameUpgradeFailurePoint::AfterMeshPublication,
      ModelingLegacyLocalFrameUpgradeFailurePoint::AfterTransformPublication,
      ModelingLegacyLocalFrameUpgradeFailurePoint::
          AfterModelingMarkerPublication,
  };

  for (const ModelingLegacyLocalFrameUpgradeFailurePoint failure_point :
       kFailurePoints) {
    SCOPED_TRACE(static_cast<int>(failure_point));
    SceneDocument scene;
    AssetRegistry assets;
    ModelingMeshSyncService mesh_sync;

    constexpr float kBakedAngle = 0.2617993878f;
    const QVec3 baked_x{std::cos(kBakedAngle), 0.0f,
                        -std::sin(kBakedAngle)};
    const quader_poly::Document baked = baked_cube_document(
        {3.0f, 1.5f, -2.0f}, baked_x, {0.0f, 1.0f, 0.0f}, 4.0f,
        2.0f, 6.0f);
    const AssetId baked_mesh =
        assets.add_mesh(authored_mesh(baked, "Atomic Legacy Box"));
    ASSERT_NE(baked_mesh, kInvalidAssetId);

    const EntityId target =
        scene.create_entity("Atomic Legacy Box", EntityKind::Mesh);
    const TransformComponent target_transform{
        .position = {-1.0f, 0.5f, 4.0f},
        .rotation_euler = {0.0f, -kBakedAngle, 0.0f},
        .scale = {1.0f, 1.0f, 1.0f},
    };
    const MeshComponent target_mesh{
        .mesh = baked_mesh,
        .editor_only = true,
        .cast_shadows = false,
        .double_sided = true,
        .generate_collisions = false,
        .collision_type = MeshCollisionType::ConvexHull,
        .display_collision = true,
        .body_type = MeshPhysicsBodyType::RigidBody,
    };
    const ModelingObjectComponent target_modeling{
        .object_id = 91,
        .material_slot = 7u,
        .sdk_owned = false,
        .local_frame_version =
            kLegacyUnknownModelingObjectLocalFrameVersion,
    };
    ASSERT_TRUE(scene.set_transform(target, target_transform));
    ASSERT_TRUE(scene.set_mesh(target, target_mesh));
    ASSERT_TRUE(scene.set_modeling_object(target, target_modeling));

    const EntityId child = scene.create_empty("Atomic Child");
    const TransformComponent child_transform{
        .position = {0.5f, 2.0f, -1.0f},
        .rotation_euler = {0.2f, -0.1f, 0.3f},
        .scale = {1.25f, 0.75f, 1.5f},
        .custom_pivot = true,
    };
    ASSERT_TRUE(scene.set_transform(child, child_transform));
    ASSERT_TRUE(scene.set_parent(child, target, false));

    const EntityId passive =
        scene.create_entity("Atomic Passive Consumer", EntityKind::Mesh);
    const MeshComponent passive_mesh{
        .mesh = baked_mesh,
        .editor_only = false,
        .cast_shadows = true,
        .double_sided = false,
        .generate_collisions = true,
        .collision_type = MeshCollisionType::Sphere,
        .display_collision = false,
        .body_type = MeshPhysicsBodyType::StaticBody,
    };
    ASSERT_TRUE(scene.set_mesh(passive, passive_mesh));

    const std::vector<EntityId> entities_before = scene.entities();
    const std::vector<EntityId> tree_order_before = scene.entity_tree_order();
    const std::vector<AssetId> mesh_ids_before = assets.mesh_ids();
    const std::size_t baked_reference_count_before =
        scene.mesh_binding_reference_count(baked_mesh);
    ASSERT_EQ(baked_reference_count_before, 2u);
    const SceneRevisionSnapshot scene_revisions_before =
        scene_revisions(scene);
    const AssetRevisionSnapshot asset_revisions_before =
        asset_revisions(assets);
    const MeshAssetRevision mesh_revision_before =
        assets.mesh_revision(baked_mesh);
    const MeshAsset *asset_before_pointer = assets.mesh(baked_mesh);
    ASSERT_NE(asset_before_pointer, nullptr);
    const MeshAsset asset_before = clone_mesh_asset_payload(*asset_before_pointer);
    const QMat4 target_world_before = scene.world_transform_matrix(target);
    const QMat4 child_world_before = scene.world_transform_matrix(child);
    const EntityId expected_next_entity =
        *std::ranges::max_element(entities_before) + 1u;
    const EditorObjectId expected_next_editor_object = [&] {
      EditorObjectId result = kInvalidEditorObjectId;
      for (const EntityId entity : entities_before) {
        result = std::max(result, scene.editor_object_id(entity));
      }
      return result + 1u;
    }();
    const AssetId expected_next_asset =
        *std::ranges::max_element(mesh_ids_before) + 1u;

    const ScopedLegacyUpgradeFailure injected_failure(failure_point);
    const ModelingLegacyLocalFrameUpgradeStats stats =
        ModelingLegacyLocalFrameUpgradeService::upgrade_scene(
            scene, assets, mesh_sync);

    EXPECT_EQ(stats.legacy_candidate_count, 1u);
    EXPECT_EQ(stats.failed_object_count, 1u);
    EXPECT_EQ(stats.upgraded_object_count, 0u);
    EXPECT_EQ(stats.cloned_mesh_asset_count, 0u);
    EXPECT_EQ(scene.entities(), entities_before);
    EXPECT_EQ(scene.entity_tree_order(), tree_order_before);
    EXPECT_EQ(scene_revisions(scene), scene_revisions_before);
    EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);
    EXPECT_EQ(assets.mesh_count(), mesh_ids_before.size());
    EXPECT_EQ(asset_revisions(assets), asset_revisions_before);
    EXPECT_EQ(assets.mesh_revision(baked_mesh), mesh_revision_before);
    EXPECT_EQ(scene.mesh_binding_reference_count(baked_mesh),
              baked_reference_count_before);
    EXPECT_EQ(scene.mesh_binding_reference_count(expected_next_asset), 0u);

    const TransformComponent *target_transform_after =
        scene.transform(target);
    const TransformComponent *child_transform_after = scene.transform(child);
    const MeshComponent *target_mesh_after = scene.mesh(target);
    const MeshComponent *passive_mesh_after = scene.mesh(passive);
    const ModelingObjectComponent *target_modeling_after =
        scene.modeling_object(target);
    ASSERT_NE(target_transform_after, nullptr);
    ASSERT_NE(child_transform_after, nullptr);
    ASSERT_NE(target_mesh_after, nullptr);
    ASSERT_NE(passive_mesh_after, nullptr);
    ASSERT_NE(target_modeling_after, nullptr);
    expect_transform_exact(*target_transform_after, target_transform);
    expect_transform_exact(*child_transform_after, child_transform);
    EXPECT_EQ(*target_mesh_after, target_mesh);
    EXPECT_EQ(*passive_mesh_after, passive_mesh);
    expect_modeling_object_exact(*target_modeling_after, target_modeling);
    EXPECT_EQ(scene.parent(child), target);
    EXPECT_EQ(scene.parent(target), kInvalidEntityId);
    EXPECT_EQ(scene.parent(passive), kInvalidEntityId);
    EXPECT_EQ(scene.world_transform_matrix(target).values,
              target_world_before.values);
    EXPECT_EQ(scene.world_transform_matrix(child).values,
              child_world_before.values);

    const MeshAsset *asset_after = assets.mesh(baked_mesh);
    ASSERT_NE(asset_after, nullptr);
    EXPECT_TRUE(mesh_asset_payload_equal(*asset_after, asset_before));

    const EntityId entity_probe = scene.create_empty("Identity Probe");
    EXPECT_EQ(entity_probe, expected_next_entity);
    EXPECT_EQ(scene.editor_object_id(entity_probe),
              expected_next_editor_object);
    const AssetId asset_probe = assets.add_mesh(
        authored_mesh(quader_poly::make_cube({}), "Identity Probe"));
    EXPECT_EQ(asset_probe, expected_next_asset);
    EXPECT_EQ(scene.mesh_binding_reference_count(asset_probe), 0u);
  }
}

TEST(ModelingLegacyLocalFrameUpgradeTests,
     InPlaceMeshPublicationFailureRestoresReplacedPayload) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingMeshSyncService mesh_sync;

  constexpr float kBakedAngle = 0.2617993878f;
  const QVec3 baked_x{std::cos(kBakedAngle), 0.0f,
                      -std::sin(kBakedAngle)};
  const quader_poly::Document baked = baked_cube_document(
      {3.0f, 1.5f, -2.0f}, baked_x, {0.0f, 1.0f, 0.0f}, 4.0f,
      2.0f, 6.0f);
  const AssetId mesh_id =
      assets.add_mesh(authored_mesh(baked, "In-place Atomic Box"));
  ASSERT_NE(mesh_id, kInvalidAssetId);
  const EntityId target =
      scene.create_entity("In-place Atomic Box", EntityKind::Mesh);
  const TransformComponent transform_before{
      .position = {-1.0f, 0.5f, 4.0f},
      .rotation_euler = {0.0f, -kBakedAngle, 0.0f},
      .scale = {1.0f, 1.0f, 1.0f},
  };
  const ModelingObjectComponent modeling_before{
      .object_id = 92,
      .material_slot = 4u,
      .sdk_owned = false,
      .local_frame_version =
          kLegacyUnknownModelingObjectLocalFrameVersion,
  };
  ASSERT_TRUE(scene.set_transform(target, transform_before));
  ASSERT_TRUE(scene.set_mesh(target, {.mesh = mesh_id}));
  ASSERT_TRUE(scene.set_modeling_object(target, modeling_before));

  const MeshAsset *asset_before_pointer = assets.mesh(mesh_id);
  ASSERT_NE(asset_before_pointer, nullptr);
  const MeshAsset asset_before = clone_mesh_asset_payload(*asset_before_pointer);
  const MeshAssetRevision mesh_revision_before =
      assets.mesh_revision(mesh_id);
  const AssetRevisionSnapshot asset_revisions_before =
      asset_revisions(assets);
  const SceneRevisionSnapshot scene_revisions_before =
      scene_revisions(scene);
  const std::size_t mesh_reference_count_before =
      scene.mesh_binding_reference_count(mesh_id);
  ASSERT_EQ(mesh_reference_count_before, 1u);

  const ScopedLegacyUpgradeFailure injected_failure(
      ModelingLegacyLocalFrameUpgradeFailurePoint::AfterMeshPublication);
  const ModelingLegacyLocalFrameUpgradeStats stats =
      ModelingLegacyLocalFrameUpgradeService::upgrade_scene(scene, assets,
                                                             mesh_sync);

  EXPECT_EQ(stats.failed_object_count, 1u);
  EXPECT_EQ(stats.upgraded_object_count, 0u);
  ASSERT_NE(assets.mesh(mesh_id), nullptr);
  EXPECT_TRUE(mesh_asset_payload_equal(*assets.mesh(mesh_id), asset_before));
  EXPECT_EQ(assets.mesh_revision(mesh_id), mesh_revision_before);
  EXPECT_EQ(asset_revisions(assets), asset_revisions_before);
  EXPECT_EQ(scene_revisions(scene), scene_revisions_before);
  EXPECT_EQ(scene.mesh_binding_reference_count(mesh_id),
            mesh_reference_count_before);
  ASSERT_NE(scene.mesh(target), nullptr);
  EXPECT_EQ(scene.mesh(target)->mesh, mesh_id);
  ASSERT_NE(scene.transform(target), nullptr);
  expect_transform_exact(*scene.transform(target), transform_before);
  ASSERT_NE(scene.modeling_object(target), nullptr);
  expect_modeling_object_exact(*scene.modeling_object(target),
                               modeling_before);
}

TEST(ModelingLegacyLocalFrameUpgradeTests,
     SceneSessionInstallUpgradesLegacyNonSdkCubeBeforeFirstHydration) {
  SceneContentDocument document;
  SceneDocument &scene = document.scene();
  AssetRegistry &assets = document.assets();

  constexpr float kBakedAngle = 0.2617993878f;
  const QVec3 baked_x{std::cos(kBakedAngle), 0.0f,
                      -std::sin(kBakedAngle)};
  const quader_poly::Document baked = baked_cube_document(
      {-16.0f, 0.25f, -17.25f}, baked_x, {0.0f, 1.0f, 0.0f}, 4.0f,
      2.0f, 6.0f);
  const AssetId mesh_id =
      assets.add_mesh(authored_mesh(baked, "Loaded Legacy Box"));
  ASSERT_NE(mesh_id, kInvalidAssetId);

  const EntityId entity =
      scene.create_entity("Loaded Legacy Box", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_transform(
      entity, TransformComponent{
                  .rotation_euler = {0.0f, -kBakedAngle, 0.0f},
              }));
  ASSERT_TRUE(scene.set_mesh(entity, MeshComponent{.mesh = mesh_id}));
  ASSERT_TRUE(scene.set_modeling_object(
      entity, ModelingObjectComponent{
                  .object_id = 19,
                  .sdk_owned = false,
                  .local_frame_version =
                      kLegacyUnknownModelingObjectLocalFrameVersion,
              }));

  const MeshAsset *mesh_before = assets.mesh(mesh_id);
  ASSERT_NE(mesh_before, nullptr);
  const std::array<QVec3, 8> world_before =
      authored_world_positions(scene, *mesh_before, entity);

  SceneSession session(std::move(document));

  const ModelingObjectComponent *modeling =
      session.scene().modeling_object(entity);
  ASSERT_NE(modeling, nullptr);
  EXPECT_TRUE(modeling->sdk_owned);
  EXPECT_EQ(modeling->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);
  ASSERT_EQ(session.modeling().objects().size(), 1u);
  EXPECT_FALSE(session.dirty());
  EXPECT_FALSE(session.history().can_undo());

  const MeshComponent *mesh_component = session.scene().mesh(entity);
  ASSERT_NE(mesh_component, nullptr);
  EXPECT_EQ(mesh_component->mesh, mesh_id);
  const MeshAsset *mesh_after = session.assets().mesh(mesh_component->mesh);
  ASSERT_NE(mesh_after, nullptr);
  ASSERT_NE(mesh_after->authored_polygon_document, nullptr);
  const std::array<QVec3, 8> world_after =
      authored_world_positions(session.scene(), *mesh_after, entity);
  for (std::size_t index = 0u; index < world_before.size(); ++index) {
    EXPECT_TRUE(near_point(world_after[index], world_before[index]));
  }

  const QVec3 visible_edge = normalize(world_after[1] - world_after[0]);
  QVec3 visible_center{};
  for (const QVec3 point : world_after) {
    visible_center += point;
  }
  visible_center = visible_center / static_cast<float>(world_after.size());
  const QVec3 gizmo_origin = transform_point(
      session.scene().world_transform_matrix(entity), {});
  EXPECT_TRUE(near_point(visible_center, gizmo_origin));
  const QVec3 local_gizmo_x = normalize(transform_vector(
      session.scene().world_transform_matrix(entity), {1.0f, 0.0f, 0.0f}));
  EXPECT_TRUE(near_point(visible_edge, local_gizmo_x));

  const std::size_t mesh_count_after_install = session.assets().mesh_count();
  session.modeling().reset_from_scene();
  EXPECT_EQ(session.assets().mesh_count(), mesh_count_after_install);
  EXPECT_FALSE(session.dirty());
  EXPECT_FALSE(session.history().can_undo());
}

} // namespace
} // namespace quader
