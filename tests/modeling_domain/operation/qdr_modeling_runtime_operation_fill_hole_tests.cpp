////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrModelingRuntimeOperationFillHoleTests, FillHoleSwitchesEditorSelectionToTheNewFaceAndRejectsRepeatedFills) {
	Document document = make_cube({});
	const Face removed_face = document.faces.front();
	document.faces.erase(document.faces.begin());

	Selection edge_selection;
        edge_selection.mode = SelectionMode::Edge;
        for (std::size_t index = 0; index < removed_face.vertices.size(); ++index) {
		edge_selection.edges.push_back(make_edge(
				removed_face.vertices[index],
				removed_face.vertices[(index + 1U) % removed_face.vertices.size()]));
	}

	quader_editor_core::EditorSession session({ qdr_test::make_object(1, document) });
        session.set_component_selection(
            1, edge_selection, quader_editor_core::SelectionMode::Edge);
        const std::size_t face_count_before = session.objects().front().document.faces.size();

	const quader_editor_core::CommandResult first_result = session.fill_hole_from_selected_edges();

	EXPECT_TRUE(first_result.ok);
	EXPECT_TRUE(first_result.changed);
        EXPECT_TRUE(session.selection_mode() ==
                    quader_editor_core::SelectionMode::Face);
        EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before + 1U);
        EXPECT_TRUE(session.objects().front().selection.mode ==
                    SelectionMode::Face);
        EXPECT_TRUE(session.objects().front().selection.faces.size() == 1U);

	const std::size_t face_count_after_fill = session.objects().front().document.faces.size();
	const quader_editor_core::CommandResult repeat_result = session.fill_hole_from_selected_edges();

	EXPECT_FALSE(repeat_result.ok);
	EXPECT_FALSE(repeat_result.changed);
	EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_after_fill);
}
