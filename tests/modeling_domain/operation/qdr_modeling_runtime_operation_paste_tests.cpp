////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <editor/modeling/runtime/editor_runtime.h>

#include <vector>

TEST_CASE("paste operation inserts copied objects with fresh ids", "[operation][paste]")
{
	SECTION("explicit paste target offsets the new selected object")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int source_id = session.objects().front().id;
		const std::vector<quader_editor_core::PolygonObject> copied = session.selected_object_copies();

		const quader_editor_core::CommandResult paste = session.paste_objects(copied, quader::QVec3 { 4.0F, 0.0F, 0.0F });

		REQUIRE(paste.ok);
		REQUIRE(paste.changed);
		REQUIRE(session.objects().size() == 2U);
		REQUIRE(session.selected_object_ids().size() == 1U);
		REQUIRE(session.selected_object_ids().front() != source_id);
		REQUIRE(session.active_object_id() == session.selected_object_ids().front());
	}

	SECTION("paste without an explicit target centers on the current selection")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const std::vector<quader_editor_core::PolygonObject> copied = session.selected_object_copies();
		REQUIRE(session.translate_selected({ 5.0F, 0.0F, 0.0F }).changed);

		const quader_editor_core::CommandResult paste = session.paste_objects(copied, std::nullopt);

		REQUIRE(paste.ok);
		REQUIRE(paste.changed);
		REQUIRE(session.objects().size() == 2U);
		const quader_editor_core::Bounds3 selected_bounds = session.selection_bounds();
		REQUIRE_FALSE(selected_bounds.empty);
		REQUIRE((selected_bounds.min.x + selected_bounds.max.x) * 0.5F == Catch::Approx(5.0F));
	}

	SECTION("empty paste reports an error")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

		const quader_editor_core::CommandResult paste = session.paste_objects({}, std::nullopt);

		REQUIRE_FALSE(paste.ok);
		REQUIRE_FALSE(paste.changed);
		REQUIRE(session.objects().size() == 1U);
	}
}

TEST_CASE("editor runtime clipboard paste routes through session paste policy", "[operation][paste]")
{
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	session.set_selected_object_ids({ 1 }, 1);
	quader_editor_core::EditorRuntime runtime;
	REQUIRE(runtime.copy_selected_objects_to_clipboard(session) == 1);

	const quader_editor_core::CommandResult paste =
		runtime.paste_clipboard(session, quader::QVec3 { 3.0F, 0.0F, 3.0F });

	REQUIRE(paste.ok);
	CHECK(paste.changed);
	CHECK(session.objects().size() == 2);
	REQUIRE(session.selected_object_ids().size() == 1);
	CHECK(session.selected_object_ids().front() == 2);
}
