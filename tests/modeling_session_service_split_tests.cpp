////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/AssetRegistry.h"
#include "editor/commands/modeling/SceneAwareDeleteSelectionCommand.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/modeling/services/ModelingSessionServices.h"
#include "editor/modeling/services/ModelingOperationSettings.h"
#include "editor/modeling/services/ModelingInstantOperationCatalog.h"
#include "editor/selection/SelectionProjectionService.h"
#include "editor/tools/ViewportToolDescriptorCatalog.h"
#include "scene/SceneDocument.h"
#include "support/GuardrailFileScan.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <variant>

#include <editor/modeling/runtime/session.h>

namespace quader {
namespace {

using test::guardrail::readSourceFile;
using test::guardrail::sourceRoot;

TEST(ModelingSessionServiceSplitTests, ObjectRuntimeMapOwnsModelingSceneMappingsAndBuildsRecords) {
    SceneDocument scene;
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);
    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    scene.setMesh(second, MeshComponent{.mesh = 42});
    scene.setMesh(first, MeshComponent{.mesh = 7});

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(200, second);
    objectMap.bind(100, first);

    EXPECT_EQ(objectMap.size(), 2U);
    EXPECT_EQ(objectMap.entityForModelingObject(100), first);
    EXPECT_EQ(objectMap.modelingObjectForEntity(second), 200);

    const std::vector<ModelingObjectRecord> records = objectMap.records(scene);
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].entity, second);
    EXPECT_EQ(records[0].modelingObjectId, 200);
    EXPECT_EQ(records[0].mesh, 42);
    EXPECT_EQ(records[0].name, "Second");
    EXPECT_EQ(records[1].entity, first);
    EXPECT_EQ(records[1].modelingObjectId, 100);
    EXPECT_EQ(records[1].mesh, 7);
    EXPECT_EQ(records[1].name, "First");

    objectMap.unbindObject(100);
    EXPECT_EQ(objectMap.entityForModelingObject(100), std::nullopt);
    EXPECT_EQ(objectMap.modelingObjectForEntity(first), std::nullopt);
}

TEST(ModelingSessionServiceSplitTests, ObjectRuntimeMapRebindClearsStaleReverseMappings) {
    SceneDocument scene;
    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(100, first);
    objectMap.bind(100, second);

    EXPECT_EQ(objectMap.entityForModelingObject(100), second);
    EXPECT_EQ(objectMap.modelingObjectForEntity(first), std::nullopt);
    EXPECT_EQ(objectMap.modelingObjectForEntity(second), 100);

    objectMap.bind(200, second);

    EXPECT_EQ(objectMap.entityForModelingObject(100), std::nullopt);
    EXPECT_EQ(objectMap.entityForModelingObject(200), second);
    EXPECT_EQ(objectMap.modelingObjectForEntity(second), 200);
}

TEST(ModelingSessionServiceSplitTests, ObjectRuntimeMapKeepsDestroyedSceneIdentityForRedoProjection) {
    SceneDocument scene;
    const EntityId parent = scene.createEntity("Parent", EntityKind::Empty);
    const EntityId entity = scene.createEntity("Redo Source", EntityKind::Mesh);
    scene.setMesh(entity, MeshComponent{.mesh = 99});
    scene.setMaterial(entity, MaterialComponent{.material = 42});
    TransformComponent transform;
    transform.position = {2.0f, 3.0f, 4.0f};
    scene.setTransform(entity, transform);
    ASSERT_TRUE(scene.setParent(entity, parent, false));
    scene.setVisible(entity, false);
    scene.setLocked(entity, true);
    const EditorObjectId editorObjectId = scene.editorObjectId(entity);

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(300, entity);
    objectMap.rememberSceneEntity(300, scene, entity);

    const std::optional<ModelingObjectSceneIdentity> identity = objectMap.sceneIdentityForModelingObject(300);
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(identity->entity, entity);
    EXPECT_EQ(identity->editorObjectId, editorObjectId);
    EXPECT_EQ(identity->name, "Redo Source");
    EXPECT_EQ(identity->parent, parent);
    EXPECT_EQ(identity->transform.position.x, transform.position.x);
    EXPECT_FALSE(identity->visibility.visible);
    EXPECT_TRUE(identity->lock.locked);
    ASSERT_TRUE(identity->mesh.has_value());
    EXPECT_EQ(identity->mesh->mesh, 99);
    ASSERT_TRUE(identity->material.has_value());
    EXPECT_EQ(identity->material->material, 42);

    ASSERT_TRUE(scene.destroyEntity(entity));
    objectMap.unbindObject(300);
    EXPECT_EQ(objectMap.entityForModelingObject(300), std::nullopt);
    EXPECT_TRUE(objectMap.records(scene).empty());
    ASSERT_TRUE(objectMap.sceneIdentityForModelingObject(300).has_value());
    EXPECT_EQ(objectMap.sceneIdentityForModelingObject(300)->entity, entity);
}

TEST(ModelingSessionServiceSplitTests, ObjectRuntimeMapCapturesSceneIdentitiesInRequestedOrder) {
    SceneDocument scene;
    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);
    TransformComponent firstTransform;
    firstTransform.position = {1.0f, 0.0f, 0.0f};
    TransformComponent secondTransform;
    secondTransform.position = {2.0f, 0.0f, 0.0f};
    scene.setTransform(first, firstTransform);
    scene.setTransform(second, secondTransform);

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(100, first);
    objectMap.bind(200, second);

    const std::array<int, 2> requested{200, 100};
    const std::vector<ModelingObjectSceneIdentity> identities =
            objectMap.captureSceneIdentitiesForModelingObjects(scene, requested);

    ASSERT_EQ(identities.size(), 2U);
    EXPECT_EQ(identities[0].entity, second);
    EXPECT_EQ(identities[0].transform.position.x, secondTransform.position.x);
    EXPECT_EQ(identities[1].entity, first);
    EXPECT_EQ(identities[1].transform.position.x, firstTransform.position.x);
}

TEST(ModelingSessionServiceSplitTests, MeshSyncServiceAppliesDuplicateSceneIdentityPreservingMeshAsset) {
    SceneDocument scene;
    const EntityId parent = scene.createEntity("Parent", EntityKind::Empty);
    const EntityId source = scene.createEntity("Source", EntityKind::Mesh);
    const EntityId duplicate = scene.createEntity("Duplicate", EntityKind::Mesh);

    TransformComponent transform;
    transform.position = {3.0f, 4.0f, 5.0f};
    scene.setTransform(source, transform);
    ASSERT_TRUE(scene.setParent(source, parent, false));
    scene.setVisible(source, false);
    scene.setLocked(source, true);
    scene.setMesh(source, MeshComponent{
            .mesh = 11,
            .editorOnly = true,
            .castShadows = false,
            .doubleSided = false,
            .generateCollisions = false,
            .collisionType = MeshCollisionType::Mesh,
            .displayCollision = true,
            .bodyType = MeshPhysicsBodyType::RigidBody,
    });
    scene.setMaterial(source, MaterialComponent{.material = 44});
    scene.setMesh(duplicate, MeshComponent{.mesh = 99});
    scene.setMaterial(duplicate, MaterialComponent{.material = 55});

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(100, source);
    const std::array<int, 1> requested{100};
    const std::vector<ModelingObjectSceneIdentity> identities =
            objectMap.captureSceneIdentitiesForModelingObjects(scene, requested);
    ASSERT_EQ(identities.size(), 1U);

    ModelingMeshSyncService meshSync;
    meshSync.applySceneIdentityToDuplicateEntity(scene, duplicate, identities.front());

    const TransformComponent* duplicateTransform = scene.transform(duplicate);
    ASSERT_NE(duplicateTransform, nullptr);
    EXPECT_EQ(duplicateTransform->position.x, transform.position.x);
    EXPECT_EQ(scene.parent(duplicate), parent);
    ASSERT_NE(scene.visibility(duplicate), nullptr);
    EXPECT_FALSE(scene.visibility(duplicate)->visible);
    ASSERT_NE(scene.lock(duplicate), nullptr);
    EXPECT_TRUE(scene.lock(duplicate)->locked);
    const MeshComponent* duplicateMesh = scene.mesh(duplicate);
    ASSERT_NE(duplicateMesh, nullptr);
    EXPECT_EQ(duplicateMesh->mesh, 99);
    EXPECT_TRUE(duplicateMesh->editorOnly);
    EXPECT_FALSE(duplicateMesh->castShadows);
    EXPECT_FALSE(duplicateMesh->doubleSided);
    EXPECT_FALSE(duplicateMesh->generateCollisions);
    EXPECT_EQ(duplicateMesh->collisionType, MeshCollisionType::Mesh);
    EXPECT_TRUE(duplicateMesh->displayCollision);
    EXPECT_EQ(duplicateMesh->bodyType, MeshPhysicsBodyType::RigidBody);
    ASSERT_NE(scene.material(duplicate), nullptr);
    EXPECT_EQ(scene.material(duplicate)->material, 44);
}

TEST(ModelingSessionServiceSplitTests, MeshSyncPreservesPerEntityPrimaryMaterialsForSharedModelingSlots) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingObjectRuntimeMap objectMap;
    SceneModelingSyncService sceneSync(objectMap);
    ModelingMeshSyncService meshSync;

    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);
    scene.setModelingObject(first, ModelingObjectComponent{.objectId = 100, .materialSlot = 0});
    scene.setModelingObject(second, ModelingObjectComponent{.objectId = 200, .materialSlot = 0});
    scene.setMaterial(first, MaterialComponent{.material = 11});
    scene.setMaterial(second, MaterialComponent{.material = 22});
    objectMap.bind(100, first);
    objectMap.bind(200, second);

    quader_editor_core::EditorSession session({
            quader_editor_core::PolygonObject{.id = 100, .material_id = 0},
            quader_editor_core::PolygonObject{.id = 200, .material_id = 0},
    });

    meshSync.syncSceneObjectsFromModeling(session, scene, assets, sceneSync, true);

    const MaterialComponent* firstMaterial = scene.material(first);
    const MaterialComponent* secondMaterial = scene.material(second);
    ASSERT_NE(firstMaterial, nullptr);
    ASSERT_NE(secondMaterial, nullptr);
    EXPECT_EQ(firstMaterial->material, 11u);
    EXPECT_EQ(secondMaterial->material, 22u);
    ASSERT_TRUE(firstMaterial->materialSlots.contains(0u));
    ASSERT_TRUE(secondMaterial->materialSlots.contains(0u));
    EXPECT_EQ(firstMaterial->materialSlots.at(0u), 11u);
    EXPECT_EQ(secondMaterial->materialSlots.at(0u), 11u);
}

TEST(ModelingSessionServiceSplitTests, SelectionProjectionServiceProjectsSceneSelectionIntoModelingOnly) {
    SceneDocument scene;
    const EntityId group = scene.createEntity("Group", EntityKind::Empty);
    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(100, first);
    objectMap.bind(200, second);

    quader_editor_core::EditorSession session({
            quader_editor_core::PolygonObject{.id = 100},
            quader_editor_core::PolygonObject{.id = 200},
    });

    scene.setSelected(group, true);
    scene.setSelected(first, true);
    scene.setSelected(second, true);
    scene.setActiveSelection(second);

    const SelectionProjectionService projection;
    EXPECT_TRUE(projection.projectObjectSelectionFromScene(session, scene, objectMap));

    std::vector<int> selectedObjectIds = session.selected_object_ids();
    std::sort(selectedObjectIds.begin(), selectedObjectIds.end());
    EXPECT_EQ(selectedObjectIds, (std::vector<int>{100, 200}));
    EXPECT_EQ(session.active_object_id(), 200);

    std::vector<EntityId> selectedEntities = scene.selectedEntities();
    std::sort(selectedEntities.begin(), selectedEntities.end());
    EXPECT_EQ(selectedEntities, (std::vector<EntityId>{group, first, second}));
    EXPECT_EQ(scene.primarySelection(), second);
}

TEST(ModelingSessionServiceSplitTests, SelectionProjectionServiceProjectsModelingSelectionIntoScene) {
    SceneDocument scene;
    const EntityId group = scene.createEntity("Group", EntityKind::Empty);
    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(100, first);
    objectMap.bind(200, second);

    quader_editor_core::EditorSession session({
            quader_editor_core::PolygonObject{.id = 100},
            quader_editor_core::PolygonObject{.id = 200},
    });
    session.set_selected_object_ids({100, 200}, 200);
    scene.setSelected(group, true);

    const SelectionProjectionService projection;
    EXPECT_TRUE(projection.projectSceneSelectionFromModeling(session, scene, objectMap));

    std::vector<EntityId> selectedEntities = scene.selectedEntities();
    std::sort(selectedEntities.begin(), selectedEntities.end());
    EXPECT_EQ(selectedEntities, (std::vector<EntityId>{first, second}));
    EXPECT_EQ(scene.primarySelection(), second);
}

TEST(ModelingSessionServiceSplitTests, SelectionProjectionServiceUsesOneActiveFallbackInBothDirections) {
    SceneDocument scene;
    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(100, first);
    objectMap.bind(200, second);

    quader_editor_core::EditorSession session({
            quader_editor_core::PolygonObject{.id = 100},
            quader_editor_core::PolygonObject{.id = 200},
    });

    const SelectionProjectionService projection;
    const std::array<EntityId, 2> sceneSelection{first, second};
    RuntimeObjectSelectionProjectionResult result =
            projection.projectObjectSelectionForSceneEntities(session, sceneSelection, InvalidEntityId, objectMap);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.changed);
    EXPECT_EQ(session.active_object_id(), 200);

    session.set_selected_object_ids({100, 200}, -1);
    EXPECT_TRUE(projection.projectSceneSelectionFromModeling(session, scene, objectMap));
    EXPECT_EQ(scene.primarySelection(), second);
}

TEST(ModelingSessionServiceSplitTests, SelectionProjectionServiceProjectsExplicitObjectSelection) {
    SceneDocument scene;
    const EntityId group = scene.createEntity("Group", EntityKind::Empty);
    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(100, first);
    objectMap.bind(200, second);

    quader_editor_core::EditorSession session({
            quader_editor_core::PolygonObject{.id = 100},
            quader_editor_core::PolygonObject{.id = 200},
    });

    const SelectionProjectionService projection;
    const std::array<EntityId, 2> selected{first, second};
    RuntimeObjectSelectionProjectionResult result =
            projection.projectObjectSelectionForSceneEntities(session, selected, InvalidEntityId, objectMap);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.changed);
    EXPECT_TRUE(result.hasModelingObjects);

    std::vector<int> selectedObjectIds = session.selected_object_ids();
    std::sort(selectedObjectIds.begin(), selectedObjectIds.end());
    EXPECT_EQ(selectedObjectIds, (std::vector<int>{100, 200}));
    EXPECT_EQ(session.active_object_id(), 200);

    result = projection.projectObjectSelectionForSceneEntities(session, selected, second, objectMap);
    EXPECT_TRUE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_TRUE(result.hasModelingObjects);

    result = projection.projectObjectSelectionForSceneEntities(session, selected, first, objectMap);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.changed);
    EXPECT_EQ(session.active_object_id(), 100);

    const std::array<EntityId, 1> nonModeling{group};
    result = projection.projectObjectSelectionForSceneEntities(session, nonModeling, group, objectMap);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_FALSE(result.hasModelingObjects);
    EXPECT_EQ(session.active_object_id(), 100);
}

TEST(ModelingSessionServiceSplitTests, SelectionProjectionServicePromotesToolObjectSelection) {
    SceneDocument scene;
    const EntityId group = scene.createEntity("Group", EntityKind::Empty);
    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(100, first);
    objectMap.bind(200, second);

    quader_editor_core::EditorSession session({
            quader_editor_core::PolygonObject{.id = 100},
            quader_editor_core::PolygonObject{.id = 200},
    });

    scene.setSelected(group, true);
    scene.setSelected(first, true);
    scene.setSelected(second, true);
    scene.setActiveSelection(second);

    const SelectionProjectionService projection;
    EXPECT_TRUE(projection.promoteObjectSelectionForTool(session, scene, objectMap));

    std::vector<int> selectedObjectIds = session.selected_object_ids();
    std::sort(selectedObjectIds.begin(), selectedObjectIds.end());
    EXPECT_EQ(selectedObjectIds, (std::vector<int>{100, 200}));
    EXPECT_EQ(session.active_object_id(), 200);
}

TEST(ModelingSessionServiceSplitTests, SelectionProjectionServicePromotionFallsBackToRuntimeSelection) {
    SceneDocument scene;
    const EntityId group = scene.createEntity("Group", EntityKind::Empty);
    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);

    ModelingObjectRuntimeMap objectMap;
    objectMap.bind(100, first);
    objectMap.bind(200, second);

    quader_editor_core::EditorSession session({
            quader_editor_core::PolygonObject{.id = 100},
            quader_editor_core::PolygonObject{.id = 200},
    });
    session.set_selected_object_ids({100, 200}, -1);
    scene.setSelected(group, true);
    scene.setActiveSelection(group);

    const SelectionProjectionService projection;
    EXPECT_TRUE(projection.promoteObjectSelectionForTool(session, scene, objectMap));

    std::vector<int> selectedObjectIds = session.selected_object_ids();
    std::sort(selectedObjectIds.begin(), selectedObjectIds.end());
    EXPECT_EQ(selectedObjectIds, (std::vector<int>{100, 200}));
    EXPECT_EQ(session.active_object_id(), 200);
}

TEST(ModelingSessionServiceSplitTests, ObjectRuntimeMapStaysOutOfServiceBucket) {
    const std::string cmake = readSourceFile("CMakeLists.txt");
    const std::string services = readSourceFile("src/editor/modeling/services/ModelingSessionServices.cpp");
    const std::string serviceAggregate = readSourceFile("src/editor/modeling/services/ModelingSessionServices.h");

    EXPECT_NE(cmake.find("src/editor/modeling/runtime/ModelingObjectRuntimeMap.cpp"), std::string::npos);
    EXPECT_NE(serviceAggregate.find("editor/modeling/runtime/ModelingObjectRuntimeMap.h"), std::string::npos);
    EXPECT_EQ(cmake.find("src/editor/modeling/services/ModelingObjectRegistry"), std::string::npos);
    EXPECT_EQ(serviceAggregate.find("ModelingObjectRegistry"), std::string::npos);
    EXPECT_EQ(services.find("ModelingObjectRuntimeMap::"), std::string::npos);
    const std::string runtimeMapHeader = readSourceFile("src/editor/modeling/runtime/ModelingObjectRuntimeMap.h");
    EXPECT_EQ(runtimeMapHeader.find("\n    std::unordered_map<int, EntityId>& entitiesByObject()"), std::string::npos);
    EXPECT_EQ(runtimeMapHeader.find("\n    std::unordered_map<EntityId, int>& objectsByEntity()"), std::string::npos);
    EXPECT_NE(runtimeMapHeader.find("void bind(int modelingObjectId, EntityId entity)"), std::string::npos);
    EXPECT_NE(runtimeMapHeader.find("void unbindObject(int modelingObjectId)"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests, ObjectSelectionMappingLivesInSelectionProjectionService) {
    const std::string facade = readSourceFile("src/editor/modeling/services/ModelingSessionService.cpp");
    const std::string projection = readSourceFile("src/editor/selection/SelectionProjectionService.cpp");

    EXPECT_NE(facade.find("selectionProjectionService_.projectObjectSelectionForSceneEntities("), std::string::npos);
    EXPECT_EQ(facade.find("const auto object = objectRegistry_.modelingObjectForEntity(entity);"), std::string::npos);
    EXPECT_EQ(facade.find("const auto object = objectRegistry_.modelingObjectForEntity(active);"), std::string::npos);
    EXPECT_NE(projection.find("projectObjectSelectionForSceneEntities("), std::string::npos);
    EXPECT_NE(projection.find("runtimeObjectSelectionForSceneEntities("), std::string::npos);
    EXPECT_NE(projection.find("resolveProjectedActiveSelection("), std::string::npos);
    EXPECT_NE(projection.find("applyRuntimeObjectSelection("), std::string::npos);
    EXPECT_NE(projection.find("applyExactSceneSelection("), std::string::npos);
    EXPECT_EQ(projection.find("orderedSelected.front()"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests, SelectionCenterWorldProjectionLivesInSelectionProjectionService) {
    const std::string facade = readSourceFile("src/editor/modeling/services/ModelingSessionService.cpp");
    const std::string projectionHeader = readSourceFile("src/editor/selection/SelectionProjectionService.h");
    const std::string projection = readSourceFile("src/editor/selection/SelectionProjectionService.cpp");

    EXPECT_NE(
            facade.find("selectionProjectionService_.worldSelectionCenter(session_, scene_, assets_, objectRegistry_)"),
            std::string::npos);
    EXPECT_EQ(facade.find("selectedWorldBounds(scene_, assets_)"), std::string::npos);
    EXPECT_EQ(facade.find("quader_poly::selected_vertex_ids(object.document, object.selection)"), std::string::npos);
    EXPECT_EQ(facade.find("transformObjectPoint("), std::string::npos);
    EXPECT_NE(projectionHeader.find("worldSelectionCenter("), std::string::npos);
    EXPECT_NE(projection.find("SelectionProjectionService::worldSelectionCenter("), std::string::npos);
    EXPECT_NE(projection.find("selectedWorldBounds(scene, assets)"), std::string::npos);
    EXPECT_NE(projection.find("worldPointForModelingObject("), std::string::npos);
    EXPECT_NE(projection.find("quader_poly::selected_vertex_ids(object.document, object.selection)"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests, DuplicateSceneIdentityLivesInRuntimeMapAndMeshSync) {
    const std::string facade = readSourceFile("src/editor/modeling/services/ModelingSessionService.cpp");
    const std::string runtimeMap = readSourceFile("src/editor/modeling/runtime/ModelingObjectRuntimeMap.cpp");
    const std::string meshSync = readSourceFile("src/editor/modeling/services/ModelingMeshSyncService.cpp");

    EXPECT_EQ(facade.find("DuplicateSceneState"), std::string::npos);
    EXPECT_EQ(facade.find("scene_.setTransform(*duplicate"), std::string::npos);
    EXPECT_EQ(facade.find("scene_.setMaterial(*duplicate"), std::string::npos);
    EXPECT_NE(facade.find("captureSelectedDuplicateSceneIdentities()"), std::string::npos);
    EXPECT_NE(facade.find("meshSyncService_.applySceneIdentityToDuplicateEntity("), std::string::npos);
    EXPECT_NE(runtimeMap.find("captureSceneIdentitiesForModelingObjects("), std::string::npos);
    EXPECT_NE(meshSync.find("applySceneIdentityToDuplicateEntity("), std::string::npos);
    EXPECT_NE(meshSync.find("std::sort(entitiesByObject.begin(), entitiesByObject.end()"), std::string::npos);
    EXPECT_LT(
            meshSync.find("existingMaterial != nullptr && existingMaterial->material != InvalidAssetId"),
            meshSync.find("materialBySlot.find(materialSlot)"));
}

TEST(ModelingSessionServiceSplitTests, TransformDragPreviewRoutingLivesInController) {
    const std::string facade = readSourceFile("src/editor/modeling/services/ModelingSessionService.cpp");
    const std::string controller = readSourceFile("src/editor/modeling/runtime_state/TransformDragController.cpp");

    EXPECT_NE(facade.find("TransformDragController::previewRouting("), std::string::npos);
    EXPECT_EQ(facade.find("duplicateSceneTransformPreview"), std::string::npos);
    EXPECT_EQ(facade.find("needsRuntimePreviewTransaction ="), std::string::npos);
    EXPECT_NE(controller.find("TransformDragController::previewRouting("), std::string::npos);
    EXPECT_NE(controller.find("sceneTransformPreviewEligible"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / "src/editor/modeling/services/TransformDragController.h"));
    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / "src/editor/modeling/services/TransformPreviewService.h"));
    EXPECT_FALSE(std::filesystem::exists(sourceRoot() / "src/editor/modeling/services/ModalToolRuntime.h"));
}

TEST(ModelingSessionServiceSplitTests, PickingAndOverlayProjectionHelpersStayOutOfFacade) {
    const std::string facade = readSourceFile("src/editor/modeling/services/ModelingSessionService.cpp");
    const std::string services = readSourceFile("src/editor/modeling/services/ModelingSessionServices.cpp");
    const std::string pickingProjection = readSourceFile("src/editor/projection/PickingCacheProjection.cpp");

    EXPECT_EQ(facade.find("objectPickFaceGeometrySignature("), std::string::npos);
    EXPECT_EQ(facade.find("mixPickGeometryHash("), std::string::npos);
    EXPECT_EQ(facade.find("buildObjectPickBounds("), std::string::npos);
    EXPECT_EQ(facade.find("includePickBoundsPoint("), std::string::npos);
    EXPECT_EQ(facade.find("appendSelectedHoverFilteredBaseFrame("), std::string::npos);
    EXPECT_EQ(facade.find("hoverFrameSuppressesTriangle("), std::string::npos);
    EXPECT_EQ(facade.find("hoverFrameSuppressesLine("), std::string::npos);
    EXPECT_EQ(facade.find("hoverFrameSuppressesPoint("), std::string::npos);
    EXPECT_EQ(facade.find("appendOverlayFrame("), std::string::npos);

    EXPECT_NE(services.find("appendSelectedHoverFilteredBaseFrame("), std::string::npos);
    EXPECT_NE(services.find("appendOverlayFrame("), std::string::npos);
    EXPECT_NE(services.find("PickingCacheProjection::buildObjectPickBounds("), std::string::npos);
    EXPECT_NE(services.find("PickingCacheProjection::objectFaceGeometrySignature("), std::string::npos);
    EXPECT_NE(pickingProjection.find("objectFaceGeometrySignature("), std::string::npos);
    EXPECT_NE(pickingProjection.find("buildObjectPickBounds("), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests, GroupDragHierarchyDuplicatePolicyLivesInFocusedService) {
    const std::string facade = readSourceFile("src/editor/modeling/services/ModelingSessionService.cpp");
    const std::string service = readSourceFile("src/editor/modeling/services/ModelingHierarchyDuplicateService.cpp");
    const std::string documentHelpers = readSourceFile("src/editor/commands/document/DocumentCommandSceneHelpers.h");
    const std::string cmake = readSourceFile("CMakeLists.txt");

    EXPECT_NE(cmake.find("src/editor/modeling/services/ModelingHierarchyDuplicateService.cpp"), std::string::npos);
    EXPECT_NE(facade.find("hierarchyDuplicateService_.duplicateSelectedHierarchyForGroupDrag("), std::string::npos);
    EXPECT_EQ(facade.find("std::unordered_map<EntityId, EntityId> cloneBySource"), std::string::npos);
    EXPECT_EQ(facade.find("auto copyCommonEntityState"), std::string::npos);
    EXPECT_NE(service.find("pairDuplicateMeshes("), std::string::npos);
    EXPECT_NE(service.find("duplicateHierarchyWithExistingClones("), std::string::npos);
    EXPECT_EQ(service.find("copyCommonEntityState("), std::string::npos);
    EXPECT_EQ(service.find("cloneHierarchyEntity("), std::string::npos);
    EXPECT_NE(documentHelpers.find("documentCommandDuplicateEntitySnapshotTree("), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests, SceneAwareDeletePolicyLivesInFocusedCommand) {
    const std::string facade = readSourceFile("src/editor/modeling/services/ModelingSessionService.cpp");
    const std::string command = readSourceFile("src/editor/commands/modeling/SceneAwareDeleteSelectionCommand.cpp");
    const std::string commandHeader = readSourceFile("src/editor/commands/modeling/SceneAwareDeleteSelectionCommand.h");
    const std::string cmake = readSourceFile("CMakeLists.txt");

    EXPECT_NE(cmake.find("src/editor/commands/modeling/SceneAwareDeleteSelectionCommand.cpp"), std::string::npos);
    EXPECT_NE(facade.find("createSceneAwareDeleteSelectionCommand("), std::string::npos);
    EXPECT_NE(facade.find("SceneAwareDeleteRuntime{"), std::string::npos);
    EXPECT_NE(facade.find(".entityForModelingObject = [&](int modelingObjectId)"), std::string::npos);
    EXPECT_EQ(facade.find("struct SceneAwareDeleteState"), std::string::npos);
    EXPECT_EQ(facade.find("class SceneAwareDeleteSelectionCommand"), std::string::npos);
    EXPECT_EQ(facade.find("captureSceneAwareDeleteState"), std::string::npos);
    EXPECT_EQ(facade.find("redoSceneAwareDelete"), std::string::npos);
    EXPECT_EQ(facade.find("undoSceneAwareDelete"), std::string::npos);
    EXPECT_NE(commandHeader.find("entityForModelingObject"), std::string::npos);
    EXPECT_EQ(command.find("for (const EntityId entity : scene.entities())"), std::string::npos);
    EXPECT_NE(command.find("runtime.entityForModelingObject(snapshot.modelingObject->objectId)"), std::string::npos);
    EXPECT_NE(command.find("struct SceneAwareDeleteState"), std::string::npos);
    EXPECT_NE(command.find("class SceneAwareDeleteSelectionCommand"), std::string::npos);
    EXPECT_NE(command.find("captureSceneAwareDeleteState"), std::string::npos);
    EXPECT_NE(command.find("redoSceneAwareDelete"), std::string::npos);
    EXPECT_NE(command.find("undoSceneAwareDelete"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests, SceneAwareDeleteRestoreUsesRuntimeMapInsteadOfStaleDuplicateSceneScan) {
    SceneDocument scene;
    ModelingObjectRuntimeMap objectMap;
    const int modelingObjectId = 42;

    const EntityId stale = scene.createEntity("Stale Duplicate", EntityKind::Mesh);
    scene.setModelingObject(stale, ModelingObjectComponent{.objectId = modelingObjectId});

    const EntityId selected = scene.createEntity("Selected Runtime", EntityKind::Mesh);
    scene.setModelingObject(selected, ModelingObjectComponent{.objectId = modelingObjectId});
    ASSERT_TRUE(scene.setSelected(selected, true));
    ASSERT_TRUE(scene.setActiveSelection(selected));
    objectMap.bind(modelingObjectId, selected);

    std::unique_ptr<IUndoableCommand> command = createSceneAwareDeleteSelectionCommand(
            scene,
            SceneAwareDeleteRuntime{
                    .captureModelingMemento = [] {
                        return ModelingSessionMemento{};
                    },
                    .restoreModelingMemento = [&](const ModelingSessionMemento&) {
                        const EntityId restored = scene.createEntity("Runtime Restored", EntityKind::Mesh);
                        scene.setModelingObject(restored, ModelingObjectComponent{.objectId = modelingObjectId});
                        objectMap.bind(modelingObjectId, restored);
                        return ModelingCommandResult{.ok = true, .changed = true};
                    },
                    .appSelectionMode = [] {
                        return ModelingSelectionMode::Object;
                    },
                    .setAppSelectionMode = [](ModelingSelectionMode) {},
                    .deleteModelingObjects = [&](const std::vector<int>& objectIds) {
                        EXPECT_EQ(objectIds, std::vector<int>{modelingObjectId});
                        objectMap.unbindObject(modelingObjectId);
                        return ModelingCommandResult{.ok = true, .changed = true};
                    },
                    .entityForModelingObject = [&](int objectId) {
                        return objectMap.entityForModelingObject(objectId);
                    },
                    .syncObjectSelectionFromScene = [](bool) {},
                    .clearRuntimeObjectSelectionForGroupMode = [] {},
                    .invalidateSelectionOverlayCaches = [] {},
            });

    const CommandResult deleted = command->redo();
    ASSERT_TRUE(deleted.ok) << deleted.message;
    EXPECT_FALSE(scene.exists(selected));
    ASSERT_TRUE(scene.exists(stale));
    ASSERT_NE(scene.name(stale), nullptr);
    EXPECT_EQ(scene.name(stale)->name, "Stale Duplicate");

    const CommandResult restored = command->undo();
    ASSERT_TRUE(restored.ok) << restored.message;
    const std::optional<EntityId> restoredEntity = objectMap.entityForModelingObject(modelingObjectId);
    ASSERT_TRUE(restoredEntity.has_value());
    ASSERT_TRUE(scene.exists(*restoredEntity));
    ASSERT_NE(scene.name(*restoredEntity), nullptr);
    EXPECT_EQ(scene.name(*restoredEntity)->name, "Selected Runtime");
    ASSERT_NE(scene.name(stale), nullptr);
    EXPECT_EQ(scene.name(stale)->name, "Stale Duplicate");
}

TEST(ModelingSessionServiceSplitTests, ServicesOwnIndependentModelingState) {
    TransformDragController drag;
    EXPECT_FALSE(drag.previewDriven());
    drag.setPreviewDriven(true, true);
    EXPECT_TRUE(drag.previewDriven());
    EXPECT_TRUE(drag.duplicateSelected());
    drag.clear();
    EXPECT_FALSE(drag.previewDriven());
    const TransformDragPreviewRouting objectTransform =
            TransformDragController::previewRouting(false, true, true);
    EXPECT_FALSE(objectTransform.previewDriven);
    EXPECT_FALSE(objectTransform.needsRuntimePreviewTransaction);
    EXPECT_TRUE(objectTransform.sceneTransformPreviewEligible);
    const TransformDragPreviewRouting duplicateObjectTransform =
            TransformDragController::previewRouting(true, true, true);
    EXPECT_FALSE(duplicateObjectTransform.previewDriven);
    EXPECT_TRUE(duplicateObjectTransform.needsRuntimePreviewTransaction);
    EXPECT_TRUE(duplicateObjectTransform.sceneTransformPreviewEligible);
    const TransformDragPreviewRouting componentTransform =
            TransformDragController::previewRouting(false, false, true);
    EXPECT_TRUE(componentTransform.previewDriven);
    EXPECT_TRUE(componentTransform.needsRuntimePreviewTransaction);
    EXPECT_FALSE(componentTransform.sceneTransformPreviewEligible);
    const TransformDragPreviewRouting objectExtrude =
            TransformDragController::previewRouting(false, true, false);
    EXPECT_FALSE(objectExtrude.previewDriven);
    EXPECT_FALSE(objectExtrude.needsRuntimePreviewTransaction);
    EXPECT_FALSE(objectExtrude.sceneTransformPreviewEligible);

    ModelingToolSettingsService toolSettings;
    toolSettings.setPreviewMetadata("edge_bevel", "edge_bevel", "Edge Bevel");
    const ModelingToolSettingsTarget target = toolSettings.operationPreviewTarget(
            ModelingToolSettingsTargetKind::EdgeBevel,
            "Fallback",
            "fallback_operation");
    EXPECT_TRUE(target.active);
    EXPECT_TRUE(target.operationPreviewActive);
    EXPECT_EQ(target.actionId, "edge_bevel");
    EXPECT_EQ(target.operationId, "edge_bevel");
    EXPECT_EQ(target.label, "Edge Bevel");
    toolSettings.clearPreviewMetadata();
    const ModelingToolSettingsTarget fallback = toolSettings.operationPreviewTarget(
            ModelingToolSettingsTargetKind::Thicken,
            "Thicken",
            "thicken");
    EXPECT_EQ(fallback.label, "Thicken");
    EXPECT_EQ(fallback.operationId, "thicken");
    quader_editor_core::PreviewTransactionStatus preview;
    preview.active = true;
    preview.kind = quader_editor_core::PreviewTransactionKind::edge_bevel;
    const ModelingToolSettingsTarget previewTarget = toolSettings.activePreviewTarget(preview);
    EXPECT_TRUE(previewTarget.active);
    EXPECT_TRUE(previewTarget.operationPreviewActive);
    EXPECT_EQ(previewTarget.kind, ModelingToolSettingsTargetKind::EdgeBevel);
    EXPECT_EQ(previewTarget.operationId, "edge_bevel");
    EXPECT_EQ(previewTarget.label, "Bevel Edges");
    quader_editor_core::EditorRuntimeStatus runtimeStatus;
    runtimeStatus.cut_active = true;
    const ModelingToolSettingsTarget runtimeTarget = toolSettings.activeRuntimeToolTarget(runtimeStatus);
    EXPECT_TRUE(runtimeTarget.active);
    EXPECT_FALSE(runtimeTarget.operationPreviewActive);
    EXPECT_EQ(runtimeTarget.kind, ModelingToolSettingsTargetKind::Cut);
    EXPECT_EQ(runtimeTarget.label, "Cut");

    ModelingInteractionSettingsService interactionSettings;
    EXPECT_EQ(interactionSettings.settings().cutMode, ModelingCutMode::KeepBoth);
    interactionSettings.setCutMode(ModelingCutMode::DiscardLeft);
    EXPECT_EQ(interactionSettings.settings().cutMode, ModelingCutMode::DiscardLeft);
    interactionSettings.cycleCutModeWithoutRuntime();
    EXPECT_EQ(interactionSettings.settings().cutMode, ModelingCutMode::DiscardRight);
}

TEST(ModelingSessionServiceSplitTests, PolicyServicesExposeDirectBehavior) {
    ModelingSelectionSummary componentSelection;
    EXPECT_FALSE(ModelingSelectionService::hasComponentSelection(componentSelection));
    componentSelection.selectedEdgeCount = 1;
    EXPECT_TRUE(ModelingSelectionService::hasComponentSelection(componentSelection));

    ModelingToolStatus status;
    EXPECT_FALSE(ModelingOverlayService::hasModalOverlay(status));
    status.cutHasLine = true;
    EXPECT_TRUE(ModelingOverlayService::hasModalOverlay(status));

    EXPECT_TRUE(ModalToolRuntime::ownsViewport(ModelingViewportTool::Box));
    EXPECT_TRUE(ModalToolRuntime::ownsViewport(ModelingViewportTool::Mirror));
    EXPECT_FALSE(ModalToolRuntime::ownsViewport(ModelingViewportTool::Move));
    for (const ToolDescriptor& descriptor : viewportToolDescriptors()) {
        EXPECT_EQ(ModalToolRuntime::ownsViewport(descriptor.modelingTool), descriptor.ownsViewport)
                << descriptor.id;
    }
    EXPECT_TRUE(ModelingMeshSyncService::shouldForceRefresh(true, false));
    EXPECT_TRUE(ModelingMeshSyncService::shouldForceRefresh(false, true));
    EXPECT_FALSE(ModelingMeshSyncService::shouldForceRefresh(false, false));
    const quader_editor_core::EditorRuntimeCommand copy = ModelingClipboardService::makeCopyCommand();
    EXPECT_EQ(copy.command_id, ModelingClipboardService::copyCommand());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(copy.settings));
    const quader_editor_core::EditorRuntimeCommand paste = ModelingClipboardService::makePasteCommand();
    EXPECT_EQ(paste.command_id, ModelingClipboardService::pasteCommand());
    EXPECT_TRUE(std::holds_alternative<quader_editor_core::PasteCopiedMeshesRuntimeCommandSettings>(
            paste.settings));
    EXPECT_TRUE(TransformPreviewService::isTransform(ModelingTransformOperation::Transform));
    EXPECT_FALSE(TransformPreviewService::isTransform(ModelingTransformOperation::Extrude));
    EXPECT_FALSE(ModelingCommandExecutor::runtimeUnavailable().ok);
    EXPECT_STREQ(fallbackToolSettingsLabel(ModelingToolSettingsTargetKind::EdgeBevel), "Bevel Edges");
    EXPECT_STREQ(fallbackToolSettingsLabel(ModelingToolSettingsTargetKind::None), "Tool Settings");
}

TEST(ModelingSessionServiceSplitTests, ContextActionServiceAppliesModalQueryState) {
    ModelingContextActionService service;
    ModelingActionQuery query;
    EXPECT_FALSE(query.modalToolOwnsViewport);

    const ModelingActionQuery effective = service.effectiveActionQuery(query, true);
    EXPECT_TRUE(effective.modalToolOwnsViewport);

    query.suppressWhenModalToolOwnsViewport = false;
    const ModelingActionQuery unsuppressed = service.effectiveActionQuery(query, true);
    EXPECT_FALSE(unsuppressed.modalToolOwnsViewport);

    ModelingSelectionSummary summary;
    summary.mode = ModelingSelectionMode::Object;
    summary.hasSelection = true;
    summary.selectedObjectCount = 1;
    const ModelingActionSet actionSet = service.actionSet(summary, {}, false);
    EXPECT_EQ(actionSet.surface, ModelingActionSurface::OperationsPanel);
    EXPECT_EQ(actionSet.surfaceId, "operations_top_bar");

    const ModelingContextActionResolution modalResult =
            service.resolveExecutableAction(summary, "delete_selection", true);
    EXPECT_FALSE(modalResult.ok);
    EXPECT_EQ(modalResult.message, "Viewport is owned by an active modal tool.");

    ModelingSelectionSummary emptySummary;
    emptySummary.mode = ModelingSelectionMode::Object;
    const ModelingContextActionResolution disabledResult =
            service.resolveExecutableAction(emptySummary, "delete_selection", false);
    EXPECT_FALSE(disabledResult.ok);
    EXPECT_EQ(disabledResult.message, "Action is disabled for the current selection.");
    EXPECT_EQ(disabledResult.action.disabledReasonKey, "modeling.action.disabled.delete_selection");

    const ModelingContextActionResolution enabledResult =
            service.resolveExecutableAction(summary, "delete_selection", false);
    EXPECT_TRUE(enabledResult.ok);
    EXPECT_EQ(enabledResult.action.actionId, "delete_selection");
}

TEST(ModelingSessionServiceSplitTests, InstantOperationEnablementUsesHostSingleObjectConstraint) {
    const ModelingInstantOperationCatalogEntry* connect =
            findModelingInstantOperationCatalogEntry(ModelingInstantOperationId::ConnectVertices);
    ASSERT_NE(connect, nullptr);
    const ModelingInstantOperationCatalogEntry* combineFaces =
            findModelingInstantOperationCatalogEntry(ModelingInstantOperationId::CombineFaces);
    ASSERT_NE(combineFaces, nullptr);

    ModelingSelectionSummary summary;
    summary.mode = ModelingSelectionMode::Vertex;
    summary.hasSelection = true;
    summary.activeKind = ModelingActiveSelectionKind::Vertex;
    summary.selectedVertexCount = 1;
    summary.selectedObjectCount = 1;

    EXPECT_FALSE(modelingInstantOperationEnabled(*connect, summary));

    summary.selectedVertexCount = 2;
    summary.selectedObjectCount = 2;

    EXPECT_FALSE(modelingInstantOperationEnabled(*connect, summary));

    summary.selectedObjectCount = 1;
    EXPECT_TRUE(modelingInstantOperationEnabled(*connect, summary));

    ModelingSelectionSummary faceSummary;
    faceSummary.mode = ModelingSelectionMode::Face;
    faceSummary.hasSelection = true;
    faceSummary.selectedFaceCount = 1;
    faceSummary.selectedObjectCount = 1;

    EXPECT_FALSE(modelingInstantOperationEnabled(*combineFaces, faceSummary));

    faceSummary.selectedFaceCount = 2;
    EXPECT_TRUE(modelingInstantOperationEnabled(*combineFaces, faceSummary));
}

TEST(ModelingSessionServiceSplitTests, InstantOperationAvailabilityUsesActionCatalogEvaluator) {
    const std::string instantHeader =
            readSourceFile("src/editor/modeling/services/ModelingInstantOperationCatalog.h");
    const std::string instantCatalog =
            readSourceFile("src/editor/modeling/services/ModelingInstantOperationCatalog.cpp");
    const std::string actionCatalog =
            readSourceFile("src/editor/modeling/services/ModelingActionCatalog.cpp");

    EXPECT_EQ(instantHeader.find("ModelingInstantOperationEnablement"), std::string::npos);
    EXPECT_EQ(instantCatalog.find("switch (entry"), std::string::npos);
    EXPECT_EQ(instantCatalog.find("HostActionDescriptor"), std::string::npos);
    EXPECT_NE(instantCatalog.find("ModelingActionCatalog::evaluateOperationAvailability("), std::string::npos);
    EXPECT_NE(actionCatalog.find("evaluateHostOperationAvailability("), std::string::npos);
    EXPECT_NE(actionCatalog.find("descriptor.availability.selection_mode"), std::string::npos);
    EXPECT_NE(actionCatalog.find("evaluateAvailabilityRule("), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests, InstantOperationSettingsComeFromSharedOperationSettingsOwner) {
    RuntimeOperationExecutor executor;
    ModelingInteractionSettings interactionSettings;
    interactionSettings.bridgeSteps = 4;

    const std::optional<RuntimeOperationExecutor::OperationCommand> bridge =
            executor.operationForInstantCommand(ModelingInstantOperationId::Bridge, interactionSettings);
    ASSERT_TRUE(bridge.has_value());
    EXPECT_EQ(bridge->operation, quader_editor_core::EditorOperationId::bridge);
    const auto& bridgeSettings = std::get<quader_editor_core::BridgeOperationSettings>(bridge->settings);
    const auto expectedBridge = std::get<quader_editor_core::BridgeOperationSettings>(
            settingsForOperation(quader_editor_core::EditorOperationId::bridge, interactionSettings));
    EXPECT_EQ(bridgeSettings.steps, expectedBridge.steps);

    const std::optional<RuntimeOperationExecutor::OperationCommand> bevel =
            executor.operationForInstantCommand(ModelingInstantOperationId::BevelVertices, interactionSettings);
    ASSERT_TRUE(bevel.has_value());
    EXPECT_TRUE(std::holds_alternative<quader_editor_core::BevelVerticesOperationSettings>(bevel->settings));

    const std::optional<RuntimeOperationExecutor::OperationCommand> recalculate =
            executor.operationForInstantCommand(ModelingInstantOperationId::RecalculateFaceNormals, interactionSettings);
    ASSERT_TRUE(recalculate.has_value());
    const auto& recalculateSettings =
            std::get<quader_editor_core::RecalculateFaceNormalsOperationSettings>(recalculate->settings);
    EXPECT_TRUE(recalculateSettings.outside);
}

TEST(ModelingSessionServiceSplitTests, InstantOperationSettingsMappingHasSingleOwner) {
    const std::string catalogHeader =
            readSourceFile("src/editor/modeling/services/ModelingInstantOperationCatalog.h");
    const std::string catalog = readSourceFile("src/editor/modeling/services/ModelingInstantOperationCatalog.cpp");
    const std::string services = readSourceFile("src/editor/modeling/services/ModelingSessionServices.cpp");
    const std::string operationSettings = readSourceFile("src/editor/modeling/services/ModelingOperationSettings.cpp");

    EXPECT_EQ(catalogHeader.find("RuntimeOperationSettingsKind"), std::string::npos);
    EXPECT_EQ(catalog.find("RuntimeOperationSettingsKind"), std::string::npos);
    EXPECT_EQ(catalog.find("settingsKind"), std::string::npos);
    EXPECT_EQ(services.find("settingsKind"), std::string::npos);
    EXPECT_NE(services.find("settingsForOperation(*entry->runtimeOperation, interactionSettings)"), std::string::npos);
    EXPECT_NE(operationSettings.find("command_schema_for_operation(operation)"), std::string::npos);
    EXPECT_NE(operationSettings.find("schema->settings_kind"), std::string::npos);
    EXPECT_EQ(operationSettings.find("case quader_editor_core::EditorOperationId::connect_vertices"), std::string::npos);
    EXPECT_EQ(operationSettings.find("case quader_editor_core::EditorOperationId::delete_selection"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests, FacadeDelegatesToSplitServicesThroughPublicApi) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService facade(scene, assets);

    EXPECT_TRUE(facade.objects().empty());
    EXPECT_EQ(facade.selectionMode(), ModelingSelectionMode::Object);
    EXPECT_FALSE(facade.selectionSummary().hasSelection);
    EXPECT_FALSE(facade.modalToolOwnsViewport());

    const ModelingToolSettingsTarget settings = facade.toolSettingsTarget();
    EXPECT_FALSE(settings.active);
    EXPECT_EQ(facade.contextActionSet().surfaceId, "operations_top_bar");
}

} // namespace
} // namespace quader
