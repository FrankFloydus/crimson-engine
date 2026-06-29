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

    [[nodiscard]] ModelingInstantOperationId instantOperationId() const override {
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

    [[nodiscard]] std::string_view actionId() const override {
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

    [[nodiscard]] std::string_view actionId() const override {
        return descriptor_.id;
    }

private:
    CommandToolDescriptor descriptor_;
};

[[nodiscard]] CommandToolDescriptor instantDescriptor(const ModelingInstantOperationCatalogEntry& entry) {
    const quader_host_contract::HostActionDescriptor* action =
            quader_host_contract::find_action_descriptor(entry.id);
    return {
            .id = entry.id,
            .operationId = action == nullptr ? entry.operationId : action->operation_id,
            .label = action == nullptr ? entry.commandLabel : action->label,
            .kind = CommandToolKind::Instant,
            .previewKind = ModelingActionPreviewKind::None,
            .rebuildsMeshOnSuccess = entry.rebuildsMeshOnSuccess,
            .publicCommandId = entry.publicCommandId,
            .registersPublicCommand = entry.registersPublicCommand,
    };
}

[[nodiscard]] CommandToolDescriptor contextDescriptor(
        const quader_host_contract::HostActionDescriptor& action,
        bool rebuildsMeshOnSuccess = true,
        std::string_view publicCommandId = {},
        bool registersPublicCommand = true) {
    assert(action.kind == quader_host_contract::HostActionKind::operation);
    return {
            .id = action.id,
            .operationId = action.operation_id,
            .label = action.label,
            .kind = CommandToolKind::ContextAction,
            .previewKind = ModelingActionPreviewKind::None,
            .rebuildsMeshOnSuccess = rebuildsMeshOnSuccess,
            .publicCommandId = publicCommandId,
            .registersPublicCommand = registersPublicCommand,
    };
}

[[nodiscard]] CommandToolDescriptor previewDescriptor(
        const quader_host_contract::HostActionDescriptor& action,
        bool rebuildsMeshOnSuccess = true) {
    assert(action.kind == quader_host_contract::HostActionKind::operation);
    return {
            .id = action.id,
            .operationId = action.operation_id,
            .label = action.label,
            .kind = CommandToolKind::Preview,
            .previewKind = ModelingActionCatalog::previewKindFromHost(action.preview_kind),
            .rebuildsMeshOnSuccess = rebuildsMeshOnSuccess,
    };
}

[[nodiscard]] bool instantOperationCatalogOwnsActionId(std::string_view actionId) {
    const std::span<const ModelingInstantOperationCatalogEntry> entries = modelingInstantOperationCatalog();
    return std::any_of(entries.begin(), entries.end(), [&](const ModelingInstantOperationCatalogEntry& entry) {
        return entry.id == actionId;
    });
}

[[nodiscard]] bool commandToolOwnsHostActionId(std::string_view actionId) {
    return actionId == std::string_view("delete_selection") || instantOperationCatalogOwnsActionId(actionId);
}

[[nodiscard]] CommandToolDescriptor contextDescriptorFromHostAction(
        const quader_host_contract::HostActionDescriptor& action) {
    if (action.id == std::string_view("apply_active_material")) {
        return contextDescriptor(
                action,
                true,
                "edit.apply_material",
                false);
    }
    return contextDescriptor(action);
}

} // namespace

std::span<const InstantCommandTool* const> modelingInstantOperationCommandTools() {
    static const std::vector<std::unique_ptr<CatalogInstantOperationTool>> ownedTools = [] {
        std::vector<std::unique_ptr<CatalogInstantOperationTool>> result;
        for (const ModelingInstantOperationCatalogEntry& entry : modelingInstantOperationCatalog()) {
            result.push_back(std::make_unique<CatalogInstantOperationTool>(
                    instantDescriptor(entry),
                    entry.operation));
        }
        return result;
    }();

    static const std::vector<const InstantCommandTool*> tools = [] {
        std::vector<const InstantCommandTool*> result;
        result.reserve(ownedTools.size());
        for (const std::unique_ptr<CatalogInstantOperationTool>& tool : ownedTools) {
            result.push_back(tool.get());
        }
        return result;
    }();
    return {tools.data(), tools.size()};
}

std::span<const InstantCommandTool* const> modelingServiceInstantCommandTools() {
    static const SelectAllTool selectAll;
    static const InvertSelectionTool invertSelection;
    static const ClearSelectionTool clearSelection;
    static const DeleteSelectionTool deleteSelection;
    static const CopySelectionTool copySelection;
    static const PasteSelectionTool pasteSelection;
    static const RepeatLastActionTool repeatLastAction;
    static const AssignMaterialTool assignMaterial;
    static const MirrorSelectionTool mirrorX{ModelingMirrorAxis::X, "mirror_selection_x", "Mirror X"};
    static const MirrorSelectionTool mirrorY{ModelingMirrorAxis::Y, "mirror_selection_y", "Mirror Y"};
    static const MirrorSelectionTool mirrorZ{ModelingMirrorAxis::Z, "mirror_selection_z", "Mirror Z"};
    static const FlipHorizontalTool flipHorizontal;
    static const FlipVerticalTool flipVertical;

    static const std::array<const InstantCommandTool*, 13> tools{{
            &selectAll,
            &invertSelection,
            &clearSelection,
            &deleteSelection,
            &copySelection,
            &pasteSelection,
            &repeatLastAction,
            &assignMaterial,
            &mirrorX,
            &mirrorY,
            &mirrorZ,
            &flipHorizontal,
            &flipVertical,
    }};
    return {tools.data(), tools.size()};
}

std::span<const InstantCommandTool* const> modelingInstantCommandTools() {
    static const std::vector<const InstantCommandTool*> tools = [] {
        std::vector<const InstantCommandTool*> result;
        for (const InstantCommandTool* tool : modelingInstantOperationCommandTools()) {
            result.push_back(tool);
        }
        for (const InstantCommandTool* tool : modelingServiceInstantCommandTools()) {
            result.push_back(tool);
        }
        return result;
    }();
    return {tools.data(), tools.size()};
}

std::span<const ContextActionTool* const> modelingContextActionTools() {
    static const std::vector<std::unique_ptr<CatalogContextActionTool>> ownedTools = [] {
        std::vector<std::unique_ptr<CatalogContextActionTool>> result;
        for (const quader_host_contract::HostActionDescriptor& action : quader_host_contract::action_catalog()) {
            if (action.kind != quader_host_contract::HostActionKind::operation ||
                    action.preview_kind != quader_host_contract::HostPreviewKind::none ||
                    commandToolOwnsHostActionId(action.id)) {
                continue;
            }
            result.push_back(std::make_unique<CatalogContextActionTool>(
                    contextDescriptorFromHostAction(action)));
        }
        return result;
    }();

    static const std::vector<const ContextActionTool*> tools = [] {
        std::vector<const ContextActionTool*> result;
        result.reserve(ownedTools.size());
        for (const std::unique_ptr<CatalogContextActionTool>& tool : ownedTools) {
            result.push_back(tool.get());
        }
        return result;
    }();
    return {tools.data(), tools.size()};
}

std::span<const PreviewCommandTool* const> modelingPreviewCommandTools() {
    static const std::vector<std::unique_ptr<CatalogPreviewActionTool>> ownedTools = [] {
        std::vector<std::unique_ptr<CatalogPreviewActionTool>> result;
        for (const quader_host_contract::HostActionDescriptor& action : quader_host_contract::action_catalog()) {
            if (action.kind != quader_host_contract::HostActionKind::operation ||
                    action.preview_kind == quader_host_contract::HostPreviewKind::none) {
                continue;
            }
            result.push_back(std::make_unique<CatalogPreviewActionTool>(previewDescriptor(action)));
        }
        return result;
    }();

    static const std::vector<const PreviewCommandTool*> tools = [] {
        std::vector<const PreviewCommandTool*> result;
        result.reserve(ownedTools.size());
        for (const std::unique_ptr<CatalogPreviewActionTool>& tool : ownedTools) {
            result.push_back(tool.get());
        }
        return result;
    }();
    return {tools.data(), tools.size()};
}

std::span<const CommandTool* const> modelingCommandTools() {
    static const std::vector<const CommandTool*> tools = [] {
        std::vector<const CommandTool*> result;
        for (const InstantCommandTool* tool : modelingInstantCommandTools()) {
            result.push_back(tool);
        }
        for (const ContextActionTool* tool : modelingContextActionTools()) {
            result.push_back(tool);
        }
        for (const PreviewCommandTool* tool : modelingPreviewCommandTools()) {
            result.push_back(tool);
        }
        return result;
    }();
    return {tools.data(), tools.size()};
}

const CommandTool* findModelingCommandTool(std::string_view id) {
    const std::span<const CommandTool* const> tools = modelingCommandTools();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const CommandTool* tool) {
        return tool != nullptr && tool->id() == id;
    });
    return found == tools.end() ? nullptr : *found;
}

const InstantCommandTool* findModelingInstantCommandTool(ModelingInstantOperationId operation) {
    const std::span<const InstantCommandTool* const> tools = modelingInstantOperationCommandTools();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const InstantCommandTool* tool) {
        return tool != nullptr && tool->hasInstantOperation() && tool->operation() == operation;
    });
    return found == tools.end() ? nullptr : *found;
}

const ContextActionTool* findModelingContextActionTool(std::string_view actionId) {
    const std::span<const ContextActionTool* const> tools = modelingContextActionTools();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const ContextActionTool* tool) {
        return tool != nullptr && tool->actionId() == actionId;
    });
    return found == tools.end() ? nullptr : *found;
}

const PreviewCommandTool* findModelingPreviewCommandTool(std::string_view actionId) {
    const std::span<const PreviewCommandTool* const> tools = modelingPreviewCommandTools();
    const auto found = std::find_if(tools.begin(), tools.end(), [&](const PreviewCommandTool* tool) {
        return tool != nullptr && tool->actionId() == actionId;
    });
    return found == tools.end() ? nullptr : *found;
}

std::string_view modelingMirrorSelectionToolId(ModelingMirrorAxis axis) {
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
