////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/modeling_mode_shelf_controller.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/metadata/host_contract.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace quader {
namespace {

std::string modelingActionLabelKey(std::string_view actionId) {
    std::string key("modeling.action.");
    key.append(actionId);
    key.append(".label");
    return key;
}

std::string shelfLabelKeyForHostSelectionMode(quader_host_contract::HostSelectionMode mode) {
    std::string key("ui.modeling_shelf.tab.");
    key.append(hostSelectionModeId(mode));
    return key;
}

std::string_view shelfFallbackLabelForHostSelectionMode(
        const quader_host_contract::HostOperationPanelDescriptor& panel) {
    using quader_host_contract::HostSelectionMode;
    switch (panel.selection_mode) {
    case HostSelectionMode::object:
        return "Mesh";
    case HostSelectionMode::vertex:
        return "Vertices";
    case HostSelectionMode::edge:
        return "Edges";
    case HostSelectionMode::face:
        return "Faces";
    case HostSelectionMode::group:
        return panel.label;
    }
    return panel.label;
}

UIIconId shelfIconForHostSelectionMode(quader_host_contract::HostSelectionMode mode) {
    using quader_host_contract::HostSelectionMode;
    switch (mode) {
    case HostSelectionMode::object:
        return UIIconId::MeshSelection;
    case HostSelectionMode::vertex:
        return UIIconId::VertexSelection;
    case HostSelectionMode::edge:
        return UIIconId::EdgeSelection;
    case HostSelectionMode::face:
        return UIIconId::FaceSelection;
    case HostSelectionMode::group:
        return UIIconId::Mesh;
    }
    return UIIconId::Mesh;
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

ModelingActiveSelectionKind toModelingActiveSelectionKind(QuaderActiveSelectionKind kind) {
    switch (kind) {
    case QuaderActiveSelectionKind::Object:
        return ModelingActiveSelectionKind::Object;
    case QuaderActiveSelectionKind::Vertex:
        return ModelingActiveSelectionKind::Vertex;
    case QuaderActiveSelectionKind::Edge:
        return ModelingActiveSelectionKind::Edge;
    case QuaderActiveSelectionKind::Face:
        return ModelingActiveSelectionKind::Face;
    case QuaderActiveSelectionKind::Light:
        return ModelingActiveSelectionKind::Light;
    case QuaderActiveSelectionKind::None:
        return ModelingActiveSelectionKind::None;
    }
    return ModelingActiveSelectionKind::None;
}

ModelingSelectionSummary toModelingSelectionSummary(const QuaderSelectionSummary& summary) {
    return {
            .mode = toModelingSelectionMode(summary.mode),
            .hasSelection = summary.hasSelection,
            .activeKind = toModelingActiveSelectionKind(summary.activeKind),
            .activeObjectId = summary.activeObjectId,
            .activeVertexId = summary.activeVertexId,
            .activeEdgeA = summary.activeEdgeA,
            .activeEdgeB = summary.activeEdgeB,
            .activeFaceId = summary.activeFaceId,
            .selectedObjectCount = summary.selectedObjectCount,
            .selectedVertexCount = summary.selectedVertexCount,
            .selectedEdgeCount = summary.selectedEdgeCount,
            .selectedFaceCount = summary.selectedFaceCount,
            .contentRevision = summary.contentRevision,
            .selectionRevision = summary.selectionRevision,
    };
}

std::optional<ModelingModeShelfControl> controlFromOperationPanel(
        const quader_host_contract::HostOperationPanelDescriptor& panel,
        const LocalizationService& localization) {
    if (hostSelectionModeToApp(panel.selection_mode) == ModelingSelectionMode::Group) {
        return std::nullopt;
    }
    const std::string_view controlId = hostSelectionModeId(panel.selection_mode);
    if (controlId.empty()) {
        return std::nullopt;
    }
    return ModelingModeShelfControl{
            .id = std::string(controlId),
            .label = localization.text(
                    shelfLabelKeyForHostSelectionMode(panel.selection_mode),
                    shelfFallbackLabelForHostSelectionMode(panel)),
            .icon = shelfIconForHostSelectionMode(panel.selection_mode),
            .labelVisibility = ModelingModeShelfControlLabelVisibility::Hidden,
            .behavior = ModelingModeShelfControlBehavior::OpensTabContent,
    };
}

std::vector<const quader_host_contract::HostOperationPanelDescriptor*> operationPanelsForModelingShelf() {
    std::vector<const quader_host_contract::HostUiPlacementDescriptor*> placements;
    for (const quader_host_contract::HostUiPlacementDescriptor& placement :
            quader_host_contract::ui_placement_catalog()) {
        if (placement.surface_id == std::string_view("operations_top_bar") &&
                placement.target_kind == quader_host_contract::HostUiPlacementTargetKind::panel) {
            placements.push_back(&placement);
        }
    }
    std::stable_sort(placements.begin(), placements.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->order < rhs->order;
    });

    std::vector<const quader_host_contract::HostOperationPanelDescriptor*> panels;
    panels.reserve(placements.size());
    for (const quader_host_contract::HostUiPlacementDescriptor* placement : placements) {
        if (const quader_host_contract::HostOperationPanelDescriptor* panel =
                    quader_host_contract::find_operation_panel_descriptor(placement->target_id)) {
            panels.push_back(panel);
        }
    }
    return panels;
}

std::string commandIdForAction(const QuaderEditorApi& editor, const QuaderActionItem& action) {
    if (action.kind == QuaderActionKind::Tool && !action.toolId.empty()) {
        return editor.commands().commandIdForToolActivation(action.toolId);
    }
    return editor.commands().commandIdForModelingAction(action.actionId);
}

std::string localizedDisabledReason(
        const LocalizationService& localization,
        const QuaderActionItem& action) {
    if (action.disabledReasonKey.empty()) {
        return action.disabledReason;
    }
    if (action.disabledReasonCount != 0U) {
        const std::string count = std::to_string(action.disabledReasonCount);
        return localization.format(
                action.disabledReasonKey,
                action.disabledReasonKey,
                {{"count", count}});
    }
    return localization.text(action.disabledReasonKey, action.disabledReasonKey);
}

ModelingModeShelfCommand commandFromAction(
        const QuaderEditorApi& editor,
        const LocalizationService& localization,
        const QuaderActionItem& action,
        bool modalToolOwnsViewport) {
    const std::string commandId = commandIdForAction(editor, action);
    const bool actionDisabled = action.visibility != QuaderActionVisibility::Enabled;
    const bool disabled = actionDisabled || modalToolOwnsViewport;
    return ModelingModeShelfCommand{
            .id = action.actionId,
            .actionId = action.actionId,
            .commandId = commandId,
            .label = localization.text(modelingActionLabelKey(action.actionId), action.label),
            .secondaryText = editor.commands().shortcutFor(commandId),
            .disabledReason = actionDisabled ? localizedDisabledReason(localization, action) : modalToolOwnsViewport
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
    QuaderActionQuery query{
            .surface = QuaderActionSurface::OperationsPanel,
            .suppressWhenModalToolOwnsViewport = false,
            .includeDisabled = true,
            .includeHidden = false,
    };
    const bool modalToolOwnsViewport = editor.query().toolStatus().modalToolOwnsViewport;

    const QuaderActionSet actionSet = editor.modeling().availableActionSet(query);
    const auto activePanel = std::find_if(
            actionSet.panels.begin(),
            actionSet.panels.end(),
            [](const QuaderActionPanel& panel) {
                return panel.visibility == QuaderActionVisibility::Enabled;
            });
    if (activePanel == actionSet.panels.end()) {
        return {};
    }

    std::vector<ModelingModeShelfCommand> activeStripCommands;
    activeStripCommands.reserve(activePanel->actions.size());
    for (const QuaderActionItem& action : activePanel->actions) {
        if (action.visibility == QuaderActionVisibility::Hidden) {
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
    bool changed = syncSelectionSummary(toModelingSelectionSummary(editor.modeling().selectionSummary()));
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
    const std::vector<const quader_host_contract::HostOperationPanelDescriptor*> panels =
            operationPanelsForModelingShelf();
    result.reserve(panels.size());
    for (const quader_host_contract::HostOperationPanelDescriptor* panel : panels) {
        if (std::optional<ModelingModeShelfControl> control = controlFromOperationPanel(*panel, localization)) {
            result.push_back(std::move(*control));
        }
    }
    return result;
}

std::optional<std::string_view> ModelingModeShelfController::controlIdForSelectionMode(
        ModelingSelectionMode mode) const {
    for (const quader_host_contract::HostOperationPanelDescriptor* panel : operationPanelsForModelingShelf()) {
        if (hostSelectionModeToApp(panel->selection_mode) == mode) {
            return hostSelectionModeId(panel->selection_mode);
        }
    }
    return std::nullopt;
}

std::optional<ModelingSelectionMode> ModelingModeShelfController::selectionModeForControlId(
        std::string_view controlId) const {
    for (const quader_host_contract::HostOperationPanelDescriptor* panel : operationPanelsForModelingShelf()) {
        if (hostSelectionModeId(panel->selection_mode) == controlId) {
            return hostSelectionModeToApp(panel->selection_mode);
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
