////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderEditorApi.h"
#include "assets/AssetRegistry.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "scene/SceneDocument.h"
#include "scene/document/ProjectSession.h"
#include "editor/tools/ViewportToolCatalog.h"
#include "editor/tools/ViewportToolRuntime.h"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

std::string readSourceFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string sourceSlice(
        const std::string& source,
        std::string_view beginMarker,
        std::string_view endMarker) {
    const std::size_t begin = source.find(beginMarker);
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = source.find(endMarker, begin + beginMarker.size());
    if (end == std::string::npos) {
        return source.substr(begin);
    }
    return source.substr(begin, end - begin + endMarker.size());
}

TEST(ViewportToolCatalogTests, CatalogContainsExpectedInteractiveToolContracts) {
    /**
     * Stores expected viewport tool descriptor values for catalog contract checks.
     */
    struct ExpectedTool {
        ViewportToolId tool;
        ModelingViewportTool modelingTool;
        std::string_view id;
        std::string_view label;
        ToolKind kind;
        ToolMenuGroup menuGroup;
        bool ownsViewport;
        bool blocksFocusShortcuts;
    };
    constexpr std::array<ExpectedTool, 12> expected{{
            {ViewportToolId::Select, ModelingViewportTool::Select, "select", "Select", ToolKind::Selection, ToolMenuGroup::Navigation, false, false},
            {ViewportToolId::Move, ModelingViewportTool::Move, "move", "Move", ToolKind::Transform, ToolMenuGroup::Navigation, false, false},
            {ViewportToolId::Extend, ModelingViewportTool::Extend, "extend", "Extend", ToolKind::Transform, ToolMenuGroup::Modeling, false, false},
            {ViewportToolId::Extrude, ModelingViewportTool::Extrude, "extrude", "Extrude", ToolKind::Transform, ToolMenuGroup::Modeling, false, false},
            {ViewportToolId::Rotate, ModelingViewportTool::Rotate, "rotate", "Rotate", ToolKind::Transform, ToolMenuGroup::Navigation, false, false},
            {ViewportToolId::Scale, ModelingViewportTool::Scale, "scale", "Scale", ToolKind::Transform, ToolMenuGroup::Navigation, false, false},
            {ViewportToolId::Box, ModelingViewportTool::Box, "box", "Box", ToolKind::Modal, ToolMenuGroup::Modeling, true, false},
            {ViewportToolId::InsertEdgeLoop, ModelingViewportTool::InsertEdgeLoop, "insert_edge_loop", "Insert Edge Loop", ToolKind::Modal, ToolMenuGroup::Modeling, true, false},
            {ViewportToolId::Knife, ModelingViewportTool::Knife, "knife", "Knife", ToolKind::Modal, ToolMenuGroup::Modeling, true, true},
            {ViewportToolId::Cut, ModelingViewportTool::Cut, "cut", "Cut", ToolKind::Modal, ToolMenuGroup::Modeling, true, true},
            {ViewportToolId::Poly, ModelingViewportTool::Poly, "poly", "Poly", ToolKind::Modal, ToolMenuGroup::Modeling, true, true},
            {ViewportToolId::Mirror, ModelingViewportTool::Mirror, "mirror", "Mirror", ToolKind::Modal, ToolMenuGroup::Modeling, true, true},
    }};

    ASSERT_EQ(viewportToolCatalog().size(), expected.size());
    for (const ExpectedTool& item : expected) {
        const ViewportTool* tool = findViewportTool(item.tool);
        ASSERT_NE(tool, nullptr) << static_cast<int>(item.tool);
        EXPECT_EQ(findViewportTool(item.id), tool);
        EXPECT_EQ(tool->viewportTool(), item.tool);
        EXPECT_EQ(tool->modelingTool(), item.modelingTool);
        EXPECT_EQ(tool->id(), item.id);
        EXPECT_EQ(tool->label(), item.label);
        EXPECT_EQ(tool->kind(), item.kind);
        EXPECT_EQ(tool->menuGroup(), item.menuGroup);
        EXPECT_EQ(tool->ownsViewport(), item.ownsViewport);
        EXPECT_EQ(tool->blocksViewportFocusShortcuts(), item.blocksFocusShortcuts);
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
    EXPECT_EQ(modelingViewportToolFor(ViewportToolId::Cut), ModelingViewportTool::Cut);
    EXPECT_EQ(viewportToolForModeling(ModelingViewportTool::Extend), ViewportToolId::Extend);
    EXPECT_EQ(viewportToolForModeling(ModelingViewportTool::Extrude), ViewportToolId::Extrude);
    EXPECT_EQ(viewportToolForModeling(ModelingViewportTool::Mirror), ViewportToolId::Mirror);
    EXPECT_TRUE(viewportToolBlocksFocusShortcuts(ViewportToolId::Cut));
    EXPECT_TRUE(viewportToolOwnsViewport(ViewportToolId::InsertEdgeLoop));
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
    QuaderEditorApi editor(session);
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
    const ModelingBoxToolEvent heightEnd{
            .screenPosition = {400.0f, 360.0f},
            .ray = start.ray,
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
        (void)editor.selection().setMode(mode);
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
            .leftDown = true,
    });
    ASSERT_TRUE(result.handled);
    ASSERT_TRUE(modalState.boxPreview.valid);

    currentEvent = start;
    result = dispatchFrame(ViewportPointerToolEvent{
            .hovered = true,
            .leftReleased = true,
    });
    ASSERT_TRUE(result.handled);
    ASSERT_EQ(modalState.boxPreview.dragState, ModelingBoxDragState::Height);

    currentEvent = heightEnd;
    result = dispatchFrame(ViewportPointerToolEvent{
            .hovered = true,
    });
    ASSERT_TRUE(result.handled);
    ASSERT_TRUE(modalState.boxPreview.valid);

    result = dispatchFrame(ViewportPointerToolEvent{
            .hovered = true,
            .leftClicked = true,
    });
    ASSERT_TRUE(result.handled);
    ASSERT_TRUE(result.activeToolChanged);
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
    const std::filesystem::path root = std::filesystem::path(QUADER_SOURCE_DIR);
    const std::string toolHeader = readSourceFile(root / "src" / "editor" / "tools" / "ViewportTool.h");
    const std::string catalogHeader = readSourceFile(root / "src" / "editor" / "tools" / "ViewportToolCatalog.h");
    EXPECT_EQ(toolHeader.find("ViewportToolBase"), std::string::npos);
    EXPECT_EQ(catalogHeader.find("ViewportToolBase"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, BoxToolUsesFocusedContextInsteadOfBroadToolBag) {
    const std::filesystem::path root = std::filesystem::path(QUADER_SOURCE_DIR);
    const std::string toolHeader = readSourceFile(root / "src" / "editor" / "tools" / "ViewportTool.h");
    const std::string broadContext =
            sourceSlice(toolHeader, "struct ViewportToolContext {", "};");
    const std::string boxToolSource =
            readSourceFile(root / "src" / "editor" / "tools" / "base" / "TwoStageBoxTool.cpp");

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
    const std::filesystem::path root = std::filesystem::path(QUADER_SOURCE_DIR);
    const std::string toolHeader = readSourceFile(root / "src" / "editor" / "tools" / "ViewportTool.h");
    const std::string broadContext =
            sourceSlice(toolHeader, "struct ViewportToolContext {", "};");
    const std::string dragToolSource =
            readSourceFile(root / "src" / "editor" / "tools" / "base" / "DragCommitTool.cpp");
    const std::string pointSequenceToolSource =
            readSourceFile(root / "src" / "editor" / "tools" / "base" / "PointSequenceTool.cpp");
    const std::string drawnPlaneToolSource =
            readSourceFile(root / "src" / "editor" / "tools" / "base" / "DrawnPlaneTool.cpp");

    EXPECT_NE(toolHeader.find("struct ViewportPointToolContext"), std::string::npos);
    EXPECT_NE(broadContext.find("ViewportPointToolContext* point"), std::string::npos);
    EXPECT_EQ(broadContext.find("makeToolEventAt"), std::string::npos);
    EXPECT_EQ(broadContext.find("hitDrawnPlaneCapturedPoint"), std::string::npos);
    EXPECT_EQ(broadContext.find("hitDrawnPlaneLine"), std::string::npos);

    EXPECT_NE(dragToolSource.find("ViewportPointToolContext& pointContext = *context.point"), std::string::npos);
    EXPECT_NE(pointSequenceToolSource.find("ViewportPointToolContext* pointContext = context.point"), std::string::npos);
    EXPECT_NE(drawnPlaneToolSource.find("ViewportPointToolContext& pointContext = *context.point"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, EditorOwnsViewportToolRuntimeSources) {
    const std::filesystem::path root = std::filesystem::path(QUADER_SOURCE_DIR);
    EXPECT_TRUE(std::filesystem::exists(root / "src" / "editor" / "tools" / "ViewportToolRuntime.h"));
    EXPECT_TRUE(std::filesystem::exists(root / "src" / "editor" / "tools" / "ViewportToolRuntime.cpp"));
    EXPECT_TRUE(std::filesystem::exists(root / "src" / "editor" / "tools" / "base" / "ViewportToolCatalog.cpp"));
    EXPECT_FALSE(std::filesystem::exists(root / "src" / "viewport" / "modes"));
}

TEST(ViewportToolGuardrailTests, UiShellHasNoDirectModalToolBranches) {
    const std::filesystem::path root = std::filesystem::path(QUADER_SOURCE_DIR);
    const std::string uiShell = readSourceFile(root / "src" / "ui" / "UISystem.cpp");
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::Box"), std::string::npos);
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::InsertEdgeLoop"), std::string::npos);
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::Knife"), std::string::npos);
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::Cut"), std::string::npos);
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::Mirror"), std::string::npos);
    EXPECT_EQ(uiShell.find("activeTool == ViewportToolId::Poly"), std::string::npos);
    EXPECT_EQ(uiShell.find("boxPreviewController_"), std::string::npos);
}

} // namespace
} // namespace quader
