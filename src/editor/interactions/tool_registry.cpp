////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/runtime/tool_registry.h>

#include <array>

namespace quader_editor_core {
namespace {

/**
 * Stores the Tool Registry Record data contract used by the modal modeling interaction layer.
 */
struct ToolRegistryRecord {
	std::string_view id;
	const EditorTool *tool;
};

std::span<const ToolRegistryRecord> tool_registry_records()
{
	static const std::array<ToolRegistryRecord, 12> records {{
		{ "select", &select_tool() },
		{ "move", &move_tool() },
		{ "extend", &extend_tool() },
		{ "extrude", &extrude_tool() },
		{ "rotate", &rotate_tool() },
		{ "scale", &scale_tool() },
		{ "box", &box_tool() },
		{ "insert_edge_loop", &insert_edge_loop_tool() },
		{ "knife", &knife_tool() },
		{ "cut", &cut_tool() },
		{ "mirror", &mirror_tool() },
		{ "poly", &poly_tool() },
	}};
	return records;
}

std::span<const EditorTool* const> tool_entries()
{
	static const std::array<const EditorTool*, 12> tools {
		&select_tool(),
		&move_tool(),
		&extend_tool(),
		&extrude_tool(),
		&rotate_tool(),
		&scale_tool(),
		&box_tool(),
		&insert_edge_loop_tool(),
		&knife_tool(),
		&cut_tool(),
		&mirror_tool(),
		&poly_tool(),
	};
	return tools;
}

} // namespace

std::span<const EditorTool* const> ToolRegistry::tools() const
{
	return tool_entries();
}

const EditorTool *ToolRegistry::find(ViewportInteractionTool tool) const
{
	for (const EditorTool *registered_tool : tools()) {
		if (registered_tool != nullptr && registered_tool->tool_id() == tool) {
			return registered_tool;
		}
	}
	return nullptr;
}

const EditorTool *ToolRegistry::find(std::string_view id) const
{
	for (const ToolRegistryRecord& record : tool_registry_records()) {
		if (record.id == id) {
			return record.tool;
		}
	}
	return nullptr;
}

const ToolRegistry& tool_registry()
{
	static const ToolRegistry registry;
	return registry;
}

std::span<const EditorTool* const> editor_tool_registry()
{
	return tool_registry().tools();
}

const EditorTool *editor_tool_for_kind(ViewportInteractionTool tool)
{
	return tool_registry().find(tool);
}

const EditorTool *editor_tool_for_id(std::string_view id)
{
	return tool_registry().find(id);
}

} // namespace quader_editor_core
