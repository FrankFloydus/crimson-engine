////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <modeling/operations/operation_registry.h>
#include <modeling/runtime/tool_operation_contracts.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace qec = quader_editor_core;

namespace {

/**
 * Represents an Expected Tool value used by the automated test suite.
 */
struct ExpectedTool {
	qec::ViewportInteractionTool tool_id;
	std::string_view id;
	std::string_view label;
	const qec::EditorTool *singleton;
};

/**
 * Implements the Expected Operation modeling operation data used by the automated test suite.
 */
struct ExpectedOperation {
	qec::EditorOperationId operation_id;
	std::string_view id;
	std::string_view label;
	bool supports_preview;
	const qec::EditorOperation *singleton;
};

} // namespace

TEST_CASE("editor tool registry exposes concrete viewport tools", "[editor-core][tools][contracts]")
{
	const auto registry = qec::editor_tool_registry();
	REQUIRE(registry.size() == 12U);

	const std::array expected {
		ExpectedTool { qec::ViewportInteractionTool::select, "select", "Select", &qec::select_tool() },
		ExpectedTool { qec::ViewportInteractionTool::move, "move", "Move", &qec::move_tool() },
		ExpectedTool { qec::ViewportInteractionTool::extend, "extend", "Extend", &qec::extend_tool() },
		ExpectedTool { qec::ViewportInteractionTool::extrude, "extrude", "Extrude", &qec::extrude_tool() },
		ExpectedTool { qec::ViewportInteractionTool::rotate, "rotate", "Rotate", &qec::rotate_tool() },
		ExpectedTool { qec::ViewportInteractionTool::scale, "scale", "Scale", &qec::scale_tool() },
		ExpectedTool { qec::ViewportInteractionTool::box, "box", "Box", &qec::box_tool() },
		ExpectedTool { qec::ViewportInteractionTool::insert_edge_loop, "insert_edge_loop", "Insert Edge Loop", &qec::insert_edge_loop_tool() },
		ExpectedTool { qec::ViewportInteractionTool::knife, "knife", "Knife", &qec::knife_tool() },
		ExpectedTool { qec::ViewportInteractionTool::cut, "cut", "Cut", &qec::cut_tool() },
		ExpectedTool { qec::ViewportInteractionTool::mirror, "mirror", "Mirror", &qec::mirror_tool() },
		ExpectedTool { qec::ViewportInteractionTool::poly, "poly", "Poly", &qec::poly_tool() },
	};

	REQUIRE(registry.size() == expected.size());
	for (std::size_t index = 0; index < expected.size(); ++index) {
		REQUIRE(registry[index] != nullptr);
		CHECK(registry[index] == expected[index].singleton);
	}

	for (const ExpectedTool& expected_tool : expected) {
		const qec::EditorTool *tool = qec::editor_tool_for_kind(expected_tool.tool_id);
		REQUIRE(tool != nullptr);
		CHECK(tool == expected_tool.singleton);
		CHECK(tool->tool_id() == expected_tool.tool_id);
		CHECK(std::string(tool->id()) == std::string(expected_tool.id));
		CHECK(std::string(tool->label()) == std::string(expected_tool.label));
		CHECK(qec::editor_tool_for_id(expected_tool.id) == tool);
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
	REQUIRE(registry.size() == 33U);

	const std::array expected {
		ExpectedOperation { qec::EditorOperationId::bridge, "bridge", "Bridge", true, &qec::bridge_operation() },
		ExpectedOperation { qec::EditorOperationId::edge_bevel, "edge_bevel", "Edge Bevel", true, &qec::edge_bevel_operation() },
		ExpectedOperation { qec::EditorOperationId::thicken, "thicken", "Thicken", true, &qec::thicken_operation() },
		ExpectedOperation { qec::EditorOperationId::slice_quad, "slice_quad", "Slice Quad", true, &qec::slice_quad_operation() },
		ExpectedOperation { qec::EditorOperationId::merge_by_distance, "merge_by_distance", "Merge by Distance", true, &qec::merge_by_distance_operation() },
		ExpectedOperation { qec::EditorOperationId::bevel_vertices, "bevel_vertices", "Bevel Vertices", false, &qec::bevel_vertices_operation() },
		ExpectedOperation { qec::EditorOperationId::duplicate_meshes, "duplicate_meshes", "Duplicate Meshes", false, &qec::duplicate_meshes_operation() },
		ExpectedOperation { qec::EditorOperationId::connect_vertices, "connect_vertices", "Connect Vertices", false, &qec::connect_vertices_operation() },
		ExpectedOperation { qec::EditorOperationId::dissolve_vertices, "dissolve_vertices", "Dissolve Vertices", false, &qec::dissolve_vertices_operation() },
		ExpectedOperation { qec::EditorOperationId::connect_edges, "connect_edges", "Connect Edges", false, &qec::connect_edges_operation() },
		ExpectedOperation { qec::EditorOperationId::split_edges, "split_edges", "Split Edges", false, &qec::split_edges_operation() },
		ExpectedOperation { qec::EditorOperationId::dissolve_edges, "dissolve_edges", "Dissolve Edges", false, &qec::dissolve_edges_operation() },
		ExpectedOperation { qec::EditorOperationId::merge_edges, "merge_edges", "Merge Edges", false, &qec::merge_edges_operation() },
		ExpectedOperation { qec::EditorOperationId::collapse_edges, "collapse_edges", "Collapse Edges", false, &qec::collapse_edges_operation() },
		ExpectedOperation { qec::EditorOperationId::fill_hole, "fill_hole", "Fill Hole", false, &qec::fill_hole_operation() },
		ExpectedOperation { qec::EditorOperationId::combine_faces, "combine_faces", "Combine Faces", false, &qec::combine_faces_operation() },
		ExpectedOperation { qec::EditorOperationId::collapse_faces, "collapse_faces", "Collapse Faces", false, &qec::collapse_faces_operation() },
		ExpectedOperation { qec::EditorOperationId::radial_align, "radial_align", "Radial Align", false, &qec::radial_align_operation() },
		ExpectedOperation { qec::EditorOperationId::flip_face_normals, "flip_face_normals", "Flip Face Normals", false, &qec::flip_face_normals_operation() },
		ExpectedOperation { qec::EditorOperationId::shade_faces_smooth, "shade_faces_smooth", "Shade Faces Smooth", false, &qec::shade_faces_smooth_operation() },
		ExpectedOperation { qec::EditorOperationId::shade_faces_flat, "shade_faces_flat", "Shade Faces Flat", false, &qec::shade_faces_flat_operation() },
		ExpectedOperation { qec::EditorOperationId::harden_edge_normals, "harden_edge_normals", "Harden Edge Normals", false, &qec::harden_edge_normals_operation() },
		ExpectedOperation { qec::EditorOperationId::soften_edge_normals, "soften_edge_normals", "Soften Edge Normals", false, &qec::soften_edge_normals_operation() },
		ExpectedOperation { qec::EditorOperationId::combine_meshes, "combine_meshes", "Combine Meshes", false, &qec::combine_meshes_operation() },
		ExpectedOperation { qec::EditorOperationId::extract_faces, "extract_faces", "Extract Faces", false, &qec::extract_faces_operation() },
		ExpectedOperation { qec::EditorOperationId::detach_faces, "detach_faces", "Detach Faces", false, &qec::detach_faces_operation() },
		ExpectedOperation { qec::EditorOperationId::snap_vertices_to_active, "snap_vertices_to_active", "Snap Vertices To Active", false, &qec::snap_vertices_to_active_operation() },
		ExpectedOperation { qec::EditorOperationId::snap_edges_to_active, "snap_edges_to_active", "Snap Edges To Active", false, &qec::snap_edges_to_active_operation() },
		ExpectedOperation { qec::EditorOperationId::merge_vertices_to_active, "merge_vertices_to_active", "Merge Vertices To Active", false, &qec::merge_vertices_to_active_operation() },
		ExpectedOperation { qec::EditorOperationId::merge_vertices_to_center, "merge_vertices_to_center", "Merge Vertices To Center", false, &qec::merge_vertices_to_center_operation() },
		ExpectedOperation { qec::EditorOperationId::recalculate_face_normals, "recalculate_face_normals", "Recalculate Face Normals", false, &qec::recalculate_face_normals_operation() },
		ExpectedOperation { qec::EditorOperationId::assign_material, "assign_material", "Apply Material", false, &qec::assign_material_operation() },
		ExpectedOperation { qec::EditorOperationId::delete_selection, "delete_selection", "Delete Selection", false, &qec::delete_selection_operation() },
	};

	REQUIRE(registry.size() == expected.size());
	for (std::size_t index = 0; index < expected.size(); ++index) {
		REQUIRE(registry[index] != nullptr);
		CHECK(registry[index] == expected[index].singleton);
	}

	for (const ExpectedOperation& expected_operation : expected) {
		const qec::EditorOperation *operation = qec::editor_operation_for_id(expected_operation.operation_id);
		REQUIRE(operation != nullptr);
		CHECK(operation == expected_operation.singleton);
		CHECK(operation->operation_id() == expected_operation.operation_id);
		CHECK(std::string(operation->id()) == std::string(expected_operation.id));
		CHECK(std::string(operation->label()) == std::string(expected_operation.label));
		CHECK(operation->supports_preview() == expected_operation.supports_preview);
		CHECK(qec::editor_operation_for_id(operation->id()) == operation);
		CHECK(qec::editor_operation_for_id(expected_operation.id) == operation);
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
