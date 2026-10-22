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

constexpr std::array<ToolDescriptor, 21> kViewportToolDescriptors{{
    {
        .tool = ViewportToolId::Select,
        .modeling_tool = ModelingViewportTool::Select,
        .id = "select",
        .label = "Select",
        // Select remains remappable; Q opens the viewport marking menu.
        .shortcut = "",
        .kind = ToolKind::Selection,
        .menu_group = ToolMenuGroup::Navigation,
        .allows_selection_fallback = true,
    },
    {
        .tool = ViewportToolId::LassoSelect,
        .modeling_tool = ModelingViewportTool::LassoSelect,
        .id = "lasso_select",
        .label = "Lasso Select",
        .shortcut = "Shift+Q",
        .kind = ToolKind::Selection,
        .menu_group = ToolMenuGroup::Navigation,
        .allows_selection_fallback = true,
    },
    {
        .tool = ViewportToolId::PaintSelect,
        .modeling_tool = ModelingViewportTool::PaintSelect,
        .id = "paint_select",
        .label = "Paint Select",
        .shortcut = "Ctrl+Q",
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
        .tool = ViewportToolId::Extrude,
        .modeling_tool = ModelingViewportTool::Extrude,
        .id = "extrude",
        .label = "Extrude",
        .shortcut = "E",
        .kind = ToolKind::Transform,
        .menu_group = ToolMenuGroup::Modeling,
        .transform_tool = ModelingTransformTool::Move,
        .allows_selection_fallback = true,
        .editor_owned_native_lifecycle = true,
        .repeat_activation_when_active = true,
        .supported_editor_modes = EditorModeSupport::Edit,
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
        .tool = ViewportToolId::CreatePrimitive,
        .modeling_tool = ModelingViewportTool::Box,
        .id = "create_primitive",
        .label = "Create Primitive",
        .shortcut = "",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .editor_owned_native_lifecycle = true,
        .supported_editor_modes = EditorModeSupport::ObjectAndEdit,
    },
    {
        .tool = ViewportToolId::CreateStairs,
        .modeling_tool = ModelingViewportTool::Box,
        .id = "create_stairs",
        .label = "Stairs & Ramps",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .editor_owned_native_lifecycle = true,
        .supported_editor_modes = EditorModeSupport::ObjectAndEdit,
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
        .supported_editor_modes = EditorModeSupport::Edit,
        .required_component_kind = ModelingSelectionMode::Edge,
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
        .supported_editor_modes = EditorModeSupport::Edit,
        .required_component_kind = ModelingSelectionMode::Edge,
    },
    {
        .tool = ViewportToolId::Cut,
        .modeling_tool = ModelingViewportTool::Cut,
        .id = "cut",
        .label = "Cut",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
        .supported_editor_modes = EditorModeSupport::ObjectAndEdit,
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
        .supported_editor_modes = EditorModeSupport::Object,
    },
    {
        .tool = ViewportToolId::Mirror,
        .modeling_tool = ModelingViewportTool::Mirror,
        .id = "mirror",
        .label = "Mirror",
        .shortcut = "M",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
        .supported_editor_modes = EditorModeSupport::Object,
    },
    {
        .tool = ViewportToolId::UvPeel,
        .modeling_tool = ModelingViewportTool::Select,
        .id = "uv_peel",
        .label = "UV Peel",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .allows_selection_fallback = true,
        .editor_owned_native_lifecycle = true,
        .supported_editor_modes = EditorModeSupport::Edit,
        .required_component_kind = ModelingSelectionMode::Edge,
    },
    {
        .tool = ViewportToolId::Pivot,
        .modeling_tool = ModelingViewportTool::Select,
        .id = "pivot",
        .label = "Pivot",
        // Deliberately unassigned: P belongs to Parent to Active.
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Navigation,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
        .editor_owned_native_lifecycle = true,
        .supported_editor_modes = EditorModeSupport::ObjectAndEdit,
    },
    {
        .tool = ViewportToolId::LinearArray,
        .modeling_tool = ModelingViewportTool::Select,
        .id = "linear_array",
        .label = "Linear Array",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
        .editor_owned_native_lifecycle = true,
        .supported_editor_modes = EditorModeSupport::Object,
    },
    {
        .tool = ViewportToolId::RadialArray,
        .modeling_tool = ModelingViewportTool::Select,
        .id = "radial_array",
        .label = "Radial Array",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
        .editor_owned_native_lifecycle = true,
        .supported_editor_modes = EditorModeSupport::Object,
    },
    {
        .tool = ViewportToolId::PathArray,
        .modeling_tool = ModelingViewportTool::Select,
        .id = "path_array",
        .label = "Path Array",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
        .editor_owned_native_lifecycle = true,
        .supported_editor_modes = EditorModeSupport::Object,
    },
    {
        .tool = ViewportToolId::Curve,
        .modeling_tool = ModelingViewportTool::Select,
        .id = "curve",
        .label = "Curve",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .owns_viewport = true,
        .blocks_viewport_focus_shortcuts = true,
        .editor_owned_native_lifecycle = true,
        .supported_editor_modes = EditorModeSupport::Object,
    },
    {
        .tool = ViewportToolId::Align,
        .modeling_tool = ModelingViewportTool::Select,
        .id = "align",
        .label = "Align Tool",
        .kind = ToolKind::Modal,
        .menu_group = ToolMenuGroup::Modeling,
        .blocks_viewport_focus_shortcuts = true,
        .allows_selection_fallback = true,
        .editor_owned_native_lifecycle = true,
        .supported_editor_modes = EditorModeSupport::Object,
    },
}};

} // namespace

std::span<const ToolDescriptor> viewport_tool_descriptors() {
  return kViewportToolDescriptors;
}

const char *viewport_tool_id(ViewportToolId tool) {
  const ToolDescriptor *descriptor = find_viewport_tool_descriptor(tool);
  return descriptor != nullptr ? descriptor->id.data() : "unknown";
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
  if (tool == ModelingViewportTool::Cylinder ||
      tool == ModelingViewportTool::Sphere) {
    tool = ModelingViewportTool::Box;
  }
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

bool viewport_tool_supports_editor_mode(ViewportToolId tool, EditorMode mode) {
  const auto *descriptor = find_viewport_tool_descriptor(tool);
  return descriptor != nullptr &&
         editor_mode_supported(descriptor->supported_editor_modes, mode);
}

bool viewport_tool_supports_editor_mode(ModelingViewportTool tool, EditorMode mode) {
  const auto *descriptor = find_viewport_tool_descriptor(tool);
  return descriptor != nullptr &&
         editor_mode_supported(descriptor->supported_editor_modes, mode);
}

bool viewport_tool_supports_editor_mode(std::string_view tool_id, EditorMode mode) {
  const auto *descriptor = find_viewport_tool_descriptor(tool_id);
  return descriptor != nullptr &&
         editor_mode_supported(descriptor->supported_editor_modes, mode);
}

} // namespace quader
