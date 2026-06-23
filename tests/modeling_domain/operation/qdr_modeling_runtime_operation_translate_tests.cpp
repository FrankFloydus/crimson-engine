////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

TEST_CASE("translate operation moves selected object vertices and records content change", "[operation][translate]")
{
	SECTION("object mode moves every selected object vertex")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader::QVec3 before = session.objects().front().document.vertices.front().position;

		const quader_editor_core::CommandResult result = session.translate_selected({ 2.0F, 0.0F, 0.0F });

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		const quader::QVec3 after = session.objects().front().document.vertices.front().position;
		REQUIRE(after.x == Catch::Approx(before.x + 2.0F));
		REQUIRE(after.y == Catch::Approx(before.y));
		REQUIRE(after.z == Catch::Approx(before.z));
	}

	SECTION("vertex mode moves only selected vertices")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::ElementId selected_id = session.objects().front().document.vertices.front().id;
		const quader::QVec3 selected_before = session.objects().front().document.vertices.front().position;
		const quader::QVec3 unselected_before = session.objects().front().document.vertices[1].position;
		session.set_component_selection(
			session.objects().front().id,
			qdr_test::vertex_selection({ selected_id }, selected_id),
			quader_editor_core::SelectionMode::vertex);

		const quader_editor_core::CommandResult result = session.translate_selected({ 0.0F, 3.0F, 0.0F });

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		REQUIRE(qdr_test::vec_nearly_equal(session.objects().front().document.vertices.front().position, selected_before + quader::QVec3 { 0.0F, 3.0F, 0.0F }));
		REQUIRE(qdr_test::vec_nearly_equal(session.objects().front().document.vertices[1].position, unselected_before));
	}

	SECTION("no selected target is an unchanged success")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		session.clear_selection();

		const quader_editor_core::CommandResult result = session.translate_selected({ 1.0F, 0.0F, 0.0F });

		REQUIRE(result.ok);
		REQUIRE_FALSE(result.changed);
	}
}
