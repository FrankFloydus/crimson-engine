////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/interactions/tool_identity.hpp"
#include "editor/tools/viewport_tool.hpp"

#include <span>
#include <string_view>

namespace quader {

[[nodiscard]] std::span<const ViewportTool *const> viewport_tool_catalog();
[[nodiscard]] const ViewportTool *find_viewport_tool(ViewportToolId tool);
[[nodiscard]] const ViewportTool *find_viewport_tool(std::string_view id);
[[nodiscard]] bool viewport_tool_available(ViewportToolId tool);
[[nodiscard]] std::string_view viewport_tool_catalog_id(ViewportToolId tool);
[[nodiscard]] std::string_view viewport_tool_label(ViewportToolId tool);
[[nodiscard]] bool viewport_tool_owns_viewport(ViewportToolId tool);
[[nodiscard]] bool viewport_tool_blocks_focus_shortcuts(ViewportToolId tool);
[[nodiscard]] bool viewport_tool_supports_transform_gizmo(ViewportToolId tool);
[[nodiscard]] ModelingTransformTool
viewport_tool_transform_tool(ViewportToolId tool);
[[nodiscard]] quader_editor_core::ViewportInteractionTool
viewport_tool_interaction_tool(ViewportToolId tool);
[[nodiscard]] ModelingViewportTool
modeling_viewport_tool_for(ViewportToolId tool);
[[nodiscard]] ViewportToolId
viewport_tool_for_modeling(ModelingViewportTool tool);

} // namespace quader
