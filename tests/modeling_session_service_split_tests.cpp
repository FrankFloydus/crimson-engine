////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/modeling_session_services.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "editor/selection/selection_projection_service.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "scene/scene_document.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <editor/interactions/cut_tool.hpp>
#include <editor/modeling/runtime/session.hpp>

namespace quader {
namespace {

using test::guardrail::read_source_file;
using test::guardrail::source_root;

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
      RuntimeToolCommandService::update_poly_hover(ray_event);
  EXPECT_EQ(poly_hover.command_id,
            quader_editor_core::EditorRuntimeCommandId::UpdatePolyHover);
  const auto *poly_settings =
      std::get_if<quader_editor_core::PolyHoverRuntimeCommandSettings>(
          &poly_hover.settings);
  ASSERT_NE(poly_settings, nullptr);
  EXPECT_TRUE(poly_settings->constrain_axis);
  EXPECT_FALSE(poly_settings->snap_enabled);
  EXPECT_EQ(poly_settings->grid_size, ray_event.grid_size);

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

  ModelingCreateBoxRequest box_request;
  box_request.bounds.min = {-2.0f, -1.0f, -3.0f};
  box_request.bounds.max = {2.0f, 1.0f, 3.0f};
  const quader_editor_core::EditorRuntimeCommand create_box =
      RuntimeToolCommandService::create_box_from_bounds(box_request);
  EXPECT_EQ(create_box.command_id,
            quader_editor_core::EditorRuntimeCommandId::CreateBoxFromBounds);
  const auto *create_box_settings = std::get_if<
      quader_editor_core::CreateBoxFromBoundsRuntimeCommandSettings>(
      &create_box.settings);
  ASSERT_NE(create_box_settings, nullptr);
  EXPECT_NEAR(create_box_settings->min_bounds.x, -2.0, 0.0001);
  EXPECT_NEAR(create_box_settings->max_bounds.z, 3.0, 0.0001);
}

TEST(ModelingSessionServiceSplitTests,
     RuntimeToolCommandConstructionLivesOutsideFacade) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string command_service = read_source_file(
      "src/editor/modeling/services/runtime_tool_command_service.cpp");

  const std::array<std::string, 10> settings_types = {
      "InsertEdgeLoopPickRuntimeCommandSettings",
      "CutRayRuntimeCommandSettings",
      "CutPointDragRuntimeCommandSettings",
      "SetCutModeRuntimeCommandSettings",
      "MirrorRayRuntimeCommandSettings",
      "MirrorPointDragRuntimeCommandSettings",
      "PolyHoverRuntimeCommandSettings",
      "BoxRayRuntimeCommandSettings",
      "BoxHeightRuntimeCommandSettings",
      "CreateBoxFromBoundsRuntimeCommandSettings",
  };
  for (const std::string &settings_type : settings_types) {
    EXPECT_EQ(facade.find(settings_type), std::string::npos) << settings_type;
    EXPECT_NE(command_service.find(settings_type), std::string::npos)
        << settings_type;
  }

  EXPECT_NE(
      facade.find("RuntimeToolCommandService::update_insert_edge_loop_hover"),
      std::string::npos);
  EXPECT_NE(facade.find("RuntimeToolCommandService::update_cut_hover"),
            std::string::npos);
  EXPECT_NE(facade.find("RuntimeToolCommandService::update_mirror_hover"),
            std::string::npos);
  EXPECT_NE(facade.find("RuntimeToolCommandService::update_poly_hover"),
            std::string::npos);
  EXPECT_NE(facade.find("RuntimeToolCommandService::update_box_hover"),
            std::string::npos);
  EXPECT_EQ(facade.find("viewport_grid_point_from_ray("), std::string::npos);
  EXPECT_EQ(facade.find("runtime_.update_poly_hover"), std::string::npos);
  EXPECT_EQ(facade.find("runtime_.accept_poly_current_point"),
            std::string::npos);
  EXPECT_EQ(facade.find("runtime_.commit_poly_tool"), std::string::npos);
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
  EXPECT_EQ(identity->material->material, 42);

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
  EXPECT_EQ(scene.material(duplicate)->material, 44);
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

  mesh_sync.sync_scene_objects_from_modeling(session, scene, assets, scene_sync,
                                             true);

  const MaterialComponent *first_material = scene.material(first);
  const MaterialComponent *second_material = scene.material(second);
  ASSERT_NE(first_material, nullptr);
  ASSERT_NE(second_material, nullptr);
  EXPECT_EQ(first_material->material, 11u);
  EXPECT_EQ(second_material->material, 22u);
  ASSERT_TRUE(first_material->material_slots.contains(0u));
  ASSERT_TRUE(second_material->material_slots.contains(0u));
  EXPECT_EQ(first_material->material_slots.at(0u), 11u);
  EXPECT_EQ(second_material->material_slots.at(0u), 11u);
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
     ObjectRuntimeMapStaysOutOfServiceBucket) {
  const std::string cmake = read_source_file("CMakeLists.txt");
  const std::string services = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string service_aggregate = read_source_file(
      "src/editor/modeling/services/modeling_session_services.hpp");

  EXPECT_NE(
      cmake.find("src/editor/modeling/runtime/modeling_object_runtime_map.cpp"),
      std::string::npos);
  EXPECT_NE(service_aggregate.find(
                "editor/modeling/runtime/modeling_object_runtime_map.hpp"),
            std::string::npos);
  EXPECT_EQ(cmake.find("src/editor/modeling/services/ModelingObjectRegistry"),
            std::string::npos);
  EXPECT_EQ(service_aggregate.find("ModelingObjectRegistry"),
            std::string::npos);
  EXPECT_EQ(services.find("ModelingObjectRuntimeMap::"), std::string::npos);
  const std::string runtime_map_header = read_source_file(
      "src/editor/modeling/runtime/modeling_object_runtime_map.hpp");
  const std::string projection_source = read_source_file(
      "src/editor/projection/selection_overlay_projection.cpp");
  const std::string picking_projection_source =
      read_source_file("src/editor/projection/picking_cache_projection.cpp");
  const std::string mesh_sync_source = read_source_file(
      "src/editor/modeling/services/modeling_mesh_sync_service.cpp");
  EXPECT_EQ(runtime_map_header.find("entities_by_object()"), std::string::npos);
  EXPECT_EQ(runtime_map_header.find("objectsByEntity()"), std::string::npos);
  EXPECT_EQ(services.find("entities_by_object()"), std::string::npos);
  EXPECT_EQ(services.find("objectsByEntity()"), std::string::npos);
  EXPECT_EQ(projection_source.find("entities_by_object()"), std::string::npos);
  EXPECT_EQ(picking_projection_source.find("entities_by_object()"),
            std::string::npos);
  EXPECT_EQ(mesh_sync_source.find("entities_by_object()"), std::string::npos);
  EXPECT_NE(
      runtime_map_header.find("object_entity_bindings_by_modeling_object()"),
      std::string::npos);
  EXPECT_NE(runtime_map_header.find(
                "entity_for_modeling_object(int modeling_object_id)"),
            std::string::npos);
  EXPECT_NE(
      runtime_map_header.find("modeling_object_for_entity(EntityId entity)"),
      std::string::npos);
  EXPECT_NE(runtime_map_header.find(
                "void bind(int modeling_object_id, EntityId entity)"),
            std::string::npos);
  EXPECT_NE(
      runtime_map_header.find("void unbind_object(int modeling_object_id)"),
      std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectSelectionMappingLivesInSelectionProjectionService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string projection =
      read_source_file("src/editor/selection/selection_projection_service.cpp");
  const std::string selection_owner =
      read_source_file("src/editor/selection/scene_selection_application.cpp");

  EXPECT_NE(facade.find("project_object_selection_for_scene_entities("),
            std::string::npos);
  EXPECT_EQ(facade.find("const auto object = "
                        "object_registry_.modeling_object_for_entity(entity);"),
            std::string::npos);
  EXPECT_EQ(facade.find("const auto object = "
                        "object_registry_.modeling_object_for_entity(active);"),
            std::string::npos);
  EXPECT_NE(projection.find("project_object_selection_for_scene_entities("),
            std::string::npos);
  EXPECT_NE(projection.find("runtime_object_selection_for_scene_entities("),
            std::string::npos);
  EXPECT_NE(projection.find("resolve_projected_active_selection("),
            std::string::npos);
  EXPECT_NE(projection.find("apply_runtime_object_selection("),
            std::string::npos);
  EXPECT_EQ(projection.find("apply_exact_scene_selection("), std::string::npos);
  EXPECT_NE(projection.find("SceneSelectionApplicationMode::RuntimeSync"),
            std::string::npos);
  EXPECT_NE(selection_owner.find("apply_runtime_sync_scene_selection("),
            std::string::npos);
  EXPECT_EQ(projection.find("ordered_selected.front()"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     ClickPickSelectionUsesSelectionServiceCachedInputs) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string selection_service = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string runtime_picking =
      read_source_file("src/editor/modeling/runtime/session_picking.cpp");
  const std::string runtime_header =
      read_source_file("src/editor/modeling/runtime/session.hpp");

  EXPECT_NE(facade.find("selection_service_.apply_pick_selection("),
            std::string::npos);
  EXPECT_NE(facade.find("session_,"), std::string::npos);
  EXPECT_NE(facade.find("scene_, object_registry_, effective_request)"),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::pick_select_all_faces(\n          "
                        "          picking_objects"),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::pick_select_edge_loop(\n          "
                        "          picking_objects"),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::pick_select_all_vertices(\n       "
                        "             picking_objects"),
            std::string::npos);
  EXPECT_NE(
      selection_service.find("ModelingSelectionService::apply_pick_selection("),
      std::string::npos);
  EXPECT_NE(selection_service.find(
                "cached_pick_object_bounds(session, scene, object_registry)"),
            std::string::npos);
  EXPECT_NE(selection_service.find("cached_pick_face_geometry(session, scene,"),
            std::string::npos);
  EXPECT_NE(selection_service.find("object_registry, pick_options)"),
            std::string::npos);
  EXPECT_NE(selection_service.find("quader_editor_core::apply_pick_selection("),
            std::string::npos);
  EXPECT_EQ(selection_service.find("applyPickedSelectionToSession"),
            std::string::npos);
  EXPECT_EQ(selection_service.find("sessionHasComponentSelection"),
            std::string::npos);
  EXPECT_EQ(selection_service.find("pickSelectRequestHitsTarget"),
            std::string::npos);
  EXPECT_EQ(
      selection_service.find("quader_editor_core::pick_select_all_faces("),
      std::string::npos);
  EXPECT_EQ(
      selection_service.find("quader_editor_core::pick_select_edge_loop("),
      std::string::npos);
  EXPECT_EQ(
      selection_service.find("quader_editor_core::pick_select_all_vertices("),
      std::string::npos);
  EXPECT_NE(runtime_header.find("struct ApplyPickSelectionRequest"),
            std::string::npos);
  EXPECT_NE(runtime_header.find("apply_pick_selection(EditorSession &session"),
            std::string::npos);
  EXPECT_NE(
      runtime_picking.find("ApplyPickSelectionResult apply_pick_selection("),
      std::string::npos);
  EXPECT_NE(runtime_picking.find("apply_picked_selection_to_session("),
            std::string::npos);
  EXPECT_NE(
      runtime_header.find("std::span<const PickObjectBounds> object_bounds"),
      std::string::npos);
  EXPECT_NE(
      runtime_picking.find("pick_nearest_object(objects, ray, options, "
                           "hit_index, hit, object_bounds, face_geometry)"),
      std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionApplicationLivesInModelingSelectionService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string selection_header = read_source_file(
      "src/editor/modeling/services/modeling_selection_service.hpp");
  const std::string selection_service = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");

  EXPECT_NE(selection_header.find("struct ModelingSelectionApplyResult"),
            std::string::npos);
  EXPECT_NE(selection_header.find("selection_summary("), std::string::npos);
  EXPECT_NE(selection_header.find("set_component_selection("),
            std::string::npos);
  EXPECT_NE(selection_header.find("set_group_selection_mode("),
            std::string::npos);

  EXPECT_NE(
      selection_service.find("ModelingSelectionService::selection_summary("),
      std::string::npos);
  EXPECT_NE(selection_service.find(
                "ModelingSelectionService::set_component_selection("),
            std::string::npos);
  EXPECT_NE(selection_service.find("validated_component_selection_edit("),
            std::string::npos);
  EXPECT_NE(selection_service.find(
                "ModelingSelectionService::apply_rectangle_selection("),
            std::string::npos);
  EXPECT_NE(selection_service.find("to_runtime_selection_edit(request.edit)"),
            std::string::npos);

  EXPECT_NE(facade.find("selection_service_.selection_summary("),
            std::string::npos);
  EXPECT_NE(facade.find("selection_service_.set_component_selection("),
            std::string::npos);
  EXPECT_NE(facade.find("selection_service_.apply_rectangle_selection("),
            std::string::npos);
  EXPECT_NE(
      facade.find("selection_projection_service_.restore_scene_selection("),
      std::string::npos);
  EXPECT_EQ(facade.find("validated_component_selection_edit("),
            std::string::npos);
  EXPECT_EQ(facade.find("ComponentSelectionEditResult"), std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::apply_rectangle_selection("
                        "rectangle_request)"),
            std::string::npos);
  EXPECT_EQ(facade.find("scene_.clear_selection()"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionCenterWorldProjectionLivesInSelectionProjectionService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string projection_header =
      read_source_file("src/editor/selection/selection_projection_service.hpp");
  const std::string projection =
      read_source_file("src/editor/selection/selection_projection_service.cpp");

  EXPECT_NE(
      facade.find("selection_projection_service_.world_selection_center("),
      std::string::npos);
  EXPECT_NE(facade.find("session_, scene_, assets_, object_registry_)"),
            std::string::npos);
  EXPECT_EQ(facade.find("selected_world_bounds(scene_, assets_)"),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_poly::selected_vertex_ids(object.document, "
                        "object.selection)"),
            std::string::npos);
  EXPECT_EQ(facade.find("transform_object_point("), std::string::npos);
  EXPECT_NE(projection_header.find("world_selection_center("),
            std::string::npos);
  EXPECT_NE(
      projection.find("SelectionProjectionService::world_selection_center("),
      std::string::npos);
  EXPECT_NE(projection.find("selected_world_bounds(scene, assets)"),
            std::string::npos);
  EXPECT_NE(projection.find("world_point_for_modeling_object("),
            std::string::npos);
  EXPECT_NE(projection.find("quader_poly::selected_vertex_ids(object.document, "
                            "object.selection)"),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     DuplicateSceneIdentityLivesInRuntimeMapAndMeshSync) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string runtime_map = read_source_file(
      "src/editor/modeling/runtime/modeling_object_runtime_map.cpp");
  const std::string mesh_sync = read_source_file(
      "src/editor/modeling/services/modeling_mesh_sync_service.cpp");

  EXPECT_EQ(facade.find("DuplicateSceneState"), std::string::npos);
  EXPECT_EQ(facade.find("scene_.set_transform(*duplicate"), std::string::npos);
  EXPECT_EQ(facade.find("scene_.set_material(*duplicate"), std::string::npos);
  EXPECT_NE(facade.find("capture_selected_duplicate_scene_identities()"),
            std::string::npos);
  EXPECT_NE(facade.find(
                "mesh_sync_service_.apply_scene_identity_to_duplicate_entity("),
            std::string::npos);
  EXPECT_NE(runtime_map.find("capture_scene_identities_for_modeling_objects("),
            std::string::npos);
  EXPECT_NE(runtime_map.find("object_entity_bindings_by_modeling_object("),
            std::string::npos);
  EXPECT_NE(runtime_map.find("std::sort("), std::string::npos);
  EXPECT_NE(runtime_map.find("bindings.begin(), bindings.end()"),
            std::string::npos);
  EXPECT_NE(mesh_sync.find("apply_scene_identity_to_duplicate_entity("),
            std::string::npos);
  EXPECT_NE(mesh_sync.find("object_entity_bindings_by_modeling_object()"),
            std::string::npos);
  EXPECT_EQ(
      mesh_sync.find(
          "std::sort(entities_by_object.begin(), entities_by_object.end()"),
      std::string::npos);
  const std::size_t existing_material_check =
      mesh_sync.find("existing_material != nullptr");
  ASSERT_NE(existing_material_check, std::string::npos);
  EXPECT_NE(mesh_sync.find("existing_material->material != kInvalidAssetId",
                           existing_material_check),
            std::string::npos);
  EXPECT_LT(existing_material_check,
            mesh_sync.find("material_by_slot.find(material_slot)"));
}

TEST(ModelingSessionServiceSplitTests,
     TransformDragPreviewRoutingLivesInController) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string service = read_source_file(
      "src/editor/modeling/services/modeling_transform_drag_service.cpp");
  const std::string controller = read_source_file(
      "src/editor/modeling/runtime_state/transform_drag_controller.cpp");

  EXPECT_EQ(facade.find("TransformDragController::preview_routing("),
            std::string::npos);
  EXPECT_NE(service.find("TransformDragController::preview_routing("),
            std::string::npos);
  EXPECT_EQ(facade.find("duplicate_scene_transform_preview"),
            std::string::npos);
  EXPECT_EQ(facade.find("needs_runtime_preview_transaction ="),
            std::string::npos);
  EXPECT_NE(controller.find("TransformDragController::preview_routing("),
            std::string::npos);
  EXPECT_NE(controller.find("scene_transform_preview_eligible"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(
      source_root() /
      "src/editor/modeling/services/transform_drag_controller.hpp"));
  EXPECT_FALSE(std::filesystem::exists(
      source_root() /
      "src/editor/modeling/services/transform_preview_service.hpp"));
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/editor/modeling/services/modal_tool_runtime.hpp"));
}

TEST(ModelingSessionServiceSplitTests,
     TransformDragLifecycleLivesInFocusedService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string service = read_source_file(
      "src/editor/modeling/services/modeling_transform_drag_service.cpp");
  const std::string service_header = read_source_file(
      "src/editor/modeling/services/modeling_transform_drag_service.hpp");
  const std::string transform_tool =
      read_source_file("src/editor/tools/base/transform_viewport_tool.hpp");

  EXPECT_NE(facade.find("transform_drag_service_.begin_transform_drag("),
            std::string::npos);
  EXPECT_NE(facade.find("transform_drag_service_.update_transform_drag("),
            std::string::npos);
  EXPECT_NE(facade.find("transform_drag_service_.commit_transform_drag("),
            std::string::npos);
  EXPECT_NE(facade.find("transform_drag_service_.cancel_transform_drag("),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::begin_move_drag("),
            std::string::npos);
  EXPECT_EQ(facade.find("SceneTransformDragService"), std::string::npos);
  EXPECT_NE(service.find("quader_editor_core::begin_move_drag("),
            std::string::npos);
  EXPECT_NE(service.find("quader_editor_core::begin_rotate_drag("),
            std::string::npos);
  EXPECT_NE(service.find("quader_editor_core::begin_scale_drag("),
            std::string::npos);
  EXPECT_NE(service_header.find("SceneTransformDragService"),
            std::string::npos);
  EXPECT_NE(transform_tool.find("TransformGizmoInteraction owns"),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     PickingAndOverlayProjectionHelpersStayOutOfFacade) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string services = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string picking_projection =
      read_source_file("src/editor/projection/picking_cache_projection.cpp");

  EXPECT_EQ(facade.find("objectPickFaceGeometrySignature("), std::string::npos);
  EXPECT_EQ(facade.find("mix_pick_geometry_hash("), std::string::npos);
  EXPECT_EQ(facade.find("build_object_pick_bounds("), std::string::npos);
  EXPECT_EQ(facade.find("include_pick_bounds_point("), std::string::npos);
  EXPECT_EQ(facade.find("append_selected_hover_filtered_base_frame("),
            std::string::npos);
  EXPECT_EQ(facade.find("hover_frame_suppresses_triangle("), std::string::npos);
  EXPECT_EQ(facade.find("hover_frame_suppresses_line("), std::string::npos);
  EXPECT_EQ(facade.find("hover_frame_suppresses_point("), std::string::npos);
  EXPECT_EQ(facade.find("append_overlay_frame("), std::string::npos);

  EXPECT_NE(services.find("append_selected_hover_filtered_base_frame("),
            std::string::npos);
  EXPECT_NE(services.find("append_overlay_frame("), std::string::npos);
  EXPECT_NE(services.find("PickingCacheProjection::build_object_pick_bounds("),
            std::string::npos);
  EXPECT_NE(
      services.find("PickingCacheProjection::object_face_geometry_signature("),
      std::string::npos);
  EXPECT_NE(picking_projection.find("object_face_geometry_signature("),
            std::string::npos);
  EXPECT_NE(picking_projection.find("build_object_pick_bounds("),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     ToolStatusProjectionLivesInOverlayService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string services = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string overlay_header = read_source_file(
      "src/editor/modeling/services/modeling_overlay_service.hpp");

  EXPECT_NE(overlay_header.find("ModelingToolStatus tool_status("),
            std::string::npos);
  EXPECT_NE(services.find("ModelingOverlayService::tool_status("),
            std::string::npos);
  EXPECT_NE(facade.find("return overlay_service_.tool_status("
                        "runtime_, modal_tool_owns_viewport());"),
            std::string::npos);
  EXPECT_EQ(facade.find("runtime_.cut_state()"), std::string::npos);
  EXPECT_EQ(facade.find("runtime_.mirror_state()"), std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::cut_overlay_points("),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::mirror_overlay_points("),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::cut_is_dragging("),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::mirror_is_dragging("),
            std::string::npos);
  EXPECT_NE(services.find("quader_editor_core::cut_overlay_points("),
            std::string::npos);
  EXPECT_NE(services.find("quader_editor_core::mirror_overlay_points("),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     GroupDragHierarchyDuplicatePolicyLivesInFocusedService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string service = read_source_file(
      "src/editor/modeling/services/modeling_hierarchy_duplicate_service.cpp");
  const std::string document_helpers = read_source_file(
      "src/editor/commands/document/document_command_scene_helpers.hpp");
  const std::string duplicate_command = read_source_file(
      "src/editor/commands/document/duplicate_selected_hierarchy_command.hpp");
  const std::string naming_header =
      read_source_file("src/scene/scene_entity_naming.hpp");
  const std::string cmake = read_source_file("CMakeLists.txt");

  EXPECT_NE(cmake.find("src/editor/modeling/services/"
                       "modeling_hierarchy_duplicate_service.cpp"),
            std::string::npos);
  EXPECT_NE(
      facade.find(
          "hierarchy_duplicate_service_.duplicate_selected_entity_hierarchy("),
      std::string::npos);
  EXPECT_NE(facade.find("duplicate_selected_hierarchy_for_group_drag("),
            std::string::npos);
  EXPECT_EQ(
      facade.find("std::unordered_map<EntityId, EntityId> clone_by_source"),
      std::string::npos);
  EXPECT_EQ(facade.find("auto copyCommonEntityState"), std::string::npos);
  EXPECT_NE(service.find("pair_duplicate_meshes("), std::string::npos);
  EXPECT_NE(service.find("duplicate_hierarchy_with_existing_clones("),
            std::string::npos);
  EXPECT_EQ(service.find("copyCommonEntityState("), std::string::npos);
  EXPECT_EQ(service.find("cloneHierarchyEntity("), std::string::npos);
  EXPECT_NE(
      document_helpers.find("document_command_duplicate_entity_snapshot_tree("),
      std::string::npos);
  EXPECT_NE(document_helpers.find("SceneEntityNameAllocator *name_allocator"),
            std::string::npos);
  EXPECT_NE(duplicate_command.find(
                "SceneEntityNameAllocator name_allocator(scene())"),
            std::string::npos);
  EXPECT_NE(duplicate_command.find("&name_allocator"), std::string::npos);
  EXPECT_NE(naming_header.find("next_index_by_base_"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     SceneAwareDeletePolicyLivesInFocusedCommand) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string command = read_source_file(
      "src/editor/commands/modeling/scene_aware_delete_selection_command.cpp");
  const std::string command_header = read_source_file(
      "src/editor/commands/modeling/scene_aware_delete_selection_command.hpp");
  const std::string cmake = read_source_file("CMakeLists.txt");

  EXPECT_NE(cmake.find("src/editor/commands/modeling/"
                       "scene_aware_delete_selection_command.cpp"),
            std::string::npos);
  EXPECT_NE(facade.find("create_scene_aware_delete_selection_command("),
            std::string::npos);
  EXPECT_NE(facade.find("SceneAwareDeleteRuntime{"), std::string::npos);
  EXPECT_NE(facade.find(".entity_for_modeling_object ="), std::string::npos);
  EXPECT_EQ(facade.find("struct SceneAwareDeleteState"), std::string::npos);
  EXPECT_EQ(facade.find("class SceneAwareDeleteSelectionCommand"),
            std::string::npos);
  EXPECT_EQ(facade.find("capture_scene_aware_delete_state"), std::string::npos);
  EXPECT_EQ(facade.find("redo_scene_aware_delete"), std::string::npos);
  EXPECT_EQ(facade.find("undo_scene_aware_delete"), std::string::npos);
  EXPECT_NE(command_header.find("entity_for_modeling_object"),
            std::string::npos);
  EXPECT_EQ(command.find("for (const EntityId entity : scene.entities())"),
            std::string::npos);
  EXPECT_NE(command.find("scene_.destroy_entities(state_.current_roots)"),
            std::string::npos);
  EXPECT_EQ(command.find("scene_.destroy_entity(root)"), std::string::npos);
  EXPECT_NE(command.find("runtime.entity_for_modeling_object(snapshot.modeling_"
                         "object->object_id)"),
            std::string::npos);
  EXPECT_NE(command.find("struct SceneAwareDeleteState"), std::string::npos);
  EXPECT_NE(command.find("class SceneAwareDeleteSelectionCommand"),
            std::string::npos);
  EXPECT_NE(command.find("capture_scene_aware_delete_state"),
            std::string::npos);
  EXPECT_NE(command.find("redo_scene_aware_delete"), std::string::npos);
  EXPECT_NE(command.find("undo_scene_aware_delete"), std::string::npos);
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
  tool_settings.set_preview_metadata("edge_bevel", "edge_bevel", "Edge Bevel");
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
  const ModelingToolSettingsTarget fallback =
      tool_settings.operation_preview_target(
          ModelingToolSettingsTargetKind::Thicken, "Thicken", "thicken");
  EXPECT_EQ(fallback.label, "Thicken");
  EXPECT_EQ(fallback.operation_id, "thicken");
  quader_editor_core::PreviewTransactionStatus preview;
  preview.active = true;
  preview.kind = quader_editor_core::PreviewTransactionKind::EdgeBevel;
  const ModelingToolSettingsTarget preview_target =
      tool_settings.active_preview_target(preview);
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
  EXPECT_EQ(interaction_settings.settings().cut_mode,
            ModelingCutMode::KeepBoth);
  interaction_settings.set_cut_mode(ModelingCutMode::DiscardLeft);
  EXPECT_EQ(interaction_settings.settings().cut_mode,
            ModelingCutMode::DiscardLeft);
  interaction_settings.cycle_cut_mode_without_runtime();
  EXPECT_EQ(interaction_settings.settings().cut_mode,
            ModelingCutMode::DiscardRight);
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
    if (descriptor.tool == ViewportToolId::Pivot) {
      continue;
    }
    EXPECT_EQ(ModalToolRuntime::owns_viewport(descriptor.modeling_tool),
              descriptor.owns_viewport)
        << descriptor.id;
  }
  EXPECT_TRUE(ModelingMeshSyncService::should_force_refresh(true, false));
  EXPECT_TRUE(ModelingMeshSyncService::should_force_refresh(false, true));
  EXPECT_FALSE(ModelingMeshSyncService::should_force_refresh(false, false));
  const quader_editor_core::EditorRuntimeCommand copy =
      ModelingClipboardService::make_copy_command();
  EXPECT_EQ(copy.command_id, ModelingClipboardService::copy_command());
  EXPECT_TRUE(std::holds_alternative<std::monostate>(copy.settings));
  const quader_editor_core::EditorRuntimeCommand paste =
      ModelingClipboardService::make_paste_command();
  EXPECT_EQ(paste.command_id, ModelingClipboardService::paste_command());
  EXPECT_TRUE(std::holds_alternative<
              quader_editor_core::PasteCopiedMeshesRuntimeCommandSettings>(
      paste.settings));
  EXPECT_TRUE(TransformPreviewService::is_transform(
      ModelingTransformOperation::Transform));
  EXPECT_FALSE(TransformPreviewService::is_transform(
      ModelingTransformOperation::Extrude));
  EXPECT_FALSE(ModelingCommandExecutor::runtime_unavailable().ok);
  EXPECT_STREQ(
      fallback_tool_settings_label(ModelingToolSettingsTargetKind::EdgeBevel),
      "Bevel Edges");
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
     InstantOperationAvailabilityUsesActionCatalogEvaluator) {
  const std::string instant_header = read_source_file(
      "src/editor/modeling/services/modeling_instant_operation_catalog.hpp");
  const std::string instant_catalog = read_source_file(
      "src/editor/modeling/services/modeling_instant_operation_catalog.cpp");
  const std::string action_catalog = read_source_file(
      "src/editor/modeling/services/modeling_action_catalog.cpp");

  EXPECT_EQ(instant_header.find("ModelingInstantOperationEnablement"),
            std::string::npos);
  EXPECT_EQ(instant_catalog.find("switch (entry"), std::string::npos);
  EXPECT_EQ(instant_catalog.find("HostActionDescriptor"), std::string::npos);
  EXPECT_NE(instant_catalog.find(
                "ModelingActionCatalog::evaluate_operation_availability("),
            std::string::npos);
  EXPECT_NE(action_catalog.find("evaluate_host_operation_availability("),
            std::string::npos);
  EXPECT_NE(action_catalog.find("descriptor.availability.selection_mode"),
            std::string::npos);
  EXPECT_NE(action_catalog.find("evaluate_availability_rule("),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     InstantOperationSettingsComeFromSharedOperationSettingsOwnerExceptNormalBridge) {
  RuntimeOperationExecutor executor;
  ModelingInteractionSettings interaction_settings;
  interaction_settings.bridge_steps = 4;

  const std::optional<RuntimeOperationExecutor::OperationCommand> bridge =
      executor.operation_for_instant_command(ModelingInstantOperationId::Bridge,
                                             interaction_settings);
  ASSERT_TRUE(bridge.has_value());
  EXPECT_EQ(bridge->operation, quader_editor_core::EditorOperationId::Bridge);
  const auto &bridge_settings =
      std::get<quader_editor_core::BridgeOperationSettings>(bridge->settings);
  const auto interpolated_bridge =
      std::get<quader_editor_core::BridgeOperationSettings>(
          settings_for_operation(quader_editor_core::EditorOperationId::Bridge,
                                 interaction_settings));
  EXPECT_EQ(interpolated_bridge.steps, 4);
  EXPECT_EQ(bridge_settings.steps, 1);

  const std::optional<RuntimeOperationExecutor::OperationCommand> bevel =
      executor.operation_for_instant_command(
          ModelingInstantOperationId::BevelVertices, interaction_settings);
  ASSERT_TRUE(bevel.has_value());
  EXPECT_TRUE(
      std::holds_alternative<
          quader_editor_core::BevelVerticesOperationSettings>(bevel->settings));

  const std::optional<RuntimeOperationExecutor::OperationCommand> recalculate =
      executor.operation_for_instant_command(
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

  EXPECT_FALSE(apply_modeling_interaction_setting(settings, "edge_bevel",
                                                  "tolerance", 0.25));
  EXPECT_FALSE(apply_modeling_interaction_setting(settings, "edge_bevel",
                                                  "__missing__", 1.0));
}

TEST(ModelingSessionServiceSplitTests,
     InstantOperationSettingsMappingHasSingleOwner) {
  const std::string catalog_header = read_source_file(
      "src/editor/modeling/services/modeling_instant_operation_catalog.hpp");
  const std::string catalog = read_source_file(
      "src/editor/modeling/services/modeling_instant_operation_catalog.cpp");
  const std::string services = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string operation_settings = read_source_file(
      "src/editor/modeling/services/modeling_operation_settings.cpp");
  const std::string tools_api =
      read_source_file("src/api/quader_tools_api.cpp");

  EXPECT_EQ(catalog_header.find("RuntimeOperationSettingsKind"),
            std::string::npos);
  EXPECT_EQ(catalog.find("RuntimeOperationSettingsKind"), std::string::npos);
  EXPECT_EQ(catalog.find("settingsKind"), std::string::npos);
  EXPECT_EQ(services.find("settingsKind"), std::string::npos);
  EXPECT_NE(services.find("settings_for_operation(*entry->runtime_operation, "
                          "interaction_settings)"),
            std::string::npos);
  EXPECT_NE(operation_settings.find("command_schema_for_operation(operation)"),
            std::string::npos);
  EXPECT_NE(
      operation_settings.find("operation_option_for_setting(tool_id, name)"),
      std::string::npos);
  EXPECT_NE(operation_settings.find("schema->settings_kind"),
            std::string::npos);
  EXPECT_EQ(operation_settings.find("bridge_operation_schema()"),
            std::string::npos);
  EXPECT_EQ(operation_settings.find("edge_bevel_operation_schema()"),
            std::string::npos);
  EXPECT_EQ(operation_settings.find("matchesAny("), std::string::npos);
  EXPECT_EQ(operation_settings.find("\"edge_bevel_width\""), std::string::npos);
  EXPECT_EQ(operation_settings.find("\"distance\""), std::string::npos);
  EXPECT_EQ(operation_settings.find(
                "case quader_editor_core::EditorOperationId::ConnectVertices"),
            std::string::npos);
  EXPECT_EQ(operation_settings.find(
                "case quader_editor_core::EditorOperationId::DeleteSelection"),
            std::string::npos);
  EXPECT_NE(
      tools_api.find(
          "apply_modeling_interaction_setting(settings, tool_id, name, value)"),
      std::string::npos);
  EXPECT_EQ(tools_api.find("edge_bevel_width = value"), std::string::npos);
  EXPECT_EQ(tools_api.find("merge_by_distance_tolerance = value"),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     HostOperationMetadataUsesSchemaOptionDescriptorCatalog) {
  const std::string operation_schema =
      read_source_file("src/editor/modeling/operations/operation_schema.hpp");
  const std::string host_contract =
      read_source_file("src/editor/metadata/host_contract.cpp");

  EXPECT_NE(operation_schema.find("struct EditorOperationOptionDescriptor"),
            std::string::npos);
  EXPECT_NE(operation_schema.find("operation_option_descriptor_catalog()"),
            std::string::npos);
  EXPECT_NE(operation_schema.find("operation_option_for_setting("),
            std::string::npos);
  EXPECT_NE(operation_schema.find("\"edge_bevel_width\""), std::string::npos);
  EXPECT_NE(operation_schema.find("\"distance\""), std::string::npos);
  EXPECT_NE(host_contract.find("operation_option_descriptor_catalog()"),
            std::string::npos);
  EXPECT_NE(host_contract.find("operation_preference("), std::string::npos);
  EXPECT_EQ(host_contract.find("static const std::array<HostOptionDescriptor"),
            std::string::npos);
  EXPECT_EQ(host_contract.find("operation_int_preference("), std::string::npos);
  EXPECT_EQ(host_contract.find("operation_float_preference("),
            std::string::npos);
  EXPECT_EQ(host_contract.find("operation_bool_preference("),
            std::string::npos);
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

} // namespace
} // namespace quader
