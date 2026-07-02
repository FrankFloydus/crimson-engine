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

ProjectSession &project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::project_session(session);
}

} // namespace

QuaderToolsApi::QuaderToolsApi(QuaderApiSession session)
    : session_(session) {}

void QuaderToolsApi::set_runtime_hooks(QuaderToolsRuntimeHooks hooks) {
  hooks_ = std::move(hooks);
}

QuaderApiVoidResult QuaderToolsApi::activate(std::string_view tool_id) {
  if (hooks_.activate) {
    return hooks_.activate(tool_id);
  }

  const ViewportTool *tool = find_viewport_tool(tool_id);
  if (tool == nullptr) {
    return quader_api_failure_status(project_session(session_),
                                     "Unknown viewport tool.");
  }
  ModelingCommandResult result =
      tool->activate(project_session(session_).modeling());
  return quader_api_void_status(project_session(session_), result.ok,
                                result.changed, std::move(result.message),
                                QuaderMutationKind::Modeling, tool_id);
}

QuaderApiVoidResult QuaderToolsApi::activate(ViewportToolId tool) {
  return activate(viewport_tool_catalog_id(tool));
}

QuaderApiVoidResult QuaderToolsApi::set_setting(std::string_view tool_id,
                                                std::string_view name,
                                                double value) {
  ModelingInteractionSettings settings =
      project_session(session_).modeling().interaction_settings();

  if (!apply_modeling_interaction_setting(settings, tool_id, name, value)) {
    return quader_api_failure_status(project_session(session_),
                                     "Unknown tool setting.");
  }
  ModelingCommandResult result =
      project_session(session_).modeling().set_interaction_settings(settings);
  return quader_api_void_status(project_session(session_), result.ok,
                                result.changed, std::move(result.message),
                                QuaderMutationKind::Modeling,
                                "Set Tool Setting");
}

QuaderApiVoidResult QuaderToolsApi::cancel_modal_tool() {
  if (hooks_.cancel_modal_tool) {
    return hooks_.cancel_modal_tool();
  }
  ModelingCommandResult result =
      project_session(session_).modeling().cancel_active_modal_tool();
  return quader_api_void_status(project_session(session_), result.ok,
                                result.changed, std::move(result.message),
                                QuaderMutationKind::Modeling,
                                "Cancel Modal Tool");
}

std::string QuaderToolsApi::active_tool() const {
  return std::string(viewport_tool_catalog_id(viewport_tool_for_modeling(
      project_session(session_).modeling().tool_status().active_tool)));
}

} // namespace quader
