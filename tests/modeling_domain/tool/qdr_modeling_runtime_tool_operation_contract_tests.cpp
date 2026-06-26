////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/operations/operation_registry.h>
#include <editor/modeling/runtime/tool_operation_contracts.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <set>
#include <string>

namespace qec = quader_editor_core;

TEST_CASE("editor tool registry exposes concrete viewport tools", "[editor-core][tools][contracts]")
{
	const auto registry = qec::editor_tool_registry();
	const std::span<const qec::EditorTool* const> tools = qec::tool_registry().tools();
	REQUIRE_FALSE(tools.empty());
	REQUIRE(registry.size() == tools.size());

	std::set<qec::ViewportInteractionTool> tool_ids;
	std::set<std::string> stable_ids;
	for (std::size_t index = 0; index < tools.size(); ++index) {
		REQUIRE(registry[index] != nullptr);
		REQUIRE(tools[index] != nullptr);
		CHECK(registry[index] == tools[index]);

		const qec::EditorTool *tool = tools[index];
		CHECK(tool->tool_id() != qec::ViewportInteractionTool::unknown);
		CHECK_FALSE(tool->id().empty());
		CHECK_FALSE(tool->label().empty());
		CHECK(tool_ids.insert(tool->tool_id()).second);
		CHECK(stable_ids.insert(std::string(tool->id())).second);
		CHECK(qec::editor_tool_for_kind(tool->tool_id()) == tool);
		CHECK(qec::editor_tool_for_id(tool->id()) == tool);
	}

	CHECK(qec::editor_tool_for_kind(qec::ViewportInteractionTool::unknown) == nullptr);
	CHECK(qec::editor_tool_for_id("missing") == nullptr);
}

TEST_CASE("tool compatibility APIs expose insert-edge-loop and knife behavior", "[editor-core][tools][contracts]")
{
	qec::InsertEdgeLoopToolState edge_loop;
	edge_loop.object_id = 1;
	edge_loop.edge = quader_poly::make_edge(1, 2);
	CHECK(qec::insert_edge_loop_tool_has_seed(edge_loop));

	qec::reset_insert_edge_loop_tool(edge_loop);
	CHECK_FALSE(qec::insert_edge_loop_tool_has_seed(edge_loop));

	qec::KnifeToolState knife;
	knife.active = true;
	knife.has_current = true;
	knife.current_point = { 1.0F, 2.0F, 3.0F };
	CHECK(qec::knife_tool_preview(knife).active);
	CHECK(qec::knife_tool_preview(knife).has_current);

	qec::reset_knife_tool(knife);
	CHECK_FALSE(qec::knife_tool_preview(knife).active);
}

TEST_CASE("editor operation registry exposes catalog operations", "[editor-core][operations][contracts]")
{
	const auto registry = qec::editor_operation_registry();
	const std::span<const qec::EditorOperationRegistryEntry> entries = qec::operation_registry().entries();
	REQUIRE_FALSE(entries.empty());
	REQUIRE(registry.size() == entries.size());

	std::set<qec::EditorOperationId> operation_ids;
	std::set<std::string> stable_ids;
	for (std::size_t index = 0; index < entries.size(); ++index) {
		REQUIRE(registry[index] != nullptr);
		REQUIRE(entries[index].operation != nullptr);
		CHECK(registry[index] == entries[index].operation);

		const qec::EditorOperation *operation = entries[index].operation;
		CHECK(operation->operation_id() == entries[index].operation_id);
		CHECK(std::string(operation->id()) == std::string(entries[index].id));
		CHECK_FALSE(operation->label().empty());
		CHECK_FALSE(entries[index].class_name.empty());
		CHECK(operation_ids.insert(operation->operation_id()).second);
		CHECK(stable_ids.insert(std::string(operation->id())).second);
		CHECK(qec::editor_operation_for_id(operation->operation_id()) == operation);
		CHECK(qec::editor_operation_for_id(operation->id()) == operation);
	}

	const qec::EditorOperation *operation = qec::editor_operation_for_id(qec::EditorOperationId::bridge);
	REQUIRE(operation != nullptr);
	CHECK(operation == &qec::bridge_operation());
	CHECK(operation->operation_id() == qec::EditorOperationId::bridge);
	CHECK(std::string(operation->id()) == std::string(qec::bridge_operation_schema().operation_id));
	CHECK(operation->supports_preview());
	CHECK(qec::editor_operation_for_id(operation->id()) == operation);

	CHECK(qec::editor_operation_for_id("missing") == nullptr);
}
