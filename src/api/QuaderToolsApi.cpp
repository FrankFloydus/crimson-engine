////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderToolsApi.h"

#include "scene/document/ProjectSession.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/tools/ViewportToolCatalog.h"

#include <string>
#include <utility>

namespace quader {
namespace {

bool sameTool(std::string_view left, std::string_view right) {
    return left.empty() || left == right;
}

} // namespace

QuaderToolsApi::QuaderToolsApi(ProjectSession& session)
    : session_(session) {}

void QuaderToolsApi::setRuntimeHooks(QuaderToolsRuntimeHooks hooks) {
    hooks_ = std::move(hooks);
}

QuaderApiVoidResult QuaderToolsApi::activate(std::string_view toolId) {
    if (hooks_.activate) {
        return hooks_.activate(toolId);
    }

    const ViewportTool* tool = findViewportTool(toolId);
    if (tool == nullptr) {
        return failure("Unknown viewport tool.");
    }
    return statusFrom(tool->activate(session_.modeling()), toolId);
}

QuaderApiVoidResult QuaderToolsApi::activate(ViewportToolId tool) {
    return activate(viewportToolCatalogId(tool));
}

QuaderApiVoidResult QuaderToolsApi::setSetting(std::string_view toolId, std::string_view name, double value) {
    ModelingInteractionSettings settings = session_.modeling().interactionSettings();
    bool handled = false;

    if (sameTool(toolId, "bridge") && (name == "steps" || name == "bridge_steps")) {
        settings.bridgeSteps = static_cast<int>(value);
        handled = true;
    } else if (sameTool(toolId, "edge_bevel") && (name == "amount" || name == "width" || name == "edge_bevel_width")) {
        settings.edgeBevelWidth = value;
        handled = true;
    } else if (sameTool(toolId, "edge_bevel") && (name == "profile" || name == "edge_bevel_profile")) {
        settings.edgeBevelProfile = value;
        handled = true;
    } else if (sameTool(toolId, "edge_bevel") && (name == "segments" || name == "edge_bevel_segments")) {
        settings.edgeBevelSegments = static_cast<int>(value);
        handled = true;
    } else if (sameTool(toolId, "thicken") && (name == "thickness" || name == "amount")) {
        settings.thickenThickness = value;
        handled = true;
    } else if (sameTool(toolId, "slice_quad") && name == "x_slices") {
        settings.sliceQuadXSlices = static_cast<int>(value);
        handled = true;
    } else if (sameTool(toolId, "slice_quad") && name == "y_slices") {
        settings.sliceQuadYSlices = static_cast<int>(value);
        handled = true;
    } else if (sameTool(toolId, "merge_by_distance") && (name == "tolerance" || name == "distance")) {
        settings.mergeByDistanceTolerance = value;
        handled = true;
    }

    if (!handled) {
        return failure("Unknown tool setting.");
    }
    return statusFrom(session_.modeling().setInteractionSettings(settings), "Set Tool Setting");
}

QuaderApiVoidResult QuaderToolsApi::cancelModalTool() {
    if (hooks_.cancelModalTool) {
        return hooks_.cancelModalTool();
    }
    return statusFrom(session_.modeling().cancelActiveModalTool(), "Cancel Modal Tool");
}

std::string QuaderToolsApi::activeTool() const {
    return std::string(viewportToolCatalogId(viewportToolForModeling(session_.modeling().toolStatus().activeTool)));
}

QuaderRevisionStamp QuaderToolsApi::currentRevisions() const {
    const ModelingSelectionSummary summary = session_.modeling().selectionSummary();
    return {
            .projectRevision = session_.document().revision(),
            .sceneRevision = session_.scene().revision(),
            .assetRevision = session_.assets().revision(),
            .modelingContentRevision = summary.contentRevision,
            .modelingSelectionRevision = summary.selectionRevision,
    };
}

QuaderApiVoidResult QuaderToolsApi::statusFrom(ModelingCommandResult result, std::string_view label) const {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = result.message.empty() ? std::string(label) : std::move(result.message),
            .mutationKind = result.ok && result.changed ? QuaderMutationKind::Modeling : QuaderMutationKind::None,
            .revisions = currentRevisions(),
    };
}

QuaderApiVoidResult QuaderToolsApi::failure(std::string message) const {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
            .revisions = currentRevisions(),
    };
}

} // namespace quader
