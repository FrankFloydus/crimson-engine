#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("combine faces removes a support edge between adjacent selected faces", "[operation][combine_faces]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection split = qdr_test::edge_selection({
		make_edge(fixture.bottom_left, fixture.bottom_right),
		make_edge(fixture.top_right, fixture.top_left),
	});
	REQUIRE(connect_selected_edges(fixture.document, split).changed);
	REQUIRE(fixture.document.faces.size() == 2U);
	Selection selection;
	selection.mode = SelectionMode::face;
	for (const Face& face : fixture.document.faces) {
		selection.faces.push_back(face.id);
	}

	const OperationResult result = combine_selected_faces(fixture.document, selection);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.faces.size() == 1U);
	REQUIRE(fixture.document.faces.front().vertices.size() == 4U);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("combine faces restores an inset face island without depending on dissolve order", "[operation][combine_faces]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection inset_selection = qdr_test::face_selection({ fixture.face }, fixture.face);
	REQUIRE(inset_selected_faces(fixture.document, inset_selection, 0.2F).changed);
	REQUIRE(fixture.document.faces.size() == 5U);

	Selection selection = qdr_test::all_face_selection(fixture.document);
	const OperationResult result = combine_selected_faces(fixture.document, selection);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.faces.size() == 1U);
	REQUIRE(fixture.document.faces.front().vertices.size() == 4U);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}
