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

constexpr std::array<ToolDescriptor, 12> kViewportToolDescriptors{{
    {
        .tool = ViewportToolId::Select,
        .modeling_tool = ModelingViewportTool::Select,
        .id = "select",
        .label = "Select",
        .shortcut = "Q",
        .kind = ToolKind::Selection,
        .menu_group = ToolMenuGroup::Navigation,
        .allows_selection_fallback = true,
    },
    {
        .tool = ViewportToolId::Move,
        .modeling_tool = ModelingViewportTool::Move,
        .id = "move",
        .label = "Move",
        .shortcut = "W",
        .kind = ToolKind::Transform,
        .menu_group = ToolMenuGroup::Navigation,
        .transform_tool = ModelingTransformTool::Move,
        .allows_selection_fallback = true,
        .supports_transform_gizmo = true,
    },
    {
        .tool = ViewportToolId::Extend,
        .modeling_tool = ModelingViewportTool::Extend,
        .id = "extend",
        .label = "Extend",
        .kind = ToolKind::Transform,
        .menu_group = ToolMenuGroup::Modeling,
        .transform_tool = ModelingTransformTool::Move,
        .allows_selection_fallback = true,
    },
    {
        .tool = ViewportToolId::Extrude,
        .modeling_tool = ModelingViewportTool::Extrude,
        .id = "extrude",
        .label = "Extrude",
        .kind = ToolKind::Transform,
        .menu_group = ToolMenuGroup::Modeling,
        .transform_tool = ModelingTransformTool::Move,
        .allows_selection_fallback = true,
    },
    {
        .tool = ViewportToolId::Rotate,
        .modeling_tool = ModelingViewportTool::Rotate,
        .id = "rotate",
        .label = "Rotate",
        .shortcut = "R",
        .kind = ToolKind::Transform,
        .menu_group = ToolMenuGroup::Navigation,
        .transform_tool = ModelingTransformTool::Rotate,
        .allows_selection_fallback = true,
        .supports_transform_gizmo = true,
    },
    {
        .tool = ViewportToolId::Scale,
        .modeling_tool = ModelingViewportTool::Scale,
        .id = "scale",
        .label = "Scale",
        .shortcut = "S",
        .kind = ToolKind::Transform,
        .menu_group = ToolMenuGroup::Navigation,
        .transform_tool = ModelingTransformTool::Scale,
        .allows_selection_fallback = true,
        .supports_transform_gizmo = true,
    },
    {
        .tool = ViewportToolId::Box,
        .modeling_tool = ModelingViewportTool::Box,
        .id = "box",
        .label = "Box",
        .shortcut = "B",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
    },
    {
        .tool = ViewportToolId::InsertEdgeLoop,
        .modeling_tool = ModelingViewportTool::InsertEdgeLoop,
        .id = "insert_edge_loop",
        .label = "Insert Edge Loop",
        .shortcut = "Shift+R",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
    },
    {
        .tool = ViewportToolId::Knife,
        .modeling_tool = ModelingViewportTool::Knife,
        .id = "knife",
        .label = "Knife",
        .shortcut = "C",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
    },
    {
        .tool = ViewportToolId::Cut,
        .modeling_tool = ModelingViewportTool::Cut,
        .id = "cut",
        .label = "Cut",
        .shortcut = "Shift+C",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
    },
    {
        .tool = ViewportToolId::Poly,
        .modeling_tool = ModelingViewportTool::Poly,
        .id = "poly",
        .label = "Poly",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
    },
    {
        .tool = ViewportToolId::Mirror,
        .modeling_tool = ModelingViewportTool::Mirror,
        .id = "mirror",
        .label = "Mirror",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
    },
}};

} // namespace

std::span<const ToolDescriptor> viewport_tool_descriptors() {
  return kViewportToolDescriptors;
}

const ToolDescriptor *find_viewport_tool_descriptor(ViewportToolId tool) {
  const auto found = std::find_if(kViewportToolDescriptors.begin(),
                                  kViewportToolDescriptors.end(),
                                  [&](const ToolDescriptor &descriptor) {
                                    return descriptor.tool == tool;
                                  });
  return found == kViewportToolDescriptors.end() ? nullptr : &(*found);
}

const ToolDescriptor *find_viewport_tool_descriptor(ModelingViewportTool tool) {
  const auto found = std::find_if(kViewportToolDescriptors.begin(),
                                  kViewportToolDescriptors.end(),
                                  [&](const ToolDescriptor &descriptor) {
                                    return descriptor.modeling_tool == tool;
                                  });
  return found == kViewportToolDescriptors.end() ? nullptr : &(*found);
}

const ToolDescriptor *find_viewport_tool_descriptor(std::string_view id) {
  const auto found = std::find_if(
      kViewportToolDescriptors.begin(), kViewportToolDescriptors.end(),
      [&](const ToolDescriptor &descriptor) { return descriptor.id == id; });
  return found == kViewportToolDescriptors.end() ? nullptr : &(*found);
}

ToolDescriptor viewport_tool_descriptor(ViewportToolId tool) {
  const ToolDescriptor *descriptor = find_viewport_tool_descriptor(tool);
  return descriptor != nullptr ? *descriptor : ToolDescriptor{};
}

} // namespace quader
