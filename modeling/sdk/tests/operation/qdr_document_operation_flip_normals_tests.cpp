#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("flip normals reverses only selected face winding", "[operation][flip_normals]")
{
	Document document = make_cube({});
	Selection selection = qdr_test::all_face_selection(document);
	REQUIRE(qdr_test::all_faces_point_relative_to_center(document, true));

	const OperationResult result = flip_selected_face_normals(document, selection);

	REQUIRE(result.changed);
	REQUIRE(qdr_test::all_faces_point_relative_to_center(document, false));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
}
