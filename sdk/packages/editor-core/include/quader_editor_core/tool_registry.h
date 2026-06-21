#pragma once

#include <quader_editor_core/editor_tool.h>

#include <span>
#include <string_view>

namespace quader_editor_core {

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
