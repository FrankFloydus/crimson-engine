////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "assets/asset_registry.hpp"
#include "editor/interactions/tool_identity.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/metadata/host_contract.hpp"
#include "scene/scene_document.hpp"
#include "scene/document/project_session.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/viewport_tool_runtime.hpp"
#include "support/guardrail_file_scan.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

using test::guardrail::readSourceFile;
using test::guardrail::sourceRoot;
using test::guardrail::sourceSlice;

TEST(ViewportToolCatalogTests, CatalogContainsExpectedInteractiveToolContracts) {
    ASSERT_EQ(viewportToolCatalog().size(), viewportToolDescriptors().size());
    for (const ToolDescriptor& descriptor : viewportToolDescriptors()) {
        EXPECT_EQ(findViewportToolDescriptor(descriptor.id), &descriptor);
        EXPECT_EQ(findViewportToolDescriptor(descriptor.tool), &descriptor);
        EXPECT_EQ(findViewportToolDescriptor(descriptor.modelingTool), &descriptor);

        const ViewportTool* tool = findViewportTool(descriptor.tool);
        ASSERT_NE(tool, nullptr) << static_cast<int>(descriptor.tool);
        EXPECT_EQ(findViewportTool(descriptor.id), tool);
        EXPECT_EQ(tool->viewportTool(), descriptor.tool);
        EXPECT_EQ(tool->modelingTool(), descriptor.modelingTool);
        EXPECT_EQ(tool->id(), descriptor.id);
        EXPECT_EQ(tool->label(), descriptor.label);
        EXPECT_EQ(tool->kind(), descriptor.kind);
        EXPECT_EQ(tool->menuGroup(), descriptor.menuGroup);
        EXPECT_EQ(tool->ownsViewport(), descriptor.ownsViewport);
        EXPECT_EQ(tool->blocksViewportFocusShortcuts(), descriptor.blocksViewportFocusShortcuts);
        EXPECT_EQ(tool->supportsTransformGizmo(), descriptor.supportsTransformGizmo);
        EXPECT_EQ(viewportToolSupportsTransformGizmo(descriptor.tool), descriptor.supportsTransformGizmo);
        EXPECT_EQ(viewportToolInteractionTool(descriptor.tool),
                quader_editor_core::viewport_interaction_tool_from_id(descriptor.id));
    }
}

TEST(ViewportToolCatalogTests, ExposesToolMetadataAndConversions) {
    const Tool* box = findViewportTool("box");
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->viewportTool(), ViewportToolId::Box);
    EXPECT_EQ(box->modelingTool(), ModelingViewportTool::Box);
    EXPECT_EQ(box->kind(), ToolKind::Modal);
    EXPECT_EQ(box->label(), "Box");
    EXPECT_EQ(box->shortcut(), "B");
    EXPECT_TRUE(box->ownsViewport());
    EXPECT_FALSE(box->blocksViewportFocusShortcuts());

    EXPECT_EQ(viewportToolTransformTool(ViewportToolId::Move), ModelingTransformTool::Move);
    EXPECT_EQ(modelingViewportToolFor(ViewportToolId::Extend), ModelingViewportTool::Extend);
    EXPECT_EQ(modelingViewportToolFor(ViewportToolId::Extrude), ModelingViewportTool::Extrude);
    EXPECT_EQ(viewportToolTransformTool(ViewportToolId::Rotate), ModelingTransformTool::Rotate);
    EXPECT_EQ(viewportToolTransformTool(ViewportToolId::Scale), ModelingTransformTool::Scale);
    EXPECT_EQ(viewportToolInteractionTool(ViewportToolId::Move), quader_editor_core::ViewportInteractionTool::move);
    EXPECT_EQ(viewportToolInteractionTool(ViewportToolId::Rotate), quader_editor_core::ViewportInteractionTool::rotate);
    EXPECT_EQ(viewportToolInteractionTool(ViewportToolId::Scale), quader_editor_core::ViewportInteractionTool::scale);
    EXPECT_EQ(modelingViewportToolFor(ViewportToolId::Cut), ModelingViewportTool::Cut);
    EXPECT_EQ(viewportToolForModeling(ModelingViewportTool::Extend), ViewportToolId::Extend);
    EXPECT_EQ(viewportToolForModeling(ModelingViewportTool::Extrude), ViewportToolId::Extrude);
    EXPECT_EQ(viewportToolForModeling(ModelingViewportTool::Mirror), ViewportToolId::Mirror);
    EXPECT_TRUE(viewportToolBlocksFocusShortcuts(ViewportToolId::Cut));
    EXPECT_TRUE(viewportToolOwnsViewport(ViewportToolId::InsertEdgeLoop));
    EXPECT_TRUE(viewportToolSupportsTransformGizmo(ViewportToolId::Move));
    EXPECT_TRUE(viewportToolSupportsTransformGizmo(ViewportToolId::Rotate));
    EXPECT_TRUE(viewportToolSupportsTransformGizmo(ViewportToolId::Scale));
    EXPECT_FALSE(viewportToolSupportsTransformGizmo(ViewportToolId::Extend));
    EXPECT_FALSE(viewportToolSupportsTransformGizmo(ViewportToolId::Extrude));
    EXPECT_FALSE(viewportToolSupportsTransformGizmo(ViewportToolId::Mirror));
}

TEST(ViewportToolCatalogTests, HostToolMetadataDerivesFromViewportToolCatalog) {
    ASSERT_EQ(quader_host_contract::tool_catalog().size(), viewportToolDescriptors().size());
    for (const ToolDescriptor& tool : viewportToolDescriptors()) {
        const quader_host_contract::HostToolDescriptor* descriptor =
                quader_host_contract::find_tool_descriptor(tool.id);
        ASSERT_NE(descriptor, nullptr) << tool.id;
        EXPECT_EQ(descriptor->label, tool.label);
        EXPECT_EQ(descriptor->primary_shortcut, tool.shortcut);
        EXPECT_EQ(descriptor->transform_tool, tool.kind == ToolKind::Transform);
        EXPECT_EQ(descriptor->modal_tool, tool.kind == ToolKind::Modal);
    }

    for (const quader_host_contract::HostToolDescriptor& descriptor : quader_host_contract::tool_catalog()) {
        EXPECT_NE(findViewportToolDescriptor(descriptor.id), nullptr) << descriptor.id;
    }
}

TEST(ViewportToolCatalogTests, HostToolPlacementsAndGroupsResolveToViewportCatalog) {
    for (const quader_host_contract::HostToolGroupDescriptor& group : quader_host_contract::tool_group_catalog()) {
        for (std::string_view toolId : group.tool_ids) {
            EXPECT_NE(findViewportToolDescriptor(toolId), nullptr) << group.id << " references " << toolId;
        }
    }

    for (const quader_host_contract::HostUiPlacementDescriptor& placement : quader_host_contract::ui_placement_catalog()) {
        if (placement.target_kind != quader_host_contract::HostUiPlacementTargetKind::tool) {
            continue;
        }
        const ToolDescriptor* tool = findViewportToolDescriptor(placement.target_id);
        ASSERT_NE(tool, nullptr) << placement.id << " targets " << placement.target_id;
        EXPECT_NE(quader_host_contract::find_tool_descriptor(placement.target_id), nullptr) << placement.target_id;
        EXPECT_EQ(placement.label, tool->label) << placement.id;
    }
}

TEST(ViewportToolRuntimeTests, RegistryFindsConcreteToolObjects) {
    ViewportToolRegistry registry;

    ASSERT_EQ(registry.find(ViewportToolId::Knife), findViewportTool(ViewportToolId::Knife));
    ASSERT_EQ(registry.find("mirror"), findViewportTool(ViewportToolId::Mirror));
    EXPECT_TRUE(registry.available(ViewportToolId::Mirror));
}

TEST(ViewportToolRuntimeTests, RuntimeOwnsActiveToolState) {
    ViewportToolRuntime runtime;
    ViewportToolState state;
    ViewportInputResult result;

    ASSERT_NE(runtime.activeTool(), nullptr);
    EXPECT_EQ(runtime.activeTool()->viewportTool(), ViewportToolId::Select);

    ASSERT_TRUE(runtime.setActiveTool(state, ViewportToolId::Mirror, &result));
    ASSERT_NE(runtime.activeTool(), nullptr);
    EXPECT_TRUE(result.toolChanged);
    EXPECT_EQ(state.activeTool, ViewportToolId::Mirror);
    EXPECT_EQ(runtime.activeTool()->viewportTool(), ViewportToolId::Mirror);
    EXPECT_TRUE(runtime.activeToolOwnsViewport());
    EXPECT_TRUE(runtime.activeToolBlocksViewportFocusShortcuts());
}

TEST(ViewportToolRuntimeTests, RuntimeDispatchesShortcutStateChanges) {
    ViewportToolRuntime runtime;
    ViewportToolState state;
    ViewportCameraController camera;
    ViewportInputResult result;

    ViewportShortcut shortcut;
    shortcut.kind = ViewportShortcutKind::ActivateTool;
    shortcut.tool = ViewportToolId::Move;
    ASSERT_TRUE(runtime.handleShortcut(shortcut, state, camera, result));
    EXPECT_TRUE(result.toolChanged);
    EXPECT_EQ(state.activeTool, ViewportToolId::Move);
    EXPECT_EQ(runtime.activeTool()->viewportTool(), ViewportToolId::Move);

    result = {};
    shortcut = {};
    shortcut.kind = ViewportShortcutKind::FocusDocument;
    ASSERT_TRUE(runtime.handleShortcut(shortcut, state, camera, result));
    EXPECT_TRUE(result.focusDocumentRequested);

    result = {};
    ASSERT_TRUE(runtime.setActiveTool(state, ViewportToolId::Mirror));
    EXPECT_FALSE(runtime.handleShortcut(shortcut, state, camera, result));
    EXPECT_FALSE(result.focusDocumentRequested);
}

TEST(ViewportToolRuntimeTests, RuntimeRoutesToolEventsToActiveTool) {
    ViewportToolRuntime runtime;
    ViewportToolState state;
    ViewportCameraController camera;
    SceneDocument document;
    AssetRegistry assets;
    ModelingSessionService modeling(document, assets);

    ASSERT_TRUE(runtime.setActiveTool(state, ViewportToolId::Mirror));
    ViewportToolContext context{
            .state = state,
            .camera = camera,
            .modeling = modeling,
    };
    bool changedTool = false;
    context.setActiveTool = [&](ViewportToolId tool) {
        state.activeTool = tool;
        changedTool = true;
    };
    const ViewportToolResult result = runtime.handleActiveToolEvent(state, context, ViewportToolEvent{
            .kind = ViewportToolEventKind::Cancel,
            .tool = ViewportToolId::Mirror,
            .exitActiveModalTool = true,
    });

    EXPECT_TRUE(result.handled);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.syncActiveToolFromModeling);
    EXPECT_FALSE(changedTool);
    EXPECT_EQ(state.activeTool, ViewportToolId::Select);
    ASSERT_NE(runtime.activeTool(), nullptr);
    EXPECT_EQ(runtime.activeTool()->viewportTool(), ViewportToolId::Select);
}

TEST(ViewportToolRuntimeTests, BoxToolCommitUsesCommandOwnedTransaction) {
    ProjectSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::fromProjectSession(session));
    ViewportToolState state;
    ViewportCameraController camera;
    ViewportModalToolState modalState;

    const QuaderApiResult<QuaderAssetRef> material = editor.assets().addMaterial(QuaderMaterialArgs{
            .name = "Viewport Box Material",
            .baseColor = {0.2f, 0.4f, 0.8f},
    });
    ASSERT_TRUE(material.ok) << material.message;

    const ViewportTool* boxTool = findViewportTool(ViewportToolId::Box);
    ASSERT_NE(boxTool, nullptr);
    ASSERT_TRUE(session.modeling().activateBoxTool().ok);

    const ModelingBoxToolEvent start{
            .screenPosition = {400.0f, 300.0f},
            .ray = {
                    .origin = {4.0f, 4.0f, 4.0f},
                    .direction = {0.0f, -1.0f, 0.0f},
            },
            .gridSize = 1.0,
    };
    const ModelingBoxToolEvent footprintEnd{
            .screenPosition = {400.0f, 300.0f},
            .ray = {
                    .origin = {6.0f, 4.0f, 7.0f},
                    .direction = {0.0f, -1.0f, 0.0f},
            },
            .gridSize = 1.0,
    };
    ModelingBoxToolEvent currentEvent = start;
    EntityId committedEntity = InvalidEntityId;
    int commitCount = 0;

    ViewportBoxToolContext boxContext{
            .defaultMaterial = material.value.id,
    };
    boxContext.makeEvent = [&]() {
        return currentEvent;
    };
    boxContext.commitPreview = [&](std::string name, AssetId defaultMaterial) {
        ++commitCount;
        const QuaderApiResult<QuaderModelingObjectRef> result =
                editor.modeling().commitBoxPreview(std::move(name), defaultMaterial);
        ModelingCreateBoxResult committed{
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
        if (result.ok && result.changed) {
            committedEntity = result.value.entity;
            committed.object = {
                    .modelingObjectId = result.value.modelingObjectId,
                    .entity = result.value.entity,
                    .mesh = result.value.mesh,
                    .name = "Viewport Box",
            };
        }
        return committed;
    };
    boxContext.nextMeshName = [] {
        return std::string{"Viewport Box"};
    };

    ViewportToolContext context{
            .state = state,
            .camera = camera,
            .modeling = session.modeling(),
            .document = &session.scene(),
            .modalState = &modalState,
            .box = &boxContext,
    };
    context.setAppSelectionMode = [&](ModelingSelectionMode mode) {
        (void)editor.selection().setMode(toApiSelectionMode(mode));
    };

    auto dispatchFrame = [&](ViewportPointerToolEvent pointer) {
        return boxTool->handleEvent(context, ViewportToolEvent{
                .kind = ViewportToolEventKind::Frame,
                .tool = ViewportToolId::Box,
                .pointer = pointer,
        });
    };

    ViewportToolResult result = dispatchFrame(ViewportPointerToolEvent{
            .hovered = true,
            .leftClicked = true,
    });
    ASSERT_TRUE(result.handled);
    ASSERT_EQ(modalState.boxPreview.dragState, ModelingBoxDragState::Footprint);

    currentEvent = footprintEnd;
    result = dispatchFrame(ViewportPointerToolEvent{
            .hovered = true,
    });
    ASSERT_TRUE(result.handled);
    ASSERT_TRUE(modalState.boxPreview.valid);
    ASSERT_EQ(modalState.boxPreview.dragState, ModelingBoxDragState::Footprint);
    EXPECT_EQ(commitCount, 0);

    currentEvent = footprintEnd;
    result = dispatchFrame(ViewportPointerToolEvent{
            .hovered = true,
            .leftReleased = true,
    });
    ASSERT_TRUE(result.handled);
    ASSERT_TRUE(result.activeToolChanged);
    EXPECT_EQ(modalState.boxPreview.dragState, ModelingBoxDragState::Idle);
    EXPECT_EQ(commitCount, 1);
    ASSERT_NE(committedEntity, InvalidEntityId);
    ASSERT_TRUE(session.scene().exists(committedEntity));
    ASSERT_NE(session.scene().material(committedEntity), nullptr);
    EXPECT_EQ(session.scene().material(committedEntity)->material, material.value.id);
    EXPECT_EQ(session.scene().primarySelection(), committedEntity);

    ASSERT_TRUE(editor.history().canUndo());
    const QuaderApiVoidResult undo = editor.history().undo();
    ASSERT_TRUE(undo.ok) << undo.message;
    ASSERT_TRUE(undo.changed);
    EXPECT_FALSE(session.scene().exists(committedEntity));
    EXPECT_EQ(session.scene().primarySelection(), InvalidEntityId);
    ASSERT_TRUE(editor.history().canRedo());

    const QuaderApiVoidResult redo = editor.history().redo();
    ASSERT_TRUE(redo.ok) << redo.message;
    ASSERT_TRUE(redo.changed);
    ASSERT_TRUE(session.scene().exists(committedEntity));
    ASSERT_NE(session.scene().material(committedEntity), nullptr);
    EXPECT_EQ(session.scene().material(committedEntity)->material, material.value.id);
    EXPECT_EQ(session.scene().primarySelection(), committedEntity);
}

TEST(ViewportToolGuardrailTests, RemovedLegacyViewportToolBase) {
    const std::filesystem::path root = sourceRoot();
    const std::string toolHeader = readSourceFile(root / "src" / "editor" / "tools" / "viewport_tool.hpp");
    const std::string catalogHeader = readSourceFile(root / "src" / "editor" / "tools" / "viewport_tool_catalog.hpp");
    EXPECT_EQ(toolHeader.find("ViewportToolBase"), std::string::npos);
    EXPECT_EQ(catalogHeader.find("ViewportToolBase"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, BoxToolUsesFocusedContextInsteadOfBroadToolBag) {
    const std::filesystem::path root = sourceRoot();
    const std::string toolHeader = readSourceFile(root / "src" / "editor" / "tools" / "viewport_tool.hpp");
    const std::string broadContext =
            sourceSlice(toolHeader, "struct ViewportToolContext {", "};");
    const std::string boxToolSource =
            readSourceFile(root / "src" / "editor" / "tools" / "base" / "two_stage_box_tool.cpp");

    EXPECT_NE(toolHeader.find("struct ViewportBoxToolContext"), std::string::npos);
    EXPECT_NE(broadContext.find("ViewportBoxToolContext* box"), std::string::npos);
    EXPECT_EQ(broadContext.find("makeBoxEvent"), std::string::npos);
    EXPECT_EQ(broadContext.find("commitBoxPreview"), std::string::npos);
    EXPECT_EQ(broadContext.find("nextMeshName"), std::string::npos);
    EXPECT_EQ(broadContext.find("defaultMaterial"), std::string::npos);

    EXPECT_NE(boxToolSource.find("ViewportBoxToolContext& boxContext = *context.box"), std::string::npos);
    EXPECT_EQ(boxToolSource.find("context.makeBoxEvent"), std::string::npos);
    EXPECT_EQ(boxToolSource.find("context.commitBoxPreview"), std::string::npos);
    EXPECT_EQ(boxToolSource.find("context.nextMeshName"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, PointToolsUseFocusedContextInsteadOfBroadToolBag) {
    const std::filesystem::path root = sourceRoot();
    const std::string toolHeader = readSourceFile(root / "src" / "editor" / "tools" / "viewport_tool.hpp");
    const std::string broadContext =
            sourceSlice(toolHeader, "struct ViewportToolContext {", "};");
    const std::string dragToolSource =
            readSourceFile(root / "src" / "editor" / "tools" / "base" / "drag_commit_tool.cpp");
    const std::string pointSequenceToolSource =
            readSourceFile(root / "src" / "editor" / "tools" / "base" / "point_sequence_tool.cpp");
    const std::string drawnPlaneToolSource =
            readSourceFile(root / "src" / "editor" / "tools" / "base" / "drawn_plane_tool.cpp");

    EXPECT_NE(toolHeader.find("struct ViewportPointToolContext"), std::string::npos);
    EXPECT_NE(broadContext.find("ViewportPointToolContext* point"), std::string::npos);
    EXPECT_EQ(broadContext.find("makeToolEventAt"), std::string::npos);
    EXPECT_EQ(broadContext.find("hitDrawnPlaneCapturedPoint"), std::string::npos);
    EXPECT_EQ(broadContext.find("hitDrawnPlaneLine"), std::string::npos);

    EXPECT_NE(dragToolSource.find("ViewportPointToolContext& pointContext = *context.point"), std::string::npos);
    EXPECT_NE(pointSequenceToolSource.find("ViewportPointToolContext* pointContext = context.point"), std::string::npos);
    EXPECT_NE(drawnPlaneToolSource.find("ViewportPointToolContext& pointContext = *context.point"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, DrawnPlaneToolUsesSharedCutMirrorLifecycle) {
    const std::filesystem::path root = sourceRoot();
    const std::string drawnPlaneToolSource =
            readSourceFile(root / "src" / "editor" / "tools" / "base" / "drawn_plane_tool.cpp");

    EXPECT_NE(drawnPlaneToolSource.find("enum class DrawnPlaneToolKind"), std::string::npos);
    EXPECT_NE(drawnPlaneToolSource.find("handleDrawnPlaneLifecycle("), std::string::npos);
    EXPECT_NE(drawnPlaneToolSource.find("beginDrawnPlanePointDrag("), std::string::npos);
    EXPECT_NE(drawnPlaneToolSource.find("finalizeDrawnPlaneTool("), std::string::npos);
    EXPECT_EQ(drawnPlaneToolSource.find("if (modelingTool() == ModelingViewportTool::Cut) {"), std::string::npos);
    EXPECT_EQ(drawnPlaneToolSource.find("if (modelingTool() == ModelingViewportTool::Mirror) {"), std::string::npos);
    EXPECT_EQ(drawnPlaneToolSource.find("context.modalState->cutDragActive = command.ok"), std::string::npos);
    EXPECT_EQ(drawnPlaneToolSource.find("context.modalState->mirrorDragActive = command.ok"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, ViewportPanelProvidesPointToolContextToModalTools) {
    const std::filesystem::path root = sourceRoot();
    const std::string viewportPanel = readSourceFile(root / "src" / "ui" / "viewport_panel.cpp");
    const std::string transformGizmo =
            readSourceFile(root / "src" / "editor" / "viewport" / "transform_gizmo_interaction.cpp");

    EXPECT_NE(viewportPanel.find("ViewportPointToolContext pointToolContext"), std::string::npos);
    EXPECT_NE(viewportPanel.find("pointToolContext.makeEventAt"), std::string::npos);
    EXPECT_NE(viewportPanel.find("pointToolContext.hitDrawnPlaneCapturedPoint"), std::string::npos);
    EXPECT_NE(viewportPanel.find("pointToolContext.hitDrawnPlaneLine"), std::string::npos);
    EXPECT_NE(viewportPanel.find(".point = &pointToolContext"), std::string::npos);
    EXPECT_NE(viewportPanel.find("makeModelingToolEventAt"), std::string::npos);
    EXPECT_NE(viewportPanel.find("snapEnabled = viewportGridSnapActive(settings, input.modifiers.primary)"), std::string::npos);
    EXPECT_NE(viewportPanel.find("vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius)"), std::string::npos);
    EXPECT_NE(viewportPanel.find("constrainAxis = input.modifiers.shift"), std::string::npos);
    EXPECT_NE(transformGizmo.find("!frame.settings.boolValue(ViewportBoolSettingId::ShowGizmos)"), std::string::npos);
    EXPECT_NE(transformGizmo.find("const bool snapActive = viewportGridSnapActive(frame.settings, frame.input.modifiers.primary)"), std::string::npos);
    EXPECT_NE(transformGizmo.find(".vertexSnapRadius = snapActive ? viewportVertexSnapRadius(frame.settings) : 0.0"), std::string::npos);
    EXPECT_NE(transformGizmo.find(".gridSnapEnabled = snapActive"), std::string::npos);
    EXPECT_NE(transformGizmo.find(".precision = frame.input.modifiers.primary"), std::string::npos);
    const std::size_t navigationSkip = transformGizmo.find("cameraNavigationFrame && !dragging_ && !pendingDrag_.has_value()");
    const std::size_t selectionCenter = transformGizmo.find("editor.query().selectionCenter()");
    ASSERT_NE(navigationSkip, std::string::npos);
    ASSERT_NE(selectionCenter, std::string::npos);
    EXPECT_LT(navigationSkip, selectionCenter);
    const std::size_t activeDragPivot = transformGizmo.find("pivot = activeDragPivot_");
    ASSERT_NE(activeDragPivot, std::string::npos);
    EXPECT_LT(activeDragPivot, selectionCenter);
    EXPECT_NE(transformGizmo.find("activeDragPivot_ = preview.value().pivot"), std::string::npos);

    const std::string selectionInteraction =
            readSourceFile(root / "src" / "editor" / "viewport" / "viewport_selection_interaction.cpp");
    EXPECT_NE(selectionInteraction.find("frame.inputRouter.state().selectionMode == ViewportSelectionMode::Group"), std::string::npos);
    EXPECT_EQ(selectionInteraction.find("editor.selection().summary().mode == QuaderSelectionMode::Group"), std::string::npos);
    EXPECT_EQ(viewportPanel.find("selectionOverlaySuppressedForInteraction()"), std::string::npos);
    EXPECT_NE(viewportPanel.find("overlaysVisible && selectionOverlayCache_.valid()"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, EditorOwnsViewportToolRuntimeSources) {
    const std::filesystem::path root = sourceRoot();
    EXPECT_TRUE(std::filesystem::exists(root / "src" / "editor" / "tools" / "viewport_tool_runtime.hpp"));
    EXPECT_TRUE(std::filesystem::exists(root / "src" / "editor" / "tools" / "viewport_tool_runtime.cpp"));
    EXPECT_TRUE(std::filesystem::exists(root / "src" / "editor" / "tools" / "base" / "viewport_tool_catalog.cpp"));
    EXPECT_FALSE(std::filesystem::exists(root / "src" / "viewport" / "modes"));
}

TEST(ViewportToolGuardrailTests, UiShellHasNoDirectModalToolBranches) {
    const std::filesystem::path root = sourceRoot();
    const std::string uiShell = readSourceFile(root / "src" / "ui" / "ui_system.cpp");
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::Box"), std::string::npos);
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::InsertEdgeLoop"), std::string::npos);
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::Knife"), std::string::npos);
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::Cut"), std::string::npos);
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::Mirror"), std::string::npos);
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::Poly"), std::string::npos);
    EXPECT_EQ(uiShell.find("boxPreviewController_"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, TransformGizmoEligibilityLivesInViewportToolMetadata) {
    const std::filesystem::path root = sourceRoot();
    const std::string interaction =
            readSourceFile(root / "src" / "editor" / "viewport" / "transform_gizmo_interaction.cpp");
    const std::string descriptor =
            readSourceFile(root / "src" / "editor" / "tools" / "viewport_tool_descriptor_catalog.hpp");
    const std::string catalog =
            readSourceFile(root / "src" / "editor" / "tools" / "viewport_tool_catalog.hpp");

    EXPECT_NE(descriptor.find("supportsTransformGizmo"), std::string::npos);
    EXPECT_NE(catalog.find("viewportToolSupportsTransformGizmo"), std::string::npos);
    EXPECT_NE(catalog.find("viewportToolInteractionTool"), std::string::npos);
    EXPECT_NE(interaction.find("viewportToolSupportsTransformGizmo(tool)"), std::string::npos);
    EXPECT_NE(interaction.find("viewportToolInteractionTool(tool)"), std::string::npos);
    EXPECT_EQ(interaction.find("bool supportsTransformGizmo("), std::string::npos);
    EXPECT_EQ(interaction.find("return tool == ViewportToolId::Move"), std::string::npos);
    EXPECT_EQ(interaction.find("ViewportInteractionTool::move"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, HostViewportAdapterUsesViewportToolDescriptors) {
    ViewportToolId tool = ViewportToolId::Select;
    EXPECT_TRUE(viewportToolFromContractId("insert_edge_loop_tool", tool));
    EXPECT_EQ(tool, ViewportToolId::InsertEdgeLoop);
    EXPECT_TRUE(viewportToolFromContractId("mirror_tool", tool));
    EXPECT_EQ(tool, ViewportToolId::Mirror);
    EXPECT_EQ(viewportToolContractId(ViewportToolId::Knife), std::string_view("knife"));
    EXPECT_TRUE(viewportToolAllowsSelectionFallback(ViewportToolId::Move));
    EXPECT_FALSE(viewportToolAllowsSelectionFallback(ViewportToolId::Knife));

    const std::filesystem::path root = sourceRoot();
    const std::string adapter = readSourceFile(root / "src" / "viewport" / "host_viewport_contract_adapter.cpp");
    EXPECT_NE(adapter.find("findViewportToolDescriptor(tool)"), std::string::npos);
    EXPECT_NE(adapter.find("findViewportToolDescriptor(id)"), std::string::npos);
    EXPECT_NE(adapter.find("quader_host_contract::find_action_descriptor(id)"), std::string::npos);
    EXPECT_EQ(adapter.find("case ViewportToolId::"), std::string::npos);
    EXPECT_EQ(adapter.find("tool == ViewportToolId::"), std::string::npos);
}

} // namespace
} // namespace quader
