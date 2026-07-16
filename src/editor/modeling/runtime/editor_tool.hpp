////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/editor_runtime.hpp>

#include <string_view>

namespace quader_editor_core {

/**
 * Implements Editor Tool behavior for the modeling runtime domain.
 */
class EditorTool {
public:
	virtual ~EditorTool() = default;

	[[nodiscard]] virtual ViewportInteractionTool tool_id() const = 0;
	[[nodiscard]] virtual std::string_view id() const = 0;
	[[nodiscard]] virtual std::string_view label() const = 0;
	virtual void reset_runtime_state(EditorRuntimeState& runtime, bool keep_active_when_current_tool = false) const = 0;
};

[[nodiscard]] const EditorTool& select_tool();
[[nodiscard]] const EditorTool& move_tool();
[[nodiscard]] const EditorTool& extrude_tool();
[[nodiscard]] const EditorTool& rotate_tool();
[[nodiscard]] const EditorTool& scale_tool();
[[nodiscard]] const EditorTool& box_tool();
[[nodiscard]] const EditorTool& insert_edge_loop_tool();
[[nodiscard]] const EditorTool& knife_tool();
[[nodiscard]] const EditorTool& cut_tool();
[[nodiscard]] const EditorTool& mirror_tool();
[[nodiscard]] const EditorTool& poly_tool();

} // namespace quader_editor_core
