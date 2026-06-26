////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderToolsApi.h"
#include "api/QuaderApiSessionInternalAccess.h"

#include "scene/document/ProjectSession.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/tools/ViewportToolCatalog.h"

#include <string>
#include <utility>

namespace quader {
namespace {

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
}


bool sameTool(std::string_view left, std::string_view right) {
    return left.empty() || left == right;
}

} // namespace

QuaderToolsApi::QuaderToolsApi(QuaderApiSession session)
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
    ModelingCommandResult result = tool->activate(projectSession(session_).modeling());
    return statusFrom(result.ok, result.changed, std::move(result.message), toolId);
}

QuaderApiVoidResult QuaderToolsApi::activate(ViewportToolId tool) {
    return activate(viewportToolCatalogId(tool));
}

QuaderApiVoidResult QuaderToolsApi::setSetting(std::string_view toolId, std::string_view name, double value) {
    ModelingInteractionSettings settings = projectSession(session_).modeling().interactionSettings();
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
    ModelingCommandResult result = projectSession(session_).modeling().setInteractionSettings(settings);
    return statusFrom(result.ok, result.changed, std::move(result.message), "Set Tool Setting");
}

QuaderApiVoidResult QuaderToolsApi::cancelModalTool() {
    if (hooks_.cancelModalTool) {
        return hooks_.cancelModalTool();
    }
    ModelingCommandResult result = projectSession(session_).modeling().cancelActiveModalTool();
    return statusFrom(result.ok, result.changed, std::move(result.message), "Cancel Modal Tool");
}

std::string QuaderToolsApi::activeTool() const {
    return std::string(viewportToolCatalogId(viewportToolForModeling(projectSession(session_).modeling().toolStatus().activeTool)));
}

QuaderRevisionStamp QuaderToolsApi::currentRevisions() const {
    const ModelingSelectionSummary summary = projectSession(session_).modeling().selectionSummary();
    return {
            .projectRevision = projectSession(session_).document().revision(),
            .sceneRevision = projectSession(session_).scene().revision(),
            .assetRevision = projectSession(session_).assets().revision(),
            .modelingContentRevision = summary.contentRevision,
            .modelingSelectionRevision = summary.selectionRevision,
    };
}

QuaderApiVoidResult QuaderToolsApi::statusFrom(
        bool ok,
        bool changed,
        std::string message,
        std::string_view label) const {
    return {
            .ok = ok,
            .changed = changed,
            .message = message.empty() ? std::string(label) : std::move(message),
            .mutationKind = ok && changed ? QuaderMutationKind::Modeling : QuaderMutationKind::None,
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
