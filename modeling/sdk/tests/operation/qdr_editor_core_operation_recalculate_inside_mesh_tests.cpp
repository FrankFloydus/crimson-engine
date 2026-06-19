#include "editor_operation_test_helpers.h"

TEST_CASE("recalculate inside mesh operation orients selected object normals inward", "[operation][recalculate_inside_mesh]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	REQUIRE(qdr_test::all_faces_point_relative_to_center(session.objects().front().document, true));

	const quader_editor_core::CommandResult result = session.recalculate_selected_face_normals(false);

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(qdr_test::all_faces_point_relative_to_center(session.objects().front().document, false));
}
