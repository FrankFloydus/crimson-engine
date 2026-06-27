////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/interactions/interaction_api.hpp>

#include "test_support.hpp"

#include <algorithm>

namespace {

quader_editor_core::EditorSession selected_offset_cube_session()
{
	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult result =
		session.create_box_from_bounds({ 1.0F, -1.0F, -1.0F }, { 3.0F, 1.0F, 1.0F });
	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	return session;
}

float object_center_x(const quader_editor_core::PolygonObject& object)
{
	EXPECT_FALSE(object.document.vertices.empty());
	float min_x = object.document.vertices.front().position.x;
	float max_x = object.document.vertices.front().position.x;
	for (const quader_poly::Vertex& vertex : object.document.vertices) {
		min_x = std::min(min_x, vertex.position.x);
		max_x = std::max(max_x, vertex.position.x);
	}
	return (min_x + max_x) * 0.5F;
}

float object_center_y(const quader_editor_core::PolygonObject& object)
{
	EXPECT_FALSE(object.document.vertices.empty());
	float min_y = object.document.vertices.front().position.y;
	float max_y = object.document.vertices.front().position.y;
	for (const quader_poly::Vertex& vertex : object.document.vertices) {
		min_y = std::min(min_y, vertex.position.y);
		max_y = std::max(max_y, vertex.position.y);
	}
	return (min_y + max_y) * 0.5F;
}

std::vector<quader::QVec3> yz_mirror_plane_points()
{
	return {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
		{ 0.0F, -1.0F, 1.0F },
	};
}

} // namespace

using qdr_test::find_object;

TEST(QdrModelingRuntimeOperationMirrorTests, MirrorOperationCreatesFreshSelectedMirroredObjectIds) {
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const int source_id = session.objects().front().id;
	const std::vector<int> selected_ids = session.selected_object_ids();
	const std::vector<quader::QVec3> plane_points = yz_mirror_plane_points();

	const quader_editor_core::CommandResult mirror =
		session.mirror_objects_across_plane(selected_ids, plane_points);

	EXPECT_TRUE(mirror.ok);
	EXPECT_TRUE(mirror.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
	EXPECT_TRUE(session.selected_object_ids().size() == 1U);
	const int copy_id = session.selected_object_ids().front();
	EXPECT_TRUE(copy_id != source_id);
	EXPECT_TRUE(session.active_object_id() == copy_id);
	EXPECT_TRUE(session.selection_mode() == quader_editor_core::SelectionMode::object);

	const quader_editor_core::PolygonObject* source = find_object(session, source_id);
	const quader_editor_core::PolygonObject* copy = find_object(session, copy_id);
	EXPECT_TRUE(source != nullptr);
	EXPECT_TRUE(copy != nullptr);
	EXPECT_FALSE(source->selected);
	EXPECT_TRUE(copy->selected);
	EXPECT_FALSE(source->document.vertices.empty());
	EXPECT_FALSE(copy->document.vertices.empty());
	EXPECT_NEAR(copy->document.vertices.front().position.x, -source->document.vertices.front().position.x, 0.0001);
	EXPECT_NEAR(copy->document.vertices.front().position.y, source->document.vertices.front().position.y, 0.0001);
	EXPECT_NEAR(copy->document.vertices.front().position.z, source->document.vertices.front().position.z, 0.0001);
	EXPECT_FALSE(quader_poly::compile_document(copy->document).indices.empty());
}

TEST(QdrModelingRuntimeOperationMirrorTests, AxisMirrorDuplicatesSelectedObjectsAroundWorldOrigin) {
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const int source_id = session.objects().front().id;

	const quader_editor_core::CommandResult mirror =
		session.mirror_selected_objects_across_axis(quader_editor_core::TransformAxis::x);

	EXPECT_TRUE(mirror.ok);
	EXPECT_TRUE(mirror.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
	EXPECT_TRUE(session.selected_object_ids().size() == 1U);
	const int copy_id = session.selected_object_ids().front();
	EXPECT_TRUE(copy_id != source_id);

	const quader_editor_core::PolygonObject* source = find_object(session, source_id);
	const quader_editor_core::PolygonObject* copy = find_object(session, copy_id);
	EXPECT_TRUE(source != nullptr);
	EXPECT_TRUE(copy != nullptr);
	EXPECT_TRUE(copy->selected);
	EXPECT_FALSE(source->selected);
	EXPECT_FALSE(source->document.vertices.empty());
	EXPECT_FALSE(copy->document.vertices.empty());
	EXPECT_NEAR(copy->document.vertices.front().position.x, -source->document.vertices.front().position.x, 0.0001);
	EXPECT_NEAR(copy->document.vertices.front().position.y, source->document.vertices.front().position.y, 0.0001);
	EXPECT_NEAR(copy->document.vertices.front().position.z, source->document.vertices.front().position.z, 0.0001);
	EXPECT_FALSE(quader_poly::compile_document(copy->document).indices.empty());
}

TEST(QdrModelingRuntimeOperationMirrorTests, MeshFlipMutatesSelectedObjectsInPlaceAroundEachMeshCenter) {
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const quader_editor_core::CommandResult second_result =
		session.create_box_from_bounds({ 9.0F, 3.0F, -1.0F }, { 11.0F, 5.0F, 1.0F });
	EXPECT_TRUE(second_result.ok);
	EXPECT_TRUE(second_result.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
	const int first_id = session.objects()[0].id;
	const int second_id = session.objects()[1].id;
	session.set_selected_object_ids({ first_id, second_id }, first_id);

	const quader_editor_core::PolygonObject* first_before_object = find_object(session, first_id);
	const quader_editor_core::PolygonObject* second_before_object = find_object(session, second_id);
	EXPECT_TRUE(first_before_object != nullptr);
	EXPECT_TRUE(second_before_object != nullptr);
	const quader::QVec3 first_before = first_before_object->document.vertices.front().position;
	const quader::QVec3 second_before = second_before_object->document.vertices.front().position;
	const float first_center_x = object_center_x(*first_before_object);
	const float second_center_x = object_center_x(*second_before_object);

	const quader_editor_core::CommandResult flip =
		session.flip_selected_objects_by_orientation(quader_editor_core::ViewportFlipOrientation::horizontal);

	EXPECT_TRUE(flip.ok);
	EXPECT_TRUE(flip.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
	const std::vector<int> selected_ids = session.selected_object_ids();
	EXPECT_TRUE(selected_ids.size() == 2U);
	EXPECT_TRUE(std::find(selected_ids.begin(), selected_ids.end(), first_id) != selected_ids.end());
	EXPECT_TRUE(std::find(selected_ids.begin(), selected_ids.end(), second_id) != selected_ids.end());
	EXPECT_TRUE(session.active_object_id() == first_id);

	const quader_editor_core::PolygonObject* first_flipped = find_object(session, first_id);
	const quader_editor_core::PolygonObject* second_flipped = find_object(session, second_id);
	EXPECT_TRUE(first_flipped != nullptr);
	EXPECT_TRUE(second_flipped != nullptr);
	EXPECT_FALSE(first_flipped->document.vertices.empty());
	EXPECT_FALSE(second_flipped->document.vertices.empty());
	EXPECT_NEAR(first_flipped->document.vertices.front().position.x, (2.0F * first_center_x) - first_before.x, 0.0001);
	EXPECT_NEAR(first_flipped->document.vertices.front().position.y, first_before.y, 0.0001);
	EXPECT_NEAR(first_flipped->document.vertices.front().position.z, first_before.z, 0.0001);
	EXPECT_NEAR(second_flipped->document.vertices.front().position.x, (2.0F * second_center_x) - second_before.x, 0.0001);
	EXPECT_NEAR(second_flipped->document.vertices.front().position.y, second_before.y, 0.0001);
	EXPECT_NEAR(second_flipped->document.vertices.front().position.z, second_before.z, 0.0001);
	EXPECT_FALSE(quader_poly::compile_document(first_flipped->document).indices.empty());
	EXPECT_FALSE(quader_poly::compile_document(second_flipped->document).indices.empty());
}

TEST(QdrModelingRuntimeOperationMirrorTests, VerticalMeshFlipUsesTheMeshCenterAndGlobalYAxis) {
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const int source_id = session.objects().front().id;
	const quader::QVec3 first_before = session.objects().front().document.vertices.front().position;
	const float center_y = object_center_y(session.objects().front());

	const quader_editor_core::CommandResult flip =
		session.flip_selected_objects_by_orientation(quader_editor_core::ViewportFlipOrientation::vertical);

	EXPECT_TRUE(flip.ok);
	EXPECT_TRUE(flip.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	EXPECT_TRUE(session.selected_object_ids().size() == 1U);
	EXPECT_TRUE(session.selected_object_ids().front() == source_id);
	EXPECT_TRUE(session.active_object_id() == source_id);

	const quader_editor_core::PolygonObject* flipped = find_object(session, source_id);
	EXPECT_TRUE(flipped != nullptr);
	EXPECT_FALSE(flipped->document.vertices.empty());
	EXPECT_NEAR(flipped->document.vertices.front().position.x, first_before.x, 0.0001);
	EXPECT_NEAR(flipped->document.vertices.front().position.y, (2.0F * center_y) - first_before.y, 0.0001);
	EXPECT_NEAR(flipped->document.vertices.front().position.z, first_before.z, 0.0001);
	EXPECT_FALSE(quader_poly::compile_document(flipped->document).indices.empty());
}

TEST(QdrModelingRuntimeOperationMirrorTests, MeshFlipRejectsComponentModeSelection) {
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
	session.set_component_selection(
		session.objects().front().id,
		qdr_test::vertex_selection({ vertex_id }, vertex_id),
		quader_editor_core::SelectionMode::vertex);

	const quader_editor_core::CommandResult flip =
		session.flip_selected_objects_by_orientation(quader_editor_core::ViewportFlipOrientation::vertical);

	EXPECT_FALSE(flip.ok);
	EXPECT_FALSE(flip.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	EXPECT_TRUE(flip.message == "Flip works only in object/mesh selection mode.");
}

TEST(QdrModelingRuntimeOperationMirrorTests, MirrorOperationRejectsComponentModeSelectionWithoutObjectCopies) {
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
	session.set_component_selection(
		session.objects().front().id,
		qdr_test::vertex_selection({ vertex_id }, vertex_id),
		quader_editor_core::SelectionMode::vertex);

	quader_editor_core::MirrorToolState state;
	state.plane.points = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.plane.face_normal = { 0.0F, 0.0F, 1.0F };

	const quader_editor_core::CommandResult mirror = session.commit_mirror_tool(state);

	EXPECT_FALSE(mirror.ok);
	EXPECT_FALSE(mirror.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	EXPECT_TRUE(quader_editor_core::mirror_is_active(state));
}

TEST(QdrModelingRuntimeOperationMirrorTests, CommitMirrorToolAppliesMirrorThroughEditorHistory) {
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const int source_id = session.objects().front().id;

	quader_editor_core::MirrorToolState state;
	state.plane.points = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.plane.face_normal = { 0.0F, 0.0F, 1.0F };
	state.target_object_ids = { source_id };

	const quader_editor_core::CommandResult result = session.commit_mirror_tool(state);

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
	EXPECT_TRUE(session.selected_object_ids().size() == 1U);
	EXPECT_TRUE(session.selected_object_ids().front() != source_id);
	EXPECT_FALSE(quader_editor_core::mirror_is_active(state));
	EXPECT_TRUE(session.undo().applied);
	EXPECT_TRUE(session.objects().size() == 1U);
}
