////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_command_ids.hpp"
#include "editor/commands/modeling_command_tool_catalog.hpp"

#include "editor/commands/activate_viewport_tool_action_tool.hpp"
#include "editor/commands/custom_instant_command_tool.hpp"
#include "editor/commands/modeling_command_dispatcher.hpp"
#include "editor/commands/runtime_command_tool.hpp"
#include "editor/commands/context_operation_command_tool.hpp"
#include "editor/commands/instant_operation_command_tool.hpp"
#include "editor/commands/preview_operation_command_tool.hpp"
#include "editor/commands/modeling/assign_material_tool.hpp"
#include "editor/commands/modeling/delete_selection_tool.hpp"
#include "editor/commands/modeling/flip_horizontal_tool.hpp"
#include "editor/commands/modeling/mirror_selection_tool.hpp"
#include "editor/commands/modeling/select_all_tool.hpp"
#include "editor/modeling/services/modeling_action_catalog.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "support/guardrail_file_scan.hpp"

#include <editor/metadata/host_contract.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace quader {
namespace {

using test::guardrail::readSourceFile;
using test::guardrail::sourceRoot;

bool instantOperationCatalogOwnsActionId(std::string_view actionId) {
    for (const ModelingInstantOperationCatalogEntry& entry : modelingInstantOperationCatalog()) {
        if (entry.id == actionId) {
            return true;
        }
    }
    return false;
}

bool commandToolOwnsHostActionId(std::string_view actionId) {
    return actionId == std::string_view("delete_selection") || instantOperationCatalogOwnsActionId(actionId);
}

TEST(ModelingToolCatalogTests, ContainsEveryInstantOperationCommandTool) {
    const std::span<const InstantCommandTool* const> tools = modelingInstantOperationCommandTools();
    ASSERT_EQ(tools.size(), modelingInstantOperationCatalog().size());

    for (const ModelingInstantOperationCatalogEntry& operation : modelingInstantOperationCatalog()) {
        const InstantCommandTool* tool = findModelingInstantCommandTool(operation.operation);
        ASSERT_NE(tool, nullptr) << operation.id;
        EXPECT_TRUE(tool->hasInstantOperation());
        EXPECT_EQ(tool->operation(), operation.operation);
        EXPECT_EQ(tool->kind(), CommandToolKind::Instant);
        EXPECT_EQ(tool->previewKind(), ModelingActionPreviewKind::None);
        EXPECT_EQ(tool->historyPolicy().mode, ModelingCommandHistoryMode::ModelingUndoRedo);
        EXPECT_EQ(tool->id(), operation.id);
        const quader_host_contract::HostActionDescriptor* action =
                quader_host_contract::find_action_descriptor(operation.id);
        EXPECT_EQ(tool->label(), action == nullptr ? operation.commandLabel : action->label);
        EXPECT_EQ(tool->operationId(), action == nullptr ? operation.operationId : action->operation_id);
    }
}

TEST(ModelingToolCatalogTests, CommandFamiliesExposeTheApprovedHierarchy) {
    static_assert(std::is_base_of_v<CommandTool, InstantCommandTool>);
    static_assert(std::is_base_of_v<CommandTool, ContextActionTool>);
    static_assert(std::is_base_of_v<CommandTool, PreviewCommandTool>);
    static_assert(std::is_base_of_v<InstantCommandTool, InstantOperationCommandTool>);
    static_assert(std::is_base_of_v<InstantCommandTool, RuntimeCommandTool>);
    static_assert(std::is_base_of_v<InstantCommandTool, CustomInstantCommandTool>);
    static_assert(std::is_base_of_v<ContextActionTool, ContextOperationCommandTool>);
    static_assert(std::is_base_of_v<ContextActionTool, ActivateViewportToolActionTool>);
    static_assert(std::is_base_of_v<PreviewCommandTool, PreviewOperationCommandTool>);

    static_assert(std::is_base_of_v<CustomInstantCommandTool, SelectAllTool>);
    static_assert(std::is_base_of_v<CustomInstantCommandTool, AssignMaterialTool>);
    static_assert(std::is_base_of_v<CustomInstantCommandTool, DeleteSelectionTool>);
    static_assert(std::is_base_of_v<RuntimeCommandTool, MirrorSelectionTool>);
    static_assert(std::is_base_of_v<RuntimeCommandTool, FlipHorizontalTool>);
}

TEST(ModelingToolCatalogTests, RegisteredToolsExposeConsistentDescriptorsAndPolicies) {
    std::unordered_set<std::string> ids;
    const std::span<const CommandTool* const> tools = modelingCommandTools();
    ASSERT_EQ(
            tools.size(),
            modelingInstantCommandTools().size() + modelingContextActionTools().size() + modelingPreviewCommandTools().size());

    for (const CommandTool* tool : tools) {
        ASSERT_NE(tool, nullptr);
        const CommandToolDescriptor descriptor = tool->descriptor();
        EXPECT_FALSE(descriptor.id.empty());
        EXPECT_FALSE(descriptor.operationId.empty());
        EXPECT_FALSE(descriptor.label.empty());
        EXPECT_EQ(tool->id(), descriptor.id);
        EXPECT_EQ(tool->operationId(), descriptor.operationId);
        EXPECT_EQ(tool->label(), descriptor.label);
        EXPECT_EQ(tool->kind(), descriptor.kind);
        EXPECT_EQ(tool->previewKind(), descriptor.previewKind);
        EXPECT_EQ(tool->rebuildsMeshOnSuccess(), descriptor.rebuildsMeshOnSuccess);
        EXPECT_TRUE(ids.emplace(std::string(tool->id())).second) << tool->id();

        const ModelingCommandHistoryPolicy history = tool->historyPolicy();
        if (tool->kind() == CommandToolKind::Preview || !tool->rebuildsMeshOnSuccess()) {
            EXPECT_EQ(history.mode, ModelingCommandHistoryMode::None) << tool->id();
        } else {
            EXPECT_EQ(history.mode, ModelingCommandHistoryMode::ModelingUndoRedo) << tool->id();
        }
    }
}

TEST(ModelingToolCatalogTests, PublicCommandBridgeMetadataLivesOnCommandDescriptors) {
    const CommandTool* deleteSelection = findModelingCommandTool("delete_selection");
    ASSERT_NE(deleteSelection, nullptr);
    EXPECT_EQ(deleteSelection->publicCommandId(), QuaderCommand::DeleteSelection.string());
    EXPECT_FALSE(deleteSelection->registersPublicCommand());

    const CommandTool* duplicate = findModelingCommandTool("duplicate_meshes");
    ASSERT_NE(duplicate, nullptr);
    EXPECT_TRUE(duplicate->publicCommandId().empty());
    EXPECT_TRUE(duplicate->registersPublicCommand());

    const CommandTool* applyActiveMaterial = findModelingCommandTool("apply_active_material");
    ASSERT_NE(applyActiveMaterial, nullptr);
    EXPECT_EQ(applyActiveMaterial->publicCommandId(), QuaderCommand::ApplyMaterial.string());
    EXPECT_FALSE(applyActiveMaterial->registersPublicCommand());

    const CommandTool* combineMeshes = findModelingCommandTool("combine_meshes");
    ASSERT_NE(combineMeshes, nullptr);
    EXPECT_TRUE(combineMeshes->publicCommandId().empty());
    EXPECT_TRUE(combineMeshes->registersPublicCommand());
}

TEST(ModelingToolCatalogTests, HostOperationActionIdsResolveToCommandTools) {
    std::size_t expectedContextActions = 0;
    std::size_t expectedPreviewActions = 0;
    for (const quader_host_contract::HostActionDescriptor& action : quader_host_contract::action_catalog()) {
        if (action.kind != quader_host_contract::HostActionKind::operation) {
            continue;
        }
        if (action.preview_kind == quader_host_contract::HostPreviewKind::none &&
                !commandToolOwnsHostActionId(action.id)) {
            ++expectedContextActions;
        }
        if (action.preview_kind != quader_host_contract::HostPreviewKind::none) {
            ++expectedPreviewActions;
        }

        const CommandTool* tool = findModelingCommandTool(action.id);
        ASSERT_NE(tool, nullptr) << action.id;
        EXPECT_EQ(tool->operationId(), action.operation_id) << action.id;
        EXPECT_EQ(tool->label(), action.label) << action.id;
        EXPECT_EQ(tool->previewKind(), ModelingActionCatalog::previewKindFromHost(action.preview_kind)) << action.id;
        if (action.preview_kind == quader_host_contract::HostPreviewKind::none) {
            EXPECT_NE(tool->kind(), CommandToolKind::Preview) << action.id;
        } else {
            EXPECT_EQ(tool->kind(), CommandToolKind::Preview) << action.id;
        }
    }

    EXPECT_EQ(modelingContextActionTools().size(), expectedContextActions);
    EXPECT_EQ(modelingPreviewCommandTools().size(), expectedPreviewActions);
    EXPECT_NE(findModelingContextActionTool("apply_active_material"), nullptr);
}

TEST(ModelingToolCatalogTests, PreviewCommandsUseHostActionIds) {
    const PreviewCommandTool* edgeBevel = findModelingPreviewCommandTool("bevel_edges");
    ASSERT_NE(edgeBevel, nullptr);
    EXPECT_EQ(edgeBevel->operationId(), std::string_view("edge_bevel"));
    EXPECT_EQ(edgeBevel->previewKind(), ModelingActionPreviewKind::EdgeBevel);

    const PreviewCommandTool* edgeBridge = findModelingPreviewCommandTool("interpolated_bridge_edges");
    ASSERT_NE(edgeBridge, nullptr);
    EXPECT_EQ(edgeBridge->operationId(), std::string_view("bridge"));
    EXPECT_EQ(edgeBridge->previewKind(), ModelingActionPreviewKind::Bridge);

    const PreviewCommandTool* faceBridge = findModelingPreviewCommandTool("interpolated_bridge_faces");
    ASSERT_NE(faceBridge, nullptr);
    EXPECT_EQ(faceBridge->operationId(), std::string_view("bridge"));
    EXPECT_EQ(faceBridge->previewKind(), ModelingActionPreviewKind::Bridge);

    const PreviewCommandTool* thicken = findModelingPreviewCommandTool("thicken_faces");
    ASSERT_NE(thicken, nullptr);
    EXPECT_EQ(thicken->operationId(), std::string_view("thicken"));
    EXPECT_EQ(thicken->previewKind(), ModelingActionPreviewKind::Thicken);

    const PreviewCommandTool* outerHull = findModelingPreviewCommandTool("create_outer_hull");
    ASSERT_NE(outerHull, nullptr);
    EXPECT_EQ(outerHull->operationId(), std::string_view("thicken"));
    EXPECT_EQ(outerHull->previewKind(), ModelingActionPreviewKind::Thicken);

    const PreviewCommandTool* sliceQuad = findModelingPreviewCommandTool("slice_quad");
    ASSERT_NE(sliceQuad, nullptr);
    EXPECT_EQ(sliceQuad->operationId(), std::string_view("slice_quad"));
    EXPECT_EQ(sliceQuad->previewKind(), ModelingActionPreviewKind::SliceQuad);

    const PreviewCommandTool* mergeByDistance = findModelingPreviewCommandTool("merge_by_distance");
    ASSERT_NE(mergeByDistance, nullptr);
    EXPECT_EQ(mergeByDistance->operationId(), std::string_view("merge_by_distance"));
    EXPECT_EQ(mergeByDistance->previewKind(), ModelingActionPreviewKind::MergeByDistance);
}

TEST(ModelingToolCatalogTests, DispatcherUsesCatalogAndHistoryPolicy) {
    ModelingCommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.find("duplicate_meshes"), findModelingCommandTool("duplicate_meshes"));
    EXPECT_EQ(dispatcher.find("missing_command"), nullptr);
    EXPECT_EQ(dispatcher.historyPolicy("duplicate_meshes").mode, ModelingCommandHistoryMode::ModelingUndoRedo);
    EXPECT_EQ(dispatcher.historyPolicy("select_all").mode, ModelingCommandHistoryMode::None);
    EXPECT_EQ(dispatcher.historyPolicy("bevel_edges").mode, ModelingCommandHistoryMode::None);
    EXPECT_EQ(dispatcher.historyPolicy("missing_command").mode, ModelingCommandHistoryMode::None);
}

TEST(ModelingToolCatalogTests, HostActionCommandToolsAreDerivedFromActionCatalog) {
    const std::filesystem::path root = sourceRoot();
    const std::string source = readSourceFile(root / "src" / "editor" / "commands" / "modeling_command_tool_catalog.cpp");
    const std::string hostPreviewSwitchCase =
            std::string("case quader_host_contract::") + "HostPreviewKind::";

    EXPECT_NE(source.find("quader_host_contract::action_catalog()"), std::string::npos);
    EXPECT_NE(source.find("instantOperationCatalogOwnsActionId("), std::string::npos);
    EXPECT_NE(source.find("ModelingActionCatalog::previewKindFromHost("), std::string::npos);
    EXPECT_EQ(source.find("contextDescriptorFromHostAction(\""), std::string::npos);
    EXPECT_EQ(source.find("previewDescriptorFromHostAction("), std::string::npos);
    EXPECT_EQ(source.find(hostPreviewSwitchCase), std::string::npos);
    EXPECT_EQ(source.find("static const std::array<const ContextActionTool*"), std::string::npos);
    EXPECT_EQ(source.find("static const std::array<const PreviewCommandTool*"), std::string::npos);

    const std::string testSource = readSourceFile(root / "tests" / "modeling_tool_catalog_tests.cpp");
    EXPECT_EQ(testSource.find(hostPreviewSwitchCase), std::string::npos);
}

} // namespace
} // namespace quader
