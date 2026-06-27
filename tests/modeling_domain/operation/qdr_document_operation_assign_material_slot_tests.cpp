////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationAssignMaterialSlotTests, AssignMaterialSlotOperationUpdatesSelectedFaces) {
	Document document = make_cube({});
	EXPECT_TRUE(document.faces.size() >= 2U);
	const ElementId selected_face_id = document.faces[0].id;
	const ElementId untouched_face_id = document.faces[1].id;
	for (Face& face : document.faces) {
		face.material_slot = 7U;
	}

	const OperationResult result =
		assign_selected_face_material_slot(document, qdr_test::face_selection({ selected_face_id }, selected_face_id), 3U);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(find_face(document, selected_face_id)->material_slot == 3U);
	EXPECT_TRUE(find_face(document, untouched_face_id)->material_slot == 7U);
}

TEST(QdrDocumentOperationAssignMaterialSlotTests, AssignMaterialSlotOperationTreatsSlotZeroAsAValidDefault) {
	Document document = make_cube({});
	EXPECT_FALSE(document.faces.empty());
	const ElementId face_id = document.faces.front().id;
	for (Face& face : document.faces) {
		face.material_slot = 5U;
	}

	const OperationResult result =
		assign_selected_face_material_slot(document, qdr_test::face_selection({ face_id }, face_id), 0U);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(find_face(document, face_id)->material_slot == 0U);
}

TEST(QdrDocumentOperationAssignMaterialSlotTests, AssignMaterialSlotOperationSupportsOneExplicitFaceTarget) {
	Document document = make_cube({});
	EXPECT_TRUE(document.faces.size() >= 2U);
	const ElementId target_face_id = document.faces[1].id;
	const ElementId untouched_face_id = document.faces[0].id;

	const OperationResult result = assign_face_material_slot(document, target_face_id, 11U);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(find_face(document, target_face_id)->material_slot == 11U);
	EXPECT_TRUE(find_face(document, untouched_face_id)->material_slot == 0U);
}
