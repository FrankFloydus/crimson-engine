////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::string read_modeling_domain_source(const std::filesystem::path& relative_path)
{
	const std::filesystem::path path = std::filesystem::path { QUADER_MODELING_DOMAIN_TEST_ROOT_PATH } / relative_path;
	std::ifstream input { path, std::ios::binary };
	EXPECT_TRUE(input.is_open()) << "Missing modeling-domain source: " << path.string();
	return { std::istreambuf_iterator<char> { input }, std::istreambuf_iterator<char> {} };
}

std::size_t count_occurrences(const std::string& source, const std::string& needle)
{
	std::size_t count = 0;
	std::size_t offset = 0;
	while ((offset = source.find(needle, offset)) != std::string::npos) {
		++count;
		offset += needle.size();
	}
	return count;
}

std::size_t count_successful_changed_assertions(const std::string& source)
{
	std::size_t count = 0;
	std::size_t line_start = 0;
	while (line_start < source.size()) {
		const std::size_t line_end = source.find('\n', line_start);
		const std::string_view line {
			source.data() + line_start,
			(line_end == std::string::npos ? source.size() : line_end) - line_start,
		};
		if (line.find("EXPECT_TRUE(") != std::string_view::npos &&
			line.find(".changed);") != std::string_view::npos) {
			++count;
		}
		if (line_end == std::string::npos) {
			break;
		}
		line_start = line_end + 1U;
	}
	return count;
}

} // namespace

TEST(QdrDocumentOperationValidationGuardrailTests, SharedHelperCallsCentralValidator) {
	const std::string source = read_modeling_domain_source("operation/operation_validation_test_helpers.hpp");

	EXPECT_TRUE(source.find("#include <mesh/polygon/document_validation.hpp>") != std::string::npos);
	EXPECT_TRUE(source.find("validate_polygon_document(document)") != std::string::npos);
	EXPECT_TRUE(source.find("expect_valid_polygon_document_after_success") != std::string::npos);
	EXPECT_TRUE(source.find("expect_polygon_document_validation_issue_after_success") != std::string::npos);
}

TEST(QdrDocumentOperationValidationGuardrailTests, SuccessfulTopologyOperationTestsUseValidationHelper) {
	const std::vector<std::filesystem::path> topology_operation_tests {
		"operation/qdr_document_operation_bevel_edges_tests.cpp",
		"operation/qdr_document_operation_bevel_vertices_tests.cpp",
		"operation/qdr_document_operation_bridge_edge_boundaries_tests.cpp",
		"operation/qdr_document_operation_bridge_edge_pairs_tests.cpp",
		"operation/qdr_document_operation_bridge_edges_tests.cpp",
		"operation/qdr_document_operation_bridge_faces_tests.cpp",
		"operation/qdr_document_operation_collapse_edges_tests.cpp",
		"operation/qdr_document_operation_collapse_faces_tests.cpp",
		"operation/qdr_document_operation_combine_faces_tests.cpp",
		"operation/qdr_document_operation_connect_edges_tests.cpp",
		"operation/qdr_document_operation_connect_vertices_tests.cpp",
		"operation/qdr_document_operation_delete_selection_tests.cpp",
		"operation/qdr_document_operation_detach_faces_tests.cpp",
		"operation/qdr_document_operation_dissolve_edges_tests.cpp",
		"operation/qdr_document_operation_dissolve_vertices_tests.cpp",
		"operation/qdr_document_operation_extrude_elements_tests.cpp",
		"operation/qdr_document_operation_extrude_faces_tests.cpp",
		"operation/qdr_document_operation_extract_faces_tests.cpp",
		"operation/qdr_document_operation_fill_hole_tests.cpp",
		"operation/qdr_document_operation_insert_edge_loop_tests.cpp",
		"operation/qdr_document_operation_inset_elements_tests.cpp",
		"operation/qdr_document_operation_inset_faces_tests.cpp",
		"operation/qdr_document_operation_knife_segment_tests.cpp",
		"operation/qdr_document_operation_knife_stroke_tests.cpp",
		"operation/qdr_document_operation_merge_edges_tests.cpp",
		"operation/qdr_document_operation_merge_vertices_by_distance_tests.cpp",
		"operation/qdr_document_operation_merge_vertices_to_active_tests.cpp",
		"operation/qdr_document_operation_merge_vertices_to_center_tests.cpp",
		"operation/qdr_document_operation_plane_cut_tests.cpp",
		"operation/qdr_document_operation_slice_quad_tests.cpp",
		"operation/qdr_document_operation_split_edges_tests.cpp",
		"operation/qdr_document_operation_thicken_faces_tests.cpp",
	};

	for (const std::filesystem::path& relative_path : topology_operation_tests) {
		const std::string source = read_modeling_domain_source(relative_path);
		const std::size_t successful_mutation_assertions =
			count_successful_changed_assertions(source);
		const std::size_t helper_calls =
			count_occurrences(source, "expect_valid_polygon_document_after_success") +
			count_occurrences(source, "expect_valid_polygon_document(") +
			count_occurrences(source, "expect_polygon_document_validation_issue_after_success");
		EXPECT_GE(helper_calls, successful_mutation_assertions)
			<< relative_path.string() << " must validate every successful topology mutation";
	}
}
