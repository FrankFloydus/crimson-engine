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

using test::guardrail::read_source_file;
using test::guardrail::source_root;

bool instant_operation_catalog_owns_action_id(std::string_view action_id) {
  for (const ModelingInstantOperationCatalogEntry &entry :
       modeling_instant_operation_catalog()) {
    if (entry.id == action_id) {
      return true;
    }
  }
  return false;
}

bool command_tool_owns_host_action_id(std::string_view action_id) {
  return action_id == std::string_view("delete_selection") ||
         instant_operation_catalog_owns_action_id(action_id);
}

TEST(ModelingToolCatalogTests, ContainsEveryInstantOperationCommandTool) {
  const std::span<const InstantCommandTool *const> tools =
      modeling_instant_operation_command_tools();
  ASSERT_EQ(tools.size(), modeling_instant_operation_catalog().size());

  for (const ModelingInstantOperationCatalogEntry &operation :
       modeling_instant_operation_catalog()) {
    const InstantCommandTool *tool =
        find_modeling_instant_command_tool(operation.operation);
    ASSERT_NE(tool, nullptr) << operation.id;
    EXPECT_TRUE(tool->has_instant_operation());
    EXPECT_EQ(tool->operation(), operation.operation);
    EXPECT_EQ(tool->kind(), CommandToolKind::Instant);
    EXPECT_EQ(tool->preview_kind(), ModelingActionPreviewKind::None);
    EXPECT_EQ(tool->history_policy().mode,
              ModelingCommandHistoryMode::ModelingUndoRedo);
    EXPECT_EQ(tool->id(), operation.id);
    const quader_host_contract::HostActionDescriptor *action =
        quader_host_contract::find_action_descriptor(operation.id);
    EXPECT_EQ(tool->label(),
              action == nullptr ? operation.command_label : action->label);
    EXPECT_EQ(tool->operation_id(), action == nullptr ? operation.operation_id
                                                      : action->operation_id);
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
    const std::span<const CommandTool *const> tools = modeling_command_tools();
    ASSERT_EQ(tools.size(), modeling_instant_command_tools().size() +
                                modeling_context_action_tools().size() +
                                modeling_preview_command_tools().size());

    for (const CommandTool* tool : tools) {
        ASSERT_NE(tool, nullptr);
        const CommandToolDescriptor descriptor = tool->descriptor();
        EXPECT_FALSE(descriptor.id.empty());
        EXPECT_FALSE(descriptor.operation_id.empty());
        EXPECT_FALSE(descriptor.label.empty());
        EXPECT_EQ(tool->id(), descriptor.id);
        EXPECT_EQ(tool->operation_id(), descriptor.operation_id);
        EXPECT_EQ(tool->label(), descriptor.label);
        EXPECT_EQ(tool->kind(), descriptor.kind);
        EXPECT_EQ(tool->preview_kind(), descriptor.preview_kind);
        EXPECT_EQ(tool->rebuilds_mesh_on_success(),
                  descriptor.rebuilds_mesh_on_success);
        EXPECT_TRUE(ids.emplace(std::string(tool->id())).second) << tool->id();

        const ModelingCommandHistoryPolicy history = tool->history_policy();
        if (tool->kind() == CommandToolKind::Preview ||
            !tool->rebuilds_mesh_on_success()) {
          EXPECT_EQ(history.mode, ModelingCommandHistoryMode::None)
              << tool->id();
        } else {
          EXPECT_EQ(history.mode, ModelingCommandHistoryMode::ModelingUndoRedo)
              << tool->id();
        }
    }
}

TEST(ModelingToolCatalogTests, PublicCommandBridgeMetadataLivesOnCommandDescriptors) {
  const CommandTool *delete_selection =
      find_modeling_command_tool("delete_selection");
  ASSERT_NE(delete_selection, nullptr);
  EXPECT_EQ(delete_selection->public_command_id(),
            quader_command::kDeleteSelection.string());
  EXPECT_FALSE(delete_selection->registers_public_command());

  const CommandTool *duplicate = find_modeling_command_tool("duplicate_meshes");
  ASSERT_NE(duplicate, nullptr);
  EXPECT_TRUE(duplicate->public_command_id().empty());
  EXPECT_TRUE(duplicate->registers_public_command());

  const CommandTool *apply_active_material =
      find_modeling_command_tool("apply_active_material");
  ASSERT_NE(apply_active_material, nullptr);
  EXPECT_EQ(apply_active_material->public_command_id(),
            quader_command::kApplyMaterial.string());
  EXPECT_FALSE(apply_active_material->registers_public_command());

  const CommandTool *combine_meshes =
      find_modeling_command_tool("combine_meshes");
  ASSERT_NE(combine_meshes, nullptr);
  EXPECT_TRUE(combine_meshes->public_command_id().empty());
  EXPECT_TRUE(combine_meshes->registers_public_command());
}

TEST(ModelingToolCatalogTests, HostOperationActionIdsResolveToCommandTools) {
  std::size_t expected_context_actions = 0;
  std::size_t expected_preview_actions = 0;
  for (const quader_host_contract::HostActionDescriptor &action :
       quader_host_contract::action_catalog()) {
    if (action.kind != quader_host_contract::HostActionKind::Operation) {
      continue;
    }
    if (action.preview_kind == quader_host_contract::HostPreviewKind::None &&
        !command_tool_owns_host_action_id(action.id)) {
      ++expected_context_actions;
    }
    if (action.preview_kind != quader_host_contract::HostPreviewKind::None) {
      ++expected_preview_actions;
    }

    const CommandTool *tool = find_modeling_command_tool(action.id);
    ASSERT_NE(tool, nullptr) << action.id;
    EXPECT_EQ(tool->operation_id(), action.operation_id) << action.id;
    EXPECT_EQ(tool->label(), action.label) << action.id;
    EXPECT_EQ(
        tool->preview_kind(),
        ModelingActionCatalog::preview_kind_from_host(action.preview_kind))
        << action.id;
    if (action.preview_kind == quader_host_contract::HostPreviewKind::None) {
      EXPECT_NE(tool->kind(), CommandToolKind::Preview) << action.id;
    } else {
      EXPECT_EQ(tool->kind(), CommandToolKind::Preview) << action.id;
    }
    }

    EXPECT_EQ(modeling_context_action_tools().size(), expected_context_actions);
    EXPECT_EQ(modeling_preview_command_tools().size(),
              expected_preview_actions);
    EXPECT_NE(find_modeling_context_action_tool("apply_active_material"),
              nullptr);
}

TEST(ModelingToolCatalogTests, PreviewCommandsUseHostActionIds) {
  const PreviewCommandTool *edge_bevel =
      find_modeling_preview_command_tool("bevel_edges");
  ASSERT_NE(edge_bevel, nullptr);
  EXPECT_EQ(edge_bevel->operation_id(), std::string_view("edge_bevel"));
  EXPECT_EQ(edge_bevel->preview_kind(), ModelingActionPreviewKind::EdgeBevel);

  const PreviewCommandTool *edge_bridge =
      find_modeling_preview_command_tool("interpolated_bridge_edges");
  ASSERT_NE(edge_bridge, nullptr);
  EXPECT_EQ(edge_bridge->operation_id(), std::string_view("bridge"));
  EXPECT_EQ(edge_bridge->preview_kind(), ModelingActionPreviewKind::Bridge);

  const PreviewCommandTool *face_bridge =
      find_modeling_preview_command_tool("interpolated_bridge_faces");
  ASSERT_NE(face_bridge, nullptr);
  EXPECT_EQ(face_bridge->operation_id(), std::string_view("bridge"));
  EXPECT_EQ(face_bridge->preview_kind(), ModelingActionPreviewKind::Bridge);

  const PreviewCommandTool *thicken =
      find_modeling_preview_command_tool("thicken_faces");
  ASSERT_NE(thicken, nullptr);
  EXPECT_EQ(thicken->operation_id(), std::string_view("thicken"));
  EXPECT_EQ(thicken->preview_kind(), ModelingActionPreviewKind::Thicken);

  const PreviewCommandTool *outer_hull =
      find_modeling_preview_command_tool("create_outer_hull");
  ASSERT_NE(outer_hull, nullptr);
  EXPECT_EQ(outer_hull->operation_id(), std::string_view("thicken"));
  EXPECT_EQ(outer_hull->preview_kind(), ModelingActionPreviewKind::Thicken);

  const PreviewCommandTool *slice_quad =
      find_modeling_preview_command_tool("slice_quad");
  ASSERT_NE(slice_quad, nullptr);
  EXPECT_EQ(slice_quad->operation_id(), std::string_view("slice_quad"));
  EXPECT_EQ(slice_quad->preview_kind(), ModelingActionPreviewKind::SliceQuad);

  const PreviewCommandTool *merge_by_distance =
      find_modeling_preview_command_tool("merge_by_distance");
  ASSERT_NE(merge_by_distance, nullptr);
  EXPECT_EQ(merge_by_distance->operation_id(),
            std::string_view("merge_by_distance"));
  EXPECT_EQ(merge_by_distance->preview_kind(),
            ModelingActionPreviewKind::MergeByDistance);
}

TEST(ModelingToolCatalogTests, DispatcherUsesCatalogAndHistoryPolicy) {
    ModelingCommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.find("duplicate_meshes"),
              find_modeling_command_tool("duplicate_meshes"));
    EXPECT_EQ(dispatcher.find("missing_command"), nullptr);
    EXPECT_EQ(dispatcher.history_policy("duplicate_meshes").mode,
              ModelingCommandHistoryMode::ModelingUndoRedo);
    EXPECT_EQ(dispatcher.history_policy("select_all").mode,
              ModelingCommandHistoryMode::None);
    EXPECT_EQ(dispatcher.history_policy("bevel_edges").mode,
              ModelingCommandHistoryMode::None);
    EXPECT_EQ(dispatcher.history_policy("missing_command").mode,
              ModelingCommandHistoryMode::None);
}

TEST(ModelingToolCatalogTests, HostActionCommandToolsAreDerivedFromActionCatalog) {
  const std::filesystem::path root = source_root();
  const std::string source =
      read_source_file(root / "src" / "editor" / "commands" /
                       "modeling_command_tool_catalog.cpp");
  const std::string host_preview_switch_case =
      std::string("case quader_host_contract::") + "HostPreviewKind::";

  EXPECT_NE(source.find("quader_host_contract::action_catalog()"),
            std::string::npos);
  EXPECT_NE(source.find("instant_operation_catalog_owns_action_id("),
            std::string::npos);
  EXPECT_NE(source.find("ModelingActionCatalog::preview_kind_from_host("),
            std::string::npos);
  EXPECT_EQ(source.find("context_descriptor_from_host_action(\""),
            std::string::npos);
  EXPECT_EQ(source.find("previewDescriptorFromHostAction("), std::string::npos);
  EXPECT_EQ(source.find(host_preview_switch_case), std::string::npos);
  EXPECT_EQ(source.find("static const std::array<const ContextActionTool*"),
            std::string::npos);
  EXPECT_EQ(source.find("static const std::array<const PreviewCommandTool*"),
            std::string::npos);

  const std::string test_source =
      read_source_file(root / "tests" / "modeling_tool_catalog_tests.cpp");
  EXPECT_EQ(test_source.find(host_preview_switch_case), std::string::npos);
}

} // namespace
} // namespace quader
