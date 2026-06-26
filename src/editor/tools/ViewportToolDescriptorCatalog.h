////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/ModelingTypes.h"
#include "viewport/ViewportInputTypes.h"

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
    ModelingViewportTool modelingTool = ModelingViewportTool::Select;
    std::string_view id = "select";
    std::string_view label = "Select";
    std::string_view shortcut;
    ToolKind kind = ToolKind::Selection;
    ToolMenuGroup menuGroup = ToolMenuGroup::Navigation;
    ModelingTransformTool transformTool = ModelingTransformTool::Move;
    bool ownsViewport = false;
    bool blocksViewportFocusShortcuts = false;
    bool allowsSelectionFallback = false;
    bool supportsTransformGizmo = false;
};

[[nodiscard]] std::span<const ToolDescriptor> viewportToolDescriptors();
[[nodiscard]] const ToolDescriptor* findViewportToolDescriptor(ViewportToolId tool);
[[nodiscard]] const ToolDescriptor* findViewportToolDescriptor(ModelingViewportTool tool);
[[nodiscard]] const ToolDescriptor* findViewportToolDescriptor(std::string_view id);
[[nodiscard]] ToolDescriptor viewportToolDescriptor(ViewportToolId tool);

} // namespace quader
