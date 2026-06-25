////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <catch2/catch_approx.hpp>

using qdr_test::find_object;

TEST_CASE("duplicate operation creates fresh selected object ids", "[operation][duplicate]")
{
	SECTION("object mode duplicates the current selection")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int source_id = session.objects().front().id;

		const quader_editor_core::CommandResult duplicate =
			session.duplicate_selected_objects(quader::QVec3 { 1.0F, 0.0F, 0.0F });

		REQUIRE(duplicate.ok);
		REQUIRE(duplicate.changed);
		REQUIRE(session.objects().size() == 2U);
		REQUIRE(session.selected_object_ids().size() == 1U);
		REQUIRE(session.selected_object_ids().front() != source_id);
		REQUIRE(session.active_object_id() == session.selected_object_ids().front());
	}

	SECTION("component mode duplicate reports an error without object copies")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
		session.set_component_selection(
			session.objects().front().id,
			qdr_test::vertex_selection({ vertex_id }, vertex_id),
			quader_editor_core::SelectionMode::vertex);

		const quader_editor_core::CommandResult duplicate =
			session.duplicate_selected_objects(quader::QVec3 { 1.0F, 0.0F, 0.0F });

		REQUIRE_FALSE(duplicate.ok);
		REQUIRE_FALSE(duplicate.changed);
		REQUIRE(session.objects().size() == 1U);
	}

	SECTION("operation settings duplicate in place when only focused grid is provided")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int source_id = session.objects().front().id;

		quader_editor_core::DuplicateMeshesOperationSettings settings;
		settings.focused_grid = { 2.5, true };
		const quader_editor_core::CommandResult duplicate = session.duplicate_selected_objects(settings);

		REQUIRE(duplicate.ok);
		REQUIRE(duplicate.changed);
		REQUIRE(session.selected_object_ids().size() == 1U);
		const quader_editor_core::PolygonObject* source = find_object(session, source_id);
		const quader_editor_core::PolygonObject* copy = find_object(session, session.selected_object_ids().front());
		REQUIRE(source != nullptr);
		REQUIRE(copy != nullptr);
		REQUIRE_FALSE(source->document.vertices.empty());
		REQUIRE_FALSE(copy->document.vertices.empty());
		CHECK(copy->document.vertices.front().position.x - source->document.vertices.front().position.x == Catch::Approx(0.0F));
		CHECK(copy->document.vertices.front().position.y - source->document.vertices.front().position.y == Catch::Approx(0.0F));
		CHECK(copy->document.vertices.front().position.z - source->document.vertices.front().position.z == Catch::Approx(0.0F));
	}

	SECTION("operation settings use explicit offset when provided")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int source_id = session.objects().front().id;

		quader_editor_core::DuplicateMeshesOperationSettings settings;
		settings.focused_grid = { 1.0, true };
		settings.explicit_offset = { { -2.0F, 0.5F, 3.0F }, true };
		const quader_editor_core::CommandResult duplicate = session.duplicate_selected_objects(settings);

		REQUIRE(duplicate.ok);
		REQUIRE(duplicate.changed);
		REQUIRE(session.selected_object_ids().size() == 1U);
		const quader_editor_core::PolygonObject* source = find_object(session, source_id);
		const quader_editor_core::PolygonObject* copy = find_object(session, session.selected_object_ids().front());
		REQUIRE(source != nullptr);
		REQUIRE(copy != nullptr);
		REQUIRE_FALSE(source->document.vertices.empty());
		REQUIRE_FALSE(copy->document.vertices.empty());
		CHECK(copy->document.vertices.front().position.x - source->document.vertices.front().position.x == Catch::Approx(-2.0F));
		CHECK(copy->document.vertices.front().position.y - source->document.vertices.front().position.y == Catch::Approx(0.5F));
		CHECK(copy->document.vertices.front().position.z - source->document.vertices.front().position.z == Catch::Approx(3.0F));
	}

	SECTION("operation settings duplicate in place by default")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int source_id = session.objects().front().id;

		const quader_editor_core::CommandResult duplicate =
			session.duplicate_selected_objects(quader_editor_core::DuplicateMeshesOperationSettings {});

		REQUIRE(duplicate.ok);
		REQUIRE(duplicate.changed);
		REQUIRE(session.selected_object_ids().size() == 1U);
		const quader_editor_core::PolygonObject* source = find_object(session, source_id);
		const quader_editor_core::PolygonObject* copy = find_object(session, session.selected_object_ids().front());
		REQUIRE(source != nullptr);
		REQUIRE(copy != nullptr);
		REQUIRE_FALSE(source->document.vertices.empty());
		REQUIRE_FALSE(copy->document.vertices.empty());
		CHECK(copy->document.vertices.front().position.x - source->document.vertices.front().position.x == Catch::Approx(0.0F));
		CHECK(copy->document.vertices.front().position.y - source->document.vertices.front().position.y == Catch::Approx(0.0F));
		CHECK(copy->document.vertices.front().position.z - source->document.vertices.front().position.z == Catch::Approx(0.0F));
	}
}
