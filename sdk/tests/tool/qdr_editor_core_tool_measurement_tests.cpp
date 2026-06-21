#include <catch2/catch_test_macros.hpp>

#include <quader_editor_core/session.h>
#include <quader_editor_core/tools.h>
#include <quader_poly/document.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

quader_editor_core::ViewportCameraState front_camera()
{
	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;
	camera.transform.origin = { 0.0F, 0.0F, 5.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 800.0F, 600.0F };
	camera.fov_degrees = 60.0;
	return camera;
}

std::size_t frame_label_count(const quader_editor_core::MeasurementFrame& frame, const std::string& text)
{
	return static_cast<std::size_t>(std::count_if(frame.labels.begin(), frame.labels.end(), [&text](const quader_editor_core::MeasurementLabel& label) {
		return label.text == text;
	}));
}

quader_poly::Document box_document(quader_poly::Vec3 min_bounds, quader_poly::Vec3 max_bounds)
{
	quader_poly::CubePrimitiveSpec spec;
	spec.width = max_bounds.x - min_bounds.x;
	spec.height = max_bounds.y - min_bounds.y;
	spec.depth = max_bounds.z - min_bounds.z;
	quader_poly::Document document = quader_poly::make_cube(spec);
	const quader_poly::Vec3 center {
		(min_bounds.x + max_bounds.x) * 0.5F,
		(min_bounds.y + max_bounds.y) * 0.5F,
		(min_bounds.z + max_bounds.z) * 0.5F,
	};
	for (quader_poly::Vertex& vertex : document.vertices) {
		vertex.position += center;
	}
	return document;
}

quader_poly::Selection all_face_selection(const quader_poly::Document& document)
{
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::face;
	selection.faces.reserve(document.faces.size());
	for (const quader_poly::Face& face : document.faces) {
		selection.faces.push_back(face.id);
	}
	if (!selection.faces.empty()) {
		selection.has_active = true;
		selection.active_kind = quader_poly::ElementKind::face;
		selection.active_face = selection.faces.back();
	}
	return selection;
}

quader_editor_core::PolygonObject selected_object(int id, quader_poly::Vec3 min_bounds, quader_poly::Vec3 max_bounds)
{
	quader_editor_core::PolygonObject object;
	object.id = id;
	object.selected = true;
	object.document = box_document(min_bounds, max_bounds);
	return object;
}

quader_editor_core::PolygonObject selected_face_object(int id, quader_poly::Vec3 min_bounds, quader_poly::Vec3 max_bounds)
{
	quader_editor_core::PolygonObject object = selected_object(id, min_bounds, max_bounds);
	object.selection = all_face_selection(object.document);
	return object;
}

} // namespace

TEST_CASE("measurement frame reports selected object dimensions and transform drag delta", "[editor-core][tool][measurement]")
{
	quader_editor_core::PolygonObject object;
	object.id = 1;
	object.selected = true;
	object.document = quader_poly::make_cube({});

	quader_editor_core::EditorSession session(std::vector<quader_editor_core::PolygonObject> { object });
	session.state().active_object_id = 1;
	session.state().selection_mode = quader_editor_core::SelectionMode::object;
	quader_editor_core::SelectionHoverState hover;

	quader_editor_core::MeasurementFrameInput object_input;
	object_input.session = &session;
	object_input.hover = &hover;
	object_input.camera = front_camera();

	const quader_editor_core::MeasurementFrame object_frame =
		quader_editor_core::build_measurement_frame(object_input);
	CHECK(object_frame.labels.size() >= 3);
	CHECK(std::any_of(object_frame.labels.begin(), object_frame.labels.end(), [](const quader_editor_core::MeasurementLabel& label) {
		return label.text == "64";
	}));

	quader_editor_core::TransformDragState drag;
	drag.active = true;
	drag.tool = quader_editor_core::ViewportInteractionTool::move;
	drag.axis = quader_editor_core::TransformAxis::x;
	drag.move.center_world = {};
	drag.move.preview_delta = { 1.0F, 0.0F, 0.0F };

	quader_editor_core::MeasurementFrameInput drag_input;
	drag_input.transform_drag = &drag;
	drag_input.camera = front_camera();

	const quader_editor_core::MeasurementFrame drag_frame =
		quader_editor_core::build_measurement_frame(drag_input);
	REQUIRE(drag_frame.lines.size() == 1);
	REQUIRE(drag_frame.labels.size() == 1);
	CHECK(drag_frame.labels[0].text == "32.00");
	CHECK(drag_frame.labels[0].axis == quader_editor_core::TransformAxis::x);
}

TEST_CASE("measurement frame reports live box base and normal height dimensions", "[editor-core][tool][measurement]")
{
	quader_editor_core::BoxToolState box;
	REQUIRE(quader_editor_core::set_box_construction_plane(box, { 2.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }).ok);
	REQUIRE(quader_editor_core::begin_box_footprint(
			box,
			{ 2.0F, 0.0F, 0.0F },
			{ 2.0F, -2.0F, 3.0F },
			false,
			1.0).ok);

	quader_editor_core::MeasurementFrameInput footprint_input;
	footprint_input.box = &box;
	footprint_input.camera = front_camera();
	footprint_input.box_grid_size = 1.0;

	const quader_editor_core::MeasurementFrame footprint_frame =
		quader_editor_core::build_measurement_frame(footprint_input);
	REQUIRE(footprint_frame.labels.size() == 2U);
	CHECK(frame_label_count(footprint_frame, "64") == 1U);
	CHECK(frame_label_count(footprint_frame, "96") == 1U);

	box.drag_state = quader_editor_core::BoxDragState::height;
	box.height_distance = 4.0;
	const quader_editor_core::MeasurementFrame height_frame =
		quader_editor_core::build_measurement_frame(footprint_input);

	REQUIRE(height_frame.labels.size() == 3U);
	CHECK(frame_label_count(height_frame, "64") == 1U);
	CHECK(frame_label_count(height_frame, "96") == 1U);
	CHECK(frame_label_count(height_frame, "128") == 1U);
	CHECK(std::any_of(height_frame.labels.begin(), height_frame.labels.end(), [](const quader_editor_core::MeasurementLabel& label) {
		return label.text == "128" && label.axis == quader_editor_core::TransformAxis::x;
	}));
}

TEST_CASE("measurement frame follows scene transform preview presentation", "[editor-core][tool][measurement]")
{
	quader_editor_core::PolygonObject object;
	object.id = 1;
	object.selected = true;
	object.document = quader_poly::make_cube({});

	quader_editor_core::EditorSession session(std::vector<quader_editor_core::PolygonObject> { object });
	session.state().active_object_id = 1;
	session.state().selection_mode = quader_editor_core::SelectionMode::object;
	quader_editor_core::SelectionHoverState hover;

	quader_editor_core::MeasurementFrameInput base_input;
	base_input.session = &session;
	base_input.hover = &hover;
	base_input.camera = front_camera();

	const quader_editor_core::MeasurementFrame base_frame =
		quader_editor_core::build_measurement_frame(base_input);
	REQUIRE(base_frame.labels.size() == 3U);

	quader_editor_core::TransformDragState drag;
	drag.active = true;
	drag.tool = quader_editor_core::ViewportInteractionTool::move;
	drag.operation = quader_editor_core::TransformDragOperation::transform;
	drag.scene_transform_preview_eligible = true;
	drag.axis = quader_editor_core::TransformAxis::x;
	drag.move.active = true;
	drag.move.center_world = {};
	drag.move.preview_delta = { 1.0F, 0.0F, 0.0F };

	quader_editor_core::MeasurementFrameInput preview_input = base_input;
	preview_input.transform_drag = &drag;

	const quader_editor_core::MeasurementFrame preview_frame =
		quader_editor_core::build_measurement_frame(preview_input);
	REQUIRE(preview_frame.labels.size() >= 4U);
	CHECK(frame_label_count(preview_frame, "64") >= 3U);
	CHECK(frame_label_count(preview_frame, "32.00") == 1U);
	CHECK(preview_frame.lines.size() == 1U);
	CHECK(preview_frame.labels[0].screen_position.x > base_frame.labels[0].screen_position.x);
}

TEST_CASE("measurement frame reports one overall selected bounds measurement set", "[editor-core][tool][measurement]")
{
	quader_editor_core::SelectionHoverState hover;

	SECTION("object mode measures the combined selected object AABB")
	{
		quader_editor_core::EditorSession session(std::vector<quader_editor_core::PolygonObject> {
			selected_object(1, { -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }),
			selected_object(2, { 3.0F, -1.0F, -1.0F }, { 5.0F, 2.0F, 1.0F }),
		});
		session.state().active_object_id = 2;
		session.state().selection_mode = quader_editor_core::SelectionMode::object;

		quader_editor_core::MeasurementFrameInput input;
		input.session = &session;
		input.hover = &hover;
		input.camera = front_camera();

		const quader_editor_core::MeasurementFrame frame = quader_editor_core::build_measurement_frame(input);
		REQUIRE(frame.labels.size() == 3U);
		CHECK(frame_label_count(frame, "192") == 1U);
		CHECK(frame_label_count(frame, "96") == 1U);
		CHECK(frame_label_count(frame, "64") == 1U);
	}

	SECTION("face mode measures the combined selected face AABB")
	{
		quader_editor_core::EditorSession session(std::vector<quader_editor_core::PolygonObject> {
			selected_face_object(1, { -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }),
			selected_face_object(2, { 3.0F, -1.0F, -1.0F }, { 5.0F, 2.0F, 1.0F }),
		});
		session.state().active_object_id = 2;
		session.state().selection_mode = quader_editor_core::SelectionMode::face;

		quader_editor_core::MeasurementFrameInput input;
		input.session = &session;
		input.hover = &hover;
		input.camera = front_camera();

		const quader_editor_core::MeasurementFrame frame = quader_editor_core::build_measurement_frame(input);
		REQUIRE(frame.labels.size() == 3U);
		CHECK(frame_label_count(frame, "192") == 1U);
		CHECK(frame_label_count(frame, "96") == 1U);
		CHECK(frame_label_count(frame, "64") == 1U);
	}

	SECTION("selected bounds override supplies the measured dimensions")
	{
		quader_editor_core::EditorSession session(std::vector<quader_editor_core::PolygonObject> {
			selected_object(1, { -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }),
		});
		session.state().active_object_id = 1;
		session.state().selection_mode = quader_editor_core::SelectionMode::object;

		quader_editor_core::Bounds3 selectedBounds;
		selectedBounds.empty = false;
		selectedBounds.min = { -2.0F, -1.0F, -1.0F };
		selectedBounds.max = { 3.0F, 2.0F, 1.0F };

		quader_editor_core::MeasurementFrameInput input;
		input.session = &session;
		input.hover = &hover;
		input.selected_bounds_override = &selectedBounds;
		input.camera = front_camera();

		const quader_editor_core::MeasurementFrame frame = quader_editor_core::build_measurement_frame(input);
		REQUIRE(frame.labels.size() == 3U);
		CHECK(frame_label_count(frame, "160") == 1U);
		CHECK(frame_label_count(frame, "96") == 1U);
		CHECK(frame_label_count(frame, "64") == 1U);
	}
}
