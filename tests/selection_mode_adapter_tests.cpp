////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/selection_mode_adapter.hpp"

#include "editor/modeling/services/modeling_action_catalog.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

namespace quader {
namespace {

using test::guardrail::readSourceFile;

TEST(SelectionModeAdapterTests, AppViewportMappingPreservesGroupMode) {
    EXPECT_EQ(appSelectionModeToViewport(ModelingSelectionMode::Vertex), ViewportSelectionMode::Vertex);
    EXPECT_EQ(appSelectionModeToViewport(ModelingSelectionMode::Edge), ViewportSelectionMode::Edge);
    EXPECT_EQ(appSelectionModeToViewport(ModelingSelectionMode::Face), ViewportSelectionMode::Face);
    EXPECT_EQ(appSelectionModeToViewport(ModelingSelectionMode::Object), ViewportSelectionMode::Object);
    EXPECT_EQ(appSelectionModeToViewport(ModelingSelectionMode::Group), ViewportSelectionMode::Group);

    EXPECT_EQ(viewportSelectionModeToApp(ViewportSelectionMode::Vertex), ModelingSelectionMode::Vertex);
    EXPECT_EQ(viewportSelectionModeToApp(ViewportSelectionMode::Edge), ModelingSelectionMode::Edge);
    EXPECT_EQ(viewportSelectionModeToApp(ViewportSelectionMode::Face), ModelingSelectionMode::Face);
    EXPECT_EQ(viewportSelectionModeToApp(ViewportSelectionMode::Object), ModelingSelectionMode::Object);
    EXPECT_EQ(viewportSelectionModeToApp(ViewportSelectionMode::Group), ModelingSelectionMode::Group);
}

TEST(SelectionModeAdapterTests, AppRuntimeMappingCollapsesGroupToObjectMode) {
    EXPECT_EQ(appSelectionModeToRuntime(ModelingSelectionMode::Vertex), quader_editor_core::SelectionMode::vertex);
    EXPECT_EQ(appSelectionModeToRuntime(ModelingSelectionMode::Edge), quader_editor_core::SelectionMode::edge);
    EXPECT_EQ(appSelectionModeToRuntime(ModelingSelectionMode::Face), quader_editor_core::SelectionMode::face);
    EXPECT_EQ(appSelectionModeToRuntime(ModelingSelectionMode::Object), quader_editor_core::SelectionMode::object);
    EXPECT_EQ(appSelectionModeToRuntime(ModelingSelectionMode::Group), quader_editor_core::SelectionMode::object);

    EXPECT_EQ(runtimeSelectionModeToApp(quader_editor_core::SelectionMode::vertex), ModelingSelectionMode::Vertex);
    EXPECT_EQ(runtimeSelectionModeToApp(quader_editor_core::SelectionMode::edge), ModelingSelectionMode::Edge);
    EXPECT_EQ(runtimeSelectionModeToApp(quader_editor_core::SelectionMode::face), ModelingSelectionMode::Face);
    EXPECT_EQ(runtimeSelectionModeToApp(quader_editor_core::SelectionMode::object), ModelingSelectionMode::Object);
}

TEST(SelectionModeAdapterTests, SceneDocumentOwnsObjectAndGroupSelectionModes) {
    EXPECT_FALSE(selectionModeUsesSceneDocument(ModelingSelectionMode::Vertex));
    EXPECT_FALSE(selectionModeUsesSceneDocument(ModelingSelectionMode::Edge));
    EXPECT_FALSE(selectionModeUsesSceneDocument(ModelingSelectionMode::Face));
    EXPECT_TRUE(selectionModeUsesSceneDocument(ModelingSelectionMode::Object));
    EXPECT_TRUE(selectionModeUsesSceneDocument(ModelingSelectionMode::Group));
}

TEST(SelectionModeAdapterTests, AppHostContractMappingKeepsGroupDistinct) {
    EXPECT_EQ(appSelectionModeToHostContract(ModelingSelectionMode::Vertex), quader_host_contract::HostSelectionMode::vertex);
    EXPECT_EQ(appSelectionModeToHostContract(ModelingSelectionMode::Edge), quader_host_contract::HostSelectionMode::edge);
    EXPECT_EQ(appSelectionModeToHostContract(ModelingSelectionMode::Face), quader_host_contract::HostSelectionMode::face);
    EXPECT_EQ(appSelectionModeToHostContract(ModelingSelectionMode::Object), quader_host_contract::HostSelectionMode::object);
    EXPECT_EQ(appSelectionModeToHostContract(ModelingSelectionMode::Group), quader_host_contract::HostSelectionMode::group);
}

TEST(SelectionModeAdapterTests, HostContractIdsAndReverseMappingComeFromSelectionModeCatalog) {
    for (const quader_host_contract::HostSelectionModeDescriptor& descriptor :
            quader_host_contract::selection_mode_catalog()) {
        EXPECT_EQ(hostSelectionModeId(descriptor.mode), descriptor.id);
        ASSERT_TRUE(hostSelectionModeFromId(descriptor.id).has_value());
        EXPECT_EQ(*hostSelectionModeFromId(descriptor.id), descriptor.mode);
        EXPECT_EQ(appSelectionModeToHostContract(hostSelectionModeToApp(descriptor.mode)), descriptor.mode);
        EXPECT_EQ(hostSelectionModeToViewport(descriptor.mode), appSelectionModeToViewport(hostSelectionModeToApp(descriptor.mode)));
    }
    EXPECT_FALSE(hostSelectionModeFromId("__missing__").has_value());
}

TEST(SelectionModeAdapterTests, ActionCatalogUsesGroupHostModeWithoutObjectPanelFallback) {
    const ModelingSelectionSummary summary{
            .mode = ModelingSelectionMode::Group,
            .hasSelection = true,
            .activeKind = ModelingActiveSelectionKind::Object,
            .selectedObjectCount = 1,
    };

    const ModelingActionSet contextActions = ModelingActionCatalog::actionSetFor(
            summary,
            ModelingActionQuery{
                    .surface = ModelingActionSurface::ViewportContextMenu,
                    .includeDisabled = true,
            });
    ASSERT_EQ(contextActions.panels.size(), 1U);
    EXPECT_EQ(contextActions.panels.front().selectionModeId, "group");

    const ModelingActionSet operationPanels = ModelingActionCatalog::actionSetFor(
            summary,
            ModelingActionQuery{
                    .surface = ModelingActionSurface::OperationsPanel,
            });
    EXPECT_TRUE(operationPanels.panels.empty());
}

TEST(SelectionModeAdapterTests, SelectionModeIdsHaveOneHostCatalogAdapter) {
    const std::string adapter = readSourceFile("src/editor/modeling/services/selection_mode_adapter.hpp");
    const std::string actionCatalog = readSourceFile("src/editor/modeling/services/modeling_action_catalog.cpp");
    const std::string viewportPanel = readSourceFile("src/ui/viewport_panel.cpp");
    const std::string viewportAdapter = readSourceFile("src/viewport/host_viewport_contract_adapter.cpp");

    EXPECT_NE(adapter.find("hostSelectionModeId("), std::string::npos);
    EXPECT_NE(adapter.find("hostSelectionModeFromId("), std::string::npos);
    EXPECT_NE(actionCatalog.find("hostSelectionModeId("), std::string::npos);
    EXPECT_NE(viewportPanel.find("appSelectionModeToViewport("), std::string::npos);
    EXPECT_NE(viewportPanel.find("viewportSelectionModeToApp("), std::string::npos);
    EXPECT_NE(viewportAdapter.find("hostSelectionModeFromId("), std::string::npos);

    EXPECT_EQ(actionCatalog.find("std::string selectionModeId("), std::string::npos);
    EXPECT_EQ(viewportPanel.find("shelfControlIdForHostSelectionMode"), std::string::npos);
    EXPECT_EQ(viewportPanel.find("modelingSelectionModeFromHostSelectionMode"), std::string::npos);
    EXPECT_EQ(viewportAdapter.find("if (id == \"vertex\")"), std::string::npos);
    EXPECT_EQ(viewportAdapter.find("if (id == \"edge\")"), std::string::npos);
    EXPECT_EQ(viewportAdapter.find("if (id == \"face\")"), std::string::npos);
    EXPECT_EQ(viewportAdapter.find("if (id == \"object\")"), std::string::npos);
    EXPECT_EQ(viewportAdapter.find("if (id == \"group\")"), std::string::npos);
}

} // namespace
} // namespace quader
