#include "assets/AssetRegistry.h"
#include "modeling/ModelingSessionService.h"
#include "modeling/ModelingSessionServices.h"
#include "scene/SceneDocument.h"

#include <gtest/gtest.h>

#include <optional>

namespace quader {
namespace {

TEST(ModelingSessionServiceSplitTests, ObjectRegistryOwnsSdkSceneMappingsAndBuildsRecords) {
    SceneDocument scene;
    const EntityId second = scene.createEntity("Second", EntityKind::Mesh);
    const EntityId first = scene.createEntity("First", EntityKind::Mesh);
    scene.setMesh(second, MeshComponent{.mesh = 42});
    scene.setMesh(first, MeshComponent{.mesh = 7});

    ModelingObjectRegistry registry;
    registry.bind(200, second);
    registry.bind(100, first);

    EXPECT_EQ(registry.size(), 2U);
    EXPECT_EQ(registry.entityForSdkObject(100), first);
    EXPECT_EQ(registry.sdkObjectForEntity(second), 200);

    const std::vector<ModelingObjectRecord> records = registry.records(scene);
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].entity, second);
    EXPECT_EQ(records[0].sdkObjectId, 200);
    EXPECT_EQ(records[0].mesh, 42);
    EXPECT_EQ(records[0].name, "Second");
    EXPECT_EQ(records[1].entity, first);
    EXPECT_EQ(records[1].sdkObjectId, 100);
    EXPECT_EQ(records[1].mesh, 7);
    EXPECT_EQ(records[1].name, "First");

    registry.unbindObject(100);
    EXPECT_EQ(registry.entityForSdkObject(100), std::nullopt);
    EXPECT_EQ(registry.sdkObjectForEntity(first), std::nullopt);
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

    ModelingInteractionSettingsService interactionSettings;
    EXPECT_EQ(interactionSettings.settings().cutMode, ModelingCutMode::KeepBoth);
    interactionSettings.setCutMode(ModelingCutMode::DiscardLeft);
    EXPECT_EQ(interactionSettings.settings().cutMode, ModelingCutMode::DiscardLeft);
    interactionSettings.cycleCutModeWithoutSdk();
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
    EXPECT_TRUE(TransformPreviewService::isTransform(ModelingTransformOperation::Transform));
    EXPECT_FALSE(TransformPreviewService::isTransform(ModelingTransformOperation::Extrude));
    EXPECT_FALSE(ModelingCommandExecutor::sdkUnavailable().ok);
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
