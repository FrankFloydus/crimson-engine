////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "operation_test_helpers.h"

#include <modeling/runtime/session.h>

namespace qdr_test {

inline quader_editor_core::PolygonObject make_object(int id, quader_poly::Document document, bool selected = true)
{
	quader_editor_core::PolygonObject object;
	object.id = id;
	object.document = std::move(document);
	object.selected = selected;
	return object;
}

inline quader_editor_core::EditorSession selected_cube_session()
{
	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult result =
		session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F });
	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(session.objects().size() == 1U);
	return session;
}

} // namespace qdr_test
