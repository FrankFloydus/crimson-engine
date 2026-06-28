////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_tools_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_status_internal.hpp"
#include "scene/document/project_session.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"

#include <string>
#include <utility>

namespace quader {
namespace {

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
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
        return quaderApiFailureStatus(projectSession(session_), "Unknown viewport tool.");
    }
    ModelingCommandResult result = tool->activate(projectSession(session_).modeling());
    return quaderApiVoidStatus(
            projectSession(session_),
            result.ok,
            result.changed,
            std::move(result.message),
            QuaderMutationKind::Modeling,
            toolId);
}

QuaderApiVoidResult QuaderToolsApi::activate(ViewportToolId tool) {
    return activate(viewportToolCatalogId(tool));
}

QuaderApiVoidResult QuaderToolsApi::setSetting(std::string_view toolId, std::string_view name, double value) {
    ModelingInteractionSettings settings = projectSession(session_).modeling().interactionSettings();

    if (!applyModelingInteractionSetting(settings, toolId, name, value)) {
        return quaderApiFailureStatus(projectSession(session_), "Unknown tool setting.");
    }
    ModelingCommandResult result = projectSession(session_).modeling().setInteractionSettings(settings);
    return quaderApiVoidStatus(
            projectSession(session_),
            result.ok,
            result.changed,
            std::move(result.message),
            QuaderMutationKind::Modeling,
            "Set Tool Setting");
}

QuaderApiVoidResult QuaderToolsApi::cancelModalTool() {
    if (hooks_.cancelModalTool) {
        return hooks_.cancelModalTool();
    }
    ModelingCommandResult result = projectSession(session_).modeling().cancelActiveModalTool();
    return quaderApiVoidStatus(
            projectSession(session_),
            result.ok,
            result.changed,
            std::move(result.message),
            QuaderMutationKind::Modeling,
            "Cancel Modal Tool");
}

std::string QuaderToolsApi::activeTool() const {
    return std::string(viewportToolCatalogId(viewportToolForModeling(projectSession(session_).modeling().toolStatus().activeTool)));
}

} // namespace quader
