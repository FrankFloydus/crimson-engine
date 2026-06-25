////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/ModelingModeShelfController.h"

#include "api/QuaderEditorApi.h"
#include "core/LocalizationService.h"

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace quader {
namespace {

struct ModelingModeShelfModeDescriptor {
    std::string_view controlId;
    ModelingSelectionMode selectionMode = ModelingSelectionMode::Object;
    std::string_view labelKey;
    std::string_view fallbackLabel;
    UIIconId icon = UIIconId::Mesh;
};

std::span<const ModelingModeShelfModeDescriptor> modelingModeShelfModeDescriptors() {
    static constexpr std::array descriptors{
            ModelingModeShelfModeDescriptor{
                    .controlId = "vertex",
                    .selectionMode = ModelingSelectionMode::Vertex,
                    .labelKey = "ui.modeling_shelf.tab.vertex",
                    .fallbackLabel = "Vertices",
                    .icon = UIIconId::VertexSelection,
            },
            ModelingModeShelfModeDescriptor{
                    .controlId = "edge",
                    .selectionMode = ModelingSelectionMode::Edge,
                    .labelKey = "ui.modeling_shelf.tab.edge",
                    .fallbackLabel = "Edges",
                    .icon = UIIconId::EdgeSelection,
            },
            ModelingModeShelfModeDescriptor{
                    .controlId = "face",
                    .selectionMode = ModelingSelectionMode::Face,
                    .labelKey = "ui.modeling_shelf.tab.face",
                    .fallbackLabel = "Faces",
                    .icon = UIIconId::FaceSelection,
            },
            ModelingModeShelfModeDescriptor{
                    .controlId = "object",
                    .selectionMode = ModelingSelectionMode::Object,
                    .labelKey = "ui.modeling_shelf.tab.object",
                    .fallbackLabel = "Mesh",
                    .icon = UIIconId::MeshSelection,
            },
    };
    return descriptors;
}

std::string modelingActionLabelKey(std::string_view actionId) {
    std::string key("modeling.action.");
    key.append(actionId);
    key.append(".label");
    return key;
}

bool stringListsEqual(const std::vector<std::string>& lhs, const std::vector<std::string>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

bool commandsEqual(
        const std::vector<ModelingModeShelfCommand>& lhs,
        const std::vector<ModelingModeShelfCommand>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](const auto& left, const auto& right) {
        return left.id == right.id &&
                left.actionId == right.actionId &&
                left.commandId == right.commandId &&
                left.label == right.label &&
                left.secondaryText == right.secondaryText &&
                left.disabledReason == right.disabledReason &&
                left.icon == right.icon &&
                left.disabled == right.disabled;
    });
}

bool selectionSummariesEqual(const ModelingSelectionSummary& lhs, const ModelingSelectionSummary& rhs) {
    return lhs.mode == rhs.mode &&
            lhs.hasSelection == rhs.hasSelection &&
            lhs.activeKind == rhs.activeKind &&
            lhs.activeObjectId == rhs.activeObjectId &&
            lhs.activeVertexId == rhs.activeVertexId &&
            lhs.activeEdgeA == rhs.activeEdgeA &&
            lhs.activeEdgeB == rhs.activeEdgeB &&
            lhs.activeFaceId == rhs.activeFaceId &&
            lhs.selectedObjectCount == rhs.selectedObjectCount &&
            lhs.selectedVertexCount == rhs.selectedVertexCount &&
            lhs.selectedEdgeCount == rhs.selectedEdgeCount &&
            lhs.selectedFaceCount == rhs.selectedFaceCount &&
            lhs.contentRevision == rhs.contentRevision &&
            lhs.selectionRevision == rhs.selectionRevision;
}

ModelingModeShelfControl controlFromDescriptor(
        const ModelingModeShelfModeDescriptor& descriptor,
        const LocalizationService& localization) {
    return ModelingModeShelfControl{
            .id = std::string(descriptor.controlId),
            .label = localization.text(descriptor.labelKey, descriptor.fallbackLabel),
            .icon = descriptor.icon,
            .labelVisibility = ModelingModeShelfControlLabelVisibility::Hidden,
            .behavior = ModelingModeShelfControlBehavior::OpensTabContent,
    };
}

std::string commandIdForAction(const QuaderEditorApi& editor, const ModelingActionItem& action) {
    if (action.kind == ModelingActionKind::Tool && !action.toolId.empty()) {
        return editor.commands().commandIdForToolActivation(action.toolId);
    }
    return editor.commands().commandIdForModelingAction(action.actionId);
}

ModelingModeShelfCommand commandFromAction(
        const QuaderEditorApi& editor,
        const LocalizationService& localization,
        const ModelingActionItem& action,
        bool modalToolOwnsViewport) {
    const std::string commandId = commandIdForAction(editor, action);
    const bool actionDisabled = action.visibility != ModelingActionVisibility::Enabled;
    const bool disabled = actionDisabled || modalToolOwnsViewport;
    return ModelingModeShelfCommand{
            .id = action.actionId,
            .actionId = action.actionId,
            .commandId = commandId,
            .label = localization.text(modelingActionLabelKey(action.actionId), action.label),
            .secondaryText = editor.commands().shortcutFor(commandId),
            .disabledReason = actionDisabled ? action.disabledReason : modalToolOwnsViewport
                    ? localization.text(
                            "ui.modeling_shelf.command.disabled.modal_tool",
                            "Finish active tool before running this command.")
                    : std::string(),
            .disabled = disabled,
    };
}

std::vector<ModelingModeShelfCommand> commandsFromEditor(
        const QuaderEditorApi& editor,
        const LocalizationService& localization) {
    ModelingActionQuery query{
            .surface = ModelingActionSurface::OperationsPanel,
            .suppressWhenModalToolOwnsViewport = false,
            .includeDisabled = true,
            .includeHidden = false,
    };
    const bool modalToolOwnsViewport = editor.query().toolStatus().modalToolOwnsViewport;

    const ModelingActionSet actionSet = editor.modeling().availableActionSet(query);
    const auto activePanel = std::find_if(
            actionSet.panels.begin(),
            actionSet.panels.end(),
            [](const ModelingActionPanel& panel) {
                return panel.visibility == ModelingActionVisibility::Enabled;
            });
    if (activePanel == actionSet.panels.end()) {
        return {};
    }

    std::vector<ModelingModeShelfCommand> activeStripCommands;
    activeStripCommands.reserve(activePanel->actions.size());
    for (const ModelingActionItem& action : activePanel->actions) {
        if (action.visibility == ModelingActionVisibility::Hidden) {
            continue;
        }
        activeStripCommands.push_back(commandFromAction(editor, localization, action, modalToolOwnsViewport));
    }
    return activeStripCommands;
}

} // namespace

ModelingModeShelfState ModelingModeShelfController::buildState(
        const LocalizationService& localization) const {
    std::vector<ModelingModeShelfCommand> activeStripCommands = activeStripCommands_;

    return ModelingModeShelfState{
            .controls = controls(localization),
            .overflowControlIds = overflowControlIds_,
            .activeStripCommands = std::move(activeStripCommands),
            .activeTabControlId = activeControlId(),
            .overflowButtonLabel = localization.text("ui.modeling_shelf.more", "More"),
            .overflowMenuOpen = overflowMenuOpen_,
    };
}

bool ModelingModeShelfController::syncEditorState(
        const QuaderEditorApi& editor,
        const LocalizationService& localization) {
    bool changed = syncSelectionSummary(editor.modeling().selectionSummary());
    std::vector<ModelingModeShelfCommand> commands = commandsFromEditor(editor, localization);
    if (!commandsEqual(activeStripCommands_, commands)) {
        activeStripCommands_ = std::move(commands);
        changed = true;
    }
    return changed;
}

std::vector<ModelingModeShelfControl> ModelingModeShelfController::controls(
        const LocalizationService& localization) const {
    std::vector<ModelingModeShelfControl> result;
    const std::span<const ModelingModeShelfModeDescriptor> descriptors = modelingModeShelfModeDescriptors();
    result.reserve(descriptors.size());
    for (const ModelingModeShelfModeDescriptor& descriptor : descriptors) {
        result.push_back(controlFromDescriptor(descriptor, localization));
    }
    return result;
}

std::optional<std::string_view> ModelingModeShelfController::controlIdForSelectionMode(
        ModelingSelectionMode mode) const {
    for (const ModelingModeShelfModeDescriptor& descriptor : modelingModeShelfModeDescriptors()) {
        if (descriptor.selectionMode == mode) {
            return descriptor.controlId;
        }
    }
    return std::nullopt;
}

std::optional<ModelingSelectionMode> ModelingModeShelfController::selectionModeForControlId(
        std::string_view controlId) const {
    for (const ModelingModeShelfModeDescriptor& descriptor : modelingModeShelfModeDescriptors()) {
        if (descriptor.controlId == controlId) {
            return descriptor.selectionMode;
        }
    }
    return std::nullopt;
}

std::optional<std::string> ModelingModeShelfController::activeControlId() const {
    if (const std::optional<std::string_view> controlId = controlIdForSelectionMode(selectionSummary_.mode)) {
        return std::string(*controlId);
    }
    return std::nullopt;
}

bool ModelingModeShelfController::setOverflowControlIds(std::vector<std::string> controlIds) {
    if (stringListsEqual(controlIds, overflowControlIds_)) {
        return false;
    }
    overflowControlIds_ = std::move(controlIds);
    if (overflowControlIds_.empty()) {
        overflowMenuOpen_ = false;
    }
    return true;
}

bool ModelingModeShelfController::activateControl(std::string_view controlId) {
    if (controlId == modelingModeShelfOverflowToggleControlId()) {
        if (overflowControlIds_.empty()) {
            overflowMenuOpen_ = false;
            return false;
        }
        overflowMenuOpen_ = !overflowMenuOpen_;
        return true;
    }

    const std::optional<ModelingSelectionMode> selectionMode = selectionModeForControlId(controlId);
    if (!selectionMode) {
        return false;
    }

    overflowMenuOpen_ = false;
    pendingSelectionMode_ = *selectionMode;
    return true;
}

void ModelingModeShelfController::activateCommand(std::string_view commandId) {
    if (!commandId.empty()) {
        pendingCommandId_ = std::string(commandId);
    }
    overflowMenuOpen_ = false;
}

bool ModelingModeShelfController::syncSelectionSummary(ModelingSelectionSummary summary) {
    if (selectionSummariesEqual(selectionSummary_, summary)) {
        return false;
    }
    selectionSummary_ = summary;
    return true;
}

std::optional<ModelingSelectionMode> ModelingModeShelfController::takePendingSelectionMode() {
    std::optional<ModelingSelectionMode> mode = pendingSelectionMode_;
    pendingSelectionMode_.reset();
    return mode;
}

std::optional<std::string> ModelingModeShelfController::takePendingCommandId() {
    std::optional<std::string> commandId = std::move(pendingCommandId_);
    pendingCommandId_.reset();
    return commandId;
}

} // namespace quader
