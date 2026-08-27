////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/material/material_reference_service.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/modeling_viewport_camera_adapter.hpp"
#include "editor/modeling/services/modeling_session_services.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "editor/selection/selection_projection_service.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "scene/scene_document.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <editor/interactions/cut_tool.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

namespace quader {
namespace {

TEST(ModelingViewportCameraAdapterTests,
     PreservesAffineProjectionAndClampsDegenerateExtents) {
  ModelingViewportCamera source;
  source.orthographic = true;
  source.affine_model_space = true;
  source.viewport_size = {0.0f, -20.0f};
  source.orthographic_size = 0.0;
  source.fov_degrees = 67.0;
  source.origin = {1.0f, 2.0f, 3.0f};

  const quader_editor_core::ViewportCameraState runtime =
      modeling_runtime_camera(source);
  EXPECT_EQ(runtime.projection,
            quader_editor_core::CameraProjection::Orthographic);
  EXPECT_TRUE(runtime.affine_model_space);
  EXPECT_FLOAT_EQ(runtime.viewport_size.x, 1.0f);
  EXPECT_FLOAT_EQ(runtime.viewport_size.y, 1.0f);
  EXPECT_DOUBLE_EQ(runtime.orthographic_size, 0.0001);
  EXPECT_DOUBLE_EQ(runtime.fov_degrees, 67.0);
  EXPECT_FLOAT_EQ(runtime.transform.origin.x, 1.0f);
  EXPECT_FLOAT_EQ(runtime.transform.origin.y, 2.0f);
  EXPECT_FLOAT_EQ(runtime.transform.origin.z, 3.0f);
}

void expect_vec3_eq(QVec3 actual, QVec3 expected) {
  EXPECT_FLOAT_EQ(actual.x, expected.x);
  EXPECT_FLOAT_EQ(actual.y, expected.y);
  EXPECT_FLOAT_EQ(actual.z, expected.z);
}

TEST(ModelingSessionServiceSplitTests,
     RuntimeToolCommandServiceBuildsTypedToolCommands) {
  ModelingToolRayEvent ray_event;
  ray_event.ray.origin = {1.0f, 2.0f, 3.0f};
  ray_event.ray.direction = {0.0f, -2.0f, 0.0f};
  ray_event.camera.viewport_size = {640.0f, 480.0f};
  ray_event.grid_size = 0.25;
  ray_event.vertex_radius = 0.11;
  ray_event.edge_radius = 0.22;
  ray_event.snap_enabled = false;
  ray_event.constrain_axis = true;
  ray_event.dragging = true;

  const quader_editor_core::EditorRuntimeCommand insert_hover =
      RuntimeToolCommandService::update_insert_edge_loop_hover(ray_event);
  EXPECT_EQ(
      insert_hover.command_id,
      quader_editor_core::EditorRuntimeCommandId::UpdateInsertEdgeLoopHover);
  const auto *insert_settings =
      std::get_if<quader_editor_core::InsertEdgeLoopPickRuntimeCommandSettings>(
          &insert_hover.settings);
  ASSERT_NE(insert_settings, nullptr);
  EXPECT_NEAR(insert_settings->ray.origin.x, 1.0, 0.0001);
  EXPECT_NEAR(insert_settings->ray.direction.y, -1.0, 0.0001);
  EXPECT_EQ(insert_settings->grid_size, ray_event.grid_size);
  EXPECT_EQ(insert_settings->vertex_radius, ray_event.vertex_radius);
  EXPECT_EQ(insert_settings->edge_radius, ray_event.edge_radius);
  EXPECT_FALSE(insert_settings->snap_enabled);

  const quader_editor_core::EditorRuntimeCommand cut_line =
      RuntimeToolCommandService::begin_cut_line_drag(ray_event);
  EXPECT_EQ(cut_line.command_id,
            quader_editor_core::EditorRuntimeCommandId::BeginCutLineDrag);
  const auto *cut_settings =
      std::get_if<quader_editor_core::CutRayRuntimeCommandSettings>(
          &cut_line.settings);
  ASSERT_NE(cut_settings, nullptr);
  EXPECT_TRUE(cut_settings->dragging);
  EXPECT_EQ(cut_settings->grid_size, ray_event.grid_size);

  const quader_editor_core::EditorRuntimeCommand cut_mode =
      RuntimeToolCommandService::set_cut_mode(ModelingCutMode::DiscardLeft);
  const auto *cut_mode_settings =
      std::get_if<quader_editor_core::SetCutModeRuntimeCommandSettings>(
          &cut_mode.settings);
  ASSERT_NE(cut_mode_settings, nullptr);
  EXPECT_EQ(
      cut_mode_settings->mode,
      quader_editor_core::cut_mode_id(quader_editor_core::CutMode::KeepBack));

  const quader_editor_core::EditorRuntimeCommand mirror_point =
      RuntimeToolCommandService::begin_mirror_point_drag(2);
  EXPECT_EQ(mirror_point.command_id,
            quader_editor_core::EditorRuntimeCommandId::BeginMirrorPointDrag);
  const auto *mirror_settings =
      std::get_if<quader_editor_core::MirrorPointDragRuntimeCommandSettings>(
          &mirror_point.settings);
  ASSERT_NE(mirror_settings, nullptr);
  EXPECT_EQ(mirror_settings->point_index, 2);

  const quader_editor_core::EditorRuntimeCommand poly_hover =
      RuntimeToolCommandService::update_poly_hover(ray_event, std::nullopt);
  EXPECT_EQ(poly_hover.command_id,
            quader_editor_core::EditorRuntimeCommandId::UpdatePolyHover);
  const auto *poly_settings =
      std::get_if<quader_editor_core::PolyHoverRuntimeCommandSettings>(
          &poly_hover.settings);
  ASSERT_NE(poly_settings, nullptr);
  EXPECT_TRUE(poly_settings->constrain_axis);
  EXPECT_FALSE(poly_settings->snap_enabled);
  EXPECT_EQ(poly_settings->grid_size, ray_event.grid_size);
  EXPECT_TRUE(poly_settings->surface_hit_resolved);
  EXPECT_FALSE(poly_settings->resolved_surface_hit.has_value());

  ModelingBoxToolEvent box_event;
  box_event.screen_position = {320.0f, 240.0f};
  box_event.ray = ray_event.ray;
  box_event.grid_size = 2.0;
  box_event.snap_enabled = false;
  box_event.has_camera = true;
  box_event.camera.viewport_size = {800.0f, 600.0f};

  const quader_editor_core::EditorRuntimeCommand box_height =
      RuntimeToolCommandService::begin_box_height(box_event);
  EXPECT_EQ(box_height.command_id,
            quader_editor_core::EditorRuntimeCommandId::BeginBoxHeight);
  const auto *box_height_settings =
      std::get_if<quader_editor_core::BoxHeightRuntimeCommandSettings>(
          &box_height.settings);
  ASSERT_NE(box_height_settings, nullptr);
  EXPECT_NEAR(box_height_settings->screen_position.x, 320.0, 0.0001);
  EXPECT_TRUE(box_height_settings->has_camera);
  EXPECT_FALSE(box_height_settings->snap_enabled);

}


TEST(ModelingSessionServiceSplitTests,
     ObjectRuntimeMapOwnsModelingSceneMappingsAndBuildsRecords) {
  SceneDocument scene;
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  scene.set_mesh(second, MeshComponent{.mesh = 42});
  scene.set_mesh(first, MeshComponent{.mesh = 7});

  ModelingObjectRuntimeMap object_map;
  object_map.bind(200, second);
  object_map.bind(100, first);

  EXPECT_EQ(object_map.size(), 2U);
  EXPECT_EQ(object_map.entity_for_modeling_object(100), first);
  EXPECT_EQ(object_map.modeling_object_for_entity(second), 200);

  const std::vector<ModelingObjectRecord> records = object_map.records(scene);
  ASSERT_EQ(records.size(), 2U);
  EXPECT_EQ(records[0].entity, second);
  EXPECT_EQ(records[0].modeling_object_id, 200);
  EXPECT_EQ(records[0].mesh, 42);
  EXPECT_EQ(records[0].name, "Second");
  EXPECT_EQ(records[1].entity, first);
  EXPECT_EQ(records[1].modeling_object_id, 100);
  EXPECT_EQ(records[1].mesh, 7);
  EXPECT_EQ(records[1].name, "First");

  object_map.unbind_object(100);
  EXPECT_EQ(object_map.entity_for_modeling_object(100), std::nullopt);
  EXPECT_EQ(object_map.modeling_object_for_entity(first), std::nullopt);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectRuntimeMapRebindClearsStaleReverseMappings) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  object_map.bind(100, first);
  object_map.bind(100, second);

  EXPECT_EQ(object_map.entity_for_modeling_object(100), second);
  EXPECT_EQ(object_map.modeling_object_for_entity(first), std::nullopt);
  EXPECT_EQ(object_map.modeling_object_for_entity(second), 100);

  object_map.bind(200, second);

  EXPECT_EQ(object_map.entity_for_modeling_object(100), std::nullopt);
  EXPECT_EQ(object_map.entity_for_modeling_object(200), second);
  EXPECT_EQ(object_map.modeling_object_for_entity(second), 200);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectRuntimeMapKeepsDestroyedSceneIdentityForRedoProjection) {
  SceneDocument scene;
  const EntityId parent = scene.create_entity("Parent", EntityKind::Empty);
  const EntityId entity = scene.create_entity("Redo Source", EntityKind::Mesh);
  scene.set_mesh(entity, MeshComponent{.mesh = 99});
  scene.set_material(entity, MaterialComponent{.material = 42});
  TransformComponent transform;
  transform.position = {2.0f, 3.0f, 4.0f};
  scene.set_transform(entity, transform);
  ASSERT_TRUE(scene.set_parent(entity, parent, false));
  scene.set_visible(entity, false);
  scene.set_locked(entity, true);
  const SceneLayerId layer = scene.create_layer("Redo Layer");
  ASSERT_NE(layer, kInvalidSceneLayerId);
  ASSERT_TRUE(scene.set_entity_layer(entity, layer));
  const EditorObjectId editor_object_id = scene.editor_object_id(entity);

  ModelingObjectRuntimeMap object_map;
  object_map.bind(300, entity);
  object_map.remember_scene_entity(300, scene, entity);

  const std::optional<ModelingObjectSceneIdentity> identity =
      object_map.scene_identity_for_modeling_object(300);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->entity, entity);
  EXPECT_EQ(identity->editor_object_id, editor_object_id);
  EXPECT_EQ(identity->name, "Redo Source");
  EXPECT_EQ(identity->parent, parent);
  EXPECT_EQ(identity->transform.position.x, transform.position.x);
  EXPECT_FALSE(identity->visibility.visible);
  EXPECT_TRUE(identity->lock.locked);
  ASSERT_TRUE(identity->mesh.has_value());
  EXPECT_EQ(identity->mesh->mesh, 99);
  ASSERT_TRUE(identity->material.has_value());
  EXPECT_EQ(identity->material->material.embedded_asset(), AssetId{42u});
  EXPECT_EQ(identity->layer_id, layer);

  ASSERT_TRUE(scene.destroy_entity(entity));
  object_map.unbind_object(300);
  EXPECT_EQ(object_map.entity_for_modeling_object(300), std::nullopt);
  EXPECT_TRUE(object_map.records(scene).empty());
  ASSERT_TRUE(object_map.scene_identity_for_modeling_object(300).has_value());
  EXPECT_EQ(object_map.scene_identity_for_modeling_object(300)->entity, entity);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectRuntimeMapCapturesSceneIdentitiesInRequestedOrder) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);
  TransformComponent first_transform;
  first_transform.position = {1.0f, 0.0f, 0.0f};
  TransformComponent second_transform;
  second_transform.position = {2.0f, 0.0f, 0.0f};
  scene.set_transform(first, first_transform);
  scene.set_transform(second, second_transform);

  ModelingObjectRuntimeMap object_map;
  object_map.bind(100, first);
  object_map.bind(200, second);

  const std::array<int, 2> requested{200, 100};
  const std::vector<ModelingObjectSceneIdentity> identities =
      object_map.capture_scene_identities_for_modeling_objects(scene,
                                                               requested);

  ASSERT_EQ(identities.size(), 2U);
  EXPECT_EQ(identities[0].entity, second);
  EXPECT_EQ(identities[0].transform.position.x, second_transform.position.x);
  EXPECT_EQ(identities[1].entity, first);
  EXPECT_EQ(identities[1].transform.position.x, first_transform.position.x);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectRuntimeMapPreservesLocalFrameVersionAcrossTombstoneRestore) {
  SceneDocument scene;
  const EntityId source =
      scene.create_entity_with_id(77u, "Canonical Source", EntityKind::Mesh);
  ASSERT_NE(source, kInvalidEntityId);
  ASSERT_TRUE(scene.set_modeling_object(
      source, ModelingObjectComponent{
                  .object_id = 100,
                  .material_slot = 3u,
                  .sdk_owned = true,
                  .local_frame_version =
                      kCanonicalModelingObjectLocalFrameVersion,
              }));
  ASSERT_TRUE(scene.set_mesh(source, MeshComponent{.mesh = 11u}));

  ModelingObjectRuntimeMap object_map;
  object_map.bind(100, source);
  object_map.remember_scene_entity(100, scene, source);
  const std::optional<ModelingObjectSceneIdentity> identity =
      object_map.scene_identity_for_modeling_object(100);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);

  ASSERT_TRUE(scene.destroy_entity(source));
  object_map.unbind_object(100);
  ModelingMeshSyncService mesh_sync;
  const EntityId restored =
      mesh_sync.restore_projected_mesh_entity(scene, *identity);
  ASSERT_EQ(restored, source);
  ASSERT_TRUE(mesh_sync.bind_projected_object(
      scene, restored, 100, identity->material_slot, true, 11u,
      identity->local_frame_version));
  const ModelingObjectComponent *component = scene.modeling_object(restored);
  ASSERT_NE(component, nullptr);
  EXPECT_EQ(component->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);
}

TEST(ModelingSessionServiceSplitTests,
     MeshSyncServiceAppliesDuplicateSceneIdentityPreservingMeshAsset) {
  SceneDocument scene;
  const EntityId parent = scene.create_entity("Parent", EntityKind::Empty);
  const EntityId source = scene.create_entity("Source", EntityKind::Mesh);
  const EntityId duplicate = scene.create_entity("Duplicate", EntityKind::Mesh);

  TransformComponent transform;
  transform.position = {3.0f, 4.0f, 5.0f};
  scene.set_transform(source, transform);
  ASSERT_TRUE(scene.set_parent(source, parent, false));
  scene.set_visible(source, false);
  scene.set_locked(source, true);
  scene.set_mesh(source, MeshComponent{
                             .mesh = 11,
                             .editor_only = true,
                             .cast_shadows = false,
                             .double_sided = false,
                             .generate_collisions = false,
                             .collision_type = MeshCollisionType::Mesh,
                             .display_collision = true,
                             .body_type = MeshPhysicsBodyType::RigidBody,
                         });
  scene.set_material(source, MaterialComponent{.material = 44});
  const SceneLayerId layer = scene.create_layer("Duplicate Layer");
  ASSERT_NE(layer, kInvalidSceneLayerId);
  ASSERT_TRUE(scene.set_entity_layer(source, layer));
  scene.set_mesh(duplicate, MeshComponent{.mesh = 99});
  scene.set_material(duplicate, MaterialComponent{.material = 55});

  ModelingObjectRuntimeMap object_map;
  object_map.bind(100, source);
  const std::array<int, 1> requested{100};
  const std::vector<ModelingObjectSceneIdentity> identities =
      object_map.capture_scene_identities_for_modeling_objects(scene,
                                                               requested);
  ASSERT_EQ(identities.size(), 1U);

  ModelingMeshSyncService mesh_sync;
  mesh_sync.apply_scene_identity_to_duplicate_entity(scene, duplicate,
                                                     identities.front());

  const TransformComponent *duplicate_transform = scene.transform(duplicate);
  ASSERT_NE(duplicate_transform, nullptr);
  EXPECT_EQ(duplicate_transform->position.x, transform.position.x);
  EXPECT_EQ(scene.parent(duplicate), parent);
  ASSERT_NE(scene.visibility(duplicate), nullptr);
  EXPECT_FALSE(scene.visibility(duplicate)->visible);
  ASSERT_NE(scene.lock(duplicate), nullptr);
  EXPECT_TRUE(scene.lock(duplicate)->locked);
  const MeshComponent *duplicate_mesh = scene.mesh(duplicate);
  ASSERT_NE(duplicate_mesh, nullptr);
  EXPECT_EQ(duplicate_mesh->mesh, 99);
  EXPECT_TRUE(duplicate_mesh->editor_only);
  EXPECT_FALSE(duplicate_mesh->cast_shadows);
  EXPECT_FALSE(duplicate_mesh->double_sided);
  EXPECT_FALSE(duplicate_mesh->generate_collisions);
  EXPECT_EQ(duplicate_mesh->collision_type, MeshCollisionType::Mesh);
  EXPECT_TRUE(duplicate_mesh->display_collision);
  EXPECT_EQ(duplicate_mesh->body_type, MeshPhysicsBodyType::RigidBody);
  ASSERT_NE(scene.material(duplicate), nullptr);
  EXPECT_EQ(scene.material(duplicate)->material.embedded_asset(),
            AssetId{44u});
  EXPECT_EQ(scene.entity_layer(duplicate), layer);
}

TEST(ModelingSessionServiceSplitTests,
     MeshSyncEntityMaterialReportsNoOpMutationAndOwnerFailure) {
  SceneDocument scene;
  ModelingMeshSyncService mesh_sync;
  const EntityId entity =
      scene.create_entity("Material Result Mesh", EntityKind::Mesh);
  ASSERT_NE(entity, kInvalidEntityId);
  const MaterialComponent material{.material = MaterialReference(91u)};

  EXPECT_TRUE(mesh_sync.set_entity_material(scene, entity, material));
  EXPECT_FALSE(mesh_sync.set_entity_material(scene, entity, material));
  EXPECT_FALSE(
      mesh_sync.set_entity_material(scene, kInvalidEntityId, material));
}

TEST(ModelingSessionServiceSplitTests,
     MeshSyncPreservesPerEntityPrimaryMaterialsForSharedModelingSlots) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectRuntimeMap object_map;
  SceneModelingSyncService scene_sync(object_map);
  ModelingMeshSyncService mesh_sync;

  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);
  scene.set_modeling_object(
      first, ModelingObjectComponent{.object_id = 100, .material_slot = 0});
  scene.set_modeling_object(
      second, ModelingObjectComponent{.object_id = 200, .material_slot = 0});
  scene.set_material(first, MaterialComponent{.material = 11});
  scene.set_material(second, MaterialComponent{.material = 22});
  object_map.bind(100, first);
  object_map.bind(200, second);

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100, .material_id = 0},
      quader_editor_core::PolygonObject{.id = 200, .material_id = 0},
  });

  ASSERT_TRUE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, true));

  const MaterialComponent *first_material = scene.material(first);
  const MaterialComponent *second_material = scene.material(second);
  ASSERT_NE(first_material, nullptr);
  ASSERT_NE(second_material, nullptr);
  EXPECT_EQ(first_material->material.embedded_asset(), AssetId{11u});
  EXPECT_EQ(second_material->material.embedded_asset(), AssetId{22u});
  EXPECT_FALSE(first_material->material_slots.contains(0u));
  EXPECT_FALSE(second_material->material_slots.contains(0u));
  EXPECT_EQ(first_material->material_for_slot(0u).embedded_asset(),
            AssetId{11u});
  EXPECT_EQ(second_material->material_for_slot(0u).embedded_asset(),
            AssetId{22u});
}

TEST(ModelingSessionServiceSplitTests,
     MeshSyncCompilesTwoHundredSharedTopologyDuplicatesOnlyOnce) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectRuntimeMap object_map;
  SceneModelingSyncService scene_sync(object_map);
  ModelingMeshSyncService mesh_sync;

  quader_editor_core::PolygonObject prototype;
  prototype.id = 1;
  prototype.document = quader_poly::make_cube({});
  std::vector<quader_editor_core::PolygonObject> objects;
  objects.reserve(200u);
  for (int id = 1; id <= 200; ++id) {
    quader_editor_core::PolygonObject duplicate = prototype;
    duplicate.id = id;
    objects.push_back(std::move(duplicate));
  }
  quader_editor_core::EditorSession session(objects);

  ASSERT_TRUE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, true));
  const ModelingMeshSyncStats first = mesh_sync.last_sync_stats();
  EXPECT_EQ(first.participant_count, 200u);
  EXPECT_EQ(first.unique_topology_count, 1u);
  EXPECT_GT(first.unique_authored_topology_bytes, 0u);
  EXPECT_GT(first.unique_compiled_payload_bytes, 0u);
  EXPECT_EQ(first.compiled_topology_count, 1u);
  EXPECT_EQ(first.reused_compilation_count, 199u);
  ASSERT_EQ(assets.mesh_count(), 200u);

  std::unordered_set<std::uintptr_t> geometry_tokens;
  std::unordered_set<const quader_poly::Document *> authored_tokens;
  for (const AssetId mesh_id : assets.mesh_ids()) {
    const MeshAsset *mesh = assets.mesh(mesh_id);
    ASSERT_NE(mesh, nullptr);
    geometry_tokens.insert(mesh->geometry_token());
    authored_tokens.insert(mesh->authored_polygon_document.get());
  }
  EXPECT_EQ(geometry_tokens.size(), 1u);
  EXPECT_EQ(authored_tokens.size(), 1u);

  // Rehydrate from the scene/asset boundary. Logical assets remain distinct,
  // but every runtime object must adopt the same interned authored topology
  // instead of allocating one native document per entity.
  {
    ModelingSessionService hydrated(scene, assets);
    hydrated.reset_from_scene();
    ASSERT_EQ(hydrated.objects().size(), 200u);
    authored_tokens.clear();
    for (const ModelingObjectRecord &record : hydrated.objects()) {
      const std::optional<MeshAsset> projected =
          hydrated.compile_object_mesh(record.modeling_object_id);
      ASSERT_TRUE(projected.has_value());
      ASSERT_TRUE(projected->authored_polygon_document);
      authored_tokens.insert(projected->authored_polygon_document.get());
    }
    EXPECT_EQ(authored_tokens.size(), 1u);
  }

  ASSERT_TRUE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, false));
  const ModelingMeshSyncStats unchanged = mesh_sync.last_sync_stats();
  EXPECT_TRUE(unchanged.skipped_unchanged);
  EXPECT_EQ(unchanged.unique_topology_count, 1u);
  EXPECT_EQ(unchanged.unique_authored_topology_bytes,
            first.unique_authored_topology_bytes);
  EXPECT_EQ(unchanged.unique_compiled_payload_bytes,
            first.unique_compiled_payload_bytes);
  EXPECT_EQ(unchanged.compiled_topology_count, 0u);

  objects = session.objects();
  objects.front().document.edit().vertices.front().position.x += 0.25f;
  session.objects() = std::move(objects);
  ASSERT_TRUE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, true));
  const ModelingMeshSyncStats edited = mesh_sync.last_sync_stats();
  EXPECT_EQ(edited.participant_count, 200u);
  EXPECT_EQ(edited.unique_topology_count, 2u);
  EXPECT_EQ(edited.unique_authored_topology_bytes,
            first.unique_authored_topology_bytes * 2u);
  EXPECT_EQ(edited.unique_compiled_payload_bytes,
            first.unique_compiled_payload_bytes * 2u);
  EXPECT_EQ(edited.compiled_topology_count, 1u);
  EXPECT_EQ(edited.reused_compilation_count, 0u);

  geometry_tokens.clear();
  authored_tokens.clear();
  for (const AssetId mesh_id : assets.mesh_ids()) {
    const MeshAsset *mesh = assets.mesh(mesh_id);
    ASSERT_NE(mesh, nullptr);
    geometry_tokens.insert(mesh->geometry_token());
    authored_tokens.insert(mesh->authored_polygon_document.get());
  }
  EXPECT_EQ(geometry_tokens.size(), 2u);
  EXPECT_EQ(authored_tokens.size(), 2u);
}

TEST(ModelingSessionServiceSplitTests,
     MeshSyncClonesBeforeUpdatingAnAssetSharedWithAnotherSceneEntity) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectRuntimeMap object_map;
  SceneModelingSyncService scene_sync(object_map);
  ModelingMeshSyncService mesh_sync;

  quader_editor_core::PolygonObject object;
  object.id = 100;
  object.document = quader_poly::make_cube({});
  quader_editor_core::EditorSession session({object});

  ASSERT_TRUE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, true));
  const std::optional<EntityId> projected =
      object_map.entity_for_modeling_object(100);
  ASSERT_TRUE(projected.has_value());
  ASSERT_NE(scene.mesh(*projected), nullptr);
  const AssetId shared_mesh = scene.mesh(*projected)->mesh;
  ASSERT_NE(shared_mesh, kInvalidAssetId);
  ASSERT_NE(assets.mesh(shared_mesh), nullptr);
  const QVec3 original_first_vertex =
      assets.mesh(shared_mesh)->geometry().vertices.front().position;

  const EntityId observer = scene.create_entity("Observer", EntityKind::Mesh);
  scene.set_mesh(observer, MeshComponent{.mesh = shared_mesh});

  quader_editor_core::PolygonObject changed = object;
  ASSERT_FALSE(changed.document.vertices.empty());
  changed.document.edit().vertices.front().position.x += 3.0f;
  session.objects() = {std::move(changed)};
  ASSERT_TRUE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, false));

  ASSERT_NE(scene.mesh(*projected), nullptr);
  ASSERT_NE(scene.mesh(observer), nullptr);
  const AssetId projected_mesh = scene.mesh(*projected)->mesh;
  EXPECT_NE(projected_mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(observer)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh_count(), 2u);
  ASSERT_NE(assets.mesh(shared_mesh), nullptr);
  ASSERT_NE(assets.mesh(projected_mesh), nullptr);
  EXPECT_FLOAT_EQ(
      assets.mesh(shared_mesh)->geometry().vertices.front().position.x,
      original_first_vertex.x);
  EXPECT_NE(assets.mesh(projected_mesh)->geometry().vertices.front().position.x,
            original_first_vertex.x);
}

TEST(ModelingSessionServiceSplitTests,
     SyncValidityRequiresExactComponentSignatureAndInstalledMeshAsset) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectRuntimeMap object_map;
  SceneModelingSyncService scene_sync(object_map);
  ModelingMeshSyncService mesh_sync;

  quader_editor_core::PolygonObject object;
  object.id = 100;
  object.material_id = 7;
  object.document = quader_poly::make_cube({});
  quader_editor_core::EditorSession session({object});
  ASSERT_TRUE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, true));
  const std::optional<EntityId> entity =
      object_map.entity_for_modeling_object(object.id);
  ASSERT_TRUE(entity.has_value());
  ASSERT_NE(scene.mesh(*entity), nullptr);
  const AssetId projected_mesh = scene.mesh(*entity)->mesh;

  EXPECT_TRUE(scene_sync.scene_objects_synced_for_current_modeling_content(
      session, scene, assets));
  ASSERT_TRUE(scene.set_entity_kind(*entity, EntityKind::Generic));
  EXPECT_FALSE(scene_sync.scene_objects_synced_for_current_modeling_content(
      session, scene, assets));
  ASSERT_TRUE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, false));
  EXPECT_EQ(scene.kind(*entity), EntityKind::Mesh);
  EXPECT_TRUE(scene_sync.scene_objects_synced_for_current_modeling_content(
      session, scene, assets));

  scene.set_modeling_object(
      *entity,
      ModelingObjectComponent{.object_id = object.id, .material_slot = 9});
  EXPECT_FALSE(scene_sync.scene_objects_synced_for_current_modeling_content(
      session, scene, assets));
  scene.set_modeling_object(
      *entity,
      ModelingObjectComponent{.object_id = object.id, .material_slot = 7});
  EXPECT_TRUE(scene_sync.scene_objects_synced_for_current_modeling_content(
      session, scene, assets));

  scene_sync.forget_projected_mesh_signature(object.id);
  EXPECT_FALSE(scene_sync.scene_objects_synced_for_current_modeling_content(
      session, scene, assets));
  ASSERT_TRUE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, true));
  ASSERT_TRUE(assets.remove_mesh(projected_mesh));
  EXPECT_FALSE(scene_sync.scene_objects_synced_for_current_modeling_content(
      session, scene, assets));
}

TEST(ModelingSessionServiceSplitTests,
     MeshSyncInvalidSecondObjectPublishesNothingFromTheValidFirstObject) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectRuntimeMap object_map;
  SceneModelingSyncService scene_sync(object_map);
  ModelingMeshSyncService mesh_sync;

  const EntityId sentinel = scene.create_entity("Sentinel", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_selected(sentinel, true));
  const std::vector<EntityId> entities_before = scene.entities();
  const std::vector<EntityId> selected_before = scene.selected_entities();
  const EntityId active_before = scene.primary_selection();
  const std::size_t mesh_count_before = assets.mesh_count();

  quader_editor_core::PolygonObject valid;
  valid.id = 100;
  valid.document = quader_poly::make_cube({});
  quader_editor_core::PolygonObject invalid;
  invalid.id = 200;
  invalid.document = quader_poly::make_cube({});
  ASSERT_FALSE(invalid.document.vertices.empty());
  invalid.document.edit().vertices.front().position.x =
      std::numeric_limits<float>::quiet_NaN();
  quader_editor_core::EditorSession session({valid, invalid});

  EXPECT_FALSE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, true));
  EXPECT_EQ(scene.entities(), entities_before);
  EXPECT_EQ(scene.selected_entities(), selected_before);
  EXPECT_EQ(scene.primary_selection(), active_before);
  EXPECT_EQ(assets.mesh_count(), mesh_count_before);
  EXPECT_FALSE(object_map.entity_for_modeling_object(valid.id).has_value());
  EXPECT_FALSE(object_map.entity_for_modeling_object(invalid.id).has_value());
  EXPECT_FALSE(scene_sync.projected_mesh_signature_known(valid.id));
  EXPECT_FALSE(scene_sync.projected_mesh_signature_known(invalid.id));
  EXPECT_FALSE(scene_sync.scene_objects_sync_valid());
}

TEST(ModelingSessionServiceSplitTests,
     MeshSyncPrevalidatesEveryBoundTargetBeforeRebindingSharedAssets) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectRuntimeMap object_map;
  SceneModelingSyncService scene_sync(object_map);
  ModelingMeshSyncService mesh_sync;

  quader_editor_core::PolygonObject first;
  first.id = 100;
  first.document = quader_poly::make_cube({});
  quader_editor_core::PolygonObject second;
  second.id = 200;
  second.document = quader_poly::make_cube({});
  quader_editor_core::EditorSession session({first, second});
  ASSERT_TRUE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, true));

  const std::optional<EntityId> first_entity =
      object_map.entity_for_modeling_object(first.id);
  ASSERT_TRUE(first_entity.has_value());
  ASSERT_NE(scene.mesh(*first_entity), nullptr);
  const AssetId first_mesh = scene.mesh(*first_entity)->mesh;
  ASSERT_NE(first_mesh, kInvalidAssetId);
  const EntityId observer = scene.create_entity("Shared Observer");
  ASSERT_TRUE(scene.set_mesh(observer, MeshComponent{.mesh = first_mesh}));

  const EntityId invalid_target = scene.create_empty("Protected Definition");
  ASSERT_TRUE(scene.set_instance_definition(invalid_target));
  object_map.bind(second.id, invalid_target);

  std::vector<quader_editor_core::PolygonObject> changed = session.objects();
  ASSERT_EQ(changed.size(), 2U);
  changed[0].document.edit().vertices.front().position.x += 1.0f;
  changed[1].document.edit().vertices.front().position.y += 1.0f;
  session.objects() = std::move(changed);
  const std::size_t mesh_count_before = assets.mesh_count();

  EXPECT_FALSE(mesh_sync.sync_scene_objects_from_modeling(
      session, scene, assets, scene_sync, true));
  ASSERT_NE(scene.mesh(*first_entity), nullptr);
  EXPECT_EQ(scene.mesh(*first_entity)->mesh, first_mesh);
  EXPECT_EQ(scene.mesh(observer)->mesh, first_mesh);
  EXPECT_NE(assets.mesh(first_mesh), nullptr);
  EXPECT_EQ(assets.mesh_count(), mesh_count_before);
}

TEST(ModelingSessionServiceSplitTests,
     CreateLatestRejectsInvalidProjectionBeforeSceneAssetOrSelectionMutation) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectRuntimeMap object_map;
  SceneModelingSyncService scene_sync(object_map);
  ModelingMeshSyncService mesh_sync;

  const EntityId sentinel = scene.create_entity("Sentinel", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_selected(sentinel, true));
  const std::vector<EntityId> entities_before = scene.entities();
  const std::vector<EntityId> selected_before = scene.selected_entities();
  const EntityId active_before = scene.primary_selection();
  const std::size_t mesh_count_before = assets.mesh_count();

  quader_editor_core::PolygonObject invalid;
  invalid.id = 300;
  invalid.document = quader_poly::make_cube({});
  ASSERT_FALSE(invalid.document.vertices.empty());
  invalid.document.edit().vertices.front().position.z =
      std::numeric_limits<float>::infinity();
  quader_editor_core::EditorSession session({invalid});

  const ModelingCreateBoxResult result =
      mesh_sync.create_scene_object_from_latest(
          session, scene, assets, scene_sync, "Invalid", "Created");
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.changed);
  EXPECT_EQ(scene.entities(), entities_before);
  EXPECT_EQ(scene.selected_entities(), selected_before);
  EXPECT_EQ(scene.primary_selection(), active_before);
  EXPECT_EQ(assets.mesh_count(), mesh_count_before);
  EXPECT_FALSE(object_map.entity_for_modeling_object(invalid.id).has_value());
  EXPECT_FALSE(scene_sync.projected_mesh_signature_known(invalid.id));
  EXPECT_FALSE(scene_sync.scene_objects_sync_valid());
}

TEST(ModelingSessionServiceSplitTests,
     InvalidCreationInputsLeaveRuntimeSceneAssetsSelectionAndHistoryExact) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult baseline = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Creation Boundary Baseline",
  });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  ASSERT_TRUE(service
                  .set_selection_hover({
                      .modeling_object_id = baseline.object.modeling_object_id,
                      .kind = ModelingActiveSelectionKind::Object,
                  })
                  .ok);

  const ModelingSelectionSummary selection_before =
      service.selection_summary();
  const ModelingSelectionExport export_before = service.export_selection();
  const ModelingHoverSummary hover_before = service.selection_hover_summary();
  const std::vector<ModelingObjectRecord> objects_before = service.objects();
  const std::vector<EntityId> entities_before = scene.entities();
  const std::vector<EntityId> selected_before = scene.selected_entities();
  const EntityId active_before = scene.primary_selection();
  const std::uint64_t scene_revision_before = scene.revision();
  const std::uint64_t scene_selection_revision_before =
      scene.selection_revision();
  const std::vector<AssetId> mesh_ids_before = assets.mesh_ids();
  const std::uint64_t assets_revision_before = assets.revision();

  const auto expect_exact_state = [&]() {
    const ModelingSelectionSummary selection = service.selection_summary();
    EXPECT_EQ(selection.mode, selection_before.mode);
    EXPECT_EQ(selection.has_selection, selection_before.has_selection);
    EXPECT_EQ(selection.active_kind, selection_before.active_kind);
    EXPECT_EQ(selection.active_object_id, selection_before.active_object_id);
    EXPECT_EQ(selection.selected_object_count,
              selection_before.selected_object_count);
    EXPECT_EQ(selection.content_revision, selection_before.content_revision);
    EXPECT_EQ(selection.selection_revision,
              selection_before.selection_revision);

    const ModelingSelectionExport exported = service.export_selection();
    ASSERT_EQ(exported.objects.size(), export_before.objects.size());
    ASSERT_FALSE(exported.objects.empty());
    EXPECT_EQ(exported.objects.front().modeling_object_id,
              export_before.objects.front().modeling_object_id);
    EXPECT_EQ(exported.objects.front().entity,
              export_before.objects.front().entity);
    EXPECT_EQ(exported.objects.front().selected,
              export_before.objects.front().selected);

    const ModelingHoverSummary hover = service.selection_hover_summary();
    EXPECT_EQ(hover.target.kind, hover_before.target.kind);
    EXPECT_EQ(hover.target.object_id, hover_before.target.object_id);
    EXPECT_EQ(hover.target_selected, hover_before.target_selected);

    const std::vector<ModelingObjectRecord> objects = service.objects();
    ASSERT_EQ(objects.size(), objects_before.size());
    ASSERT_FALSE(objects.empty());
    EXPECT_EQ(objects.front().modeling_object_id,
              objects_before.front().modeling_object_id);
    EXPECT_EQ(objects.front().entity, objects_before.front().entity);
    EXPECT_EQ(objects.front().mesh, objects_before.front().mesh);
    EXPECT_EQ(scene.entities(), entities_before);
    EXPECT_EQ(scene.selected_entities(), selected_before);
    EXPECT_EQ(scene.primary_selection(), active_before);
    EXPECT_EQ(scene.revision(), scene_revision_before);
    EXPECT_EQ(scene.selection_revision(), scene_selection_revision_before);
    EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);
    EXPECT_EQ(assets.revision(), assets_revision_before);
  };

  EditorHistoryService history;
  ModelingCreateBoxRequest invalid_box;
  invalid_box.bounds.min.x = std::numeric_limits<float>::quiet_NaN();
  const CommandResult box_result = history.execute(
      std::make_unique<CreateBoxFromBoundsHistoryCommand>(service,
                                                           invalid_box));
  EXPECT_FALSE(box_result.ok);
  EXPECT_FALSE(box_result.changed);
  EXPECT_FALSE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  expect_exact_state();

  ModelingCreateMeshRequest invalid_mesh;
  invalid_mesh.document = quader_poly::make_cube({});
  ASSERT_FALSE(invalid_mesh.document.vertices.empty());
  invalid_mesh.document.vertices.front().position.y =
      std::numeric_limits<float>::infinity();
  const CommandResult mesh_result = history.execute(
      std::make_unique<CreateMeshHistoryCommand>(service,
                                                  std::move(invalid_mesh)));
  EXPECT_FALSE(mesh_result.ok);
  EXPECT_FALSE(mesh_result.changed);
  EXPECT_FALSE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  expect_exact_state();
}

TEST(ModelingSessionServiceSplitTests,
     MementoRestoreKeepsRevisionsAndObjectIdsSessionMonotonic) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult baseline = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Memento Baseline",
  });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  const EntityId baseline_entity = baseline.object.entity;
  const ModelingSessionMemento before = service.capture_history_memento();
  ASSERT_TRUE(before.valid());

  const ModelingCreateBoxResult first_transient = service.create_box_from_bounds({
      .bounds = {.min = {2.0f, 2.0f, 2.0f},
                 .max = {3.0f, 3.0f, 3.0f}},
      .name = "Canceled Primitive One",
  });
  ASSERT_TRUE(first_transient.ok) << first_transient.message;
  const ModelingCreateBoxResult second_transient = service.create_box_from_bounds({
      .bounds = {.min = {4.0f, 4.0f, 4.0f},
                 .max = {5.0f, 5.0f, 5.0f}},
      .name = "Canceled Primitive Two",
  });
  ASSERT_TRUE(second_transient.ok) << second_transient.message;
  const ModelingSelectionSummary transient_summary =
      service.selection_summary();

  const ModelingCommandResult restored = service.restore_history_memento(before);
  ASSERT_TRUE(restored.ok) << restored.message;
  ASSERT_TRUE(restored.changed);
  const std::vector<ModelingObjectRecord> restored_objects = service.objects();
  ASSERT_EQ(restored_objects.size(), 1u);
  EXPECT_EQ(restored_objects.front().modeling_object_id,
            baseline.object.modeling_object_id);
  EXPECT_EQ(restored_objects.front().entity, baseline_entity);
  const ModelingSelectionSummary restored_summary =
      service.selection_summary();
  EXPECT_GT(restored_summary.content_revision,
            transient_summary.content_revision);
  EXPECT_GT(restored_summary.selection_revision,
            transient_summary.selection_revision);

  const ModelingCreateBoxResult replacement = service.create_box_from_bounds({
      .bounds = {.min = {6.0f, 6.0f, 6.0f},
                 .max = {7.0f, 7.0f, 7.0f}},
      .name = "Replacement Primitive",
  });
  ASSERT_TRUE(replacement.ok) << replacement.message;
  const int last_transient_id = std::max(
      first_transient.object.modeling_object_id,
      second_transient.object.modeling_object_id);
  EXPECT_EQ(replacement.object.modeling_object_id, last_transient_id + 1);
}

TEST(ModelingSessionServiceSplitTests,
     CreationProjectionFailureRestoresRuntimeSelectionAndAddsNoHistory) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult baseline = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Projection Failure Baseline",
  });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  ASSERT_TRUE(service
                  .set_selection_hover({
                      .modeling_object_id = baseline.object.modeling_object_id,
                      .kind = ModelingActiveSelectionKind::Object,
                  })
                  .ok);

  const ModelingSelectionSummary selection_before =
      service.selection_summary();
  const ModelingHoverSummary hover_before = service.selection_hover_summary();
  const std::vector<ModelingObjectRecord> objects_before = service.objects();
  const std::vector<EntityId> entities_before = scene.entities();
  const std::vector<EntityId> selected_before = scene.selected_entities();
  const EntityId active_before = scene.primary_selection();
  const std::vector<AssetId> mesh_ids_before = assets.mesh_ids();

  scene.set_editor_object_id_allocator(
      [](EditorObjectId) { return kInvalidEditorObjectId; });
  EditorHistoryService history;
  const CommandResult failed = history.execute(
      std::make_unique<CreateBoxFromBoundsHistoryCommand>(
          service,
          ModelingCreateBoxRequest{
              .bounds = {.min = {2.0f, 2.0f, 2.0f},
                         .max = {4.0f, 4.0f, 4.0f}},
              .name = "Must Roll Back",
          }));
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_FALSE(history.can_undo());
  EXPECT_FALSE(history.can_redo());

  const ModelingSelectionSummary selection_after =
      service.selection_summary();
  EXPECT_EQ(selection_after.active_object_id,
            selection_before.active_object_id);
  EXPECT_EQ(selection_after.selected_object_count,
            selection_before.selected_object_count);
  EXPECT_EQ(selection_after.content_revision,
            selection_before.content_revision);
  EXPECT_EQ(selection_after.selection_revision,
            selection_before.selection_revision);
  const ModelingHoverSummary hover_after = service.selection_hover_summary();
  EXPECT_EQ(hover_after.target.kind, hover_before.target.kind);
  EXPECT_EQ(hover_after.target.object_id, hover_before.target.object_id);
  EXPECT_EQ(hover_after.target_selected, hover_before.target_selected);
  const std::vector<ModelingObjectRecord> objects_after = service.objects();
  ASSERT_EQ(objects_after.size(), objects_before.size());
  ASSERT_FALSE(objects_after.empty());
  EXPECT_EQ(objects_after.front().modeling_object_id,
            objects_before.front().modeling_object_id);
  EXPECT_EQ(objects_after.front().entity, objects_before.front().entity);
  EXPECT_EQ(scene.entities(), entities_before);
  EXPECT_EQ(scene.selected_entities(), selected_before);
  EXPECT_EQ(scene.primary_selection(), active_before);
  EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);

  ModelingCreateMeshRequest valid_mesh;
  valid_mesh.document = quader_poly::make_cube({});
  valid_mesh.name = "Must Also Roll Back";
  const CommandResult failed_mesh = history.execute(
      std::make_unique<CreateMeshHistoryCommand>(service,
                                                  std::move(valid_mesh)));
  EXPECT_FALSE(failed_mesh.ok);
  EXPECT_FALSE(failed_mesh.changed);
  EXPECT_FALSE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  const ModelingSelectionSummary selection_after_mesh =
      service.selection_summary();
  EXPECT_EQ(selection_after_mesh.active_object_id,
            selection_before.active_object_id);
  EXPECT_EQ(selection_after_mesh.selected_object_count,
            selection_before.selected_object_count);
  EXPECT_EQ(selection_after_mesh.content_revision,
            selection_before.content_revision);
  EXPECT_EQ(selection_after_mesh.selection_revision,
            selection_before.selection_revision);
  const ModelingHoverSummary hover_after_mesh =
      service.selection_hover_summary();
  EXPECT_EQ(hover_after_mesh.target.kind, hover_before.target.kind);
  EXPECT_EQ(hover_after_mesh.target.object_id, hover_before.target.object_id);
  EXPECT_EQ(hover_after_mesh.target_selected, hover_before.target_selected);
  EXPECT_EQ(service.objects().size(), objects_before.size());
  EXPECT_EQ(scene.entities(), entities_before);
  EXPECT_EQ(scene.selected_entities(), selected_before);
  EXPECT_EQ(scene.primary_selection(), active_before);
  EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);
}

TEST(ModelingSessionServiceSplitTests,
     FailedBoxCommitRestoresTheReadyPreviewForAnExactRetry) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingBoxToolEvent start{
      .screen_position = {300.0f, 240.0f},
      .ray = {.origin = {0.0f, 4.0f, 0.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  };
  const ModelingBoxToolEvent footprint_end{
      .screen_position = {420.0f, 240.0f},
      .ray = {.origin = {2.0f, 4.0f, 2.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  };
  const ModelingBoxToolEvent height_end{
      .screen_position = {420.0f, 340.0f},
      .ray = start.ray,
      .grid_size = 1.0,
  };

  ASSERT_TRUE(service.activate_box_tool().ok);
  ASSERT_TRUE(service.begin_box_footprint(start).ok);
  ASSERT_TRUE(service.update_box_footprint(footprint_end).ok);
  ASSERT_TRUE(service.begin_box_height(start).ok);
  ASSERT_TRUE(service.update_box_height(height_end).ok);
  const ModelingBoxPreview preview_before = service.box_preview();
  ASSERT_TRUE(preview_before.valid);
  ASSERT_EQ(preview_before.drag_state, ModelingBoxDragState::Height);
  const ModelingSelectionSummary selection_before =
      service.selection_summary();
  const std::vector<EntityId> entities_before = scene.entities();
  const std::vector<EntityId> selected_before = scene.selected_entities();
  const std::vector<AssetId> mesh_ids_before = assets.mesh_ids();

  scene.set_editor_object_id_allocator(
      [](EditorObjectId) { return kInvalidEditorObjectId; });
  const ModelingCreateBoxResult failed = service.commit_box("Failed Commit");
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  const ModelingBoxPreview preview_after = service.box_preview();
  EXPECT_TRUE(preview_after.valid);
  EXPECT_EQ(preview_after.drag_state, preview_before.drag_state);
  EXPECT_EQ(preview_after.corner_count, preview_before.corner_count);
  EXPECT_DOUBLE_EQ(preview_after.distance, preview_before.distance);
  EXPECT_EQ(service.selection_summary().content_revision,
            selection_before.content_revision);
  EXPECT_EQ(service.selection_summary().selection_revision,
            selection_before.selection_revision);
  EXPECT_EQ(scene.entities(), entities_before);
  EXPECT_EQ(scene.selected_entities(), selected_before);
  EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);

  scene.set_editor_object_id_allocator({});
  const ModelingCreateBoxResult retried = service.commit_box("Retried Commit");
  ASSERT_TRUE(retried.ok) << retried.message;
  EXPECT_TRUE(retried.changed);
  EXPECT_EQ(service.box_preview().drag_state, ModelingBoxDragState::Idle);
}

TEST(ModelingSessionServiceSplitTests,
     HistoryMementoRestoresSharedMeshCloneIdentityAcrossUndoRedo) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Shared History Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  const MeshComponent *source_mesh = scene.mesh(box.object.entity);
  ASSERT_NE(source_mesh, nullptr);
  const AssetId shared_mesh = source_mesh->mesh;
  ASSERT_NE(shared_mesh, kInvalidAssetId);
  const MeshAsset *shared_asset = assets.mesh(shared_mesh);
  ASSERT_NE(shared_asset, nullptr);
  ASSERT_FALSE(shared_asset->geometry().vertices.empty());
  const QVec3 original_first_vertex =
      shared_asset->geometry().vertices.front().position;

  const EntityId observer =
      scene.create_entity("Shared History Observer", EntityKind::Mesh);
  scene.set_mesh(observer, MeshComponent{.mesh = shared_mesh});
  const std::size_t original_mesh_count = assets.mesh_count();

  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  ASSERT_TRUE(service
                  .set_component_selection({
                      .modeling_object_id = box.object.modeling_object_id,
                      .mode = ModelingSelectionMode::Vertex,
                      .vertices = {1},
                      .edit = ModelingSelectionEdit::Add,
                      .active_kind = ModelingActiveSelectionKind::Vertex,
                      .active_vertex = 1,
                  })
                  .ok);
  ASSERT_TRUE(service.select_all().ok);
  EditorHistoryService history;
  const CommandResult moved = history.execute(
      std::make_unique<MoveSelectionHistoryCommand>(
          service, QVec3{2.0f, 0.0f, 0.0f}));
  ASSERT_TRUE(moved.ok) << moved.message;
  ASSERT_TRUE(moved.changed);

  ASSERT_NE(scene.mesh(box.object.entity), nullptr);
  const AssetId cloned_mesh = scene.mesh(box.object.entity)->mesh;
  ASSERT_NE(cloned_mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(observer)->mesh, shared_mesh);
  ASSERT_EQ(assets.mesh_count(), original_mesh_count + 1U);
  ASSERT_NE(assets.mesh(cloned_mesh), nullptr);
  const QVec3 changed_first_vertex =
      assets.mesh(cloned_mesh)->geometry().vertices.front().position;
  EXPECT_NE(changed_first_vertex.x, original_first_vertex.x);

  CommandResult restored = history.undo();
  ASSERT_TRUE(restored.ok) << restored.message;
  ASSERT_NE(scene.mesh(box.object.entity), nullptr);
  ASSERT_NE(scene.mesh(observer), nullptr);
  EXPECT_EQ(scene.mesh(box.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(observer)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh_count(), original_mesh_count);
  EXPECT_EQ(assets.mesh(cloned_mesh), nullptr);
  ASSERT_NE(assets.mesh(shared_mesh), nullptr);
  expect_vec3_eq(
      assets.mesh(shared_mesh)->geometry().vertices.front().position,
      original_first_vertex);

  restored = history.redo();
  ASSERT_TRUE(restored.ok) << restored.message;
  ASSERT_NE(scene.mesh(box.object.entity), nullptr);
  ASSERT_NE(scene.mesh(observer), nullptr);
  EXPECT_EQ(scene.mesh(box.object.entity)->mesh, cloned_mesh);
  EXPECT_EQ(scene.mesh(observer)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh_count(), original_mesh_count + 1U);
  ASSERT_NE(assets.mesh(cloned_mesh), nullptr);
  expect_vec3_eq(
      assets.mesh(cloned_mesh)->geometry().vertices.front().position,
      changed_first_vertex);

  restored = history.undo();
  ASSERT_TRUE(restored.ok) << restored.message;
  EXPECT_EQ(scene.mesh(box.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh_count(), original_mesh_count);
  restored = history.redo();
  ASSERT_TRUE(restored.ok) << restored.message;
  EXPECT_EQ(scene.mesh(box.object.entity)->mesh, cloned_mesh);
  EXPECT_EQ(assets.mesh_count(), original_mesh_count + 1U);
}

TEST(ModelingSessionServiceSplitTests,
     DeepNativeMementoHistoryRestoresOneWorkspaceExactly) {
  constexpr std::size_t kHistoryDepth = 128U;
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Deep Memento Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  ASSERT_TRUE(service
                  .set_component_selection({
                      .modeling_object_id = box.object.modeling_object_id,
                      .mode = ModelingSelectionMode::Vertex,
                      .vertices = {1},
                      .edit = ModelingSelectionEdit::Replace,
                      .active_kind = ModelingActiveSelectionKind::Vertex,
                      .active_vertex = 1,
                  })
                  .ok);
  ASSERT_TRUE(service.select_all().ok);

  const MeshComponent *initial_binding = scene.mesh(box.object.entity);
  ASSERT_NE(initial_binding, nullptr);
  const AssetId initial_mesh_id = initial_binding->mesh;
  const MeshAsset *initial_asset = assets.mesh(initial_mesh_id);
  ASSERT_NE(initial_asset, nullptr);
  const MeshAsset initial_payload = clone_mesh_asset_payload(*initial_asset);
  const ModelingSelectionSummary initial_selection =
      service.selection_summary();
  const std::uint64_t initial_content_revision =
      initial_selection.content_revision;

  EditorHistoryService history;
  for (std::size_t index = 0; index < kHistoryDepth; ++index) {
    const CommandResult moved = history.execute(
        std::make_unique<MoveSelectionHistoryCommand>(
            service, QVec3{0.01F, 0.0F, 0.0F}));
    ASSERT_TRUE(moved.ok) << moved.message;
    ASSERT_TRUE(moved.changed);
  }
  ASSERT_TRUE(history.can_undo());
  const MeshComponent *moved_binding = scene.mesh(box.object.entity);
  ASSERT_NE(moved_binding, nullptr);
  const AssetId moved_mesh_id = moved_binding->mesh;
  const MeshAsset *moved_asset = assets.mesh(moved_mesh_id);
  ASSERT_NE(moved_asset, nullptr);
  const MeshAsset moved_payload = clone_mesh_asset_payload(*moved_asset);
  EXPECT_FALSE(mesh_asset_payload_equal(initial_payload, moved_payload));

  for (std::size_t index = 0; index < kHistoryDepth; ++index) {
    const CommandResult restored = history.undo();
    ASSERT_TRUE(restored.ok) << restored.message;
    ASSERT_TRUE(restored.changed);
  }
  EXPECT_FALSE(history.can_undo());
  EXPECT_TRUE(history.can_redo());
  ASSERT_EQ(service.objects().size(), 1U);
  EXPECT_EQ(service.objects().front().modeling_object_id,
            box.object.modeling_object_id);
  EXPECT_EQ(service.objects().front().entity, box.object.entity);
  const MeshComponent *undone_binding = scene.mesh(box.object.entity);
  ASSERT_NE(undone_binding, nullptr);
  EXPECT_EQ(undone_binding->mesh, initial_mesh_id);
  const MeshAsset *undone_asset = assets.mesh(undone_binding->mesh);
  ASSERT_NE(undone_asset, nullptr);
  EXPECT_TRUE(mesh_asset_payload_equal(*undone_asset, initial_payload));
  const ModelingSelectionSummary undone_selection =
      service.selection_summary();
  EXPECT_EQ(undone_selection.mode, initial_selection.mode);
  EXPECT_EQ(undone_selection.active_kind, initial_selection.active_kind);
  EXPECT_EQ(undone_selection.active_object_id,
            initial_selection.active_object_id);
  EXPECT_EQ(undone_selection.selected_vertex_count,
            initial_selection.selected_vertex_count);
  EXPECT_GT(undone_selection.content_revision, initial_content_revision);

  const std::uint64_t undo_content_revision =
      undone_selection.content_revision;
  for (std::size_t index = 0; index < kHistoryDepth; ++index) {
    const CommandResult restored = history.redo();
    ASSERT_TRUE(restored.ok) << restored.message;
    ASSERT_TRUE(restored.changed);
  }
  EXPECT_TRUE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  ASSERT_EQ(service.objects().size(), 1U);
  EXPECT_EQ(service.objects().front().modeling_object_id,
            box.object.modeling_object_id);
  EXPECT_EQ(service.objects().front().entity, box.object.entity);
  const MeshComponent *redone_binding = scene.mesh(box.object.entity);
  ASSERT_NE(redone_binding, nullptr);
  EXPECT_EQ(redone_binding->mesh, moved_mesh_id);
  const MeshAsset *redone_asset = assets.mesh(redone_binding->mesh);
  ASSERT_NE(redone_asset, nullptr);
  EXPECT_TRUE(mesh_asset_payload_equal(*redone_asset, moved_payload));
  const ModelingSelectionSummary redone_selection =
      service.selection_summary();
  EXPECT_EQ(redone_selection.mode, initial_selection.mode);
  EXPECT_EQ(redone_selection.active_kind, initial_selection.active_kind);
  EXPECT_EQ(redone_selection.active_object_id,
            initial_selection.active_object_id);
  EXPECT_EQ(redone_selection.selected_vertex_count,
            initial_selection.selected_vertex_count);
  EXPECT_GT(redone_selection.content_revision, undo_content_revision);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServiceProjectsSceneSelectionIntoModelingOnly) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  object_map.bind(100, first);
  object_map.bind(200, second);

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });

  scene.set_selected(group, true);
  scene.set_selected(first, true);
  scene.set_selected(second, true);
  scene.set_active_selection(second);

  const SelectionProjectionService projection;
  EXPECT_TRUE(projection.project_object_selection_from_scene(session, scene,
                                                             object_map));

  std::vector<int> selected_object_ids = session.selected_object_ids();
  std::sort(selected_object_ids.begin(), selected_object_ids.end());
  EXPECT_EQ(selected_object_ids, (std::vector<int>{100, 200}));
  EXPECT_EQ(session.active_object_id(), 200);

  std::vector<EntityId> selected_entities = scene.selected_entities();
  std::sort(selected_entities.begin(), selected_entities.end());
  EXPECT_EQ(selected_entities, (std::vector<EntityId>{group, first, second}));
  EXPECT_EQ(scene.primary_selection(), second);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServiceComponentBoundsWorkScalesLinearly) {
  const auto exercise = [](std::size_t vertex_count) {
    SceneDocument scene;
    AssetRegistry assets;
    const EntityId entity =
        scene.create_entity("Selected Mesh", EntityKind::Mesh);
    TransformComponent transform;
    transform.position = {10.0f, 20.0f, 30.0f};
    EXPECT_TRUE(scene.set_transform(entity, transform));

    ModelingObjectRuntimeMap object_map;
    object_map.bind(100, entity);

    quader_poly::Document document;
    document.vertices.reserve(vertex_count);
    quader_poly::Selection selection;
    selection.vertices.reserve(vertex_count);
    for (std::size_t index = 0; index < vertex_count; ++index) {
      const quader_poly::ElementId id =
          static_cast<quader_poly::ElementId>(index + 1u);
      document.vertices.push_back({
          .id = id,
          .position = {static_cast<float>(index),
                       static_cast<float>(index % 7u),
                       -static_cast<float>(index)},
      });
      selection.vertices.push_back(id);
    }

    quader_editor_core::PolygonObject object;
    object.id = 100;
    object.document = std::move(document);
    quader_editor_core::EditorSession session({std::move(object)});
    session.set_component_selection(
        100, std::move(selection),
        quader_editor_core::SelectionMode::Vertex);

    const SelectionProjectionService projection;
    SelectionBoundsWorkStats work;
    const std::optional<SceneBounds> bounds =
        projection.world_selection_bounds(session, scene, assets, object_map,
                                          &work);
    EXPECT_TRUE(bounds.has_value());
    if (bounds.has_value()) {
      EXPECT_FLOAT_EQ(bounds->min.x, 10.0f);
      EXPECT_FLOAT_EQ(bounds->max.x,
                      10.0f + static_cast<float>(vertex_count - 1u));
      EXPECT_FLOAT_EQ(bounds->max.z, 30.0f);
      EXPECT_FLOAT_EQ(bounds->min.z,
                      30.0f - static_cast<float>(vertex_count - 1u));
    }
    EXPECT_EQ(work.selected_object_visit_count, 1u);
    EXPECT_EQ(work.vertex_index_visit_count, vertex_count);
    EXPECT_EQ(work.selected_vertex_lookup_count, vertex_count);
    EXPECT_EQ(work.world_transform_evaluation_count, 1u);
    EXPECT_EQ(work.included_point_count, vertex_count);
    return work;
  };

  const SelectionBoundsWorkStats work_n = exercise(256u);
  const SelectionBoundsWorkStats work_2n = exercise(512u);
  EXPECT_EQ(work_2n.vertex_index_visit_count,
            work_n.vertex_index_visit_count * 2u);
  EXPECT_EQ(work_2n.selected_vertex_lookup_count,
            work_n.selected_vertex_lookup_count * 2u);
  EXPECT_EQ(work_2n.included_point_count,
            work_n.included_point_count * 2u);
  EXPECT_EQ(work_2n.world_transform_evaluation_count,
            work_n.world_transform_evaluation_count);
}

TEST(ModelingSessionServiceSplitTests,
     SceneOwnedSelectionRevisionIgnoresUnrelatedSceneMutations) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const EntityId group = scene.create_empty("Revision Group");
  ASSERT_NE(group, kInvalidEntityId);
  ASSERT_TRUE(scene.set_selected(group, true));
  ASSERT_TRUE(scene.set_active_selection(group));
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Group).ok);

  const ModelingSelectionSummary before = service.selection_summary();
  ASSERT_EQ(before.mode, ModelingSelectionMode::Group);
  ASSERT_TRUE(before.has_selection);
  const std::uint64_t scene_revision_before = scene.revision();

  TransformComponent moved;
  moved.position = {3.0f, -2.0f, 7.0f};
  ASSERT_TRUE(scene.set_transform(group, moved));
  ASSERT_TRUE(scene.set_locked(group, true));
  ASSERT_TRUE(scene.set_visible(group, false));
  ASSERT_GT(scene.revision(), scene_revision_before);

  const ModelingSelectionSummary after_content_edits =
      service.selection_summary();
  EXPECT_EQ(after_content_edits.selection_revision,
            before.selection_revision);
  EXPECT_EQ(after_content_edits.selected_object_count,
            before.selected_object_count);

  ASSERT_TRUE(scene.set_light(group, LightComponent{}));
  const ModelingSelectionSummary after_active_kind_edit =
      service.selection_summary();
  EXPECT_EQ(after_active_kind_edit.active_kind,
            ModelingActiveSelectionKind::Light);
  EXPECT_GT(after_active_kind_edit.selection_revision,
            after_content_edits.selection_revision);

  scene.clear_selection();
  const ModelingSelectionSummary after_selection_edit =
      service.selection_summary();
  EXPECT_GT(after_selection_edit.selection_revision,
            before.selection_revision);
  EXPECT_FALSE(after_selection_edit.has_selection);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServiceProjectsModelingSelectionIntoScene) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  object_map.bind(100, first);
  object_map.bind(200, second);

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });
  session.set_selected_object_ids({100, 200}, 200);
  scene.set_selected(group, true);

  const SelectionProjectionService projection;
  EXPECT_TRUE(projection.project_scene_selection_from_modeling(session, scene,
                                                               object_map));

  std::vector<EntityId> selected_entities = scene.selected_entities();
  std::sort(selected_entities.begin(), selected_entities.end());
  EXPECT_EQ(selected_entities, (std::vector<EntityId>{first, second}));
  EXPECT_EQ(scene.primary_selection(), second);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServiceUsesOneActiveFallbackInBothDirections) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  object_map.bind(100, first);
  object_map.bind(200, second);

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });

  const SelectionProjectionService projection;
  const std::array<EntityId, 2> scene_selection{first, second};
  RuntimeObjectSelectionProjectionResult result =
      projection.project_object_selection_for_scene_entities(
          session, scene_selection, kInvalidEntityId, object_map);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(session.active_object_id(), 200);

  session.set_selected_object_ids({100, 200}, -1);
  EXPECT_TRUE(projection.project_scene_selection_from_modeling(session, scene,
                                                               object_map));
  EXPECT_EQ(scene.primary_selection(), second);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServiceProjectsExplicitObjectSelection) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  object_map.bind(100, first);
  object_map.bind(200, second);

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });

  const SelectionProjectionService projection;
  const std::array<EntityId, 2> selected{first, second};
  RuntimeObjectSelectionProjectionResult result =
      projection.project_object_selection_for_scene_entities(
          session, selected, kInvalidEntityId, object_map);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.changed);
  EXPECT_TRUE(result.has_modeling_objects);

  std::vector<int> selected_object_ids = session.selected_object_ids();
  std::sort(selected_object_ids.begin(), selected_object_ids.end());
  EXPECT_EQ(selected_object_ids, (std::vector<int>{100, 200}));
  EXPECT_EQ(session.active_object_id(), 200);

  result = projection.project_object_selection_for_scene_entities(
      session, selected, second, object_map);
  EXPECT_TRUE(result.ok);
  EXPECT_FALSE(result.changed);
  EXPECT_TRUE(result.has_modeling_objects);

  result = projection.project_object_selection_for_scene_entities(
      session, selected, first, object_map);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(session.active_object_id(), 100);

  const std::array<EntityId, 1> non_modeling{group};
  result = projection.project_object_selection_for_scene_entities(
      session, non_modeling, group, object_map);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.changed);
  EXPECT_FALSE(result.has_modeling_objects);
  EXPECT_TRUE(session.selected_object_ids().empty());
  EXPECT_EQ(session.active_object_id(), -1);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServicePromotesToolObjectSelection) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  object_map.bind(100, first);
  object_map.bind(200, second);

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });

  scene.set_selected(group, true);
  scene.set_selected(first, true);
  scene.set_selected(second, true);
  scene.set_active_selection(second);

  const SelectionProjectionService projection;
  EXPECT_TRUE(
      projection.promote_object_selection_for_tool(session, scene, object_map));

  std::vector<int> selected_object_ids = session.selected_object_ids();
  std::sort(selected_object_ids.begin(), selected_object_ids.end());
  EXPECT_EQ(selected_object_ids, (std::vector<int>{100, 200}));
  EXPECT_EQ(session.active_object_id(), 200);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServicePromotionClearsRuntimeForSceneOnlySelection) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  object_map.bind(100, first);
  object_map.bind(200, second);

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });
  session.set_selected_object_ids({100, 200}, -1);
  scene.set_selected(group, true);
  scene.set_active_selection(group);

  const SelectionProjectionService projection;
  EXPECT_TRUE(
      projection.promote_object_selection_for_tool(session, scene, object_map));

  EXPECT_TRUE(session.selected_object_ids().empty());
  EXPECT_EQ(session.active_object_id(), -1);
  EXPECT_EQ(session.selection_mode(),
            quader_editor_core::SelectionMode::Object);
}

TEST(ModelingSessionServiceSplitTests,
     SceneSelectionApplicationModesAreExplicit) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);
  const std::array<EntityId, 2> selected{first, second};
  const SceneSelectionState state =
      make_scene_selection(scene, selected, second);

  EXPECT_TRUE(apply_scene_selection(
      scene, state, SceneSelectionApplicationMode::UndoableCommand));
  EXPECT_EQ(scene.selected_entities(), (std::vector<EntityId>{first, second}));
  EXPECT_EQ(scene.primary_selection(), second);

  const std::uint64_t revision_after_first_apply = scene.selection_revision();
  EXPECT_FALSE(apply_scene_selection(
      scene, state, SceneSelectionApplicationMode::RuntimeSync));
  EXPECT_EQ(scene.selection_revision(), revision_after_first_apply);

  EXPECT_TRUE(apply_scene_selection(
      scene, state, SceneSelectionApplicationMode::UndoableCommand));
  EXPECT_GT(scene.selection_revision(), revision_after_first_apply);
}

TEST(ModelingSessionServiceSplitTests,
     EachWorkspaceOwnsOneIndependentSdkProjectionSession) {
  SceneDocument first_scene;
  SceneDocument second_scene;
  AssetRegistry first_assets;
  AssetRegistry second_assets;
  ModelingSessionService first(first_scene, first_assets);
  ModelingSessionService second(second_scene, second_assets);

  EXPECT_NE(first.modeling_sdk_session_identity_for_tests(),
            second.modeling_sdk_session_identity_for_tests());
  EXPECT_EQ(first.modeling_sdk_object_count_for_tests(), 0u);
  EXPECT_EQ(second.modeling_sdk_object_count_for_tests(), 0u);

  const ModelingCreateBoxResult created = first.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Workspace SDK Box",
  });
  ASSERT_TRUE(created.ok) << created.message;
  EXPECT_EQ(first.modeling_sdk_object_count_for_tests(), 1u);
  EXPECT_EQ(second.modeling_sdk_object_count_for_tests(), 0u);
  const ModelingObjectComponent *component =
      first_scene.modeling_object(created.object.entity);
  ASSERT_NE(component, nullptr);
  EXPECT_TRUE(component->sdk_owned);
  EXPECT_TRUE(second_scene.entities().empty());
}

TEST(ModelingSessionServiceSplitTests,
     SdkBoxHistoryRestoresTheSameHostMappingAndOwnership) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorHistoryService history;
  const CommandResult created = history.execute(
      std::make_unique<CreateBoxFromBoundsHistoryCommand>(
          service,
          ModelingCreateBoxRequest{
              .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                         .max = {1.0f, 1.0f, 1.0f}},
              .name = "SDK History Box",
          }));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.changed);
  ASSERT_EQ(service.objects().size(), 1u);
  const ModelingObjectRecord original = service.objects().front();
  ASSERT_NE(scene.modeling_object(original.entity), nullptr);
  EXPECT_TRUE(scene.modeling_object(original.entity)->sdk_owned);
  EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 1u);

  const CommandResult undone = history.undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  EXPECT_TRUE(service.objects().empty());
  EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 0u);
  EXPECT_FALSE(scene.exists(original.entity));

  const CommandResult redone = history.redo();
  ASSERT_TRUE(redone.ok) << redone.message;
  ASSERT_EQ(service.objects().size(), 1u);
  EXPECT_EQ(service.objects().front().modeling_object_id,
            original.modeling_object_id);
  EXPECT_EQ(service.objects().front().entity, original.entity);
  ASSERT_NE(scene.modeling_object(original.entity), nullptr);
  EXPECT_TRUE(scene.modeling_object(original.entity)->sdk_owned);
  EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 1u);
}

TEST(ModelingSessionServiceSplitTests,
     SdkBoxProjectionFailureBoundariesRestoreHostStateExactly) {
  constexpr std::array<ModelingSdkProjectionFailurePoint, 6> kFailures = {
      ModelingSdkProjectionFailurePoint::AfterSdkMutation,
      ModelingSdkProjectionFailurePoint::AfterAssetInstall,
      ModelingSdkProjectionFailurePoint::AfterEntityCreate,
      ModelingSdkProjectionFailurePoint::AfterSceneBinding,
      ModelingSdkProjectionFailurePoint::AfterSelection,
      ModelingSdkProjectionFailurePoint::BeforeCommit,
  };

  for (const ModelingSdkProjectionFailurePoint failure_point : kFailures) {
    SCOPED_TRACE(static_cast<int>(failure_point));
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const EntityId sentinel =
        scene.create_entity("Projection Sentinel", EntityKind::Empty);
    ASSERT_NE(sentinel, kInvalidEntityId);
    ASSERT_TRUE(scene.set_selected(sentinel, true));

    const std::vector<EntityId> entities_before = scene.entities();
    const std::vector<EntityId> selected_before = scene.selected_entities();
    const std::vector<AssetId> meshes_before = assets.mesh_ids();
    const std::uint64_t scene_revision_before = scene.revision();
    const std::uint64_t scene_selection_revision_before =
        scene.selection_revision();
    const std::uint64_t asset_revision_before = assets.revision();
    const std::uint64_t content_revision_before = service.content_revision();
    const std::uint64_t selection_revision_before =
        service.selection_revision();

    service.set_modeling_sdk_projection_failure_for_tests(failure_point);
    const ModelingCreateBoxResult failed = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Must Roll Back",
    });
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 0u);
    EXPECT_EQ(scene.entities(), entities_before);
    EXPECT_EQ(scene.selected_entities(), selected_before);
    EXPECT_EQ(scene.primary_selection(), sentinel);
    EXPECT_EQ(scene.revision(), scene_revision_before);
    EXPECT_EQ(scene.selection_revision(), scene_selection_revision_before);
    EXPECT_EQ(assets.mesh_ids(), meshes_before);
    EXPECT_EQ(assets.revision(), asset_revision_before);
    EXPECT_EQ(service.content_revision(), content_revision_before);
    EXPECT_EQ(service.selection_revision(), selection_revision_before);

    service.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    const ModelingCreateBoxResult retried = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Retry Succeeds",
    });
    ASSERT_TRUE(retried.ok) << retried.message;
    EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 1u);
    EXPECT_EQ(retried.object.modeling_object_id, 1);
  }
}

TEST(ModelingSessionServiceSplitTests,
     SdkSelectionTranslationPreviewAndCountersStayEventDriven) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const ModelingCreateBoxResult created = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "SDK Interaction Box",
  });
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(service
                  .set_component_selection({
                      .modeling_object_id = created.object.modeling_object_id,
                      .mode = ModelingSelectionMode::Face,
                      .faces = {1},
                      .edit = ModelingSelectionEdit::Replace,
                      .active_kind = ModelingActiveSelectionKind::Face,
                      .active_face = 1,
                  })
                  .ok);
  EXPECT_EQ(service.selection_summary().selected_face_count, 1u);
  const ModelingSdkProjectionStats after_initial_summary =
      service.modeling_sdk_projection_stats_for_tests();
  for (int index = 0; index < 16; ++index) {
    EXPECT_EQ(service.selection_summary().selected_face_count, 1u);
  }
  EXPECT_EQ(service.modeling_sdk_projection_stats_for_tests()
                .selection_summary_build_count,
            after_initial_summary.selection_summary_build_count);

  const ModelingSdkProjectionStats before_move =
      service.modeling_sdk_projection_stats_for_tests();
  ASSERT_TRUE(service.move_selection({1.0f, 0.0f, 0.0f}).ok);
  const ModelingSdkProjectionStats after_move =
      service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_move.mesh_compile_count,
            before_move.mesh_compile_count + 1u);
  EXPECT_EQ(after_move.asset_publication_count,
            before_move.asset_publication_count + 1u);
  EXPECT_EQ(after_move.scene_publication_count,
            before_move.scene_publication_count + 1u);

  const MeshComponent *binding = scene.mesh(created.object.entity);
  ASSERT_NE(binding, nullptr);
  const MeshAsset *committed = assets.mesh(binding->mesh);
  ASSERT_NE(committed, nullptr);
  const MeshGeometryPayload committed_geometry = committed->geometry();
  const std::uint64_t mesh_revision_before_preview =
      assets.mesh_revision(binding->mesh).value;
  const std::uint64_t asset_revision_before_preview = assets.revision();
  const std::uint64_t scene_revision_before_preview = scene.revision();

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(service.update_transform_translate_preview({0.0f, 2.0f, 0.0f})
                  .ok);
  EXPECT_TRUE(service.can_cancel_active_preview_for_undo());
  EXPECT_TRUE(service.transient_transform_preview_active());
  const ModelingSdkProjectionStats after_preview =
      service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_preview.preview_publication_count,
            after_move.preview_publication_count);
  EXPECT_EQ(after_preview.mesh_compile_count, after_move.mesh_compile_count);
  EXPECT_EQ(after_preview.asset_publication_count,
            after_move.asset_publication_count);
  EXPECT_EQ(after_preview.scene_publication_count,
            after_move.scene_publication_count);
  EXPECT_EQ(assets.mesh_revision(binding->mesh).value,
            mesh_revision_before_preview);
  EXPECT_EQ(assets.revision(), asset_revision_before_preview);
  EXPECT_EQ(scene.revision(), scene_revision_before_preview);
  const std::vector<ModelingToolPreviewMesh> preview_meshes =
      service.tool_preview_meshes();
  ASSERT_EQ(preview_meshes.size(), 1u);
  EXPECT_TRUE(preview_meshes.front().dynamic_positions);
  EXPECT_TRUE(preview_meshes.front().suppress_source);
  ASSERT_NE(preview_meshes.front().dynamic_position_payload, nullptr);
  EXPECT_EQ(preview_meshes.front().dynamic_position_payload->size(),
            preview_meshes.front().mesh.geometry().vertices.size());
  ASSERT_TRUE(service.cancel_transform_preview().ok);
  EXPECT_FALSE(service.can_cancel_active_preview_for_undo());
  EXPECT_FALSE(service.transient_transform_preview_active());
  EXPECT_TRUE(service.tool_preview_meshes().empty());
  ASSERT_NE(assets.mesh(scene.mesh(created.object.entity)->mesh), nullptr);
  EXPECT_EQ(assets.mesh(scene.mesh(created.object.entity)->mesh)
                ->geometry()
                .content_id(),
            committed_geometry.content_id());

  (void)service.selection_summary();
  const ModelingSdkProjectionStats before_queries =
      service.modeling_sdk_projection_stats_for_tests();
  for (int index = 0; index < 16; ++index) {
    (void)service.selection_summary();
    (void)service.export_selection();
    (void)service.objects();
  }
  const ModelingSdkProjectionStats after_queries =
      service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_queries.mesh_compile_count,
            before_queries.mesh_compile_count);
  EXPECT_EQ(after_queries.asset_publication_count,
            before_queries.asset_publication_count);
  EXPECT_EQ(after_queries.scene_publication_count,
            before_queries.scene_publication_count);
  EXPECT_EQ(after_queries.preview_publication_count,
            before_queries.preview_publication_count);
  EXPECT_EQ(after_queries.selection_summary_build_count,
            before_queries.selection_summary_build_count);
}

TEST(ModelingSessionServiceSplitTests,
     SdkPreviewPublicationFailuresRemainRetryableAndAtomic) {
  constexpr std::array<ModelingSdkProjectionFailurePoint, 2> kFailures = {
      ModelingSdkProjectionFailurePoint::PreviewAssetReplace,
      ModelingSdkProjectionFailurePoint::PreviewSceneNotify,
  };
  for (const ModelingSdkProjectionFailurePoint failure_point : kFailures) {
    SCOPED_TRACE(static_cast<int>(failure_point));
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const ModelingCreateBoxResult created = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Preview Failure Box",
    });
    ASSERT_TRUE(created.ok) << created.message;
    ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
    ASSERT_TRUE(service
                    .set_component_selection({
                        .modeling_object_id = created.object.modeling_object_id,
                        .mode = ModelingSelectionMode::Face,
                        .faces = {1},
                        .edit = ModelingSelectionEdit::Replace,
                        .active_kind = ModelingActiveSelectionKind::Face,
                        .active_face = 1,
                    })
                    .ok);
    ASSERT_TRUE(service.begin_transform_preview(false).ok);
    const AssetId mesh = scene.mesh(created.object.entity)->mesh;
    const MeshGeometryContentId content_before =
        assets.mesh(mesh)->geometry().content_id();
    const std::uint64_t asset_revision_before = assets.revision();
    const std::uint64_t scene_revision_before = scene.revision();

    service.set_modeling_sdk_projection_failure_for_tests(failure_point);
    const ModelingCommandResult failed =
        service.update_transform_extrude_preview({0.0f, 0.0f, 1.0f}, 1.0f);
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_TRUE(service.can_cancel_active_preview_for_undo());
    EXPECT_EQ(assets.revision(), asset_revision_before);
    EXPECT_EQ(scene.revision(), scene_revision_before);
    EXPECT_EQ(assets.mesh(mesh)->geometry().content_id(), content_before);

    service.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    ASSERT_TRUE(
        service.update_transform_extrude_preview({0.0f, 0.0f, 1.0f}, 1.0f)
            .ok);
    ASSERT_TRUE(service.cancel_transform_preview().ok);
    EXPECT_FALSE(service.can_cancel_active_preview_for_undo());
    EXPECT_EQ(assets.mesh(mesh)->geometry().content_id(), content_before);
  }
}

TEST(ModelingSessionServiceSplitTests,
     SdkSharedMeshPublicationFailureRestoresTheOriginalBinding) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const ModelingCreateBoxResult created = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Shared SDK Mesh",
  });
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(service
                  .set_component_selection({
                      .modeling_object_id = created.object.modeling_object_id,
                      .mode = ModelingSelectionMode::Face,
                      .faces = {1},
                      .edit = ModelingSelectionEdit::Replace,
                      .active_kind = ModelingActiveSelectionKind::Face,
                      .active_face = 1,
                  })
                  .ok);

  const MeshComponent *original_binding = scene.mesh(created.object.entity);
  ASSERT_NE(original_binding, nullptr);
  const AssetId original_mesh = original_binding->mesh;
  const EntityId shared_entity =
      scene.create_entity("Shared Mesh Consumer", EntityKind::Mesh);
  ASSERT_NE(shared_entity, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(shared_entity, {.mesh = original_mesh}));
  const std::vector<AssetId> mesh_ids_before = assets.mesh_ids();
  const std::uint64_t scene_revision_before = scene.revision();
  const std::uint64_t asset_revision_before = assets.revision();

  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
  const ModelingCommandResult failed =
      service.move_selection({1.0f, 0.0f, 0.0f});
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  ASSERT_NE(scene.mesh(created.object.entity), nullptr);
  ASSERT_NE(scene.mesh(shared_entity), nullptr);
  EXPECT_EQ(scene.mesh(created.object.entity)->mesh, original_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, original_mesh);
  EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);
  EXPECT_EQ(scene.revision(), scene_revision_before);
  EXPECT_EQ(assets.revision(), asset_revision_before);
}

TEST(ModelingSessionServiceSplitTests,
     SdkProjectionRejectsMissingHostEntityOrAssetWithoutSdkDrift) {
  for (const bool remove_entity : {false, true}) {
    SCOPED_TRACE(remove_entity ? "missing entity" : "missing asset");
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const ModelingCreateBoxResult created = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Broken Host Projection",
    });
    ASSERT_TRUE(created.ok) << created.message;
    ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
    ASSERT_TRUE(service
                    .set_component_selection({
                        .modeling_object_id =
                            created.object.modeling_object_id,
                        .mode = ModelingSelectionMode::Face,
                        .faces = {1},
                        .edit = ModelingSelectionEdit::Replace,
                        .active_kind = ModelingActiveSelectionKind::Face,
                        .active_face = 1,
                    })
                    .ok);
    const ModelingSelectionSummary before = service.selection_summary();
    if (remove_entity) {
      ASSERT_TRUE(scene.destroy_entity(created.object.entity));
    } else {
      ASSERT_TRUE(assets.remove_mesh(created.object.mesh));
    }

    const ModelingCommandResult failed =
        service.move_selection({1.0f, 0.0f, 0.0f});
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 1u);
    EXPECT_EQ(service.selection_summary().content_revision,
              before.content_revision);
    EXPECT_EQ(service.selection_summary().selection_revision,
              before.selection_revision);
  }
}

TEST(ModelingSessionServiceSplitTests,
     SdkSelectionAndCommittedMoveFailuresRestoreExactObservableState) {
  {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const ModelingCreateBoxResult created = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Selection Rollback Box",
    });
    ASSERT_TRUE(created.ok) << created.message;
    const ModelingSelectionSummary before = service.selection_summary();
    const std::uint64_t scene_revision_before = scene.revision();
    const std::uint64_t scene_selection_revision_before =
        scene.selection_revision();
    service.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::SelectionScenePublication);
    const ModelingCommandResult failed =
        service.set_selection_mode(ModelingSelectionMode::Face);
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Object);
    EXPECT_EQ(service.selection_summary().content_revision,
              before.content_revision);
    EXPECT_EQ(service.selection_summary().selection_revision,
              before.selection_revision);
    EXPECT_EQ(scene.revision(), scene_revision_before);
    EXPECT_EQ(scene.selection_revision(), scene_selection_revision_before);
    service.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  }

  constexpr std::array<ModelingSdkProjectionFailurePoint, 3> kMoveFailures = {
      ModelingSdkProjectionFailurePoint::AfterSdkMutation,
      ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify,
  };
  for (const ModelingSdkProjectionFailurePoint failure_point : kMoveFailures) {
    SCOPED_TRACE(static_cast<int>(failure_point));
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const ModelingCreateBoxResult created = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Move Rollback Box",
    });
    ASSERT_TRUE(created.ok) << created.message;
    ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
    ASSERT_TRUE(service
                    .set_component_selection({
                        .modeling_object_id =
                            created.object.modeling_object_id,
                        .mode = ModelingSelectionMode::Face,
                        .faces = {1},
                        .edit = ModelingSelectionEdit::Replace,
                        .active_kind = ModelingActiveSelectionKind::Face,
                        .active_face = 1,
                    })
                    .ok);
    const AssetId mesh = scene.mesh(created.object.entity)->mesh;
    const MeshGeometryContentId content_before =
        assets.mesh(mesh)->geometry().content_id();
    const ModelingSelectionSummary selection_before =
        service.selection_summary();
    const std::uint64_t scene_revision_before = scene.revision();
    const std::uint64_t asset_revision_before = assets.revision();

    service.set_modeling_sdk_projection_failure_for_tests(failure_point);
    const ModelingCommandResult failed =
        service.move_selection({1.0f, 0.0f, 0.0f});
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(service.selection_summary().content_revision,
              selection_before.content_revision);
    EXPECT_EQ(service.selection_summary().selection_revision,
              selection_before.selection_revision);
    EXPECT_EQ(scene.revision(), scene_revision_before);
    EXPECT_EQ(assets.revision(), asset_revision_before);
    ASSERT_NE(assets.mesh(mesh), nullptr);
    EXPECT_EQ(assets.mesh(mesh)->geometry().content_id(), content_before);

    service.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_TRUE(service.move_selection({1.0f, 0.0f, 0.0f}).ok);
  }
}














TEST(ModelingSessionServiceSplitTests,
     SceneAwareDeleteRestoreUsesRuntimeMapInsteadOfStaleDuplicateSceneScan) {
  SceneDocument scene;
  ModelingObjectRuntimeMap object_map;
  const int modeling_object_id = 42;

  const EntityId stale =
      scene.create_entity("Stale Duplicate", EntityKind::Mesh);
  scene.set_modeling_object(
      stale, ModelingObjectComponent{.object_id = modeling_object_id});

  const EntityId selected =
      scene.create_entity("Selected Runtime", EntityKind::Mesh);
  scene.set_modeling_object(
      selected, ModelingObjectComponent{.object_id = modeling_object_id});
  ASSERT_TRUE(scene.set_selected(selected, true));
  ASSERT_TRUE(scene.set_active_selection(selected));
  object_map.bind(modeling_object_id, selected);

  std::unique_ptr<IUndoableCommand> command =
      create_scene_aware_delete_selection_command(
          scene,
          SceneAwareDeleteRuntime{
              .capture_modeling_memento =
                  [] { return ModelingSessionMemento{}; },
              .restore_modeling_memento =
                  [&](const ModelingSessionMemento &) {
                    const EntityId restored = scene.create_entity(
                        "Runtime Restored", EntityKind::Mesh);
                    scene.set_modeling_object(
                        restored, ModelingObjectComponent{
                                      .object_id = modeling_object_id});
                    object_map.bind(modeling_object_id, restored);
                    return ModelingCommandResult{.ok = true, .changed = true};
                  },
              .app_selection_mode =
                  [] { return ModelingSelectionMode::Object; },
              .set_app_selection_mode = [](ModelingSelectionMode) {},
              .delete_modeling_objects =
                  [&](const std::vector<int> &object_ids) {
                    EXPECT_EQ(object_ids, std::vector<int>{modeling_object_id});
                    object_map.unbind_object(modeling_object_id);
                    return ModelingCommandResult{.ok = true, .changed = true};
                  },
              .entity_for_modeling_object =
                  [&](int object_id) {
                    return object_map.entity_for_modeling_object(object_id);
                  },
              .sync_object_selection_from_scene = [](bool) {},
              .clear_runtime_object_selection_for_group_mode = [] {},
              .invalidate_selection_overlay_caches = [] {},
          });

  const CommandResult deleted = command->redo();
  ASSERT_TRUE(deleted.ok) << deleted.message;
  EXPECT_FALSE(scene.exists(selected));
  ASSERT_TRUE(scene.exists(stale));
  ASSERT_NE(scene.name(stale), nullptr);
  EXPECT_EQ(scene.name(stale)->name, "Stale Duplicate");

  const CommandResult restored = command->undo();
  ASSERT_TRUE(restored.ok) << restored.message;
  const std::optional<EntityId> restored_entity =
      object_map.entity_for_modeling_object(modeling_object_id);
  ASSERT_TRUE(restored_entity.has_value());
  ASSERT_TRUE(scene.exists(*restored_entity));
  ASSERT_NE(scene.name(*restored_entity), nullptr);
  EXPECT_EQ(scene.name(*restored_entity)->name, "Selected Runtime");
  ASSERT_NE(scene.name(stale), nullptr);
  EXPECT_EQ(scene.name(stale)->name, "Stale Duplicate");
}

TEST(ModelingSessionServiceSplitTests, ServicesOwnIndependentModelingState) {
  TransformDragController drag;
  EXPECT_FALSE(drag.preview_driven());
  drag.set_preview_driven(true, true);
  EXPECT_TRUE(drag.preview_driven());
  EXPECT_TRUE(drag.duplicate_selected());
  drag.clear();
  EXPECT_FALSE(drag.preview_driven());
  const TransformDragPreviewRouting object_transform =
      TransformDragController::preview_routing(false, true, true);
  EXPECT_FALSE(object_transform.preview_driven);
  EXPECT_FALSE(object_transform.needs_runtime_preview_transaction);
  EXPECT_TRUE(object_transform.scene_transform_preview_eligible);
  const TransformDragPreviewRouting duplicate_object_transform =
      TransformDragController::preview_routing(true, true, true);
  EXPECT_FALSE(duplicate_object_transform.preview_driven);
  EXPECT_TRUE(duplicate_object_transform.needs_runtime_preview_transaction);
  EXPECT_TRUE(duplicate_object_transform.scene_transform_preview_eligible);
  const TransformDragPreviewRouting duplicate_scene_object_transform =
      TransformDragController::preview_routing(true, true, true, true);
  EXPECT_FALSE(duplicate_scene_object_transform.preview_driven);
  EXPECT_FALSE(
      duplicate_scene_object_transform.needs_runtime_preview_transaction);
  EXPECT_TRUE(
      duplicate_scene_object_transform.scene_transform_preview_eligible);
  const TransformDragPreviewRouting component_transform =
      TransformDragController::preview_routing(false, false, true);
  EXPECT_TRUE(component_transform.preview_driven);
  EXPECT_TRUE(component_transform.needs_runtime_preview_transaction);
  EXPECT_FALSE(component_transform.scene_transform_preview_eligible);
  const TransformDragPreviewRouting object_extrude =
      TransformDragController::preview_routing(false, true, false);
  EXPECT_FALSE(object_extrude.preview_driven);
  EXPECT_FALSE(object_extrude.needs_runtime_preview_transaction);
  EXPECT_FALSE(object_extrude.scene_transform_preview_eligible);

  ModelingToolSettingsService tool_settings;
  EXPECT_EQ(tool_settings.revision(), 0u);
  tool_settings.set_preview_metadata("edge_bevel", "edge_bevel", "Edge Bevel");
  const std::uint64_t metadata_revision = tool_settings.revision();
  EXPECT_GT(metadata_revision, 0u);
  tool_settings.set_preview_metadata("edge_bevel", "edge_bevel", "Edge Bevel");
  EXPECT_EQ(tool_settings.revision(), metadata_revision);
  const ModelingToolSettingsTarget target =
      tool_settings.operation_preview_target(
          ModelingToolSettingsTargetKind::EdgeBevel, "Fallback",
          "fallback_operation");
  EXPECT_TRUE(target.active);
  EXPECT_TRUE(target.operation_preview_active);
  EXPECT_EQ(target.action_id, "edge_bevel");
  EXPECT_EQ(target.operation_id, "edge_bevel");
  EXPECT_EQ(target.label, "Edge Bevel");
  tool_settings.clear_preview_metadata();
  EXPECT_GT(tool_settings.revision(), metadata_revision);
  const std::uint64_t cleared_metadata_revision = tool_settings.revision();
  tool_settings.clear_preview_metadata();
  EXPECT_EQ(tool_settings.revision(), cleared_metadata_revision);
  const ModelingToolSettingsTarget fallback =
      tool_settings.operation_preview_target(
          ModelingToolSettingsTargetKind::Thicken, "Thicken", "thicken");
  EXPECT_EQ(fallback.label, "Thicken");
  EXPECT_EQ(fallback.operation_id, "thicken");
  const ModelingToolSettingsTarget preview_target =
      tool_settings.active_preview_target(
          quader_editor_core::PreviewTransactionKind::EdgeBevel);
  EXPECT_TRUE(preview_target.active);
  EXPECT_TRUE(preview_target.operation_preview_active);
  EXPECT_EQ(preview_target.kind, ModelingToolSettingsTargetKind::EdgeBevel);
  EXPECT_EQ(preview_target.operation_id, "edge_bevel");
  EXPECT_EQ(preview_target.label, "Bevel Edges");
  quader_editor_core::EditorRuntimeStatus runtime_status;
  runtime_status.cut_active = true;
  const ModelingToolSettingsTarget runtime_target =
      tool_settings.active_runtime_tool_target(runtime_status);
  EXPECT_TRUE(runtime_target.active);
  EXPECT_FALSE(runtime_target.operation_preview_active);
  EXPECT_EQ(runtime_target.kind, ModelingToolSettingsTargetKind::Cut);
  EXPECT_EQ(runtime_target.label, "Cut");

  ModelingInteractionSettingsService interaction_settings;
  EXPECT_EQ(interaction_settings.revision(), 0u);
  EXPECT_EQ(interaction_settings.settings().cut_mode,
            ModelingCutMode::KeepBoth);
  interaction_settings.set_cut_mode(ModelingCutMode::DiscardLeft);
  const std::uint64_t cut_mode_revision = interaction_settings.revision();
  EXPECT_GT(cut_mode_revision, 0u);
  interaction_settings.set_cut_mode(ModelingCutMode::DiscardLeft);
  EXPECT_EQ(interaction_settings.revision(), cut_mode_revision);
  EXPECT_EQ(interaction_settings.settings().cut_mode,
            ModelingCutMode::DiscardLeft);
  interaction_settings.cycle_cut_mode_without_runtime();
  EXPECT_GT(interaction_settings.revision(), cut_mode_revision);
  EXPECT_EQ(interaction_settings.settings().cut_mode,
            ModelingCutMode::DiscardRight);
}

TEST(MaterialReferenceServiceTests,
     SelectedFaceFailureRollsBackTopologyBindingAndSelectionExactly) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService modeling(scene, assets);
  const ModelingCreateBoxResult created = modeling.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Material Reference Rollback Box",
  });
  ASSERT_TRUE(created.ok) << created.message;
  constexpr quader_poly::ElementId kSelectedFace = 1;
  const MaterialReference source(101u);
  const MaterialReference replacement(202u);
  const MaterialComponent component_before{.material = source};
  ASSERT_TRUE(scene.set_material(created.object.entity, component_before));
  ASSERT_TRUE(modeling.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(modeling
                  .set_component_selection({
                      .modeling_object_id = created.object.modeling_object_id,
                      .mode = ModelingSelectionMode::Face,
                      .faces = {kSelectedFace},
                      .edit = ModelingSelectionEdit::Replace,
                      .active_kind = ModelingActiveSelectionKind::Face,
                      .active_face = kSelectedFace,
                  })
                  .ok);

  const MeshComponent *mesh_component = scene.mesh(created.object.entity);
  ASSERT_NE(mesh_component, nullptr);
  const MeshAsset *mesh_before = assets.mesh(mesh_component->mesh);
  ASSERT_NE(mesh_before, nullptr);
  ASSERT_TRUE(mesh_before->has_authored_polygon_document);
  ASSERT_NE(mesh_before->authored_polygon_document, nullptr);
  const quader_poly::Face *face_before = quader_poly::find_face(
      *mesh_before->authored_polygon_document, kSelectedFace);
  ASSERT_NE(face_before, nullptr);
  const std::uint32_t slot_before = face_before->material_slot;
  const ModelingSelectionExport selection_before = modeling.export_selection();

  MaterialReferenceService references(scene, assets, modeling);
  MaterialReferenceReplacementPreparation prepared =
      references.prepare_replacement(
          source, replacement, MaterialReferenceUsageScope::Selection,
          [](const MaterialReference &) { return true; });
  ASSERT_TRUE(prepared.ok) << prepared.message;
  ASSERT_TRUE(prepared.changed);
  ASSERT_NE(prepared.command, nullptr);

  modeling.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedAssetReplace);
  const CommandResult failed = prepared.command->redo();
  modeling.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  ASSERT_NE(scene.material(created.object.entity), nullptr);
  EXPECT_EQ(*scene.material(created.object.entity), component_before);

  mesh_component = scene.mesh(created.object.entity);
  ASSERT_NE(mesh_component, nullptr);
  const MeshAsset *mesh_after = assets.mesh(mesh_component->mesh);
  ASSERT_NE(mesh_after, nullptr);
  ASSERT_NE(mesh_after->authored_polygon_document, nullptr);
  const quader_poly::Face *face_after = quader_poly::find_face(
      *mesh_after->authored_polygon_document, kSelectedFace);
  ASSERT_NE(face_after, nullptr);
  EXPECT_EQ(face_after->material_slot, slot_before);

  const ModelingSelectionExport selection_after = modeling.export_selection();
  EXPECT_EQ(selection_after.summary.mode, selection_before.summary.mode);
  ASSERT_EQ(selection_after.objects.size(), selection_before.objects.size());
  ASSERT_EQ(selection_after.objects.size(), 1u);
  EXPECT_EQ(selection_after.objects.front().modeling_object_id,
            selection_before.objects.front().modeling_object_id);
  ASSERT_EQ(selection_after.objects.front().selected_faces.size(), 1u);
  ASSERT_EQ(selection_before.objects.front().selected_faces.size(), 1u);
  EXPECT_EQ(selection_after.objects.front().selected_faces.front().id,
            selection_before.objects.front().selected_faces.front().id);
}

TEST(ModelingSessionServiceSplitTests,
     ResetForTargetRestoresOnlyThatOperationInOneRevision) {
  ModelingInteractionSettingsService service;
  ModelingInteractionSettings edited = service.settings();
  edited.edge_bevel_width = 4.0;
  edited.half_cut_axis = ModelingHalfCutAxis::Z;
  edited.half_cut_separate_parts = true;
  edited.half_cut_fill_parts = false;
  ASSERT_TRUE(service.set_normalized(edited));
  const std::uint64_t edited_revision = service.revision();

  EXPECT_TRUE(
      service.reset_for_target(ModelingToolSettingsTargetKind::HalfCut));
  EXPECT_EQ(service.revision(), edited_revision + 1u);
  const ModelingInteractionSettings reset = service.settings();
  EXPECT_EQ(reset.half_cut_axis, ModelingHalfCutAxis::X);
  EXPECT_FALSE(reset.half_cut_separate_parts);
  EXPECT_TRUE(reset.half_cut_fill_parts);
  EXPECT_DOUBLE_EQ(reset.edge_bevel_width, 4.0);

  EXPECT_FALSE(
      service.reset_for_target(ModelingToolSettingsTargetKind::HalfCut));
  EXPECT_EQ(service.revision(), edited_revision + 1u);
  EXPECT_FALSE(
      service.reset_for_target(ModelingToolSettingsTargetKind::Mirror));
  EXPECT_EQ(service.revision(), edited_revision + 1u);
}

TEST(ModelingSessionServiceSplitTests, PolicyServicesExposeDirectBehavior) {
  ModelingSelectionSummary component_selection;
  EXPECT_FALSE(
      ModelingSelectionService::has_component_selection(component_selection));
  component_selection.selected_edge_count = 1;
  EXPECT_TRUE(
      ModelingSelectionService::has_component_selection(component_selection));

  ModelingToolStatus status;
  EXPECT_FALSE(ModelingOverlayService::has_modal_overlay(status));
  status.cut_has_line = true;
  EXPECT_TRUE(ModelingOverlayService::has_modal_overlay(status));

  quader_editor_core::EditorRuntimeState runtime_state;
  runtime_state.active_tool = quader_editor_core::ViewportInteractionTool::Cut;
  runtime_state.cut.points = {{1.0f, 2.0f, 3.0f}};
  runtime_state.cut.mode = quader_editor_core::CutMode::KeepFront;
  runtime_state.cut.dragging_new_point = true;
  runtime_state.cut.dragging_point_index = 0;
  runtime_state.cut.preview_valid = true;
  runtime_state.cut_hover.valid = true;
  runtime_state.cut_hover.position = {4.0f, 5.0f, 6.0f};
  runtime_state.mirror.plane.points = {{7.0f, 8.0f, 9.0f}};
  runtime_state.mirror.plane.dragging_line = true;
  runtime_state.mirror.plane.drag_line_anchor = {7.0f, 8.0f, 9.0f};
  runtime_state.mirror.preview_valid = true;
  runtime_state.mirror_hover.valid = true;
  runtime_state.mirror_hover.position = {10.0f, 11.0f, 12.0f};
  runtime_state.poly.active = true;
  runtime_state.poly.closed = true;
  runtime_state.poly.has_current = true;
  runtime_state.poly.points = {
      {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};

  const ModelingToolStatus projected_status =
      ModelingOverlayService{}.tool_status(
          quader_editor_core::EditorRuntime(runtime_state), true);
  EXPECT_EQ(projected_status.active_tool, ModelingViewportTool::Cut);
  EXPECT_TRUE(projected_status.modal_tool_owns_viewport);
  EXPECT_TRUE(projected_status.cut_active);
  EXPECT_TRUE(projected_status.cut_dragging);
  EXPECT_TRUE(projected_status.cut_dragging_new_point);
  EXPECT_EQ(projected_status.cut_dragging_point_index, 0);
  EXPECT_EQ(projected_status.cut_mode, ModelingCutMode::DiscardRight);
  EXPECT_EQ(projected_status.cut_captured_point_count, 1);
  EXPECT_EQ(projected_status.cut_point_count, 2);
  EXPECT_TRUE(projected_status.cut_has_line);
  expect_vec3_eq(projected_status.cut_points[0], {1.0f, 2.0f, 3.0f});
  expect_vec3_eq(projected_status.cut_points[1], {4.0f, 5.0f, 6.0f});
  EXPECT_TRUE(projected_status.mirror_active);
  EXPECT_TRUE(projected_status.mirror_dragging);
  EXPECT_TRUE(projected_status.mirror_dragging_line);
  EXPECT_EQ(projected_status.mirror_captured_point_count, 1);
  EXPECT_EQ(projected_status.mirror_point_count, 2);
  EXPECT_TRUE(projected_status.mirror_has_line);
  expect_vec3_eq(projected_status.mirror_points[0], {7.0f, 8.0f, 9.0f});
  expect_vec3_eq(projected_status.mirror_points[1], {10.0f, 11.0f, 12.0f});
  EXPECT_TRUE(projected_status.poly_active);
  EXPECT_TRUE(projected_status.poly_closed);
  EXPECT_TRUE(projected_status.poly_has_current);
  EXPECT_TRUE(projected_status.poly_can_commit);
  EXPECT_EQ(projected_status.poly_point_count, 3);

  EXPECT_TRUE(ModalToolRuntime::owns_viewport(ModelingViewportTool::Box));
  EXPECT_TRUE(ModalToolRuntime::owns_viewport(ModelingViewportTool::Mirror));
  EXPECT_FALSE(ModalToolRuntime::owns_viewport(ModelingViewportTool::Move));
  for (const ToolDescriptor &descriptor : viewport_tool_descriptors()) {
    if (descriptor.editor_owned_native_lifecycle) {
      continue;
    }
    EXPECT_EQ(ModalToolRuntime::owns_viewport(descriptor.modeling_tool),
              descriptor.owns_viewport)
        << descriptor.id;
  }
  EXPECT_TRUE(ModelingMeshSyncService::should_force_refresh(true, false));
  EXPECT_TRUE(ModelingMeshSyncService::should_force_refresh(false, true));
  EXPECT_FALSE(ModelingMeshSyncService::should_force_refresh(false, false));
  EXPECT_FALSE(ModelingCommandExecutor::runtime_unavailable().ok);
  EXPECT_STREQ(
      fallback_tool_settings_label(ModelingToolSettingsTargetKind::EdgeBevel),
      "Bevel Edges");
  EXPECT_STREQ(
      fallback_tool_settings_label(ModelingToolSettingsTargetKind::Thicken),
      "Thicken");
  EXPECT_STREQ(
      fallback_tool_settings_label(ModelingToolSettingsTargetKind::HalfCut),
      "Half-Cut");
  EXPECT_STREQ(
      fallback_tool_settings_label(ModelingToolSettingsTargetKind::None),
      "Tool Settings");
}

TEST(ModelingSessionServiceSplitTests,
     ContextActionServiceAppliesModalQueryState) {
  ModelingContextActionService service;
  ModelingActionQuery query;
  EXPECT_FALSE(query.modal_tool_owns_viewport);

  const ModelingActionQuery effective =
      service.effective_action_query(query, true);
  EXPECT_TRUE(effective.modal_tool_owns_viewport);

  query.suppress_when_modal_tool_owns_viewport = false;
  const ModelingActionQuery unsuppressed =
      service.effective_action_query(query, true);
  EXPECT_FALSE(unsuppressed.modal_tool_owns_viewport);

  ModelingSelectionSummary summary;
  summary.mode = ModelingSelectionMode::Object;
  summary.has_selection = true;
  summary.selected_object_count = 1;
  const ModelingActionSet action_set = service.action_set(summary, {}, false);
  EXPECT_EQ(action_set.surface, ModelingActionSurface::OperationsPanel);
  EXPECT_EQ(action_set.surface_id, "operations_top_bar");

  const ModelingContextActionResolution modal_result =
      service.resolve_executable_action(summary, "delete_selection", true);
  EXPECT_FALSE(modal_result.ok);
  EXPECT_EQ(modal_result.message, "Viewport is owned by an active modal tool.");

  ModelingSelectionSummary empty_summary;
  empty_summary.mode = ModelingSelectionMode::Object;
  const ModelingContextActionResolution disabled_result =
      service.resolve_executable_action(empty_summary, "delete_selection",
                                        false);
  EXPECT_FALSE(disabled_result.ok);
  EXPECT_EQ(disabled_result.message,
            "Action is disabled for the current selection.");
  EXPECT_EQ(disabled_result.action.disabled_reason_key,
            "modeling.action.disabled.delete_selection");

  const ModelingContextActionResolution enabled_result =
      service.resolve_executable_action(summary, "delete_selection", false);
  EXPECT_TRUE(enabled_result.ok);
  EXPECT_EQ(enabled_result.action.action_id, "delete_selection");
}

TEST(ModelingSessionServiceSplitTests,
     InstantOperationEnablementUsesHostSingleObjectConstraint) {
  const ModelingInstantOperationCatalogEntry *connect =
      find_modeling_instant_operation_catalog_entry(
          ModelingInstantOperationId::ConnectVertices);
  ASSERT_NE(connect, nullptr);
  const ModelingInstantOperationCatalogEntry *combine_faces =
      find_modeling_instant_operation_catalog_entry(
          ModelingInstantOperationId::CombineFaces);
  ASSERT_NE(combine_faces, nullptr);

  ModelingSelectionSummary summary;
  summary.mode = ModelingSelectionMode::Vertex;
  summary.has_selection = true;
  summary.active_kind = ModelingActiveSelectionKind::Vertex;
  summary.selected_vertex_count = 1;
  summary.selected_object_count = 1;

  EXPECT_FALSE(modeling_instant_operation_enabled(*connect, summary));

  summary.selected_vertex_count = 2;
  summary.selected_object_count = 2;

  EXPECT_FALSE(modeling_instant_operation_enabled(*connect, summary));

  summary.selected_object_count = 1;
  EXPECT_TRUE(modeling_instant_operation_enabled(*connect, summary));

  ModelingSelectionSummary face_summary;
  face_summary.mode = ModelingSelectionMode::Face;
  face_summary.has_selection = true;
  face_summary.selected_face_count = 1;
  face_summary.selected_object_count = 1;

  EXPECT_FALSE(
      modeling_instant_operation_enabled(*combine_faces, face_summary));

  face_summary.selected_face_count = 2;
  EXPECT_TRUE(modeling_instant_operation_enabled(*combine_faces, face_summary));
}

TEST(ModelingSessionServiceSplitTests,
     RadialAlignAvailabilityExcludesObjectAndGroupSelectionModes) {
  const ModelingInstantOperationCatalogEntry *radial_align =
      find_modeling_instant_operation_catalog_entry(
          ModelingInstantOperationId::RadialAlign);
  ASSERT_NE(radial_align, nullptr);

  ModelingSelectionSummary summary{
      .mode = ModelingSelectionMode::Group,
      .has_selection = true,
      .active_kind = ModelingActiveSelectionKind::Object,
      .selected_object_count = 1,
  };
  EXPECT_FALSE(modeling_instant_operation_enabled(*radial_align, summary));

  summary.mode = ModelingSelectionMode::Object;
  EXPECT_FALSE(modeling_instant_operation_enabled(*radial_align, summary));

  summary.mode = ModelingSelectionMode::Vertex;
  summary.active_kind = ModelingActiveSelectionKind::Vertex;
  summary.selected_vertex_count = 3;
  EXPECT_TRUE(modeling_instant_operation_enabled(*radial_align, summary));
}


TEST(ModelingSessionServiceSplitTests,
     InstantOperationSettingsComeFromSharedOperationSettingsOwnerExceptNormalBridge) {
  ModelingInteractionSettings interaction_settings;
  interaction_settings.bridge_steps = 4;

  const std::optional<quader_editor_core::EditorOperationCommand> bridge =
      modeling_instant_operation_command(ModelingInstantOperationId::Bridge,
                                         interaction_settings);
  ASSERT_TRUE(bridge.has_value());
  EXPECT_EQ(bridge->operation_id,
            quader_editor_core::EditorOperationId::Bridge);
  const auto &bridge_settings =
      std::get<quader_editor_core::BridgeOperationSettings>(bridge->settings);
  const auto interpolated_bridge =
      std::get<quader_editor_core::BridgeOperationSettings>(
          settings_for_operation(quader_editor_core::EditorOperationId::Bridge,
                                 interaction_settings));
  EXPECT_EQ(interpolated_bridge.steps, 4);
  EXPECT_EQ(bridge_settings.steps, 1);

  const std::optional<quader_editor_core::EditorOperationCommand> bevel =
      modeling_instant_operation_command(
          ModelingInstantOperationId::BevelVertices, interaction_settings);
  ASSERT_TRUE(bevel.has_value());
  EXPECT_TRUE(
      std::holds_alternative<
          quader_editor_core::BevelVerticesOperationSettings>(bevel->settings));

  const std::optional<quader_editor_core::EditorOperationCommand> recalculate =
      modeling_instant_operation_command(
          ModelingInstantOperationId::RecalculateFaceNormals,
          interaction_settings);
  ASSERT_TRUE(recalculate.has_value());
  const auto &recalculate_settings =
      std::get<quader_editor_core::RecalculateFaceNormalsOperationSettings>(
          recalculate->settings);
  EXPECT_TRUE(recalculate_settings.outside);
}

TEST(ModelingSessionServiceSplitTests,
     ToolSettingMutationComesFromOperationSettingsOwner) {
  ModelingInteractionSettings settings;

  EXPECT_TRUE(
      apply_modeling_interaction_setting(settings, "edge_bevel", "width", 2.5));
  EXPECT_DOUBLE_EQ(settings.edge_bevel_width, 2.5);

  EXPECT_TRUE(apply_modeling_interaction_setting(settings, "edge_bevel",
                                                 "edge_bevel_segments", 5.0));
  EXPECT_EQ(settings.edge_bevel_segments, 5);

  EXPECT_TRUE(apply_modeling_interaction_setting(
      settings, "edge_bevel", "operation.edge_bevel.profile", 0.25));
  EXPECT_DOUBLE_EQ(settings.edge_bevel_profile, 0.25);

  EXPECT_TRUE(apply_modeling_interaction_setting(settings, "thicken",
                                                 "from_center", 1.0));
  EXPECT_TRUE(settings.thicken_from_center);

  EXPECT_TRUE(
      apply_modeling_interaction_setting(settings, "", "distance", 0.0625));
  EXPECT_DOUBLE_EQ(settings.merge_by_distance_tolerance, 0.0625);

  EXPECT_TRUE(apply_modeling_interaction_setting(
      settings, "half_cut", "operation.half_cut.axis", 2.0));
  EXPECT_EQ(settings.half_cut_axis, ModelingHalfCutAxis::Z);
  EXPECT_TRUE(apply_modeling_interaction_setting(
      settings, "half_cut", "separate_parts", 1.0));
  EXPECT_TRUE(settings.half_cut_separate_parts);
  EXPECT_TRUE(apply_modeling_interaction_setting(
      settings, "half_cut", "fill_parts", 0.0));
  EXPECT_FALSE(settings.half_cut_fill_parts);

  const auto half_cut =
      std::get<quader_editor_core::HalfCutOperationSettings>(
          settings_for_operation(quader_editor_core::EditorOperationId::HalfCut,
                                 settings));
  EXPECT_EQ(half_cut.axis, quader_editor_core::HalfCutAxis::Z);
  EXPECT_TRUE(half_cut.separate_parts);
  EXPECT_FALSE(half_cut.fill_parts);

  EXPECT_FALSE(apply_modeling_interaction_setting(settings, "edge_bevel",
                                                  "tolerance", 0.25));
  EXPECT_FALSE(apply_modeling_interaction_setting(settings, "edge_bevel",
                                                  "__missing__", 1.0));
}

TEST(ModelingSessionServiceSplitTests,
     KnifeActivationCancelAndCommitPreserveEveryPersistentSelectionMode) {
  constexpr std::array kModes{
      ModelingSelectionMode::Vertex,
      ModelingSelectionMode::Edge,
      ModelingSelectionMode::Face,
      ModelingSelectionMode::Object,
      ModelingSelectionMode::Group,
  };
  const auto knife_event = [](float y) {
    ModelingToolRayEvent event;
    event.ray = {
        .origin = {0.0f, y, 6.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    event.camera = {
        .orthographic = true,
        .origin = event.ray.origin,
        .viewport_size = {800.0f, 600.0f},
        .orthographic_size = 4.0,
    };
    event.grid_size = 0.5;
    event.vertex_radius = 0.001;
    event.edge_radius = 0.12;
    event.snap_enabled = false;
    return event;
  };
  const auto source_wire_segments = [](const ViewportOverlayFrame &frame,
                                       int object_id) {
    std::vector<std::array<float, 6U>> segments;
    for (const ViewportOverlayLine &line : frame.lines) {
      if (line.layer != ViewportOverlayLayer::SourceWire ||
          line.element.object_id != object_id) {
        continue;
      }
      segments.push_back({line.start.x, line.start.y, line.start.z, line.end.x,
                          line.end.y, line.end.z});
    }
    return segments;
  };
  const auto source_wire_depth_stamps = [](const ViewportOverlayFrame &frame,
                                            int object_id) {
    std::vector<std::array<float, 9U>> triangles;
    for (const ViewportOverlayTriangle &triangle : frame.triangles) {
      if (triangle.layer != ViewportOverlayLayer::SourceWireDepthStamp ||
          triangle.element.object_id != object_id) {
        continue;
      }
      triangles.push_back({triangle.a.x, triangle.a.y, triangle.a.z,
                           triangle.b.x, triangle.b.y, triangle.b.z,
                           triangle.c.x, triangle.c.y, triangle.c.z});
    }
    return triangles;
  };
  const auto tool_preview_line_count = [](const ViewportOverlayFrame &frame) {
    return std::ranges::count_if(
        frame.lines, [](const ViewportOverlayLine &line) {
          return line.source == ViewportOverlaySource::Tool &&
                 line.layer == ViewportOverlayLayer::ToolPreviewLine;
        });
  };
  const auto tool_handle_count = [](const ViewportOverlayFrame &frame) {
    return std::ranges::count_if(
        frame.points, [](const ViewportOverlayPoint &point) {
          return point.source == ViewportOverlaySource::Tool &&
                 point.layer == ViewportOverlayLayer::ToolHandle;
        });
  };

  for (const ModelingSelectionMode mode : kModes) {
    SCOPED_TRACE(static_cast<int>(mode));
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const ModelingCreateBoxResult box = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Knife Persistent Mode Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.set_selection_mode(mode).ok);

    ModelingCommandResult command =
        service.activate_viewport_tool(ModelingViewportTool::Knife);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_EQ(service.selection_mode(), mode);
    EXPECT_EQ(service.selection_summary().mode, mode);

    const int object_id = box.object.modeling_object_id;
    const ViewportOverlayFrame activated_persistent =
        service.selection_overlay_frame();
    const auto activated_source_wire =
        source_wire_segments(activated_persistent, object_id);
    const auto activated_depth_stamps =
        source_wire_depth_stamps(activated_persistent, object_id);
    EXPECT_EQ(activated_source_wire.size(), 12U);
    EXPECT_FALSE(activated_depth_stamps.empty());
    const std::uint64_t activated_signature =
        service.selection_overlay_signature();

    command = service.update_knife_hover(knife_event(0.0f));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_FALSE(service.tool_status().knife_has_current);
    EXPECT_TRUE(std::ranges::none_of(
        service.volatile_overlay_frame().points,
        [](const ViewportOverlayPoint &point) {
          return point.source == ViewportOverlaySource::Tool &&
                 point.layer == ViewportOverlayLayer::ToolHandle;
        }));
    const ViewportOverlayFrame missed_persistent =
        service.selection_overlay_frame();
    EXPECT_EQ(source_wire_segments(missed_persistent, object_id),
              activated_source_wire);
    EXPECT_EQ(source_wire_depth_stamps(missed_persistent, object_id),
              activated_depth_stamps);
    EXPECT_EQ(service.selection_overlay_signature(), activated_signature);

    command = service.update_knife_hover(knife_event(-1.0f));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.tool_status().knife_has_current);
    EXPECT_TRUE(std::ranges::any_of(
        service.volatile_overlay_frame().points,
        [](const ViewportOverlayPoint &point) {
          return point.source == ViewportOverlaySource::Tool &&
                 point.layer == ViewportOverlayLayer::ToolHandle &&
                 length_squared(point.position - QVec3{0.0f, -1.0f, 1.0f}) <
                     0.0001f;
        }));

    command = service.cancel_active_modal_tool();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_EQ(service.selection_mode(), mode);

    command = service.activate_viewport_tool(ModelingViewportTool::Knife);
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_EQ(service.selection_mode(), mode);
    command = service.accept_knife_point(knife_event(-1.0f));
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_TRUE(command.changed);
    command = service.accept_knife_point(knife_event(1.0f));
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_TRUE(command.changed);
    ASSERT_TRUE(service.tool_status().knife_can_commit);

    const ViewportOverlayFrame locked_persistent =
        service.selection_overlay_frame();
    const auto locked_source_wire =
        source_wire_segments(locked_persistent, object_id);
    const auto locked_depth_stamps =
        source_wire_depth_stamps(locked_persistent, object_id);
    ASSERT_EQ(locked_source_wire.size(), 12U);
    ASSERT_FALSE(locked_depth_stamps.empty());
    const std::uint64_t locked_signature =
        service.selection_overlay_signature();

    command = service.update_knife_hover(knife_event(0.0f));
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_TRUE(service.tool_status().knife_has_current);
    const ViewportOverlayFrame valid_hover =
        service.volatile_overlay_frame();
    EXPECT_EQ(tool_preview_line_count(valid_hover), 2);
    EXPECT_EQ(tool_handle_count(valid_hover), 3);

    command = service.update_knife_hover(knife_event(3.0f));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_FALSE(service.tool_status().knife_has_current);
    const ViewportOverlayFrame invalid_hover =
        service.volatile_overlay_frame();
    EXPECT_EQ(tool_preview_line_count(invalid_hover), 1);
    EXPECT_EQ(tool_handle_count(invalid_hover), 2);
    EXPECT_TRUE(std::ranges::none_of(
        invalid_hover.lines, [](const ViewportOverlayLine &line) {
          return line.source == ViewportOverlaySource::ComponentHover;
        }));

    const ViewportOverlayFrame invalid_hover_persistent =
        service.selection_overlay_frame();
    EXPECT_EQ(source_wire_segments(invalid_hover_persistent, object_id),
              locked_source_wire);
    EXPECT_EQ(source_wire_depth_stamps(invalid_hover_persistent, object_id),
              locked_depth_stamps);
    EXPECT_EQ(service.selection_overlay_signature(), locked_signature);
    EXPECT_EQ(service.selection_mode(), mode);

    command = service.commit_knife_tool();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(service.selection_mode(), mode);
    EXPECT_EQ(service.selection_summary().mode, mode);
  }
}

TEST(ModelingSessionServiceSplitTests,
     KnifePrelockSourceWireCacheTracksActiveSelectedObject) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const ModelingCreateBoxResult first = service.create_box_from_bounds({
      .bounds = {.min = {-3.0f, -1.0f, -1.0f},
                 .max = {-1.0f, 1.0f, 1.0f}},
      .name = "Knife First Selected Source",
  });
  const ModelingCreateBoxResult second = service.create_box_from_bounds({
      .bounds = {.min = {1.0f, -1.0f, -1.0f},
                 .max = {3.0f, 1.0f, 1.0f}},
      .name = "Knife Second Selected Source",
  });
  ASSERT_TRUE(first.ok) << first.message;
  ASSERT_TRUE(second.ok) << second.message;
  const std::array selected{first.object.entity, second.object.entity};
  ASSERT_TRUE(service.set_object_selection(selected, first.object.entity).ok);
  ASSERT_TRUE(
      service.activate_viewport_tool(ModelingViewportTool::Knife).ok);

  const auto source_wire_count = [](const ViewportOverlayFrame &frame,
                                    int object_id) {
    return std::ranges::count_if(
        frame.lines, [object_id](const ViewportOverlayLine &line) {
          return line.layer == ViewportOverlayLayer::SourceWire &&
                 line.element.object_id == object_id;
        });
  };
  const std::uint64_t first_signature =
      service.selection_overlay_signature();
  const ViewportOverlayFrame first_frame = service.selection_overlay_frame();
  EXPECT_EQ(source_wire_count(first_frame,
                              first.object.modeling_object_id),
            12);
  EXPECT_EQ(source_wire_count(first_frame,
                              second.object.modeling_object_id),
            0);

  const ModelingCommandResult switched =
      service.set_object_selection(selected, second.object.entity);
  ASSERT_TRUE(switched.ok) << switched.message;
  ASSERT_TRUE(switched.changed);
  const std::uint64_t second_signature =
      service.selection_overlay_signature();
  EXPECT_NE(second_signature, first_signature);
  const ViewportOverlayFrame second_frame = service.selection_overlay_frame();
  EXPECT_EQ(source_wire_count(second_frame,
                              first.object.modeling_object_id),
            0);
  EXPECT_EQ(source_wire_count(second_frame,
                              second.object.modeling_object_id),
            12);
}



TEST(ModelingSessionServiceSplitTests,
     FacadeDelegatesToSplitServicesThroughPublicApi) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService facade(scene, assets);

  EXPECT_TRUE(facade.objects().empty());
  EXPECT_EQ(facade.selection_mode(), ModelingSelectionMode::Object);
  EXPECT_FALSE(facade.selection_summary().has_selection);
  EXPECT_FALSE(facade.modal_tool_owns_viewport());

  const ModelingToolSettingsTarget settings = facade.tool_settings_target();
  EXPECT_FALSE(settings.active);
  EXPECT_EQ(facade.context_action_set().surface_id, "operations_top_bar");
}

TEST(ModelingSessionServiceSplitTests,
     FaceTextureSummaryIsRevisionGatedAndUsesTexturePixelDimensions) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto created = service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}},
      .name = "Pixel Texture Settings",
  });
  ASSERT_TRUE(created.ok) << created.message;
  TextureAsset texture;
  texture.name = "Reference texture";
  texture.width = 256;
  texture.height = 128;
  const AssetId texture_id = assets.add_texture(std::move(texture));
  MaterialDocument material = make_default_quader_material("Pixel reference");
  ASSERT_TRUE(set_material_texture_slot(material, material_texture_slot::kBaseColor,
                                       texture_id));
  const AssetId material_id = assets.add_material(std::move(material));
  ASSERT_TRUE(scene.set_material(created.object.entity, {.material = material_id}));
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = created.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 1,
  }).ok);
  const auto summary = service.face_texture_settings();
  ASSERT_TRUE(summary.editable);
  ASSERT_TRUE(service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ShiftX,
      .value = 128,
      .selection_token = summary.selection_token,
  }).ok);
  EXPECT_DOUBLE_EQ(service.face_texture_settings().shift_x, 128);
  const MeshAsset *mesh = assets.mesh(scene.mesh(created.object.entity)->mesh);
  ASSERT_NE(mesh, nullptr);
  ASSERT_NE(mesh->authored_polygon_document, nullptr);
  const auto *face = quader_poly::find_face(*mesh->authored_polygon_document, 1);
  ASSERT_NE(face, nullptr);
  EXPECT_FLOAT_EQ(face->texture_transform.shift_uv.x, 0.5f);

  const auto before_queries = service.modeling_sdk_projection_stats_for_tests();
  const auto revision = service.face_texture_settings_revision();
  for (int index = 0; index < 20; ++index) {
    EXPECT_EQ(service.face_texture_settings_revision(), revision);
    EXPECT_DOUBLE_EQ(service.face_texture_settings().shift_x, 128);
  }
  const auto after_queries = service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_queries.face_texture_summary_build_count,
            before_queries.face_texture_summary_build_count);
  EXPECT_EQ(after_queries.mesh_compile_count, before_queries.mesh_compile_count);
  EXPECT_EQ(after_queries.asset_publication_count, before_queries.asset_publication_count);

  ASSERT_TRUE(assets.update_texture(texture_id, [](TextureAsset &updated) {
    updated.width = 512;
  }));
  EXPECT_DOUBLE_EQ(service.face_texture_settings().shift_x, 256);
  const auto before_noop = service.content_revision();
  const auto unchanged = service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ShiftX,
      .value = 256,
      .selection_token = service.face_texture_settings().selection_token,
  });
  EXPECT_TRUE(unchanged.ok);
  EXPECT_FALSE(unchanged.changed);
  EXPECT_EQ(service.content_revision(), before_noop);

  ASSERT_TRUE(service.move_selection({0.1f, 0.0f, 0.0f}).ok);
  EXPECT_DOUBLE_EQ(service.face_texture_settings().shift_x, 256);
  ASSERT_TRUE(scene.set_locked(created.object.entity, true));
  const auto locked = service.face_texture_settings();
  EXPECT_EQ(locked.selected_face_count, 1u);
  EXPECT_FALSE(locked.editable);
  EXPECT_EQ(locked.block_reason, ModelingFaceTextureBlockReason::TargetUnavailable);
  EXPECT_FALSE(service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ScaleX,
      .value = 3,
      .selection_token = locked.selection_token,
  }).ok);
}

TEST(ModelingSessionServiceSplitTests,
     FaceTextureBatchRollsBackSharedBindingsAndPublishesAllObjectsAtomically) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto first = service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}}, .name = "First",
  });
  const auto second = service.create_box_from_bounds({
      .bounds = {.min = {3, -1, -1}, .max = {5, 1, 1}}, .name = "Second",
  });
  ASSERT_TRUE(first.ok);
  ASSERT_TRUE(second.ok);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  for (const auto *created : {&first, &second}) {
    ASSERT_TRUE(service.set_component_selection({
        .modeling_object_id = created->object.modeling_object_id,
        .mode = ModelingSelectionMode::Face,
        .faces = {1},
        .edit = created == &first ? ModelingSelectionEdit::Replace
                                 : ModelingSelectionEdit::Add,
        .active_kind = ModelingActiveSelectionKind::Face,
        .active_face = 1,
    }).ok);
  }
  const AssetId shared_mesh = scene.mesh(first.object.entity)->mesh;
  const EntityId shared_entity = scene.create_entity("Shared consumer", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(shared_entity, {.mesh = shared_mesh}));
  const auto summary = service.face_texture_settings();
  ASSERT_EQ(summary.selected_face_count, 2u);
  const auto before_mesh_ids = assets.mesh_ids();
  const auto before_scene_revision = scene.revision();
  const auto before_asset_revision = assets.revision();
  const auto before_content_revision = service.content_revision();
  const auto before_selection_revision = service.selection_revision();

  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
  const auto failed = service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ScaleY,
      .value = 4,
      .selection_token = summary.selection_token,
  });
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_EQ(scene.revision(), before_scene_revision);
  EXPECT_EQ(assets.revision(), before_asset_revision);
  EXPECT_EQ(assets.mesh_ids(), before_mesh_ids);
  EXPECT_EQ(service.content_revision(), before_content_revision);
  EXPECT_EQ(service.selection_revision(), before_selection_revision);
  EXPECT_EQ(scene.mesh(first.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, shared_mesh);
  EXPECT_EQ(service.face_texture_settings().scale_y, 1.0);

  service.set_modeling_sdk_projection_failure_for_tests(ModelingSdkProjectionFailurePoint::None);
  const auto edited = service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ScaleY,
      .value = 4,
      .selection_token = summary.selection_token,
  });
  ASSERT_TRUE(edited.ok) << edited.message;
  EXPECT_TRUE(edited.changed);
  EXPECT_EQ(service.content_revision(), before_content_revision + 1u);
  EXPECT_EQ(service.selection_revision(), before_selection_revision);
  EXPECT_EQ(service.face_texture_settings().selected_face_count, 2u);
  EXPECT_EQ(service.face_texture_settings().scale_y, 4.0);
  EXPECT_NE(scene.mesh(first.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, shared_mesh);
  const auto *untouched = quader_poly::find_face(
      *assets.mesh(shared_mesh)->authored_polygon_document, 1);
  ASSERT_NE(untouched, nullptr);
  EXPECT_FLOAT_EQ(untouched->texture_transform.scale.y, 1);
}

TEST(ModelingSessionServiceSplitTests,
     FaceTextureActionShiftUsesPerFaceExtentsAndRepeatReadsCurrentTextures) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto created = service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}},
      .name = "Mixed Texture Dimensions",
  });
  ASSERT_TRUE(created.ok) << created.message;
  constexpr std::array kWidths{256u, 128u, 64u};
  std::array<AssetId, 2> texture_ids;
  for (std::size_t index = 0; index < texture_ids.size(); ++index) {
    TextureAsset texture;
    texture.name = "Pixel step source";
    texture.width = kWidths[index];
    texture.height = 128;
    texture_ids[index] = assets.add_texture(std::move(texture));
    MaterialDocument material = make_default_quader_material("Pixel step material");
    ASSERT_TRUE(set_material_texture_slot(
        material, material_texture_slot::kBaseColor, texture_ids[index]));
    const AssetId material_id = assets.add_material(std::move(material));
    const auto assigned = service.apply_material_to_face(
        created.object.modeling_object_id, static_cast<int>(index) + 1,
        material_id);
    ASSERT_TRUE(assigned.ok) << assigned.message;
  }
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = created.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1, 2, 3},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 1,
  }).ok);
  const auto shifted = service.apply_face_texture_action({
      .options = {.action = ModelingFaceTextureAction::ShiftRight,
                  .treat_as_one = true},
      .selection_token = service.face_texture_settings().selection_token,
  });
  ASSERT_TRUE(shifted.ok) << shifted.message;
  ASSERT_TRUE(shifted.changed);
  const auto *mesh = assets.mesh(scene.mesh(created.object.entity)->mesh);
  ASSERT_NE(mesh, nullptr);
  ASSERT_NE(mesh->authored_polygon_document, nullptr);
  for (std::size_t index = 0; index < kWidths.size(); ++index) {
    SCOPED_TRACE(index);
    const auto *face = quader_poly::find_face(
        *mesh->authored_polygon_document, static_cast<int>(index) + 1);
    ASSERT_NE(face, nullptr);
    EXPECT_FLOAT_EQ(face->texture_transform.shift_uv.x,
                    -1.0f / static_cast<float>(kWidths[index]));
    EXPECT_FLOAT_EQ(face->texture_transform.shift_uv.y, 0.0f);
  }

  ASSERT_TRUE(assets.update_texture(texture_ids[0], [](TextureAsset &texture) {
    texture.width = 512;
  }));
  ASSERT_TRUE(assets.update_texture(texture_ids[1], [](TextureAsset &texture) {
    texture.width = 32;
  }));
  const auto repeated = service.repeat_last_action();
  ASSERT_TRUE(repeated.ok) << repeated.message;
  ASSERT_TRUE(repeated.changed);
  constexpr std::array kRepeatedWidths{512u, 32u, 64u};
  mesh = assets.mesh(scene.mesh(created.object.entity)->mesh);
  ASSERT_NE(mesh, nullptr);
  ASSERT_NE(mesh->authored_polygon_document, nullptr);
  for (std::size_t index = 0; index < kWidths.size(); ++index) {
    SCOPED_TRACE(index);
    const auto *face = quader_poly::find_face(
        *mesh->authored_polygon_document, static_cast<int>(index) + 1);
    ASSERT_NE(face, nullptr);
    EXPECT_FLOAT_EQ(face->texture_transform.shift_uv.x,
                    -1.0f / static_cast<float>(kWidths[index]) -
                        1.0f / static_cast<float>(kRepeatedWidths[index]));
  }
  EXPECT_EQ(service.face_texture_settings().selected_face_count, 3u);
  const auto before_numeric_shift = service.content_revision();
  EXPECT_FALSE(service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ShiftX,
      .value = 8.0,
      .selection_token = service.face_texture_settings().selection_token,
  }).ok);
  EXPECT_EQ(service.content_revision(), before_numeric_shift);
}

TEST(ModelingSessionServiceSplitTests,
     FaceTextureActionBatchFailurePreservesSharedMeshesAndAllRevisions) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto first = service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}}, .name = "First",
  });
  const auto second = service.create_box_from_bounds({
      .bounds = {.min = {3, -1, -1}, .max = {5, 1, 1}}, .name = "Second",
  });
  ASSERT_TRUE(first.ok);
  ASSERT_TRUE(second.ok);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  for (const auto *created : {&first, &second}) {
    ASSERT_TRUE(service.set_component_selection({
        .modeling_object_id = created->object.modeling_object_id,
        .mode = ModelingSelectionMode::Face,
        .faces = {1},
        .edit = created == &first ? ModelingSelectionEdit::Replace
                                 : ModelingSelectionEdit::Add,
        .active_kind = ModelingActiveSelectionKind::Face,
        .active_face = 1,
    }).ok);
  }
  const AssetId shared_mesh = scene.mesh(first.object.entity)->mesh;
  const AssetId second_mesh = scene.mesh(second.object.entity)->mesh;
  const EntityId shared_entity =
      scene.create_entity("Unselected shared consumer", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(shared_entity, {.mesh = shared_mesh}));
  const auto before_first = assets.mesh(shared_mesh)->authored_polygon_document;
  const auto before_second = assets.mesh(second_mesh)->authored_polygon_document;
  const auto before_mesh_ids = assets.mesh_ids();
  const auto before_scene_revision = scene.revision();
  const auto before_asset_revision = assets.revision();
  const auto before_content_revision = service.content_revision();
  const auto before_selection_revision = service.selection_revision();
  const ModelingFaceTextureActionRequest request{
      .options = {.action = ModelingFaceTextureAction::ScaleYUp},
      .selection_token = service.face_texture_settings().selection_token,
  };

  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
  const auto failed = service.apply_face_texture_action(request);
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_EQ(scene.revision(), before_scene_revision);
  EXPECT_EQ(assets.revision(), before_asset_revision);
  EXPECT_EQ(assets.mesh_ids(), before_mesh_ids);
  EXPECT_EQ(service.content_revision(), before_content_revision);
  EXPECT_EQ(service.selection_revision(), before_selection_revision);
  EXPECT_EQ(scene.mesh(first.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(second.object.entity)->mesh, second_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh(shared_mesh)->authored_polygon_document, before_first);
  EXPECT_EQ(assets.mesh(second_mesh)->authored_polygon_document, before_second);

  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  const auto edited = service.apply_face_texture_action(request);
  ASSERT_TRUE(edited.ok) << edited.message;
  EXPECT_TRUE(edited.changed);
  EXPECT_EQ(service.content_revision(), before_content_revision + 1u);
  EXPECT_EQ(service.selection_revision(), before_selection_revision);
  EXPECT_NE(scene.mesh(first.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, shared_mesh);
  for (const EntityId entity : {first.object.entity, second.object.entity}) {
    const auto *face = quader_poly::find_face(
        *assets.mesh(scene.mesh(entity)->mesh)->authored_polygon_document, 1);
    ASSERT_NE(face, nullptr);
    EXPECT_FLOAT_EQ(face->texture_transform.scale.y, 1.1f);
  }
  const auto *untouched = quader_poly::find_face(*before_first, 1);
  ASSERT_NE(untouched, nullptr);
  EXPECT_FLOAT_EQ(untouched->texture_transform.scale.y, 1.0f);
}

} // namespace
} // namespace quader
