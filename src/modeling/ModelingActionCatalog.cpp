#include "modeling/ModelingActionCatalog.h"

#include "modeling/ModelingSessionService.h"

#include <algorithm>
#include <sstream>
#include <utility>

#if QUADER_HAS_MODELING_SDK
#include <quader_host_contract/host_contract.h>
#endif

namespace quader {
namespace {

ModelingActionItem editAction(
        std::string actionId,
        std::string operationId,
        std::string label,
        ModelingActionVisibility visibility,
        std::string disabledReason = {}) {
    return {
            .actionId = std::move(actionId),
            .operationId = std::move(operationId),
            .label = std::move(label),
            .kind = ModelingActionKind::Operation,
            .visibility = visibility,
            .disabledReason = std::move(disabledReason),
            .rebuildsMeshOnSuccess = true,
    };
}

std::string countReason(std::string_view noun, std::uint32_t required) {
    std::ostringstream message;
    message << "Select at least " << required << ' ' << noun;
    if (required != 1U) {
        message << 's';
    }
    message << '.';
    return message.str();
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
                    summary.hasSelection ? std::string{} : "Select something to delete."),
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
                    summary.selectedObjectCount > 0U ? std::string{} : "Select at least 1 object."),
            query);
}

#if QUADER_HAS_MODELING_SDK

quader_host_contract::HostSelectionMode toHostMode(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return quader_host_contract::HostSelectionMode::vertex;
    case ModelingSelectionMode::Edge:
        return quader_host_contract::HostSelectionMode::edge;
    case ModelingSelectionMode::Face:
        return quader_host_contract::HostSelectionMode::face;
    case ModelingSelectionMode::Object:
        return quader_host_contract::HostSelectionMode::object;
    case ModelingSelectionMode::Group:
        return quader_host_contract::HostSelectionMode::object;
    }
    return quader_host_contract::HostSelectionMode::object;
}

quader_host_contract::HostActiveSelectionKind toHostActiveKind(ModelingActiveSelectionKind kind) {
    switch (kind) {
    case ModelingActiveSelectionKind::Object:
        return quader_host_contract::HostActiveSelectionKind::object;
    case ModelingActiveSelectionKind::Vertex:
        return quader_host_contract::HostActiveSelectionKind::vertex;
    case ModelingActiveSelectionKind::Edge:
        return quader_host_contract::HostActiveSelectionKind::edge;
    case ModelingActiveSelectionKind::Face:
        return quader_host_contract::HostActiveSelectionKind::face;
    case ModelingActiveSelectionKind::None:
    case ModelingActiveSelectionKind::Light:
        return quader_host_contract::HostActiveSelectionKind::none;
    }
    return quader_host_contract::HostActiveSelectionKind::none;
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
    const quader_host_contract::HostSelectionMode hostMode = toHostMode(mode);
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

ModelingActionVisibility evaluateAvailability(
        const ModelingSelectionSummary& summary,
        const quader_host_contract::HostActionAvailabilityDescriptor& availability,
        std::string& disabledReason) {
    if (availability.selection_mode != toHostMode(summary.mode)) {
        return ModelingActionVisibility::Hidden;
    }
    if (summary.selectedVertexCount < availability.minimum_selected_vertices) {
        disabledReason = countReason("vertex", availability.minimum_selected_vertices);
        return ModelingActionVisibility::Disabled;
    }
    if (summary.selectedEdgeCount < availability.minimum_selected_edges) {
        disabledReason = countReason("edge", availability.minimum_selected_edges);
        return ModelingActionVisibility::Disabled;
    }
    if (availability.exact_selected_edges != 0U &&
            summary.selectedEdgeCount != availability.exact_selected_edges) {
        std::ostringstream message;
        message << "Select exactly " << availability.exact_selected_edges << " edges.";
        disabledReason = message.str();
        return ModelingActionVisibility::Disabled;
    }
    if (summary.selectedFaceCount < availability.minimum_selected_faces) {
        disabledReason = countReason("face", availability.minimum_selected_faces);
        return ModelingActionVisibility::Disabled;
    }
    if (summary.selectedObjectCount < availability.minimum_selected_objects) {
        disabledReason = countReason("object", availability.minimum_selected_objects);
        return ModelingActionVisibility::Disabled;
    }
    if (availability.required_active_kind != quader_host_contract::HostActiveSelectionKind::none &&
            toHostActiveKind(summary.activeKind) != availability.required_active_kind) {
        disabledReason = "Set an active selection element.";
        return ModelingActionVisibility::Disabled;
    }
    if (availability.requires_single_object && summary.selectedObjectCount != 1U) {
        disabledReason = "Select elements from one object.";
        return ModelingActionVisibility::Disabled;
    }
    return ModelingActionVisibility::Enabled;
}

ModelingActionItem actionFromHostDescriptor(
        const ModelingSelectionSummary& summary,
        const quader_host_contract::HostActionDescriptor& descriptor) {
    std::string disabledReason;
    const ModelingActionVisibility visibility =
            evaluateAvailability(summary, descriptor.availability, disabledReason);
    return {
            .actionId = std::string(descriptor.id),
            .operationId = std::string(descriptor.operation_id),
            .toolId = std::string(descriptor.tool_id),
            .label = std::string(descriptor.label),
            .kind = fromHostKind(descriptor.kind),
            .previewKind = fromHostPreview(descriptor.preview_kind),
            .visibility = visibility,
            .disabledReason = std::move(disabledReason),
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

#endif

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

#if QUADER_HAS_MODELING_SDK
    if (query.surface == ModelingActionSurface::ViewportContextMenu) {
        ModelingActionPanel panel;
        panel.panelId = "viewport_context_menu";
        panel.label = "Context Actions";
        panel.selectionModeId = selectionModeId(toHostMode(summary.mode));
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

    const quader_host_contract::HostSelectionMode activeMode = toHostMode(summary.mode);
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
#else
    (void)summary;
#endif

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

bool ModelingActionCatalog::actionAvailable(
        const ModelingSelectionSummary& summary,
        std::string_view actionId) {
    const ModelingActionQuery query{
            .surface = ModelingActionSurface::ViewportContextMenu,
            .modalToolOwnsViewport = false,
            .includeDisabled = true,
            .includeHidden = false,
    };
    const std::vector<ModelingActionItem> actions = actionsFor(summary, query);
    for (const ModelingActionItem& action : actions) {
        if (action.actionId == actionId) {
            return action.visibility == ModelingActionVisibility::Enabled;
        }
    }
    return false;
}

} // namespace quader
