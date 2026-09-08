////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_modeling_tool_commit_adapter.hpp"

#include "api/quader_tools_api.hpp"

namespace quader {

ViewportModelingToolCommitAdapter::ViewportModelingToolCommitAdapter(
    QuaderToolsApi &tools)
    : tools_(tools) {}

ModelingCommandResult
ViewportModelingToolCommitAdapter::commit(ViewportToolId tool) {
  const QuaderApiVoidResult result = tools_.commit_modeling_tool(tool);
  return {.ok = result.ok(),
          .changed = result.changed(),
          .message = result.error_message()};
}

ModelingCommandResult
ViewportModelingToolCommitAdapter::commit_insert_edge_loop_drag() {
  return commit(ViewportToolId::InsertEdgeLoop);
}

ModelingCommandResult ViewportModelingToolCommitAdapter::commit_knife_tool() {
  return commit(ViewportToolId::Knife);
}

ModelingCommandResult ViewportModelingToolCommitAdapter::commit_poly_tool() {
  return commit(ViewportToolId::Poly);
}

ModelingCommandResult
ViewportModelingToolCommitAdapter::finalize_drawn_plane_tool(
    ModelingViewportTool tool) {
  switch (tool) {
  case ModelingViewportTool::Cut:
    return commit(ViewportToolId::Cut);
  case ModelingViewportTool::Mirror:
    return commit(ViewportToolId::Mirror);
  default:
    return {.ok = false, .message = "Unsupported drawn-plane viewport tool."};
  }
}

} // namespace quader
