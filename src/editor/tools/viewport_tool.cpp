////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/viewport_tool.hpp"
#include "editor/metadata/release_feature_requirements.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"

namespace quader {

Tool::Tool(ToolDescriptor descriptor) : descriptor_(descriptor) {}

Tool::~Tool() = default;

const ToolDescriptor& Tool::descriptor() const { return descriptor_; }

ViewportToolId Tool::viewport_tool() const { return descriptor_.tool; }

ModelingViewportTool Tool::modeling_tool() const {
  return descriptor_.modeling_tool;
}

std::string_view Tool::id() const { return descriptor_.id; }

std::string_view Tool::label() const { return descriptor_.label; }

std::string_view Tool::shortcut() const { return descriptor_.shortcut; }

ToolKind Tool::kind() const { return descriptor_.kind; }

ToolMenuGroup Tool::menu_group() const { return descriptor_.menu_group; }

bool Tool::available() const { return release_allows_tool(id()); }

bool Tool::owns_viewport() const { return descriptor_.owns_viewport; }

bool Tool::blocks_viewport_focus_shortcuts() const {
  return descriptor_.blocks_viewport_focus_shortcuts;
}

ModelingTransformTool Tool::transform_tool() const {
  return descriptor_.transform_tool;
}

bool Tool::supports_transform_gizmo() const {
  return descriptor_.supports_transform_gizmo;
}

ModelingCommandResult Tool::activate(ModelingSessionService& modeling) const {
  return modeling.activate_viewport_tool(modeling_tool());
}

ViewportTool::ViewportTool(ToolDescriptor descriptor) : Tool(descriptor) {}

ViewportTool::ViewportTool(ViewportToolId tool)
    : Tool(viewport_tool_descriptor(tool)) {}

ViewportTool::operator ViewportToolId() const { return viewport_tool(); }

bool ViewportTool::allows_selection_fallback() const {
  return descriptor().allows_selection_fallback;
}

ViewportPointerCursor ViewportTool::pointer_cursor() const {
  return ViewportPointerCursor::Default;
}

ViewportToolResult
ViewportTool::handle_event(ViewportToolContext &context,
                           const ViewportToolEvent &event) const {
  if (event.kind == ViewportToolEventKind::Activation) {
    return activate_by_id(context, event.tool);
  }
  return {};
}

ViewportToolResult ViewportTool::activate_by_id(ViewportToolContext &context,
                                                ViewportToolId tool) const {
  if (context.set_active_tool) {
    context.set_active_tool(tool);
  } else {
    context.state.active_tool = tool;
  }
  return {
      .handled = true,
      .active_tool_changed = true,
  };
}

} // namespace quader
