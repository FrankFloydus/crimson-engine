#include "editor_operation_test_helpers.h"

#include <quader_editor_core/tools.h>

#include <catch2/catch_approx.hpp>

#include <algorithm>

namespace {

const quader_editor_core::PolygonObject* find_object(const quader_editor_core::EditorSession& session, int object_id)
{
	const auto& objects = session.objects();
	const auto it = std::find_if(objects.begin(), objects.end(), [object_id](const quader_editor_core::PolygonObject& object) {
		return object.id == object_id;
	});
	return it == objects.end() ? nullptr : &*it;
}

quader_editor_core::EditorSession selected_offset_cube_session()
{
	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult result =
		session.create_box_from_bounds({ 1.0F, -1.0F, -1.0F }, { 3.0F, 1.0F, 1.0F });
	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(session.objects().size() == 1U);
	return session;
}

float object_center_x(const quader_editor_core::PolygonObject& object)
{
	REQUIRE_FALSE(object.document.vertices.empty());
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
	REQUIRE_FALSE(object.document.vertices.empty());
	float min_y = object.document.vertices.front().position.y;
	float max_y = object.document.vertices.front().position.y;
	for (const quader_poly::Vertex& vertex : object.document.vertices) {
		min_y = std::min(min_y, vertex.position.y);
		max_y = std::max(max_y, vertex.position.y);
	}
	return (min_y + max_y) * 0.5F;
}

std::vector<quader_poly::Vec3> yz_mirror_plane_points()
{
	return {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
		{ 0.0F, -1.0F, 1.0F },
	};
}

} // namespace

TEST_CASE("mirror operation creates fresh selected mirrored object ids", "[operation][mirror]")
{
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const int source_id = session.objects().front().id;
	const std::vector<int> selected_ids = session.selected_object_ids();
	const std::vector<quader_poly::Vec3> plane_points = yz_mirror_plane_points();

	const quader_editor_core::CommandResult mirror =
		session.mirror_objects_across_plane(selected_ids, plane_points);

	REQUIRE(mirror.ok);
	REQUIRE(mirror.changed);
	REQUIRE(session.objects().size() == 2U);
	REQUIRE(session.selected_object_ids().size() == 1U);
	const int copy_id = session.selected_object_ids().front();
	CHECK(copy_id != source_id);
	CHECK(session.active_object_id() == copy_id);
	CHECK(session.selection_mode() == quader_editor_core::SelectionMode::object);

	const quader_editor_core::PolygonObject* source = find_object(session, source_id);
	const quader_editor_core::PolygonObject* copy = find_object(session, copy_id);
	REQUIRE(source != nullptr);
	REQUIRE(copy != nullptr);
	REQUIRE_FALSE(source->selected);
	REQUIRE(copy->selected);
	REQUIRE_FALSE(source->document.vertices.empty());
	REQUIRE_FALSE(copy->document.vertices.empty());
	CHECK(copy->document.vertices.front().position.x == Catch::Approx(-source->document.vertices.front().position.x));
	CHECK(copy->document.vertices.front().position.y == Catch::Approx(source->document.vertices.front().position.y));
	CHECK(copy->document.vertices.front().position.z == Catch::Approx(source->document.vertices.front().position.z));
	CHECK_FALSE(quader_poly::compile_document(copy->document).indices.empty());
}

TEST_CASE("axis mirror duplicates selected objects around world origin", "[operation][mirror]")
{
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const int source_id = session.objects().front().id;

	const quader_editor_core::CommandResult mirror =
		session.mirror_selected_objects_across_axis(quader_editor_core::TransformAxis::x);

	REQUIRE(mirror.ok);
	REQUIRE(mirror.changed);
	REQUIRE(session.objects().size() == 2U);
	REQUIRE(session.selected_object_ids().size() == 1U);
	const int copy_id = session.selected_object_ids().front();
	REQUIRE(copy_id != source_id);

	const quader_editor_core::PolygonObject* source = find_object(session, source_id);
	const quader_editor_core::PolygonObject* copy = find_object(session, copy_id);
	REQUIRE(source != nullptr);
	REQUIRE(copy != nullptr);
	CHECK(copy->selected);
	CHECK_FALSE(source->selected);
	REQUIRE_FALSE(source->document.vertices.empty());
	REQUIRE_FALSE(copy->document.vertices.empty());
	CHECK(copy->document.vertices.front().position.x == Catch::Approx(-source->document.vertices.front().position.x));
	CHECK(copy->document.vertices.front().position.y == Catch::Approx(source->document.vertices.front().position.y));
	CHECK(copy->document.vertices.front().position.z == Catch::Approx(source->document.vertices.front().position.z));
	CHECK_FALSE(quader_poly::compile_document(copy->document).indices.empty());
}

TEST_CASE("mesh flip mutates selected objects in place around each mesh center", "[operation][mirror]")
{
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const quader_editor_core::CommandResult second_result =
		session.create_box_from_bounds({ 9.0F, 3.0F, -1.0F }, { 11.0F, 5.0F, 1.0F });
	REQUIRE(second_result.ok);
	REQUIRE(second_result.changed);
	REQUIRE(session.objects().size() == 2U);
	const int first_id = session.objects()[0].id;
	const int second_id = session.objects()[1].id;
	session.set_selected_object_ids({ first_id, second_id }, first_id);

	const quader_editor_core::PolygonObject* first_before_object = find_object(session, first_id);
	const quader_editor_core::PolygonObject* second_before_object = find_object(session, second_id);
	REQUIRE(first_before_object != nullptr);
	REQUIRE(second_before_object != nullptr);
	const quader_poly::Vec3 first_before = first_before_object->document.vertices.front().position;
	const quader_poly::Vec3 second_before = second_before_object->document.vertices.front().position;
	const float first_center_x = object_center_x(*first_before_object);
	const float second_center_x = object_center_x(*second_before_object);

	const quader_editor_core::CommandResult flip =
		session.flip_selected_objects_by_orientation(quader_editor_core::ViewportFlipOrientation::horizontal);

	REQUIRE(flip.ok);
	REQUIRE(flip.changed);
	REQUIRE(session.objects().size() == 2U);
	const std::vector<int> selected_ids = session.selected_object_ids();
	REQUIRE(selected_ids.size() == 2U);
	CHECK(std::find(selected_ids.begin(), selected_ids.end(), first_id) != selected_ids.end());
	CHECK(std::find(selected_ids.begin(), selected_ids.end(), second_id) != selected_ids.end());
	CHECK(session.active_object_id() == first_id);

	const quader_editor_core::PolygonObject* first_flipped = find_object(session, first_id);
	const quader_editor_core::PolygonObject* second_flipped = find_object(session, second_id);
	REQUIRE(first_flipped != nullptr);
	REQUIRE(second_flipped != nullptr);
	REQUIRE_FALSE(first_flipped->document.vertices.empty());
	REQUIRE_FALSE(second_flipped->document.vertices.empty());
	CHECK(first_flipped->document.vertices.front().position.x == Catch::Approx((2.0F * first_center_x) - first_before.x));
	CHECK(first_flipped->document.vertices.front().position.y == Catch::Approx(first_before.y));
	CHECK(first_flipped->document.vertices.front().position.z == Catch::Approx(first_before.z));
	CHECK(second_flipped->document.vertices.front().position.x == Catch::Approx((2.0F * second_center_x) - second_before.x));
	CHECK(second_flipped->document.vertices.front().position.y == Catch::Approx(second_before.y));
	CHECK(second_flipped->document.vertices.front().position.z == Catch::Approx(second_before.z));
	CHECK_FALSE(quader_poly::compile_document(first_flipped->document).indices.empty());
	CHECK_FALSE(quader_poly::compile_document(second_flipped->document).indices.empty());
}

TEST_CASE("vertical mesh flip uses the mesh center and global Y axis", "[operation][mirror]")
{
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const int source_id = session.objects().front().id;
	const quader_poly::Vec3 first_before = session.objects().front().document.vertices.front().position;
	const float center_y = object_center_y(session.objects().front());

	const quader_editor_core::CommandResult flip =
		session.flip_selected_objects_by_orientation(quader_editor_core::ViewportFlipOrientation::vertical);

	REQUIRE(flip.ok);
	REQUIRE(flip.changed);
	REQUIRE(session.objects().size() == 1U);
	REQUIRE(session.selected_object_ids().size() == 1U);
	CHECK(session.selected_object_ids().front() == source_id);
	CHECK(session.active_object_id() == source_id);

	const quader_editor_core::PolygonObject* flipped = find_object(session, source_id);
	REQUIRE(flipped != nullptr);
	REQUIRE_FALSE(flipped->document.vertices.empty());
	CHECK(flipped->document.vertices.front().position.x == Catch::Approx(first_before.x));
	CHECK(flipped->document.vertices.front().position.y == Catch::Approx((2.0F * center_y) - first_before.y));
	CHECK(flipped->document.vertices.front().position.z == Catch::Approx(first_before.z));
	CHECK_FALSE(quader_poly::compile_document(flipped->document).indices.empty());
}

TEST_CASE("mesh flip rejects component-mode selection", "[operation][mirror]")
{
	quader_editor_core::EditorSession session = selected_offset_cube_session();
	const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
	session.set_component_selection(
		session.objects().front().id,
		qdr_test::vertex_selection({ vertex_id }, vertex_id),
		quader_editor_core::SelectionMode::vertex);

	const quader_editor_core::CommandResult flip =
		session.flip_selected_objects_by_orientation(quader_editor_core::ViewportFlipOrientation::vertical);

	REQUIRE_FALSE(flip.ok);
	REQUIRE_FALSE(flip.changed);
	REQUIRE(session.objects().size() == 1U);
	CHECK(flip.message == "Flip works only in object/mesh selection mode.");
}

TEST_CASE("mirror operation rejects component-mode selection without object copies", "[operation][mirror]")
{
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

	REQUIRE_FALSE(mirror.ok);
	REQUIRE_FALSE(mirror.changed);
	REQUIRE(session.objects().size() == 1U);
	CHECK(quader_editor_core::mirror_is_active(state));
}

TEST_CASE("commit mirror tool applies mirror through editor history", "[operation][mirror]")
{
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

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(session.objects().size() == 2U);
	REQUIRE(session.selected_object_ids().size() == 1U);
	CHECK(session.selected_object_ids().front() != source_id);
	CHECK_FALSE(quader_editor_core::mirror_is_active(state));
	REQUIRE(session.undo().applied);
	REQUIRE(session.objects().size() == 1U);
}
