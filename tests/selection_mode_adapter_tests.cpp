#include "modeling/SelectionModeAdapter.h"

#include "modeling/ModelingActionCatalog.h"

#include <gtest/gtest.h>

namespace quader {
namespace {

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

#if QUADER_HAS_MODELING_SDK
TEST(SelectionModeAdapterTests, AppSdkMappingCollapsesGroupToObjectMode) {
    EXPECT_EQ(appSelectionModeToSdk(ModelingSelectionMode::Vertex), quader_editor_core::SelectionMode::vertex);
    EXPECT_EQ(appSelectionModeToSdk(ModelingSelectionMode::Edge), quader_editor_core::SelectionMode::edge);
    EXPECT_EQ(appSelectionModeToSdk(ModelingSelectionMode::Face), quader_editor_core::SelectionMode::face);
    EXPECT_EQ(appSelectionModeToSdk(ModelingSelectionMode::Object), quader_editor_core::SelectionMode::object);
    EXPECT_EQ(appSelectionModeToSdk(ModelingSelectionMode::Group), quader_editor_core::SelectionMode::object);

    EXPECT_EQ(sdkSelectionModeToApp(quader_editor_core::SelectionMode::vertex), ModelingSelectionMode::Vertex);
    EXPECT_EQ(sdkSelectionModeToApp(quader_editor_core::SelectionMode::edge), ModelingSelectionMode::Edge);
    EXPECT_EQ(sdkSelectionModeToApp(quader_editor_core::SelectionMode::face), ModelingSelectionMode::Face);
    EXPECT_EQ(sdkSelectionModeToApp(quader_editor_core::SelectionMode::object), ModelingSelectionMode::Object);
}

TEST(SelectionModeAdapterTests, AppHostContractMappingKeepsGroupDistinct) {
    EXPECT_EQ(appSelectionModeToHostContract(ModelingSelectionMode::Vertex), quader_host_contract::HostSelectionMode::vertex);
    EXPECT_EQ(appSelectionModeToHostContract(ModelingSelectionMode::Edge), quader_host_contract::HostSelectionMode::edge);
    EXPECT_EQ(appSelectionModeToHostContract(ModelingSelectionMode::Face), quader_host_contract::HostSelectionMode::face);
    EXPECT_EQ(appSelectionModeToHostContract(ModelingSelectionMode::Object), quader_host_contract::HostSelectionMode::object);
    EXPECT_EQ(appSelectionModeToHostContract(ModelingSelectionMode::Group), quader_host_contract::HostSelectionMode::group);
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
#endif

} // namespace
} // namespace quader
