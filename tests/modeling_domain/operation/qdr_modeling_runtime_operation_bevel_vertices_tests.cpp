////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include "test_support.hpp"

TEST(QdrModelingRuntimeOperationBevelVerticesTests, BevelVertexCommandSettingsUseFocusedGridDistance) {
	quader_editor_core::BevelVerticesOperationSettings settings;
	settings.focused_grid = { 0.25, true };

	EXPECT_NEAR(quader_editor_core::bevel_vertices_distance_from_settings(settings), 0.25F, 0.0001);

	settings.focused_grid = { 0.0, false };
        EXPECT_NEAR(
            quader_editor_core::bevel_vertices_distance_from_settings(settings),
            static_cast<float>(
                quader_editor_core::kDefaultCommandGridWorldSize),
            0.0001);

        settings.focused_grid = { 1.0, true };
	settings.explicit_distance = { 0.125, true };
	EXPECT_NEAR(quader_editor_core::bevel_vertices_distance_from_settings(settings), 0.125F, 0.0001);
}

TEST(QdrModelingRuntimeOperationBevelVerticesTests, BevelVertexCommandSettingsApplyThroughEditorSession) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	const int object_id = session.objects().front().id;
	const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
        session.set_component_selection(
            object_id, qdr_test::vertex_selection({vertex_id}, vertex_id),
            quader_editor_core::SelectionMode::Vertex);

        quader_editor_core::BevelVerticesOperationSettings settings;
	settings.focused_grid = { 0.25, true };
	const quader_editor_core::CommandResult bevel = session.bevel_selected_vertices(settings);

	EXPECT_TRUE(bevel.ok);
	EXPECT_TRUE(bevel.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
        EXPECT_TRUE(session.objects().front().selection.mode ==
                    quader_poly::SelectionMode::Vertex);
        EXPECT_TRUE(session.objects().front().selection.vertices.size() >= 2U);
}
