////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/ViewportTool.h"

#include "editor/modeling/services/ModelingSessionService.h"

namespace quader {
namespace {

ToolDescriptor descriptorFor(ViewportToolId tool) {
    switch (tool) {
    case ViewportToolId::Select:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Select,
                .id = "select",
                .label = "Select",
                .kind = ToolKind::Selection,
                .menuGroup = ToolMenuGroup::Navigation,
        };
    case ViewportToolId::Move:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Move,
                .id = "move",
                .label = "Move",
                .kind = ToolKind::Transform,
                .menuGroup = ToolMenuGroup::Navigation,
        };
    case ViewportToolId::Extend:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Extend,
                .id = "extend",
                .label = "Extend",
                .kind = ToolKind::Transform,
                .menuGroup = ToolMenuGroup::Modeling,
        };
    case ViewportToolId::Extrude:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Extrude,
                .id = "extrude",
                .label = "Extrude",
                .kind = ToolKind::Transform,
                .menuGroup = ToolMenuGroup::Modeling,
        };
    case ViewportToolId::Rotate:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Rotate,
                .id = "rotate",
                .label = "Rotate",
                .kind = ToolKind::Transform,
                .menuGroup = ToolMenuGroup::Navigation,
        };
    case ViewportToolId::Scale:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Scale,
                .id = "scale",
                .label = "Scale",
                .kind = ToolKind::Transform,
                .menuGroup = ToolMenuGroup::Navigation,
        };
    case ViewportToolId::Box:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Box,
                .id = "box",
                .label = "Box",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
        };
    case ViewportToolId::InsertEdgeLoop:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::InsertEdgeLoop,
                .id = "insert_edge_loop",
                .label = "Insert Edge Loop",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
        };
    case ViewportToolId::Knife:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Knife,
                .id = "knife",
                .label = "Knife",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
                .blocksViewportFocusShortcuts = true,
        };
    case ViewportToolId::Cut:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Cut,
                .id = "cut",
                .label = "Cut",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
                .blocksViewportFocusShortcuts = true,
        };
    case ViewportToolId::Poly:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Poly,
                .id = "poly",
                .label = "Poly",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
                .blocksViewportFocusShortcuts = true,
        };
    case ViewportToolId::Mirror:
        return {
                .tool = tool,
                .modelingTool = ModelingViewportTool::Mirror,
                .id = "mirror",
                .label = "Mirror",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
                .blocksViewportFocusShortcuts = true,
        };
    }
    return {};
}

} // namespace

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

ModelingTransformTool Tool::transformTool() const { return ModelingTransformTool::Move; }

ModelingCommandResult Tool::activate(ModelingSessionService& modeling) const {
    return modeling.activateViewportTool(modelingTool());
}

ViewportTool::ViewportTool(ToolDescriptor descriptor) : Tool(descriptor) {}

ViewportTool::ViewportTool(ViewportToolId tool) : Tool(descriptorFor(tool)) {}

ViewportTool::operator ViewportToolId() const { return viewportTool(); }

bool ViewportTool::allowsSelectionFallback() const {
    switch (viewportTool()) {
    case ViewportToolId::Select:
    case ViewportToolId::Move:
    case ViewportToolId::Extend:
    case ViewportToolId::Extrude:
    case ViewportToolId::Rotate:
    case ViewportToolId::Scale:
        return true;
    case ViewportToolId::Box:
    case ViewportToolId::InsertEdgeLoop:
    case ViewportToolId::Knife:
    case ViewportToolId::Cut:
    case ViewportToolId::Poly:
    case ViewportToolId::Mirror:
        return false;
    }
    return false;
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
