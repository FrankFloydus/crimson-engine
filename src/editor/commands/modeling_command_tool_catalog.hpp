////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/context_action_tool.hpp"
#include "editor/commands/instant_command_tool.hpp"
#include "editor/commands/preview_command_tool.hpp"

#include <span>
#include <string_view>

namespace quader {

[[nodiscard]] std::span<const InstantCommandTool *const>
modeling_instant_operation_command_tools();
[[nodiscard]] std::span<const InstantCommandTool *const>
modeling_service_instant_command_tools();
[[nodiscard]] std::span<const InstantCommandTool *const>
modeling_instant_command_tools();
[[nodiscard]] std::span<const ContextActionTool *const>
modeling_context_action_tools();
[[nodiscard]] std::span<const PreviewCommandTool *const>
modeling_preview_command_tools();
[[nodiscard]] std::span<const CommandTool *const> modeling_command_tools();

[[nodiscard]] const CommandTool *
find_modeling_command_tool(std::string_view id);
[[nodiscard]] const InstantCommandTool *
find_modeling_instant_command_tool(ModelingInstantOperationId operation);
[[nodiscard]] const ContextActionTool *
find_modeling_context_action_tool(std::string_view action_id);
[[nodiscard]] const PreviewCommandTool *
find_modeling_preview_command_tool(std::string_view action_id);
[[nodiscard]] std::string_view
modeling_mirror_selection_tool_id(ModelingMirrorAxis axis);

} // namespace quader
