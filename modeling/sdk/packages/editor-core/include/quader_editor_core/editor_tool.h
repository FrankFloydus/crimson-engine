#pragma once

#include <quader_editor_core/editor_runtime.h>

#include <string_view>

namespace quader_editor_core {

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
[[nodiscard]] const EditorTool& extend_tool();
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
