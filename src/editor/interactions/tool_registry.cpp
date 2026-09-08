////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/runtime/tool_registry.hpp>

#include <array>

namespace quader_editor_core {
namespace {

/// Shares selection cleanup while the viewport gesture owner traces the lasso.
class LassoSelectTool final : public EditorTool {
public:
  [[nodiscard]] ViewportInteractionTool tool_id() const override {
    return ViewportInteractionTool::LassoSelect;
  }
  [[nodiscard]] std::string_view id() const override { return "lasso_select"; }
  [[nodiscard]] std::string_view label() const override { return "Lasso Select"; }
  void reset_runtime_state(EditorRuntimeState &runtime,
                           bool keep_active_when_current_tool) const override {
    select_tool().reset_runtime_state(runtime, keep_active_when_current_tool);
  }
};

const EditorTool &lasso_select_tool() {
  static const LassoSelectTool kTool;
  return kTool;
}

/**
 * Stores the Tool Registry Record data contract used by the modal modeling interaction layer.
 */
struct ToolRegistryRecord {
	std::string_view id;
	const EditorTool *tool;
};

std::span<const ToolRegistryRecord> tool_registry_records()
{
  static const std::array<ToolRegistryRecord, 12> kRecords{{
      {"select", &select_tool()},
      {"lasso_select", &lasso_select_tool()},
      {"move", &move_tool()},
      {"extrude", &extrude_tool()},
      {"rotate", &rotate_tool()},
      {"scale", &scale_tool()},
      {"box", &box_tool()},
      {"insert_edge_loop", &insert_edge_loop_tool()},
      {"knife", &knife_tool()},
      {"cut", &cut_tool()},
      {"mirror", &mirror_tool()},
      {"poly", &poly_tool()},
  }};
  return kRecords;
}

std::span<const EditorTool* const> tool_entries()
{
  static const std::array<const EditorTool *, 12> kTools{
      &select_tool(), &move_tool(),   &extrude_tool(), &rotate_tool(),
      &scale_tool(),  &box_tool(),    &insert_edge_loop_tool(),
      &knife_tool(),  &cut_tool(),    &mirror_tool(),  &poly_tool(),
      &lasso_select_tool(),
  };
  return kTools;
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
  static const ToolRegistry kRegistry;
  return kRegistry;
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
