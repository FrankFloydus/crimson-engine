////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "operation_test_helpers.hpp"

#include <editor/modeling/runtime/session.hpp>

#include <algorithm>
#include <cstddef>

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
	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	return session;
}

inline const quader_editor_core::PolygonObject* find_object(const quader_editor_core::EditorSession& session, int object_id)
{
	const auto& objects = session.objects();
	const auto it = std::find_if(objects.begin(), objects.end(), [object_id](const quader_editor_core::PolygonObject& object) {
		return object.id == object_id;
	});
	return it == objects.end() ? nullptr : &*it;
}

inline const quader_editor_core::PolygonObject& require_object(const quader_editor_core::EditorSession& session, int object_id)
{
	const quader_editor_core::PolygonObject* object = find_object(session, object_id);
	if (object == nullptr) {
		ADD_FAILURE() << "Missing polygon object " << object_id;
                static const quader_editor_core::PolygonObject kMissingObject{};
                return kMissingObject;
        }
	return *object;
}

inline std::size_t selected_face_count(const quader_editor_core::EditorSession& session)
{
	std::size_t count = 0;
	for (const quader_editor_core::PolygonObject& object : session.objects()) {
		count += object.selection.faces.size();
	}
	return count;
}

inline std::size_t selected_vertex_count(const quader_editor_core::EditorSession& session)
{
	std::size_t count = 0;
	for (const quader_editor_core::PolygonObject& object : session.objects()) {
		count += object.selection.vertices.size();
	}
	return count;
}

inline std::size_t selected_edge_count(const quader_editor_core::EditorSession& session)
{
	std::size_t count = 0;
	for (const quader_editor_core::PolygonObject& object : session.objects()) {
		count += object.selection.edges.size();
	}
	return count;
}

} // namespace qdr_test
