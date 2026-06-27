////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"

#include <algorithm>
#include <array>

namespace quader {
namespace {

constexpr std::array<ToolDescriptor, 12> kViewportToolDescriptors {{
        {
                .tool = ViewportToolId::Select,
                .modelingTool = ModelingViewportTool::Select,
                .id = "select",
                .label = "Select",
                .shortcut = "Q",
                .kind = ToolKind::Selection,
                .menuGroup = ToolMenuGroup::Navigation,
                .allowsSelectionFallback = true,
        },
        {
                .tool = ViewportToolId::Move,
                .modelingTool = ModelingViewportTool::Move,
                .id = "move",
                .label = "Move",
                .shortcut = "W",
                .kind = ToolKind::Transform,
                .menuGroup = ToolMenuGroup::Navigation,
                .transformTool = ModelingTransformTool::Move,
                .allowsSelectionFallback = true,
                .supportsTransformGizmo = true,
        },
        {
                .tool = ViewportToolId::Extend,
                .modelingTool = ModelingViewportTool::Extend,
                .id = "extend",
                .label = "Extend",
                .kind = ToolKind::Transform,
                .menuGroup = ToolMenuGroup::Modeling,
                .transformTool = ModelingTransformTool::Move,
                .allowsSelectionFallback = true,
        },
        {
                .tool = ViewportToolId::Extrude,
                .modelingTool = ModelingViewportTool::Extrude,
                .id = "extrude",
                .label = "Extrude",
                .kind = ToolKind::Transform,
                .menuGroup = ToolMenuGroup::Modeling,
                .transformTool = ModelingTransformTool::Move,
                .allowsSelectionFallback = true,
        },
        {
                .tool = ViewportToolId::Rotate,
                .modelingTool = ModelingViewportTool::Rotate,
                .id = "rotate",
                .label = "Rotate",
                .shortcut = "R",
                .kind = ToolKind::Transform,
                .menuGroup = ToolMenuGroup::Navigation,
                .transformTool = ModelingTransformTool::Rotate,
                .allowsSelectionFallback = true,
                .supportsTransformGizmo = true,
        },
        {
                .tool = ViewportToolId::Scale,
                .modelingTool = ModelingViewportTool::Scale,
                .id = "scale",
                .label = "Scale",
                .shortcut = "S",
                .kind = ToolKind::Transform,
                .menuGroup = ToolMenuGroup::Navigation,
                .transformTool = ModelingTransformTool::Scale,
                .allowsSelectionFallback = true,
                .supportsTransformGizmo = true,
        },
        {
                .tool = ViewportToolId::Box,
                .modelingTool = ModelingViewportTool::Box,
                .id = "box",
                .label = "Box",
                .shortcut = "B",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
        },
        {
                .tool = ViewportToolId::InsertEdgeLoop,
                .modelingTool = ModelingViewportTool::InsertEdgeLoop,
                .id = "insert_edge_loop",
                .label = "Insert Edge Loop",
                .shortcut = "Shift+R",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
        },
        {
                .tool = ViewportToolId::Knife,
                .modelingTool = ModelingViewportTool::Knife,
                .id = "knife",
                .label = "Knife",
                .shortcut = "C",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
                .blocksViewportFocusShortcuts = true,
        },
        {
                .tool = ViewportToolId::Cut,
                .modelingTool = ModelingViewportTool::Cut,
                .id = "cut",
                .label = "Cut",
                .shortcut = "Shift+C",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
                .blocksViewportFocusShortcuts = true,
        },
        {
                .tool = ViewportToolId::Poly,
                .modelingTool = ModelingViewportTool::Poly,
                .id = "poly",
                .label = "Poly",
                .shortcut = "P",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
                .blocksViewportFocusShortcuts = true,
        },
        {
                .tool = ViewportToolId::Mirror,
                .modelingTool = ModelingViewportTool::Mirror,
                .id = "mirror",
                .label = "Mirror",
                .kind = ToolKind::Modal,
                .menuGroup = ToolMenuGroup::Modeling,
                .ownsViewport = true,
                .blocksViewportFocusShortcuts = true,
        },
}};

} // namespace

std::span<const ToolDescriptor> viewportToolDescriptors()
{
    return kViewportToolDescriptors;
}

const ToolDescriptor* findViewportToolDescriptor(ViewportToolId tool)
{
    const auto found = std::find_if(kViewportToolDescriptors.begin(), kViewportToolDescriptors.end(),
            [&](const ToolDescriptor& descriptor) {
                return descriptor.tool == tool;
            });
    return found == kViewportToolDescriptors.end() ? nullptr : &(*found);
}

const ToolDescriptor* findViewportToolDescriptor(ModelingViewportTool tool)
{
    const auto found = std::find_if(kViewportToolDescriptors.begin(), kViewportToolDescriptors.end(),
            [&](const ToolDescriptor& descriptor) {
                return descriptor.modelingTool == tool;
            });
    return found == kViewportToolDescriptors.end() ? nullptr : &(*found);
}

const ToolDescriptor* findViewportToolDescriptor(std::string_view id)
{
    const auto found = std::find_if(kViewportToolDescriptors.begin(), kViewportToolDescriptors.end(),
            [&](const ToolDescriptor& descriptor) {
                return descriptor.id == id;
            });
    return found == kViewportToolDescriptors.end() ? nullptr : &(*found);
}

ToolDescriptor viewportToolDescriptor(ViewportToolId tool)
{
    const ToolDescriptor* descriptor = findViewportToolDescriptor(tool);
    return descriptor != nullptr ? *descriptor : ToolDescriptor{};
}

} // namespace quader
