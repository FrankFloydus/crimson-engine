////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling_command_tool_catalog.hpp"

#include "editor/commands/modeling/assign_material_tool.hpp"
#include "editor/commands/modeling/clear_selection_tool.hpp"
#include "editor/commands/modeling/copy_selection_tool.hpp"
#include "editor/commands/modeling/delete_selection_tool.hpp"
#include "editor/commands/modeling/flip_horizontal_tool.hpp"
#include "editor/commands/modeling/flip_vertical_tool.hpp"
#include "editor/commands/modeling/invert_selection_tool.hpp"
#include "editor/commands/modeling/mirror_selection_tool.hpp"
#include "editor/commands/modeling/paste_selection_tool.hpp"
#include "editor/commands/modeling/repeat_last_action_tool.hpp"
#include "editor/commands/modeling/select_all_tool.hpp"
#include "editor/commands/context_operation_command_tool.hpp"
#include "editor/commands/instant_operation_command_tool.hpp"
#include "editor/commands/preview_operation_command_tool.hpp"
#include "editor/modeling/services/modeling_action_catalog.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"

#include <editor/metadata/host_contract.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <string_view>
#include <vector>

namespace quader {
namespace {

/**
 * Catalog-owned instant action for modeling operations that only need static
 * descriptor metadata and the standard ModelingSessionService execution path.
 */
class CatalogInstantOperationTool final : public InstantOperationCommandTool {
public:
    CatalogInstantOperationTool(CommandToolDescriptor descriptor, ModelingInstantOperationId operation)
        : descriptor_(descriptor)
        , operation_(operation) {}

    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return descriptor_;
    }

    [[nodiscard]] ModelingInstantOperationId
    instant_operation_id() const override {
      return operation_;
    }

private:
    CommandToolDescriptor descriptor_;
    ModelingInstantOperationId operation_;
};

/**
 * Catalog-owned context action for selection-dependent operation affordances
 * whose behavior is fully handled by the shared context-action executor.
 */
class CatalogContextActionTool final : public ContextOperationCommandTool {
public:
    explicit CatalogContextActionTool(CommandToolDescriptor descriptor)
        : descriptor_(descriptor) {}

    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return descriptor_;
    }

    [[nodiscard]] std::string_view action_id() const override {
      return descriptor_.id;
    }

private:
    CommandToolDescriptor descriptor_;
};

/**
 * Catalog-owned preview action for operation previews that only need descriptor
 * metadata and the shared preview begin/commit/cancel behavior.
 */
class CatalogPreviewActionTool final : public PreviewOperationCommandTool {
public:
    explicit CatalogPreviewActionTool(CommandToolDescriptor descriptor)
        : descriptor_(descriptor) {}

    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return descriptor_;
    }

    [[nodiscard]] std::string_view action_id() const override {
      return descriptor_.id;
    }

private:
    CommandToolDescriptor descriptor_;
};

[[nodiscard]] CommandToolDescriptor
instant_descriptor(const ModelingInstantOperationCatalogEntry &entry) {
  const quader_host_contract::HostActionDescriptor *action =
      quader_host_contract::find_action_descriptor(entry.id);
  return {
      .id = entry.id,
      .operation_id =
          action == nullptr ? entry.operation_id : action->operation_id,
      .label = action == nullptr ? entry.command_label : action->label,
      .kind = CommandToolKind::Instant,
      .preview_kind = ModelingActionPreviewKind::None,
      .rebuilds_mesh_on_success = entry.rebuilds_mesh_on_success,
      .public_command_id = entry.public_command_id,
      .registers_public_command = entry.registers_public_command,
  };
}

[[nodiscard]] CommandToolDescriptor
context_descriptor(const quader_host_contract::HostActionDescriptor &action,
                   bool rebuilds_mesh_on_success = true,
                   std::string_view public_command_id = {},
                   bool registers_public_command = true) {
  assert(action.kind == quader_host_contract::HostActionKind::Operation);
  return {
      .id = action.id,
      .operation_id = action.operation_id,
      .label = action.label,
      .kind = CommandToolKind::ContextAction,
      .preview_kind = ModelingActionPreviewKind::None,
      .rebuilds_mesh_on_success = rebuilds_mesh_on_success,
      .public_command_id = public_command_id,
      .registers_public_command = registers_public_command,
  };
}

[[nodiscard]] CommandToolDescriptor
preview_descriptor(const quader_host_contract::HostActionDescriptor &action,
                   bool rebuilds_mesh_on_success = true) {
  assert(action.kind == quader_host_contract::HostActionKind::Operation);
  return {
      .id = action.id,
      .operation_id = action.operation_id,
      .label = action.label,
      .kind = CommandToolKind::Preview,
      .preview_kind =
          ModelingActionCatalog::preview_kind_from_host(action.preview_kind),
      .rebuilds_mesh_on_success = rebuilds_mesh_on_success,
  };
}

[[nodiscard]] bool
instant_operation_catalog_owns_action_id(std::string_view action_id) {
  const std::span<const ModelingInstantOperationCatalogEntry> entries =
      modeling_instant_operation_catalog();
  return std::any_of(entries.begin(), entries.end(),
                     [&](const ModelingInstantOperationCatalogEntry &entry) {
                       return entry.id == action_id;
                     });
}

[[nodiscard]] bool
command_tool_owns_host_action_id(std::string_view action_id) {
  return action_id == std::string_view("delete_selection") ||
         instant_operation_catalog_owns_action_id(action_id);
}

[[nodiscard]] CommandToolDescriptor context_descriptor_from_host_action(
    const quader_host_contract::HostActionDescriptor &action) {
  if (action.id == std::string_view("apply_active_material")) {
    return context_descriptor(action, true, "edit.apply_material", false);
  }
  return context_descriptor(action);
}

} // namespace

std::span<const InstantCommandTool *const>
modeling_instant_operation_command_tools() {
  static const std::vector<std::unique_ptr<CatalogInstantOperationTool>>
      kOwnedTools = [] {
        std::vector<std::unique_ptr<CatalogInstantOperationTool>> result;
        for (const ModelingInstantOperationCatalogEntry &entry :
             modeling_instant_operation_catalog()) {
          result.push_back(std::make_unique<CatalogInstantOperationTool>(
              instant_descriptor(entry), entry.operation));
        }
        return result;
      }();

  static const std::vector<const InstantCommandTool *> kTools = [] {
    std::vector<const InstantCommandTool *> result;
    result.reserve(kOwnedTools.size());
    for (const std::unique_ptr<CatalogInstantOperationTool> &tool :
         kOwnedTools) {
      result.push_back(tool.get());
    }
    return result;
  }();
  return {kTools.data(), kTools.size()};
}

std::span<const InstantCommandTool *const>
modeling_service_instant_command_tools() {
  static const SelectAllTool kSelectAll;
  static const InvertSelectionTool kInvertSelection;
  static const ClearSelectionTool kClearSelection;
  static const DeleteSelectionTool kDeleteSelection;
  static const CopySelectionTool kCopySelection;
  static const PasteSelectionTool kPasteSelection;
  static const RepeatLastActionTool kRepeatLastAction;
  static const AssignMaterialTool kAssignMaterial;
  static const MirrorSelectionTool kMirrorX{ModelingMirrorAxis::X,
                                            "mirror_selection_x", "Mirror X"};
  static const MirrorSelectionTool kMirrorY{ModelingMirrorAxis::Y,
                                            "mirror_selection_y", "Mirror Y"};
  static const MirrorSelectionTool kMirrorZ{ModelingMirrorAxis::Z,
                                            "mirror_selection_z", "Mirror Z"};
  static const FlipHorizontalTool kFlipHorizontal;
  static const FlipVerticalTool kFlipVertical;

  static const std::array<const InstantCommandTool *, 13> kTools{{
      &kSelectAll,
      &kInvertSelection,
      &kClearSelection,
      &kDeleteSelection,
      &kCopySelection,
      &kPasteSelection,
      &kRepeatLastAction,
      &kAssignMaterial,
      &kMirrorX,
      &kMirrorY,
      &kMirrorZ,
      &kFlipHorizontal,
      &kFlipVertical,
  }};
  return {kTools.data(), kTools.size()};
}

std::span<const InstantCommandTool *const> modeling_instant_command_tools() {
  static const std::vector<const InstantCommandTool *> kTools = [] {
    std::vector<const InstantCommandTool *> result;
    for (const InstantCommandTool *tool :
         modeling_instant_operation_command_tools()) {
      result.push_back(tool);
    }
    for (const InstantCommandTool *tool :
         modeling_service_instant_command_tools()) {
      result.push_back(tool);
    }
    return result;
  }();
  return {kTools.data(), kTools.size()};
}

std::span<const ContextActionTool *const> modeling_context_action_tools() {
  static const std::vector<std::unique_ptr<CatalogContextActionTool>>
      kOwnedTools = [] {
        std::vector<std::unique_ptr<CatalogContextActionTool>> result;
        for (const quader_host_contract::HostActionDescriptor& action : quader_host_contract::action_catalog()) {
          if (action.kind != quader_host_contract::HostActionKind::Operation ||
              action.preview_kind !=
                  quader_host_contract::HostPreviewKind::None ||
              command_tool_owns_host_action_id(action.id)) {
            continue;
          }
          result.push_back(std::make_unique<CatalogContextActionTool>(
              context_descriptor_from_host_action(action)));
        }
        return result;
      }();

  static const std::vector<const ContextActionTool *> kTools = [] {
    std::vector<const ContextActionTool *> result;
    result.reserve(kOwnedTools.size());
    for (const std::unique_ptr<CatalogContextActionTool> &tool : kOwnedTools) {
      result.push_back(tool.get());
    }
    return result;
  }();
  return {kTools.data(), kTools.size()};
}

std::span<const PreviewCommandTool *const> modeling_preview_command_tools() {
  static const std::vector<std::unique_ptr<CatalogPreviewActionTool>>
      kOwnedTools = [] {
        std::vector<std::unique_ptr<CatalogPreviewActionTool>> result;
        for (const quader_host_contract::HostActionDescriptor& action : quader_host_contract::action_catalog()) {
          if (action.kind != quader_host_contract::HostActionKind::Operation ||
              action.preview_kind ==
                  quader_host_contract::HostPreviewKind::None) {
            continue;
          }
          result.push_back(std::make_unique<CatalogPreviewActionTool>(
              preview_descriptor(action)));
        }
        return result;
      }();

  static const std::vector<const PreviewCommandTool *> kTools = [] {
    std::vector<const PreviewCommandTool *> result;
    result.reserve(kOwnedTools.size());
    for (const std::unique_ptr<CatalogPreviewActionTool> &tool : kOwnedTools) {
      result.push_back(tool.get());
    }
    return result;
  }();
  return {kTools.data(), kTools.size()};
}

std::span<const CommandTool *const> modeling_command_tools() {
  static const std::vector<const CommandTool *> kTools = [] {
    std::vector<const CommandTool *> result;
    for (const InstantCommandTool *tool : modeling_instant_command_tools()) {
      result.push_back(tool);
    }
    for (const ContextActionTool *tool : modeling_context_action_tools()) {
      result.push_back(tool);
    }
    for (const PreviewCommandTool *tool : modeling_preview_command_tools()) {
      result.push_back(tool);
    }
    return result;
  }();
  return {kTools.data(), kTools.size()};
}

const CommandTool *find_modeling_command_tool(std::string_view id) {
  const std::span<const CommandTool *const> tools = modeling_command_tools();
  const auto found =
      std::find_if(tools.begin(), tools.end(), [&](const CommandTool *tool) {
        return tool != nullptr && tool->id() == id;
      });
  return found == tools.end() ? nullptr : *found;
}

std::string modeling_command_tool_localization_key(std::string_view id) {
  if (quader_host_contract::find_action_descriptor(id) != nullptr ||
      id == "copy_pivot_from_active" || id == "duplicate_meshes") {
    return "modeling.action." + std::string(id) + ".label";
  }
  return {};
}

const InstantCommandTool *
find_modeling_instant_command_tool(ModelingInstantOperationId operation) {
  const std::span<const InstantCommandTool *const> tools =
      modeling_instant_operation_command_tools();
  const auto found = std::find_if(
      tools.begin(), tools.end(), [&](const InstantCommandTool *tool) {
        return tool != nullptr && tool->has_instant_operation() &&
               tool->operation() == operation;
      });
  return found == tools.end() ? nullptr : *found;
}

const ContextActionTool *
find_modeling_context_action_tool(std::string_view action_id) {
  const std::span<const ContextActionTool *const> tools =
      modeling_context_action_tools();
  const auto found = std::find_if(
      tools.begin(), tools.end(), [&](const ContextActionTool *tool) {
        return tool != nullptr && tool->action_id() == action_id;
      });
  return found == tools.end() ? nullptr : *found;
}

const PreviewCommandTool *
find_modeling_preview_command_tool(std::string_view action_id) {
  const std::span<const PreviewCommandTool *const> tools =
      modeling_preview_command_tools();
  const auto found = std::find_if(
      tools.begin(), tools.end(), [&](const PreviewCommandTool *tool) {
        return tool != nullptr && tool->action_id() == action_id;
      });
  return found == tools.end() ? nullptr : *found;
}

std::string_view modeling_mirror_selection_tool_id(ModelingMirrorAxis axis) {
  switch (axis) {
  case ModelingMirrorAxis::X:
    return "mirror_selection_x";
  case ModelingMirrorAxis::Y:
    return "mirror_selection_y";
  case ModelingMirrorAxis::Z:
    return "mirror_selection_z";
  }
  return "mirror_selection_x";
}

} // namespace quader
