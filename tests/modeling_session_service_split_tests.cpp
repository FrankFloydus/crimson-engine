////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/AssetRegistry.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/modeling/services/ModelingSessionServices.h"
#include "editor/modeling/services/ModelingOperationSettings.h"
#include "editor/modeling/services/ModelingInstantOperationCatalog.h"
#include "editor/selection/SelectionProjectionService.h"
#include "scene/SceneDocument.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include <variant>

#include <editor/modeling/runtime/session.h>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

std::string readSourceFile(const std::filesystem::path& relativePath) {
    const std::filesystem::path path = std::filesystem::path(QUADER_SOURCE_DIR) / relativePath;
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << path.string();
    if (!file.is_open()) {
        return {};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

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

TEST(ModelingSessionServiceSplitTests, ObjectRuntimeMapStaysOutOfServiceBucket) {
    const std::string cmake = readSourceFile("CMakeLists.txt");
    const std::string services = readSourceFile("src/editor/modeling/services/ModelingSessionServices.cpp");
    const std::string serviceAggregate = readSourceFile("src/editor/modeling/services/ModelingSessionServices.h");

    EXPECT_NE(cmake.find("src/editor/modeling/runtime/ModelingObjectRuntimeMap.cpp"), std::string::npos);
    EXPECT_NE(serviceAggregate.find("editor/modeling/runtime/ModelingObjectRuntimeMap.h"), std::string::npos);
    EXPECT_EQ(cmake.find("src/editor/modeling/services/ModelingObjectRegistry"), std::string::npos);
    EXPECT_EQ(serviceAggregate.find("ModelingObjectRegistry"), std::string::npos);
    EXPECT_EQ(services.find("ModelingObjectRuntimeMap::"), std::string::npos);
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
    const std::string cmake = readSourceFile("CMakeLists.txt");

    EXPECT_NE(cmake.find("src/editor/commands/modeling/SceneAwareDeleteSelectionCommand.cpp"), std::string::npos);
    EXPECT_NE(facade.find("createSceneAwareDeleteSelectionCommand("), std::string::npos);
    EXPECT_NE(facade.find("SceneAwareDeleteRuntime{"), std::string::npos);
    EXPECT_EQ(facade.find("struct SceneAwareDeleteState"), std::string::npos);
    EXPECT_EQ(facade.find("class SceneAwareDeleteSelectionCommand"), std::string::npos);
    EXPECT_EQ(facade.find("captureSceneAwareDeleteState"), std::string::npos);
    EXPECT_EQ(facade.find("redoSceneAwareDelete"), std::string::npos);
    EXPECT_EQ(facade.find("undoSceneAwareDelete"), std::string::npos);
    EXPECT_NE(command.find("struct SceneAwareDeleteState"), std::string::npos);
    EXPECT_NE(command.find("class SceneAwareDeleteSelectionCommand"), std::string::npos);
    EXPECT_NE(command.find("captureSceneAwareDeleteState"), std::string::npos);
    EXPECT_NE(command.find("redoSceneAwareDelete"), std::string::npos);
    EXPECT_NE(command.find("undoSceneAwareDelete"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests, ServicesOwnIndependentModelingState) {
    TransformDragController drag;
    EXPECT_FALSE(drag.previewDriven());
    drag.setPreviewDriven(true, true);
    EXPECT_TRUE(drag.previewDriven());
    EXPECT_TRUE(drag.duplicateSelected());
    drag.clear();
    EXPECT_FALSE(drag.previewDriven());

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
    EXPECT_EQ(disabledResult.message, "Select something to delete.");

    const ModelingContextActionResolution enabledResult =
            service.resolveExecutableAction(summary, "delete_selection", false);
    EXPECT_TRUE(enabledResult.ok);
    EXPECT_EQ(enabledResult.action.actionId, "delete_selection");
}

TEST(ModelingSessionServiceSplitTests, InstantOperationEnablementUsesHostSingleObjectConstraint) {
    const ModelingInstantOperationCatalogEntry* connect =
            findModelingInstantOperationCatalogEntry(ModelingInstantOperationId::ConnectVertices);
    ASSERT_NE(connect, nullptr);

    ModelingSelectionSummary summary;
    summary.mode = ModelingSelectionMode::Vertex;
    summary.hasSelection = true;
    summary.activeKind = ModelingActiveSelectionKind::Vertex;
    summary.selectedVertexCount = 2;
    summary.selectedObjectCount = 2;

    EXPECT_FALSE(modelingInstantOperationEnabled(*connect, summary));

    summary.selectedObjectCount = 1;
    EXPECT_TRUE(modelingInstantOperationEnabled(*connect, summary));
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
