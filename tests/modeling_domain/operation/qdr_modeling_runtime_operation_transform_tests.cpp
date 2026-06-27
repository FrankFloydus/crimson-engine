////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

TEST(QdrModelingRuntimeOperationTransformTests, TransformOperationAppliesMatrixTransformsAroundTheRequestedPivotObjectModeScalesSelectedGeometryWithoutChangingTopology) {
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		quader_poly::Transform3 transform;
		transform.x_axis = { 2.0F, 0.0F, 0.0F };
		transform.y_axis = { 0.0F, 1.0F, 0.0F };
		transform.z_axis = { 0.0F, 0.0F, 1.0F };
		const std::size_t vertex_count_before = session.objects().front().document.vertices.size();

		const quader_editor_core::CommandResult result = session.transform_selected(transform, {});

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(session.objects().front().document.vertices.size() == vertex_count_before);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(session.objects().front().document));
	}

	

	

	
}

TEST(QdrModelingRuntimeOperationTransformTests, TransformOperationAppliesMatrixTransformsAroundTheRequestedPivotPivotControlsTheTransformedPoint) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader::QVec3 before = session.objects().front().document.vertices.front().position;
		quader_poly::Transform3 transform;
		transform.x_axis = { 2.0F, 0.0F, 0.0F };
		transform.y_axis = { 0.0F, 2.0F, 0.0F };
		transform.z_axis = { 0.0F, 0.0F, 2.0F };
		const quader::QVec3 pivot { 1.0F, 1.0F, 1.0F };

		const quader_editor_core::CommandResult result = session.transform_selected(transform, pivot);

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		const quader::QVec3 after = session.objects().front().document.vertices.front().position;
		EXPECT_TRUE(qdr_test::vec_nearly_equal(after, {
			pivot.x + ((before.x - pivot.x) * 2.0F),
			pivot.y + ((before.y - pivot.y) * 2.0F),
			pivot.z + ((before.z - pivot.z) * 2.0F),
		}));
	}

	

	
}

TEST(QdrModelingRuntimeOperationTransformTests, TransformOperationAppliesMatrixTransformsAroundTheRequestedPivotIdentityTransformsAreUnchanged) {
	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

		const quader_editor_core::CommandResult result = session.transform_selected({}, {});

		EXPECT_TRUE(result.ok);
		EXPECT_FALSE(result.changed);
	}

	
}

TEST(QdrModelingRuntimeOperationTransformTests, TransformOperationAppliesMatrixTransformsAroundTheRequestedPivotMultiObjectSelectionCenterUsesTheCombinedSelectionBounds) {
	

	

	

	{
		quader_editor_core::EditorSession session;
		EXPECT_TRUE(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed);
		const int first_id = session.objects().back().id;
		EXPECT_TRUE(session.create_box_from_bounds({ 9.0F, -1.0F, -1.0F }, { 11.0F, 1.0F, 1.0F }).changed);
		const int second_id = session.objects().back().id;
		session.set_selected_object_ids({ first_id, second_id }, second_id);

		const std::optional<quader::QVec3> center = session.selection_center();

		EXPECT_TRUE(center.has_value());
		EXPECT_TRUE(qdr_test::vec_nearly_equal(*center, { 5.0F, 0.0F, 0.0F }));
	}
}
