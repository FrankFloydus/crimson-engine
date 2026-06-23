////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

TEST_CASE("transform operation applies matrix transforms around the requested pivot", "[operation][transform]")
{
	SECTION("object mode scales selected geometry without changing topology")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		quader_poly::Transform3 transform;
		transform.x_axis = { 2.0F, 0.0F, 0.0F };
		transform.y_axis = { 0.0F, 1.0F, 0.0F };
		transform.z_axis = { 0.0F, 0.0F, 1.0F };
		const std::size_t vertex_count_before = session.objects().front().document.vertices.size();

		const quader_editor_core::CommandResult result = session.transform_selected(transform, {});

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		REQUIRE(session.objects().front().document.vertices.size() == vertex_count_before);
		REQUIRE(qdr_test::every_face_compiles_to_triangles(session.objects().front().document));
	}

	SECTION("pivot controls the transformed point")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader::QVec3 before = session.objects().front().document.vertices.front().position;
		quader_poly::Transform3 transform;
		transform.x_axis = { 2.0F, 0.0F, 0.0F };
		transform.y_axis = { 0.0F, 2.0F, 0.0F };
		transform.z_axis = { 0.0F, 0.0F, 2.0F };
		const quader::QVec3 pivot { 1.0F, 1.0F, 1.0F };

		const quader_editor_core::CommandResult result = session.transform_selected(transform, pivot);

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		const quader::QVec3 after = session.objects().front().document.vertices.front().position;
		REQUIRE(qdr_test::vec_nearly_equal(after, {
			pivot.x + ((before.x - pivot.x) * 2.0F),
			pivot.y + ((before.y - pivot.y) * 2.0F),
			pivot.z + ((before.z - pivot.z) * 2.0F),
		}));
	}

	SECTION("identity transforms are unchanged")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

		const quader_editor_core::CommandResult result = session.transform_selected({}, {});

		REQUIRE(result.ok);
		REQUIRE_FALSE(result.changed);
	}

	SECTION("multi-object selection center uses the combined selection bounds")
	{
		quader_editor_core::EditorSession session;
		REQUIRE(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed);
		const int first_id = session.objects().back().id;
		REQUIRE(session.create_box_from_bounds({ 9.0F, -1.0F, -1.0F }, { 11.0F, 1.0F, 1.0F }).changed);
		const int second_id = session.objects().back().id;
		session.set_selected_object_ids({ first_id, second_id }, second_id);

		const std::optional<quader::QVec3> center = session.selection_center();

		REQUIRE(center.has_value());
		REQUIRE(qdr_test::vec_nearly_equal(*center, { 5.0F, 0.0F, 0.0F }));
	}
}
