////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/editor_tool.h>

#include <span>
#include <string_view>

namespace quader_editor_core {

/**
 * Owns Tool Registry lookup and registration for the modeling runtime domain.
 */
class ToolRegistry final {
public:
	[[nodiscard]] std::span<const EditorTool* const> tools() const;
	[[nodiscard]] const EditorTool *find(ViewportInteractionTool tool) const;
	[[nodiscard]] const EditorTool *find(std::string_view id) const;
};

[[nodiscard]] const ToolRegistry& tool_registry();

[[nodiscard]] std::span<const EditorTool* const> editor_tool_registry();
[[nodiscard]] const EditorTool *editor_tool_for_kind(ViewportInteractionTool tool);
[[nodiscard]] const EditorTool *editor_tool_for_id(std::string_view id);

} // namespace quader_editor_core
