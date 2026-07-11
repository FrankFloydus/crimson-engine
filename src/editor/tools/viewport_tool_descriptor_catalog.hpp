////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "viewport/viewport_input_types.hpp"

#include <span>
#include <string_view>

namespace quader {

/**
 * Enumerates ToolKind values used by editor-owned viewport tool interactions.
 */
enum class ToolKind {
    Selection,
    Transform,
    Modal,
};

/**
 * Enumerates ToolMenuGroup values used by editor-owned viewport tool interactions.
 */
enum class ToolMenuGroup {
    Navigation,
    Modeling,
};

/**
 * Stores the Tool Descriptor data contract used by editor-owned viewport tool interactions.
 */
struct ToolDescriptor {
    ViewportToolId tool = ViewportToolId::Select;
    ModelingViewportTool modeling_tool = ModelingViewportTool::Select;
    std::string_view id = "select";
    std::string_view label = "Select";
    std::string_view shortcut;
    ToolKind kind = ToolKind::Selection;
    ToolMenuGroup menu_group = ToolMenuGroup::Navigation;
    ModelingTransformTool transform_tool = ModelingTransformTool::Move;
    bool owns_viewport = false;
    bool blocks_viewport_focus_shortcuts = false;
    bool allows_selection_fallback = false;
    bool supports_transform_gizmo = false;
    bool editor_owned_native_lifecycle = false;
};

[[nodiscard]] std::span<const ToolDescriptor> viewport_tool_descriptors();
[[nodiscard]] const ToolDescriptor *
find_viewport_tool_descriptor(ViewportToolId tool);
[[nodiscard]] const ToolDescriptor *
find_viewport_tool_descriptor(ModelingViewportTool tool);
[[nodiscard]] const ToolDescriptor *
find_viewport_tool_descriptor(std::string_view id);
[[nodiscard]] ToolDescriptor viewport_tool_descriptor(ViewportToolId tool);

} // namespace quader
