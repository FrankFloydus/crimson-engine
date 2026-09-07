////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_types.hpp"
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/mesh_collision_geometry_payload.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "scene/scene_document.hpp"

#include <gtest/gtest.h>

#include <array>
#include <barrier>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace quader {
namespace {

[[nodiscard]] MeshGeometryPayload triangle_geometry() {
  MeshGeometryPayload geometry;
  geometry.vertices = {
      {.position = {0.0f, 0.0f, 0.0f}},
      {.position = {1.0f, 0.0f, 0.0f}},
      {.position = {0.0f, 1.0f, 0.0f}},
  };
  geometry.indices = {0u, 1u, 2u};
  return geometry;
}

void expect_position(QVec3 actual, QVec3 expected) {
  EXPECT_FLOAT_EQ(actual.x, expected.x);
  EXPECT_FLOAT_EQ(actual.y, expected.y);
  EXPECT_FLOAT_EQ(actual.z, expected.z);
}

TEST(MeshCollisionGeometryTests, SnapsCoordinatesWithGodotNegativeTieRounding) {
  MeshGeometryPayload geometry = triangle_geometry();
  constexpr float kStep = 0.0001f;
  geometry.vertices[0].position = {-kStep * 0.5f, kStep * 0.5f, kStep * 1.5f};
  geometry.vertices[1].position = {-kStep * 1.5f, 0.000149f, -0.000151f};
  geometry.vertices[2].position = {0.000049f, -0.000049f, 0.000151f};

  const auto collision = mesh_collision_geometry_payload(geometry);
  ASSERT_TRUE(collision->error_message.empty());
  ASSERT_EQ(collision->faces.size(), 3u);
  // The float representation of kStep * 1.5 is just below the exact tie.
  expect_position(collision->faces[0], {0.0f, kStep, kStep});
  expect_position(collision->faces[1], {-kStep, kStep, -kStep * 2.0f});
  expect_position(collision->faces[2], {0.0f, 0.0f, kStep * 2.0f});
}

TEST(MeshCollisionGeometryTests,
     UsesRenderedSpansInOrderAndRetainsDuplicateAndDegenerateTriangles) {
  MeshGeometryPayload geometry = triangle_geometry();
  geometry.indices = {0u, 1u, 2u, 2u, 1u, 0u, 0u, 0u, 1u};
  geometry.primitives = {
      {.index_offset = 3u, .index_count = 3u, .material_slot = 9u},
      {.index_offset = 6u, .index_count = 3u, .material_slot = 2u},
      {.index_offset = 3u, .index_count = 3u, .material_slot = 4u},
      {.index_offset = 100u, .index_count = 3u},
      {.index_offset = 0u, .index_count = 0u},
  };

  const auto collision = mesh_collision_geometry_payload(geometry);
  ASSERT_TRUE(collision->error_message.empty());
  ASSERT_EQ(collision->faces.size(), 9u);
  constexpr std::array kExpectedIndices{2u, 1u, 0u, 0u, 0u, 1u, 2u, 1u, 0u};
  for (std::size_t index = 0u; index < kExpectedIndices.size(); ++index) {
    expect_position(collision->faces[index],
                    geometry.vertices[kExpectedIndices[index]].position);
  }
}

TEST(MeshCollisionGeometryTests, FallsBackToAllIndicesLikeRenderedPrimitives) {
  MeshGeometryPayload geometry = triangle_geometry();
  geometry.primitives = {{.index_offset = 90u, .index_count = 3u}};
  const auto collision = mesh_collision_geometry_payload(geometry);
  ASSERT_TRUE(collision->error_message.empty());
  ASSERT_EQ(collision->faces.size(), 3u);
  for (std::size_t index = 0u; index < 3u; ++index) {
    expect_position(collision->faces[index], geometry.vertices[index].position);
  }
}

TEST(MeshCollisionGeometryTests, TriangleSpanCanStartAfterAnUnusedIndex) {
  MeshGeometryPayload geometry = triangle_geometry();
  geometry.indices = {999u, 2u, 1u, 0u};
  geometry.primitives = {{.index_offset = 1u, .index_count = 3u}};
  const auto collision = mesh_collision_geometry_payload(geometry);
  ASSERT_TRUE(collision->error_message.empty());
  ASSERT_EQ(collision->faces.size(), 3u);
  expect_position(collision->faces[0], geometry.vertices[2].position);
  expect_position(collision->faces[1], geometry.vertices[1].position);
  expect_position(collision->faces[2], geometry.vertices[0].position);
}

TEST(MeshCollisionGeometryTests, RetainsOpenConcaveSurfaceWithoutHullGeneration) {
  MeshGeometryPayload geometry;
  geometry.vertices = {
      {.position = {0.0f, 0.0f, 0.0f}},
      {.position = {2.0f, 0.0f, 0.0f}},
      {.position = {2.0f, 1.0f, 0.0f}},
      {.position = {1.0f, 1.0f, 0.0f}},
      {.position = {1.0f, 2.0f, 0.0f}},
      {.position = {0.0f, 2.0f, 0.0f}},
  };
  geometry.indices = {0u, 1u, 3u, 1u, 2u, 3u, 0u, 3u, 5u, 3u, 4u, 5u};
  const auto collision = mesh_collision_geometry_payload(geometry);
  ASSERT_TRUE(collision->error_message.empty());
  ASSERT_EQ(collision->faces.size(), geometry.indices.size());
  for (std::size_t index = 0u; index < geometry.indices.size(); ++index) {
    expect_position(collision->faces[index],
                    geometry.vertices[geometry.indices[index]].position);
  }
}

TEST(MeshCollisionGeometryTests, EmptyGeometryHasNoFacesAndCachesItsResult) {
  MeshGeometryPayload geometry;
  const auto collision = mesh_collision_geometry_payload(geometry);
  EXPECT_TRUE(collision->faces.empty());
  EXPECT_TRUE(collision->error_message.empty());
  EXPECT_NE(collision->content_id, 0u);
  EXPECT_EQ(mesh_collision_geometry_payload(geometry), collision);
}

TEST(MeshCollisionGeometryTests, InvalidTriangleInputRetainsOneEmptyErrorPayload) {
  MeshGeometryPayload geometry = triangle_geometry();
  geometry.indices.push_back(0u);
  auto collision = mesh_collision_geometry_payload(geometry);
  EXPECT_TRUE(collision->faces.empty());
  EXPECT_NE(collision->error_message.find("triangle list"), std::string::npos);
  EXPECT_EQ(mesh_collision_geometry_payload(geometry), collision);

  geometry = triangle_geometry();
  geometry.indices.back() = 100u;
  collision = mesh_collision_geometry_payload(geometry);
  EXPECT_TRUE(collision->faces.empty());
  EXPECT_NE(collision->error_message.find("out-of-range"), std::string::npos);

  for (const float invalid : {(std::numeric_limits<float>::infinity)(),
                              (std::numeric_limits<float>::quiet_NaN)()}) {
    geometry = triangle_geometry();
    geometry.vertices.back().position.z = invalid;
    collision = mesh_collision_geometry_payload(geometry);
    EXPECT_TRUE(collision->faces.empty());
    EXPECT_NE(collision->error_message.find("non-finite"), std::string::npos);
  }
}

TEST(MeshCollisionGeometryTests, IgnoresUnrenderedVertexAndIndexData) {
  MeshGeometryPayload geometry = triangle_geometry();
  geometry.vertices.push_back(
      {.position = {(std::numeric_limits<float>::quiet_NaN)(), 0.0f, 0.0f}});
  geometry.indices.insert(geometry.indices.end(), {999u, 3u, 999u});
  geometry.primitives = {{.index_offset = 0u, .index_count = 3u}};
  const auto collision = mesh_collision_geometry_payload(geometry);
  EXPECT_TRUE(collision->error_message.empty());
  EXPECT_EQ(collision->faces.size(), 3u);
}

TEST(MeshCollisionGeometryTests, ContentIdentityDependsOnlyOnSnappedFaces) {
  MeshGeometryPayload geometry = triangle_geometry();
  const auto first = mesh_collision_geometry_payload(geometry);
  MeshGeometryPayload changed = geometry;
  changed.vertices[0].position.x = 0.000001f;
  changed.vertices[0].normal = {0.0f, -1.0f, 0.0f};
  changed.vertices[0].uv = {4.0f, 8.0f};
  changed.primitives = {{.index_offset = 0u,
                         .index_count = 3u,
                         .material_slot = 99u}};
  EXPECT_EQ(mesh_collision_geometry_payload(changed)->content_id,
            first->content_id);
  changed.indices = {0u, 2u, 1u};
  changed.invalidate_content_id();
  EXPECT_NE(mesh_collision_geometry_payload(changed)->content_id,
            first->content_id);
}

TEST(MeshCollisionGeometryTests,
     SharedAssetGeometryReusesCollisionAndEditingPreservesRetainedFaces) {
  MeshAsset mesh;
  mesh.replace_geometry(triangle_geometry());
  MeshAsset instance = mesh;
  const auto snapshot = mesh.geometry_payload();
  const auto collision = mesh_collision_geometry_payload(*snapshot);
  EXPECT_EQ(mesh_collision_geometry_payload(instance), collision);
  EXPECT_EQ(mesh_collision_geometry_payload(mesh), collision);

  mesh.edit_geometry().vertices[1].position.x = 2.0f;
  const auto edited = mesh_collision_geometry_payload(mesh);
  EXPECT_NE(edited, collision);
  EXPECT_NE(edited->content_id, collision->content_id);
  EXPECT_EQ(mesh_collision_geometry_payload(instance), collision);
  EXPECT_EQ(mesh_collision_geometry_payload(*snapshot), collision);
  EXPECT_FLOAT_EQ(collision->faces[1].x, 1.0f);
  EXPECT_FLOAT_EQ(edited->faces[1].x, 2.0f);

  mesh.replace_geometry(MeshGeometryPayload{});
  instance.share_geometry({});
  EXPECT_TRUE(mesh_collision_geometry_payload(mesh)->faces.empty());
  EXPECT_FLOAT_EQ(collision->faces[1].x, 1.0f);
}

TEST(MeshCollisionGeometryTests, InvalidatesDerivedPayloadOnExclusiveEditsAndAssignments) {
  MeshAsset mesh;
  mesh.replace_geometry(triangle_geometry());
  const auto before = mesh_collision_geometry_payload(mesh);
  mesh.edit_geometry().vertices[1].position.x = 3.0f;
  const auto edited = mesh_collision_geometry_payload(mesh);
  EXPECT_NE(edited, before);
  EXPECT_FLOAT_EQ(edited->faces[1].x, 3.0f);

  MeshGeometryPayload geometry = triangle_geometry();
  const auto copied = mesh_collision_geometry_payload(geometry);
  MeshGeometryPayload replacement = triangle_geometry();
  replacement.vertices[1].position.x = 4.0f;
  geometry = replacement;
  EXPECT_FLOAT_EQ(mesh_collision_geometry_payload(geometry)->faces[1].x, 4.0f);
  geometry = triangle_geometry();
  EXPECT_FLOAT_EQ(mesh_collision_geometry_payload(geometry)->faces[1].x, 1.0f);
  EXPECT_FLOAT_EQ(copied->faces[1].x, 1.0f);
}

TEST(MeshCollisionGeometryTests, FacesRemainValidAfterTheSourceAssetIsDestroyed) {
  std::shared_ptr<const MeshCollisionGeometryPayload> retained;
  {
    MeshAsset mesh;
    mesh.replace_geometry(triangle_geometry());
    retained = mesh_collision_geometry_payload(mesh);
  }
  ASSERT_EQ(retained->faces.size(), 3u);
  expect_position(retained->faces[1], {1.0f, 0.0f, 0.0f});
  EXPECT_TRUE(retained->error_message.empty());
}

TEST(MeshCollisionGeometryTests, ConcurrentReadersPublishOneSharedImmutablePayload) {
  MeshAsset mesh;
  mesh.replace_geometry(triangle_geometry());
  const auto snapshot = mesh.geometry_payload();
  constexpr std::size_t kReaderCount = 8u;
  std::array<std::shared_ptr<const MeshCollisionGeometryPayload>, kReaderCount>
      results;
  std::barrier start(static_cast<std::ptrdiff_t>(kReaderCount + 1u));
  std::vector<std::jthread> readers;
  readers.reserve(kReaderCount);
  for (std::size_t index = 0u; index < kReaderCount; ++index) {
    readers.emplace_back([&, index] {
      start.arrive_and_wait();
      results[index] = mesh_collision_geometry_payload(*snapshot);
    });
  }
  start.arrive_and_wait();
  // Edits detach while the independent old snapshot is being read.
  mesh.edit_geometry().vertices[1].position.x = 5.0f;
  readers.clear();
  ASSERT_NE(results[0], nullptr);
  for (const auto &result : results) {
    EXPECT_EQ(result, results[0]);
    EXPECT_FLOAT_EQ(result->faces[1].x, 1.0f);
  }
  EXPECT_FLOAT_EQ(mesh_collision_geometry_payload(mesh)->faces[1].x, 5.0f);
}

TEST(MeshCollisionMetadataTests, DefaultsEnableStaticMeshGenerationAndDisplay) {
  const MeshComponent native;
  EXPECT_TRUE(native.generate_collisions);
  EXPECT_TRUE(native.display_collision);
  EXPECT_EQ(native.collision_type, MeshCollisionType::Mesh);
  EXPECT_EQ(native.body_type, MeshPhysicsBodyType::StaticBody);
  EXPECT_TRUE(mesh_collision_supported(native));
  const QuaderMeshComponent api;
  EXPECT_TRUE(api.generate_collisions);
  EXPECT_TRUE(api.display_collision);
  EXPECT_EQ(api.collision_type, QuaderMeshCollisionType::Mesh);
  EXPECT_EQ(api.body_type, QuaderMeshPhysicsBodyType::StaticBody);
}

TEST(MeshCollisionMetadataTests, EligibilityPreservesUnsupportedSavedCombinations) {
  MeshComponent mesh;
  for (const MeshCollisionType type :
       {MeshCollisionType::Box, MeshCollisionType::Sphere,
        MeshCollisionType::Cylinder, MeshCollisionType::ConvexHull}) {
    mesh.collision_type = type;
    EXPECT_FALSE(mesh_collision_supported(mesh));
    EXPECT_EQ(mesh.collision_type, type);
  }
  mesh.collision_type = MeshCollisionType::Mesh;
  mesh.body_type = MeshPhysicsBodyType::RigidBody;
  EXPECT_FALSE(mesh_collision_supported(mesh));
  mesh.body_type = MeshPhysicsBodyType::StaticBody;
  mesh.generate_collisions = false;
  mesh.display_collision = true;
  EXPECT_TRUE(mesh_collision_supported(mesh));
}

TEST(MeshCollisionMetadataTests, MetadataHistoryRestoresLegacyAndSupportedChoices) {
  SceneDocument scene;
  AssetRegistry assets;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history, &assets);
  const AssetId asset = assets.add_mesh(MeshAsset{});
  const EntityId entity = scene.create_entity("Collision Mesh", EntityKind::Mesh);
  const MeshComponent legacy{
      .mesh = asset,
      .collision_type = MeshCollisionType::Box,
      .display_collision = false,
      .body_type = MeshPhysicsBodyType::RigidBody,
  };
  ASSERT_TRUE(scene.set_mesh(entity, legacy));
  MeshComponent supported = legacy;
  supported.collision_type = MeshCollisionType::Mesh;
  supported.body_type = MeshPhysicsBodyType::StaticBody;
  supported.display_collision = true;
  const DocumentCommandResult result = commands.set_mesh_component(entity, supported);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(*scene.mesh(entity), supported);
  EXPECT_TRUE(mesh_collision_supported(*scene.mesh(entity)));
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(*scene.mesh(entity), legacy);
  EXPECT_FALSE(mesh_collision_supported(*scene.mesh(entity)));
  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(*scene.mesh(entity), supported);
}

} // namespace
} // namespace quader
