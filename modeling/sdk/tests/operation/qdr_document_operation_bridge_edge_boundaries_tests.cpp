#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("bridge edge boundaries creates a bridge between matching open chains", "[operation][bridge_edge_boundaries]")
{
	SECTION("closed matching boundaries create a stepped shell between face islands")
	{
		Document document = qdr_test::make_parallel_quad_document();
		REQUIRE(document.faces.size() == 2U);
		const std::vector<ElementId> first_vertices = document.faces[0].vertices;
		const std::vector<ElementId> second_vertices = document.faces[1].vertices;
		Selection selection;

		const OperationResult result = bridge_edge_boundaries(document, selection, first_vertices, second_vertices, true, 2);

		REQUIRE(result.changed);
		REQUIRE(selection.mode == SelectionMode::face);
		REQUIRE(selection.faces.size() == 8U);
		REQUIRE(document.faces.size() == 10U);
		REQUIRE(qdr_test::every_edge_is_closed(document));
		REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	}

	SECTION("mismatched or too-short boundaries fail without mutating")
	{
		Document document = qdr_test::make_parallel_quad_document();
		const std::size_t face_count_before = document.faces.size();
		const std::vector<ElementId> first_vertices = document.faces[0].vertices;
		std::vector<ElementId> second_vertices = document.faces[1].vertices;
		second_vertices.pop_back();
		Selection selection;

		const OperationResult mismatch_result = bridge_edge_boundaries(document, selection, first_vertices, second_vertices, true, 1);
		REQUIRE_FALSE(mismatch_result.changed);
		REQUIRE(mismatch_result.message == "Bridge needs matching boundary vertex counts.");
		REQUIRE(document.faces.size() == face_count_before);

		const std::vector<ElementId> short_vertices { first_vertices.front() };
		const OperationResult short_result = bridge_edge_boundaries(document, selection, short_vertices, short_vertices, false, 1);
		REQUIRE_FALSE(short_result.changed);
		REQUIRE(short_result.message == "Bridge needs two valid boundary edge chains.");
	}
}
