////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/ViewportTool.h"

#include "editor/modeling/services/ModelingSessionService.h"

namespace quader {

Tool::Tool(ToolDescriptor descriptor) : descriptor_(descriptor) {}

Tool::~Tool() = default;

const ToolDescriptor& Tool::descriptor() const { return descriptor_; }

ViewportToolId Tool::viewportTool() const { return descriptor_.tool; }

ModelingViewportTool Tool::modelingTool() const { return descriptor_.modelingTool; }

std::string_view Tool::id() const { return descriptor_.id; }

std::string_view Tool::label() const { return descriptor_.label; }

std::string_view Tool::shortcut() const { return descriptor_.shortcut; }

ToolKind Tool::kind() const { return descriptor_.kind; }

ToolMenuGroup Tool::menuGroup() const { return descriptor_.menuGroup; }

bool Tool::available() const { return true; }

bool Tool::ownsViewport() const { return descriptor_.ownsViewport; }

bool Tool::blocksViewportFocusShortcuts() const { return descriptor_.blocksViewportFocusShortcuts; }

ModelingTransformTool Tool::transformTool() const { return descriptor_.transformTool; }

bool Tool::supportsTransformGizmo() const { return descriptor_.supportsTransformGizmo; }

ModelingCommandResult Tool::activate(ModelingSessionService& modeling) const {
    return modeling.activateViewportTool(modelingTool());
}

ViewportTool::ViewportTool(ToolDescriptor descriptor) : Tool(descriptor) {}

ViewportTool::ViewportTool(ViewportToolId tool) : Tool(viewportToolDescriptor(tool)) {}

ViewportTool::operator ViewportToolId() const { return viewportTool(); }

bool ViewportTool::allowsSelectionFallback() const {
    return descriptor().allowsSelectionFallback;
}

ViewportToolResult ViewportTool::handleEvent(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind == ViewportToolEventKind::Activation) {
        return activateById(context, event.tool);
    }
    return {};
}

ViewportToolResult ViewportTool::activateById(
        ViewportToolContext& context,
        ViewportToolId tool) const {
    if (context.setActiveTool) {
        context.setActiveTool(tool);
    } else {
        context.state.activeTool = tool;
    }
    return {
            .handled = true,
            .activeToolChanged = true,
    };
}

} // namespace quader
