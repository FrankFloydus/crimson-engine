////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/operations/operation_schema.hpp>

#include <algorithm>
#include <limits>

namespace {

quader::QVec3 first_vertex_position(const quader_poly::Document& document)
{
	EXPECT_FALSE(document.vertices.empty());
	return document.vertices.front().position;
}

quader_poly::ElementId first_vertex_id(const quader_poly::Document& document)
{
	EXPECT_FALSE(document.vertices.empty());
	return document.vertices.front().id;
}

quader::QVec3 vertex_position(const quader_poly::Document& document, quader_poly::ElementId vertex_id)
{
	const quader_poly::Vertex* vertex = quader_poly::find_vertex(document, vertex_id);
	EXPECT_TRUE(vertex != nullptr);
	return vertex->position;
}

quader::QVec3 averaged_face_normal_for_vertex(const quader_poly::Document& document, quader_poly::ElementId vertex_id)
{
	quader::QVec3 normal {};
	for (const quader_poly::Face& face : document.faces) {
		if (std::find(face.vertices.begin(), face.vertices.end(), vertex_id) != face.vertices.end()) {
			normal += quader_poly::face_normal(document, face);
		}
	}
	return quader::normalize_or_zero(normal);
}

} // namespace

using qdr_test::find_object;

TEST(QdrModelingRuntimeOperationThickenTests, ThickenOperationSchemaKeepsSoftThicknessLimitsAndHiddenCenterToggle) {
	const quader_editor_core::ThickenOperationSchema thicken = quader_editor_core::thicken_operation_schema();

	EXPECT_TRUE(thicken.id == quader_editor_core::EditorOperationId::thicken);
	EXPECT_TRUE(std::string(thicken.operation_id) == "thicken");
	EXPECT_TRUE(std::string(thicken.thickness.id) == "thickness");
	EXPECT_TRUE(std::string(thicken.from_center.id) == "from_center");
	EXPECT_NEAR(thicken.thickness.default_value, 0.25, 0.0001);
	EXPECT_NEAR(thicken.thickness.minimum, -4.0, 0.0001);
	EXPECT_NEAR(thicken.thickness.maximum, 4.0, 0.0001);
	EXPECT_FALSE(thicken.thickness.hard_limits);
	EXPECT_FALSE(thicken.from_center.visible);

	quader_editor_core::ThickenOperationSettings settings;
	settings.thickness = 100.0;
	settings.from_center = true;
	const quader_editor_core::ThickenOperationSettings normalized =
		quader_editor_core::normalize_thicken_operation_settings(settings);
	EXPECT_NEAR(normalized.thickness, 100.0, 0.0001);
	EXPECT_TRUE(normalized.from_center);

	settings.thickness = -std::numeric_limits<double>::infinity();
	const quader_editor_core::ThickenOperationSettings invalid =
		quader_editor_core::normalize_thicken_operation_settings(settings);
	EXPECT_NEAR(invalid.thickness, thicken.thickness.default_value, 0.0001);
}

TEST(QdrModelingRuntimeOperationThickenTests, ThickenOperationPreviewsGeneratedTopologyTransactionallyObjectModeCreatesASeparateOuterHullWithoutMutatingTheSourceMesh) {
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		EXPECT_TRUE(session.selection_mode() == quader_editor_core::SelectionMode::object);
		EXPECT_TRUE(session.selected_object_ids().size() == 1U);
		const int source_id = session.selected_object_ids().front();
		const int next_id_before = session.next_object_id();
		const std::size_t object_count_before = session.objects().size();
		const std::size_t vertex_count_before = session.objects().front().document.vertices.size();
		const std::size_t face_count_before = session.objects().front().document.faces.size();
		const quader_poly::ElementId source_vertex_id = first_vertex_id(session.objects().front().document);
		const quader::QVec3 source_position_before = first_vertex_position(session.objects().front().document);
		const quader::QVec3 source_normal = averaged_face_normal_for_vertex(session.objects().front().document, source_vertex_id);
		EXPECT_TRUE(quader::length_squared(source_normal) > 0.0F);

		EXPECT_TRUE(session.begin_thicken_preview(0.25F, false).changed);

		EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::thicken);
		EXPECT_TRUE(session.selection_mode() == quader_editor_core::SelectionMode::object);
		EXPECT_TRUE(session.objects().size() == object_count_before + 1U);
		EXPECT_TRUE(session.selected_object_ids().size() == 1U);
		const int hull_id = session.selected_object_ids().front();
		EXPECT_TRUE(hull_id != source_id);
		EXPECT_TRUE(hull_id == next_id_before);
		const quader_editor_core::PolygonObject* source = find_object(session, source_id);
		const quader_editor_core::PolygonObject* hull = find_object(session, hull_id);
		EXPECT_TRUE(source != nullptr);
		EXPECT_TRUE(hull != nullptr);
		EXPECT_FALSE(source->selected);
		EXPECT_TRUE(hull->selected);
		EXPECT_TRUE(source->document.vertices.size() == vertex_count_before);
		EXPECT_TRUE(source->document.faces.size() == face_count_before);
		EXPECT_TRUE(hull->document.vertices.size() == vertex_count_before);
		EXPECT_TRUE(hull->document.faces.size() == face_count_before);
		EXPECT_NEAR(quader::length(vertex_position(source->document, source_vertex_id) - source_position_before), 0.0F, 0.0001);
		const quader::QVec3 displacement = vertex_position(hull->document, source_vertex_id) - source_position_before;
		EXPECT_NEAR(quader::length(displacement), 0.25F, 0.0001);
		EXPECT_NEAR(quader::dot(displacement, source_normal), -0.25F, 0.0001);

		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);
		EXPECT_TRUE(commit.before.changed);
		EXPECT_TRUE(session.objects().size() == object_count_before + 1U);
		EXPECT_TRUE(session.history_status().can_undo);
		EXPECT_TRUE(session.undo().applied);
		EXPECT_TRUE(session.selection_mode() == quader_editor_core::SelectionMode::object);
		EXPECT_TRUE(session.objects().size() == object_count_before);
		EXPECT_TRUE(session.selected_object_ids().size() == 1U);
		EXPECT_TRUE(session.selected_object_ids().front() == source_id);
	}

	

	

	

	

	
}

TEST(QdrModelingRuntimeOperationThickenTests, ThickenOperationPreviewsGeneratedTopologyTransactionallyObjectModeCancelAndUpdateRebuildOneOuterHullFromThePreviewBase) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const std::size_t object_count_before = session.objects().size();
		const quader_poly::ElementId source_vertex_id = first_vertex_id(session.objects().front().document);
		const quader::QVec3 source_position_before = first_vertex_position(session.objects().front().document);
		const quader::QVec3 source_normal = averaged_face_normal_for_vertex(session.objects().front().document, source_vertex_id);
		EXPECT_TRUE(quader::length_squared(source_normal) > 0.0F);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		EXPECT_TRUE(session.begin_thicken_preview(0.25F, false).changed);
		EXPECT_TRUE(session.objects().size() == object_count_before + 1U);
		const quader_editor_core::PolygonObject* first_hull = find_object(session, session.selected_object_ids().front());
		EXPECT_TRUE(first_hull != nullptr);
		quader::QVec3 displacement = vertex_position(first_hull->document, source_vertex_id) - source_position_before;
		EXPECT_NEAR(quader::length(displacement), 0.25F, 0.0001);
		EXPECT_NEAR(quader::dot(displacement, source_normal), -0.25F, 0.0001);
		EXPECT_TRUE(session.update_thicken_preview(0.5F, false).changed);
		EXPECT_TRUE(session.objects().size() == object_count_before + 1U);
		const quader_editor_core::PolygonObject* updated_hull = find_object(session, session.selected_object_ids().front());
		EXPECT_TRUE(updated_hull != nullptr);
		displacement = vertex_position(updated_hull->document, source_vertex_id) - source_position_before;
		EXPECT_NEAR(quader::length(displacement), 0.5F, 0.0001);
		EXPECT_NEAR(quader::dot(displacement, source_normal), -0.5F, 0.0001);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		EXPECT_TRUE(cancel.restored);
		EXPECT_TRUE(session.objects().size() == object_count_before);
		EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before);
		EXPECT_FALSE(session.history_status().can_undo);
	}

	

	

	

	
}

TEST(QdrModelingRuntimeOperationThickenTests, ThickenOperationPreviewsGeneratedTopologyTransactionallyCommitRecordsGeneratedTopologyAndUndoRestoresTheBase) {
	

	

	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::face_selection({ session.objects().front().document.faces.front().id }), quader_editor_core::SelectionMode::face);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		EXPECT_TRUE(session.begin_thicken_preview(0.25F, false).changed);
		EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::thicken);
		EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);
		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);
		EXPECT_TRUE(commit.before.changed);
		EXPECT_TRUE(session.history_status().can_undo);
		EXPECT_TRUE(session.undo().applied);
		EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before);
	}

	

	

	
}

TEST(QdrModelingRuntimeOperationThickenTests, ThickenOperationPreviewsGeneratedTopologyTransactionallyZeroThicknessPreviewStaysActiveButUnchangedUntilCommitRestoresBase) {
	

	

	

	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::face_selection({ session.objects().front().document.faces.front().id }), quader_editor_core::SelectionMode::face);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		const quader_editor_core::CommandResult begin = session.begin_thicken_preview(0.0F, false);
		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		EXPECT_TRUE(begin.ok);
		EXPECT_FALSE(begin.changed);
		EXPECT_TRUE(commit.restored);
		EXPECT_FALSE(session.history_status().can_undo);
		EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before);
	}

	

	
}

TEST(QdrModelingRuntimeOperationThickenTests, ThickenOperationPreviewsGeneratedTopologyTransactionallySchemaSettingsPreserveSoftThicknessValuesForPreview) {
	

	

	

	

	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::face_selection({ session.objects().front().document.faces.front().id }), quader_editor_core::SelectionMode::face);

		quader_editor_core::ThickenOperationSettings settings;
		settings.thickness = -0.25;
		settings.from_center = true;
		const quader_editor_core::CommandResult begin = session.begin_thicken_preview(settings);

		EXPECT_TRUE(begin.ok);
		EXPECT_TRUE(begin.changed);
		EXPECT_TRUE(session.preview_transaction_status().active);
		EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::thicken);
	}

	
}

TEST(QdrModelingRuntimeOperationThickenTests, ThickenOperationPreviewsGeneratedTopologyTransactionallyCancelAndUpdateRebuildFromThePreviewBase) {
	

	

	

	

	

	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(
			1,
			qdr_test::face_selection({ session.objects().front().document.faces.front().id }, session.objects().front().document.faces.front().id),
			quader_editor_core::SelectionMode::face);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		const quader_editor_core::CommandResult begin = session.begin_thicken_preview(0.25F, true);

		EXPECT_TRUE(begin.ok);
		EXPECT_TRUE(begin.changed);
		EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::thicken);
		EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);
		EXPECT_TRUE(session.update_thicken_preview(0.5F, true).changed);
		EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		EXPECT_TRUE(cancel.restored);
		EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before);
		EXPECT_FALSE(session.history_status().can_undo);

		EXPECT_TRUE(session.begin_thicken_preview(0.25F, true).changed);
		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		EXPECT_TRUE(commit.before.changed);
		EXPECT_FALSE(commit.restored);
		EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);
		EXPECT_TRUE(session.history_status().undo_count == 1U);

		const quader_editor_core::HistoryRestoreResult undo = session.undo();
		EXPECT_TRUE(undo.applied);
		EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before);
	}
}
