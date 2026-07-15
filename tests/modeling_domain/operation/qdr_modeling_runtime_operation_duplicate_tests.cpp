////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include "test_support.hpp"

using qdr_test::find_object;

TEST(QdrModelingRuntimeOperationDuplicateTests, DuplicateOperationCreatesFreshSelectedObjectIdsObjectModeDuplicatesTheCurrentSelection) {
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int source_id = session.objects().front().id;

		const quader_editor_core::CommandResult duplicate =
			session.duplicate_selected_objects(quader::QVec3 { 1.0F, 0.0F, 0.0F });

		EXPECT_TRUE(duplicate.ok);
		EXPECT_TRUE(duplicate.changed);
		EXPECT_TRUE(session.objects().size() == 2U);
		EXPECT_TRUE(session.selected_object_ids().size() == 1U);
		EXPECT_TRUE(session.selected_object_ids().front() != source_id);
		EXPECT_TRUE(session.active_object_id() == session.selected_object_ids().front());
	}

	

	

	

	
}

TEST(QdrModelingRuntimeOperationDuplicateTests, DuplicateOperationCreatesFreshSelectedObjectIdsComponentModeDuplicateReportsAnErrorWithoutObjectCopies) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
                session.set_component_selection(
                    session.objects().front().id,
                    qdr_test::vertex_selection({vertex_id}, vertex_id),
                    quader_editor_core::SelectionMode::Vertex);

                const quader_editor_core::CommandResult duplicate =
			session.duplicate_selected_objects(quader::QVec3 { 1.0F, 0.0F, 0.0F });

		EXPECT_FALSE(duplicate.ok);
		EXPECT_FALSE(duplicate.changed);
		EXPECT_TRUE(session.objects().size() == 1U);
	}

	

	

	
}

TEST(QdrModelingRuntimeOperationDuplicateTests, DuplicateOperationCreatesFreshSelectedObjectIdsOperationSettingsDuplicateInPlaceWhenOnlyFocusedGridIsProvided) {
	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int source_id = session.objects().front().id;

		quader_editor_core::DuplicateMeshesOperationSettings settings;
		settings.focused_grid = { 2.5, true };
		const quader_editor_core::CommandResult duplicate = session.duplicate_selected_objects(settings);

		EXPECT_TRUE(duplicate.ok);
		EXPECT_TRUE(duplicate.changed);
		EXPECT_TRUE(session.selected_object_ids().size() == 1U);
		const quader_editor_core::PolygonObject* source = find_object(session, source_id);
		const quader_editor_core::PolygonObject* copy = find_object(session, session.selected_object_ids().front());
		EXPECT_TRUE(source != nullptr);
		EXPECT_TRUE(copy != nullptr);
		EXPECT_FALSE(source->document.vertices.empty());
		EXPECT_FALSE(copy->document.vertices.empty());
		EXPECT_NEAR(copy->document.vertices.front().position.x - source->document.vertices.front().position.x, 0.0F, 0.0001);
		EXPECT_NEAR(copy->document.vertices.front().position.y - source->document.vertices.front().position.y, 0.0F, 0.0001);
		EXPECT_NEAR(copy->document.vertices.front().position.z - source->document.vertices.front().position.z, 0.0F, 0.0001);
	}

	

	
}

TEST(QdrModelingRuntimeOperationDuplicateTests, DuplicateOperationCreatesFreshSelectedObjectIdsOperationSettingsUseExplicitOffsetWhenProvided) {
	

	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int source_id = session.objects().front().id;

		quader_editor_core::DuplicateMeshesOperationSettings settings;
		settings.focused_grid = { 1.0, true };
		settings.explicit_offset = { { -2.0F, 0.5F, 3.0F }, true };
		const quader_editor_core::CommandResult duplicate = session.duplicate_selected_objects(settings);

		EXPECT_TRUE(duplicate.ok);
		EXPECT_TRUE(duplicate.changed);
		EXPECT_TRUE(session.selected_object_ids().size() == 1U);
		const quader_editor_core::PolygonObject* source = find_object(session, source_id);
		const quader_editor_core::PolygonObject* copy = find_object(session, session.selected_object_ids().front());
		EXPECT_TRUE(source != nullptr);
		EXPECT_TRUE(copy != nullptr);
		EXPECT_FALSE(source->document.vertices.empty());
		EXPECT_FALSE(copy->document.vertices.empty());
		EXPECT_NEAR(copy->document.vertices.front().position.x - source->document.vertices.front().position.x, 0.0F, 0.0001);
		EXPECT_NEAR(copy->document.vertices.front().position.y - source->document.vertices.front().position.y, 0.0F, 0.0001);
		EXPECT_NEAR(copy->document.vertices.front().position.z - source->document.vertices.front().position.z, 0.0F, 0.0001);
		EXPECT_TRUE(copy->document.shares_storage_with(source->document));
	}

	
}

TEST(QdrModelingRuntimeOperationDuplicateTests,
     PolygonObjectCopiesShareTopologyUntilTheFirstTopologyEdit) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	const int source_id = session.objects().front().id;
	ASSERT_TRUE(session
	                .duplicate_selected_objects(
	                    quader_editor_core::DuplicateMeshesOperationSettings{})
	                .ok);
	const quader_editor_core::PolygonObject* source = find_object(session, source_id);
	const quader_editor_core::PolygonObject* copy =
		find_object(session, session.selected_object_ids().front());
	ASSERT_NE(source, nullptr);
	ASSERT_NE(copy, nullptr);
	EXPECT_TRUE(source->document.shares_storage_with(copy->document));

	const std::uintptr_t shared_storage = source->document.storage_token();
	const quader_editor_core::CommandResult zero_translate =
		quader_editor_core::translate_selected(
			session.objects(), quader_editor_core::SelectionMode::Object,
			{}, false);
	EXPECT_TRUE(zero_translate.ok);
	EXPECT_FALSE(zero_translate.changed);
	EXPECT_EQ(session.objects().front().document.storage_token(),
	          shared_storage);
	EXPECT_EQ(session.objects().back().document.storage_token(),
	          shared_storage);

	const quader_editor_core::CommandResult identity_transform =
		quader_editor_core::transform_selected(
			session.objects(), quader_editor_core::SelectionMode::Object,
			quader_poly::Transform3{}, {}, false, false);
	EXPECT_TRUE(identity_transform.ok);
	EXPECT_FALSE(identity_transform.changed);
	EXPECT_TRUE(session.objects().front().document.shares_storage_with(
		session.objects().back().document));

	const quader::QVec3 source_position = source->document.vertices.front().position;
	auto copy_it = std::find_if(
		session.objects().begin(), session.objects().end(),
		[copy_id = copy->id](const quader_editor_core::PolygonObject& object) {
			return object.id == copy_id;
		});
	ASSERT_NE(copy_it, session.objects().end());
	copy_it->selected = false;
	EXPECT_TRUE(session.objects().front().document.shares_storage_with(
		copy_it->document));
	copy_it->document.edit().vertices.front().position.x += 1.0F;
	EXPECT_FALSE(session.objects().front().document.shares_storage_with(
		copy_it->document));
	EXPECT_TRUE(qdr_test::vec_nearly_equal(
		session.objects().front().document.vertices.front().position,
		source_position));
}

TEST(QdrModelingRuntimeOperationDuplicateTests, DuplicateOperationCreatesFreshSelectedObjectIdsOperationSettingsDuplicateInPlaceByDefault) {
	

	

	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int source_id = session.objects().front().id;

		const quader_editor_core::CommandResult duplicate =
			session.duplicate_selected_objects(quader_editor_core::DuplicateMeshesOperationSettings {});

		EXPECT_TRUE(duplicate.ok);
		EXPECT_TRUE(duplicate.changed);
		EXPECT_TRUE(session.selected_object_ids().size() == 1U);
		const quader_editor_core::PolygonObject* source = find_object(session, source_id);
		const quader_editor_core::PolygonObject* copy = find_object(session, session.selected_object_ids().front());
		EXPECT_TRUE(source != nullptr);
		EXPECT_TRUE(copy != nullptr);
		EXPECT_FALSE(source->document.vertices.empty());
		EXPECT_FALSE(copy->document.vertices.empty());
		EXPECT_NEAR(copy->document.vertices.front().position.x - source->document.vertices.front().position.x, 0.0F, 0.0001);
		EXPECT_NEAR(copy->document.vertices.front().position.y - source->document.vertices.front().position.y, 0.0F, 0.0001);
		EXPECT_NEAR(copy->document.vertices.front().position.z - source->document.vertices.front().position.z, 0.0F, 0.0001);
	}
}
