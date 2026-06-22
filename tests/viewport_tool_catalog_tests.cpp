////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/AssetRegistry.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "scene/SceneDocument.h"
#include "viewport/modes/ViewportToolCatalog.h"
#include "viewport/modes/ViewportToolRuntime.h"
#include "viewport/modes/base/DrawnPlaneTool.h"
#include "viewport/modes/base/DragCommitTool.h"
#include "viewport/modes/base/PointSequenceTool.h"
#include "viewport/modes/base/TwoStageBoxTool.h"
#include "viewport/modes/interactive/BoxTool.h"
#include "viewport/modes/interactive/CutTool.h"
#include "viewport/modes/interactive/ExtendTool.h"
#include "viewport/modes/interactive/ExtrudeTool.h"
#include "viewport/modes/interactive/InsertEdgeLoopTool.h"
#include "viewport/modes/interactive/KnifeTool.h"
#include "viewport/modes/interactive/MirrorTool.h"
#include "viewport/modes/interactive/MoveTool.h"
#include "viewport/modes/interactive/PolyTool.h"
#include "viewport/modes/interactive/RotateTool.h"
#include "viewport/modes/interactive/ScaleTool.h"
#include "viewport/modes/interactive/SelectTool.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>

namespace quader {
namespace {

std::string readSourceFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

TEST(ViewportToolCatalogTests, ViewportToolIsConcreteToolBase) {
    static_assert(std::is_base_of_v<Tool, ViewportTool>);
    static_assert(std::is_base_of_v<ViewportTool, InteractiveViewportTool>);
    static_assert(std::is_base_of_v<InteractiveViewportTool, TransformViewportTool>);
    static_assert(std::is_base_of_v<InteractiveViewportTool, ModalViewportTool>);
    static_assert(std::is_base_of_v<ModalViewportTool, TwoStageBoxTool>);
    static_assert(std::is_base_of_v<ModalViewportTool, DrawnPlaneTool>);
    static_assert(std::is_base_of_v<ModalViewportTool, PointSequenceTool>);
    static_assert(std::is_base_of_v<ModalViewportTool, DragCommitTool>);
}

TEST(ViewportToolCatalogTests, ContainsConcreteInteractiveTools) {
    ASSERT_NE(dynamic_cast<const SelectTool*>(findViewportTool(ViewportToolId::Select)), nullptr);
    ASSERT_NE(dynamic_cast<const MoveTool*>(findViewportTool(ViewportToolId::Move)), nullptr);
    ASSERT_NE(dynamic_cast<const ExtendTool*>(findViewportTool(ViewportToolId::Extend)), nullptr);
    ASSERT_NE(dynamic_cast<const ExtrudeTool*>(findViewportTool(ViewportToolId::Extrude)), nullptr);
    ASSERT_NE(dynamic_cast<const RotateTool*>(findViewportTool(ViewportToolId::Rotate)), nullptr);
    ASSERT_NE(dynamic_cast<const ScaleTool*>(findViewportTool(ViewportToolId::Scale)), nullptr);
    ASSERT_NE(dynamic_cast<const BoxTool*>(findViewportTool(ViewportToolId::Box)), nullptr);
    ASSERT_NE(dynamic_cast<const InsertEdgeLoopTool*>(findViewportTool(ViewportToolId::InsertEdgeLoop)), nullptr);
    ASSERT_NE(dynamic_cast<const KnifeTool*>(findViewportTool(ViewportToolId::Knife)), nullptr);
    ASSERT_NE(dynamic_cast<const CutTool*>(findViewportTool(ViewportToolId::Cut)), nullptr);
    ASSERT_NE(dynamic_cast<const PolyTool*>(findViewportTool(ViewportToolId::Poly)), nullptr);
    ASSERT_NE(dynamic_cast<const MirrorTool*>(findViewportTool(ViewportToolId::Mirror)), nullptr);
}

TEST(ViewportToolCatalogTests, ConcreteToolsUseExpectedInteractiveInheritance) {
    EXPECT_NE(dynamic_cast<const InteractiveViewportTool*>(findViewportTool(ViewportToolId::Select)), nullptr);
    EXPECT_NE(dynamic_cast<const TransformViewportTool*>(findViewportTool(ViewportToolId::Move)), nullptr);
    EXPECT_NE(dynamic_cast<const TransformViewportTool*>(findViewportTool(ViewportToolId::Extend)), nullptr);
    EXPECT_NE(dynamic_cast<const TransformViewportTool*>(findViewportTool(ViewportToolId::Extrude)), nullptr);
    EXPECT_NE(dynamic_cast<const TransformViewportTool*>(findViewportTool(ViewportToolId::Rotate)), nullptr);
    EXPECT_NE(dynamic_cast<const TransformViewportTool*>(findViewportTool(ViewportToolId::Scale)), nullptr);
    EXPECT_NE(dynamic_cast<const ModalViewportTool*>(findViewportTool(ViewportToolId::Box)), nullptr);
    EXPECT_NE(dynamic_cast<const TwoStageBoxTool*>(findViewportTool(ViewportToolId::Box)), nullptr);
    EXPECT_NE(dynamic_cast<const DragCommitTool*>(findViewportTool(ViewportToolId::InsertEdgeLoop)), nullptr);
    EXPECT_NE(dynamic_cast<const PointSequenceTool*>(findViewportTool(ViewportToolId::Knife)), nullptr);
    EXPECT_NE(dynamic_cast<const DrawnPlaneTool*>(findViewportTool(ViewportToolId::Cut)), nullptr);
    EXPECT_NE(dynamic_cast<const PointSequenceTool*>(findViewportTool(ViewportToolId::Poly)), nullptr);
    EXPECT_NE(dynamic_cast<const DrawnPlaneTool*>(findViewportTool(ViewportToolId::Mirror)), nullptr);
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

TEST(ViewportToolGuardrailTests, RemovedLegacyViewportToolBase) {
    const std::filesystem::path root = std::filesystem::path(QUADER_SOURCE_DIR);
    const std::string toolHeader = readSourceFile(root / "src" / "viewport" / "modes" / "Tool.h");
    const std::string catalogHeader = readSourceFile(root / "src" / "viewport" / "modes" / "ViewportToolCatalog.h");
    EXPECT_EQ(toolHeader.find("ViewportToolBase"), std::string::npos);
    EXPECT_EQ(catalogHeader.find("ViewportToolBase"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, ViewportPanelHasNoDirectModalToolBranches) {
    const std::filesystem::path root = std::filesystem::path(QUADER_SOURCE_DIR);
    const std::string panel = readSourceFile(root / "src" / "ui" / "ViewportPanel.cpp");
    EXPECT_EQ(panel.find("activeTool == ViewportToolId::Box"), std::string::npos);
    EXPECT_EQ(panel.find("activeTool == ViewportToolId::InsertEdgeLoop"), std::string::npos);
    EXPECT_EQ(panel.find("activeTool == ViewportToolId::Knife"), std::string::npos);
    EXPECT_EQ(panel.find("activeTool == ViewportToolId::Cut"), std::string::npos);
    EXPECT_EQ(panel.find("activeTool == ViewportToolId::Mirror"), std::string::npos);
    EXPECT_EQ(panel.find("activeTool == ViewportToolId::Poly"), std::string::npos);
    EXPECT_EQ(panel.find("boxPreviewController_"), std::string::npos);
}

} // namespace
} // namespace quader
