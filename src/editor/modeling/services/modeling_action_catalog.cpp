////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_action_catalog.hpp"

#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"

#include <algorithm>
#include <utility>

#include <editor/metadata/host_contract.hpp>

namespace quader {
namespace {

ModelingActionItem editAction(
        std::string actionId,
        std::string operationId,
        std::string label,
        ModelingActionVisibility visibility,
        std::string disabledReasonKey = {},
        std::uint32_t disabledReasonCount = 0) {
    return {
            .actionId = std::move(actionId),
            .operationId = std::move(operationId),
            .label = std::move(label),
            .kind = ModelingActionKind::Operation,
            .visibility = visibility,
            .disabledReasonKey = std::move(disabledReasonKey),
            .disabledReasonCount = disabledReasonCount,
            .rebuildsMeshOnSuccess = true,
    };
}

std::string minimumCountReasonKey(std::string_view noun, std::uint32_t required) {
    std::string key("modeling.action.disabled.minimum_");
    key.append(noun);
    if (required != 1U) {
        key.push_back('s');
    }
    return key;
}

ModelingActionAvailabilityResult disabledReason(std::string key, std::uint32_t count = 0) {
    return {
            .visibility = ModelingActionVisibility::Disabled,
            .disabledReasonKey = std::move(key),
            .disabledReasonCount = count,
    };
}

std::string surfaceIdFor(ModelingActionSurface surface) {
    switch (surface) {
    case ModelingActionSurface::ViewportContextMenu:
        return "viewport_context_menu";
    case ModelingActionSurface::OperationsPanel:
        return "operations_top_bar";
    }
    return {};
}

void appendIfVisible(
        std::vector<ModelingActionItem>& actions,
        ModelingActionItem item,
        const ModelingActionQuery& query) {
    if (item.visibility == ModelingActionVisibility::Hidden && !query.includeHidden) {
        return;
    }
    if (item.visibility == ModelingActionVisibility::Disabled && !query.includeDisabled) {
        return;
    }
    actions.push_back(std::move(item));
}

void appendEditActions(
        std::vector<ModelingActionItem>& actions,
        const ModelingSelectionSummary& summary,
        const ModelingActionQuery& query) {
    appendIfVisible(
            actions,
            editAction(
                    "delete_selection",
                    "delete_selection",
                    "Delete",
                    summary.hasSelection ? ModelingActionVisibility::Enabled : ModelingActionVisibility::Disabled,
                    summary.hasSelection ? std::string{} : "modeling.action.disabled.delete_selection"),
            query);

    const bool objectMode = summary.mode == ModelingSelectionMode::Object;
    appendIfVisible(
            actions,
            editAction(
                    "duplicate_meshes",
                    "duplicate_meshes",
                    "Duplicate",
                    objectMode ? (summary.selectedObjectCount > 0U ? ModelingActionVisibility::Enabled : ModelingActionVisibility::Disabled)
                               : ModelingActionVisibility::Hidden,
                    summary.selectedObjectCount > 0U ? std::string{} : "modeling.action.disabled.minimum_object",
                    1),
            query);
}


ModelingActiveSelectionKind fromHostActiveKind(quader_host_contract::HostActiveSelectionKind kind) {
    switch (kind) {
    case quader_host_contract::HostActiveSelectionKind::object:
        return ModelingActiveSelectionKind::Object;
    case quader_host_contract::HostActiveSelectionKind::vertex:
        return ModelingActiveSelectionKind::Vertex;
    case quader_host_contract::HostActiveSelectionKind::edge:
        return ModelingActiveSelectionKind::Edge;
    case quader_host_contract::HostActiveSelectionKind::face:
        return ModelingActiveSelectionKind::Face;
    case quader_host_contract::HostActiveSelectionKind::none:
        return ModelingActiveSelectionKind::None;
    }
    return ModelingActiveSelectionKind::None;
}

ModelingSelectionMode fromHostSelectionMode(quader_host_contract::HostSelectionMode mode) {
    switch (mode) {
    case quader_host_contract::HostSelectionMode::vertex:
        return ModelingSelectionMode::Vertex;
    case quader_host_contract::HostSelectionMode::edge:
        return ModelingSelectionMode::Edge;
    case quader_host_contract::HostSelectionMode::face:
        return ModelingSelectionMode::Face;
    case quader_host_contract::HostSelectionMode::object:
        return ModelingSelectionMode::Object;
    case quader_host_contract::HostSelectionMode::group:
        return ModelingSelectionMode::Group;
    }
    return ModelingSelectionMode::Object;
}

ModelingActionKind fromHostKind(quader_host_contract::HostActionKind kind) {
    return kind == quader_host_contract::HostActionKind::tool
            ? ModelingActionKind::Tool
            : ModelingActionKind::Operation;
}

ModelingActionPreviewKind fromHostPreview(quader_host_contract::HostPreviewKind kind) {
    switch (kind) {
    case quader_host_contract::HostPreviewKind::bridge:
        return ModelingActionPreviewKind::Bridge;
    case quader_host_contract::HostPreviewKind::edge_bevel:
        return ModelingActionPreviewKind::EdgeBevel;
    case quader_host_contract::HostPreviewKind::thicken:
        return ModelingActionPreviewKind::Thicken;
    case quader_host_contract::HostPreviewKind::slice_quad:
        return ModelingActionPreviewKind::SliceQuad;
    case quader_host_contract::HostPreviewKind::merge_by_distance:
        return ModelingActionPreviewKind::MergeByDistance;
    case quader_host_contract::HostPreviewKind::none:
        return ModelingActionPreviewKind::None;
    }
    return ModelingActionPreviewKind::None;
}

const quader_host_contract::HostOperationPanelDescriptor* panelForMode(ModelingSelectionMode mode) {
    const quader_host_contract::HostSelectionMode hostMode = appSelectionModeToHostContract(mode);
    for (const quader_host_contract::HostOperationPanelDescriptor& panel : quader_host_contract::operation_panel_catalog()) {
        if (panel.selection_mode == hostMode) {
            return &panel;
        }
    }
    return nullptr;
}

std::string selectionModeId(quader_host_contract::HostSelectionMode mode) {
    switch (mode) {
    case quader_host_contract::HostSelectionMode::vertex:
        return "vertex";
    case quader_host_contract::HostSelectionMode::edge:
        return "edge";
    case quader_host_contract::HostSelectionMode::face:
        return "face";
    case quader_host_contract::HostSelectionMode::object:
        return "object";
    case quader_host_contract::HostSelectionMode::group:
        return "group";
    }
    return {};
}

bool selectionRequirementSatisfied(
        const ModelingSelectionSummary& summary,
        const ModelingActionAvailabilityRule& rule) {
    switch (rule.requirement) {
    case ModelingActionSelectionRequirement::AnySelection:
        return summary.hasSelection;
    case ModelingActionSelectionRequirement::SelectionMode:
        return summary.mode == rule.mode;
    case ModelingActionSelectionRequirement::NonObjectSelection:
        return summary.hasSelection && summary.mode != ModelingSelectionMode::Object;
    case ModelingActionSelectionRequirement::ObjectOrFaceSelection:
        return (summary.mode == ModelingSelectionMode::Object && summary.hasSelection) ||
                (summary.mode == ModelingSelectionMode::Face && summary.selectedFaceCount > 0U);
    case ModelingActionSelectionRequirement::EdgeOrFaceSelection:
        return (summary.mode == ModelingSelectionMode::Edge && summary.selectedEdgeCount > 0U) ||
                (summary.mode == ModelingSelectionMode::Face && summary.selectedFaceCount > 0U);
    }
    return false;
}

ModelingActionAvailabilityRule ruleFromHostAvailability(
        const quader_host_contract::HostActionAvailabilityDescriptor& availability) {
    return {
            .requirement = ModelingActionSelectionRequirement::SelectionMode,
            .mode = fromHostSelectionMode(availability.selection_mode),
            .minimumSelectedVertices = availability.minimum_selected_vertices,
            .minimumSelectedEdges = availability.minimum_selected_edges,
            .exactSelectedEdges = availability.exact_selected_edges,
            .minimumSelectedFaces = availability.minimum_selected_faces,
            .minimumSelectedObjects = availability.minimum_selected_objects,
            .requiredActiveKind = fromHostActiveKind(availability.required_active_kind),
            .requiresSingleObject = availability.requires_single_object,
    };
}

ModelingActionAvailabilityResult evaluateAvailabilityRule(
        const ModelingSelectionSummary& summary,
        const ModelingActionAvailabilityRule& rule) {
    ModelingActionAvailabilityResult result;
    if (!selectionRequirementSatisfied(summary, rule)) {
        result.visibility = rule.requirement == ModelingActionSelectionRequirement::SelectionMode
                ? ModelingActionVisibility::Hidden
                : ModelingActionVisibility::Disabled;
        if (result.visibility == ModelingActionVisibility::Disabled) {
            result.disabledReasonKey = "modeling.action.disabled.any_selection";
        }
        return result;
    }
    if (summary.selectedVertexCount < rule.minimumSelectedVertices) {
        return disabledReason(minimumCountReasonKey("vertex", rule.minimumSelectedVertices), rule.minimumSelectedVertices);
    }
    if (summary.selectedEdgeCount < rule.minimumSelectedEdges) {
        return disabledReason(minimumCountReasonKey("edge", rule.minimumSelectedEdges), rule.minimumSelectedEdges);
    }
    if (rule.exactSelectedEdges != 0U &&
            summary.selectedEdgeCount != rule.exactSelectedEdges) {
        return disabledReason("modeling.action.disabled.exact_edges", rule.exactSelectedEdges);
    }
    if (summary.selectedFaceCount < rule.minimumSelectedFaces) {
        return disabledReason(minimumCountReasonKey("face", rule.minimumSelectedFaces), rule.minimumSelectedFaces);
    }
    if (summary.selectedObjectCount < rule.minimumSelectedObjects) {
        return disabledReason(minimumCountReasonKey("object", rule.minimumSelectedObjects), rule.minimumSelectedObjects);
    }
    if (rule.requiredActiveKind != ModelingActiveSelectionKind::None &&
            summary.activeKind != rule.requiredActiveKind) {
        return disabledReason("modeling.action.disabled.active_element");
    }
    if (rule.requiresSingleObject && summary.selectedObjectCount != 1U) {
        return disabledReason("modeling.action.disabled.single_object");
    }
    result.visibility = ModelingActionVisibility::Enabled;
    return result;
}

std::optional<ModelingActionAvailabilityResult> evaluateHostOperationAvailability(
        const ModelingSelectionSummary& summary,
        std::string_view operationId,
        ModelingSelectionMode mode) {
    const quader_host_contract::HostSelectionMode hostMode = appSelectionModeToHostContract(mode);
    std::optional<ModelingActionAvailabilityResult> firstUnavailable;
    for (const quader_host_contract::HostActionDescriptor& descriptor :
            quader_host_contract::action_catalog()) {
        if (descriptor.kind != quader_host_contract::HostActionKind::operation ||
                descriptor.operation_id != operationId ||
                descriptor.availability.selection_mode != hostMode) {
            continue;
        }

        ModelingActionAvailabilityResult result =
                evaluateAvailabilityRule(summary, ruleFromHostAvailability(descriptor.availability));
        if (result.visibility == ModelingActionVisibility::Enabled) {
            return result;
        }
        if (!firstUnavailable.has_value() ||
                (firstUnavailable->visibility == ModelingActionVisibility::Hidden &&
                 result.visibility == ModelingActionVisibility::Disabled)) {
            firstUnavailable = std::move(result);
        }
    }
    if (firstUnavailable.has_value()) {
        return firstUnavailable;
    }
    return std::nullopt;
}

ModelingActionItem actionFromHostDescriptor(
        const ModelingSelectionSummary& summary,
        const quader_host_contract::HostActionDescriptor& descriptor) {
    ModelingActionAvailabilityResult availability =
            evaluateAvailabilityRule(summary, ruleFromHostAvailability(descriptor.availability));
    return {
            .actionId = std::string(descriptor.id),
            .operationId = std::string(descriptor.operation_id),
            .toolId = std::string(descriptor.tool_id),
            .label = std::string(descriptor.label),
            .kind = fromHostKind(descriptor.kind),
            .previewKind = fromHostPreview(descriptor.preview_kind),
            .visibility = availability.visibility,
            .disabledReason = std::move(availability.disabledReason),
            .disabledReasonKey = std::move(availability.disabledReasonKey),
            .disabledReasonCount = availability.disabledReasonCount,
            .rebuildsMeshOnSuccess = descriptor.kind == quader_host_contract::HostActionKind::operation,
    };
}

void appendHostPanelActions(
        std::vector<ModelingActionItem>& actions,
        const quader_host_contract::HostOperationPanelDescriptor& panel,
        const ModelingSelectionSummary& summary,
        const ModelingActionQuery& query) {
    for (std::string_view actionId : panel.action_ids) {
        const quader_host_contract::HostActionDescriptor* descriptor =
                quader_host_contract::find_action_descriptor(actionId);
        if (descriptor == nullptr || !descriptor->show_in_operations_panel) {
            continue;
        }
        appendIfVisible(actions, actionFromHostDescriptor(summary, *descriptor), query);
    }
}


} // namespace

ModelingActionSet ModelingActionCatalog::actionSetFor(
        const ModelingSelectionSummary& summary,
        const ModelingActionQuery& query) {
    ModelingActionSet result;
    result.surface = query.surface;
    result.surfaceId = surfaceIdFor(query.surface);
    if (query.modalToolOwnsViewport) {
        return result;
    }

    if (query.surface == ModelingActionSurface::ViewportContextMenu) {
        ModelingActionPanel panel;
        panel.panelId = "viewport_context_menu";
        panel.label = "Context Actions";
        panel.selectionModeId = selectionModeId(appSelectionModeToHostContract(summary.mode));
        panel.visibility = ModelingActionVisibility::Enabled;

        if (const quader_host_contract::HostOperationPanelDescriptor* hostPanel = panelForMode(summary.mode)) {
            appendHostPanelActions(panel.actions, *hostPanel, summary, query);
        }
        appendEditActions(panel.actions, summary, query);
        if (!panel.actions.empty()) {
            result.panels.push_back(std::move(panel));
        }
        return result;
    }

    const quader_host_contract::HostSelectionMode activeMode = appSelectionModeToHostContract(summary.mode);
    for (const quader_host_contract::HostOperationPanelDescriptor& hostPanel :
            quader_host_contract::operation_panel_catalog()) {
        const bool activePanel = hostPanel.selection_mode == activeMode;
        if (!activePanel && !query.includeHidden) {
            continue;
        }

        ModelingActionPanel panel;
        panel.panelId = std::string(hostPanel.id);
        panel.label = std::string(hostPanel.label);
        panel.selectionModeId = selectionModeId(hostPanel.selection_mode);
        panel.visibility = activePanel ? ModelingActionVisibility::Enabled : ModelingActionVisibility::Hidden;
        appendHostPanelActions(panel.actions, hostPanel, summary, query);

        if (!panel.actions.empty() || query.includeHidden) {
            result.panels.push_back(std::move(panel));
        }
    }

    return result;
}

std::vector<ModelingActionItem> ModelingActionCatalog::actionsFor(
        const ModelingSelectionSummary& summary,
        const ModelingActionQuery& query) {
    std::vector<ModelingActionItem> actions;
    const ModelingActionSet actionSet = actionSetFor(summary, query);
    for (const ModelingActionPanel& panel : actionSet.panels) {
        actions.insert(actions.end(), panel.actions.begin(), panel.actions.end());
    }
    return actions;
}

std::optional<ModelingActionItem> ModelingActionCatalog::actionFor(
        const ModelingSelectionSummary& summary,
        std::string_view actionId) {
    if (const quader_host_contract::HostActionDescriptor* descriptor =
                quader_host_contract::find_action_descriptor(actionId)) {
        return actionFromHostDescriptor(summary, *descriptor);
    }

    std::vector<ModelingActionItem> editActions;
    appendEditActions(
            editActions,
            summary,
            {
                    .surface = ModelingActionSurface::ViewportContextMenu,
                    .includeDisabled = true,
                    .includeHidden = true,
            });
    const auto found = std::find_if(editActions.begin(), editActions.end(), [&](const ModelingActionItem& action) {
        return action.actionId == actionId;
    });
    return found == editActions.end() ? std::nullopt : std::optional<ModelingActionItem>(*found);
}

bool ModelingActionCatalog::actionAvailable(
        const ModelingSelectionSummary& summary,
        std::string_view actionId) {
    const std::optional<ModelingActionItem> action = actionFor(summary, actionId);
    return action.has_value() && action->visibility == ModelingActionVisibility::Enabled;
}

ModelingActionAvailabilityResult ModelingActionCatalog::evaluateAvailability(
        const ModelingSelectionSummary& summary,
        const ModelingActionAvailabilityRule& rule) {
    return evaluateAvailabilityRule(summary, rule);
}

ModelingActionAvailabilityResult ModelingActionCatalog::evaluateOperationAvailability(
        const ModelingSelectionSummary& summary,
        std::string_view operationId,
        const ModelingActionAvailabilityRule& rule) {
    if (std::optional<ModelingActionAvailabilityResult> hostResult =
                evaluateHostOperationAvailability(summary, operationId, summary.mode)) {
        return *hostResult;
    }
    return evaluateAvailabilityRule(summary, rule);
}

} // namespace quader
