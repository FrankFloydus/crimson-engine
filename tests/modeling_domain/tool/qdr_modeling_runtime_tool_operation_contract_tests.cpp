////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/operations/operation_registry.hpp>
#include <editor/modeling/runtime/tool_operation_contracts.hpp>

#include "test_support.hpp"

#include <cstddef>
#include <set>
#include <string>

namespace qec = quader_editor_core;

TEST(QdrModelingRuntimeToolOperationContractTests, EditorToolRegistryExposesConcreteViewportTools) {
	const auto registry = qec::editor_tool_registry();
	const std::span<const qec::EditorTool* const> tools = qec::tool_registry().tools();
	EXPECT_FALSE(tools.empty());
	EXPECT_TRUE(registry.size() == tools.size());

	std::set<qec::ViewportInteractionTool> tool_ids;
	std::set<std::string> stable_ids;
	for (std::size_t index = 0; index < tools.size(); ++index) {
		EXPECT_TRUE(registry[index] != nullptr);
		EXPECT_TRUE(tools[index] != nullptr);
		EXPECT_TRUE(registry[index] == tools[index]);

		const qec::EditorTool *tool = tools[index];
                EXPECT_TRUE(tool->tool_id() !=
                            qec::ViewportInteractionTool::Unknown);
                EXPECT_FALSE(tool->id().empty());
		EXPECT_FALSE(tool->label().empty());
		EXPECT_TRUE(tool_ids.insert(tool->tool_id()).second);
		EXPECT_TRUE(stable_ids.insert(std::string(tool->id())).second);
		EXPECT_TRUE(qec::editor_tool_for_kind(tool->tool_id()) == tool);
		EXPECT_TRUE(qec::editor_tool_for_id(tool->id()) == tool);
	}

        EXPECT_TRUE(qec::editor_tool_for_kind(
                        qec::ViewportInteractionTool::Unknown) == nullptr);
        EXPECT_TRUE(qec::editor_tool_for_id("missing") == nullptr);
}

TEST(QdrModelingRuntimeToolOperationContractTests, ToolCompatibilityAPIsExposeInsertEdgeLoopAndKnifeBehavior) {
	qec::InsertEdgeLoopToolState edge_loop;
	edge_loop.object_id = 1;
	edge_loop.edge = quader_poly::make_edge(1, 2);
	EXPECT_TRUE(qec::insert_edge_loop_tool_has_seed(edge_loop));

	qec::reset_insert_edge_loop_tool(edge_loop);
	EXPECT_FALSE(qec::insert_edge_loop_tool_has_seed(edge_loop));

	qec::KnifeToolState knife;
	knife.active = true;
	knife.has_current = true;
	knife.current_point = { 1.0F, 2.0F, 3.0F };
	EXPECT_TRUE(qec::knife_tool_preview(knife).active);
	EXPECT_TRUE(qec::knife_tool_preview(knife).has_current);

	qec::reset_knife_tool(knife);
	EXPECT_FALSE(qec::knife_tool_preview(knife).active);
}

TEST(QdrModelingRuntimeToolOperationContractTests, EditorOperationRegistryExposesCatalogOperations) {
	const auto registry = qec::editor_operation_registry();
	const std::span<const qec::EditorOperationRegistryEntry> entries = qec::operation_registry().entries();
	EXPECT_FALSE(entries.empty());
	EXPECT_TRUE(registry.size() == entries.size());

	std::set<qec::EditorOperationId> operation_ids;
	std::set<std::string> stable_ids;
	for (std::size_t index = 0; index < entries.size(); ++index) {
		EXPECT_TRUE(registry[index] != nullptr);
		EXPECT_TRUE(entries[index].operation != nullptr);
		EXPECT_TRUE(registry[index] == entries[index].operation);

		const qec::EditorOperation *operation = entries[index].operation;
		EXPECT_TRUE(operation->operation_id() == entries[index].operation_id);
		EXPECT_TRUE(std::string(operation->id()) == std::string(entries[index].id));
		EXPECT_FALSE(operation->label().empty());
		EXPECT_FALSE(entries[index].class_name.empty());
		EXPECT_TRUE(operation_ids.insert(operation->operation_id()).second);
		EXPECT_TRUE(stable_ids.insert(std::string(operation->id())).second);
		EXPECT_TRUE(qec::editor_operation_for_id(operation->operation_id()) == operation);
		EXPECT_TRUE(qec::editor_operation_for_id(operation->id()) == operation);
	}

        const qec::EditorOperation *operation =
            qec::editor_operation_for_id(qec::EditorOperationId::Bridge);
        EXPECT_TRUE(operation != nullptr);
	EXPECT_TRUE(operation->portable_descriptor() ==
	            quader::modeling::operation_descriptor(
	                quader::modeling::OperationKind::Bridge));
        EXPECT_TRUE(operation->operation_id() ==
                    qec::EditorOperationId::Bridge);
        EXPECT_TRUE(std::string(operation->id()) == std::string(qec::bridge_operation_schema().operation_id));
	EXPECT_TRUE(operation->supports_preview());
	EXPECT_TRUE(qec::editor_operation_for_id(operation->id()) == operation);

	EXPECT_TRUE(qec::editor_operation_for_id("missing") == nullptr);
}
