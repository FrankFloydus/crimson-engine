#include "test_support.h"

#include <quader_editor_core/session.h>
#include <quader_editor_core/tools.h>
#include <quader_poly/document.h>

#include <algorithm>
#include <iostream>
#include <string>

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
	camera.fov_degrees = 70.0;
	return camera;
}

bool frame_has_label(const quader_editor_core::MeasurementFrame& frame, const std::string& text)
{
	return std::any_of(frame.labels.begin(), frame.labels.end(), [&text](const quader_editor_core::MeasurementLabel& label) {
		return label.text == text;
	});
}

quader_editor_core::EditorSession selected_box_session()
{
	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult result =
		session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F });
	require_test(result.ok && result.changed, "measurement fixture should create a selected box");
	return session;
}

void measurement_text_formatting_is_stable()
{
	require_test(quader_editor_core::measurement_length_text(2.0) == "64", "integer map-unit lengths should omit decimals");
	require_test(quader_editor_core::measurement_length_text(2.0, 32.0, true) == "64.00", "forced map-unit lengths should keep two decimals");
	require_test(quader_editor_core::measurement_length_text(1.5, 16.0) == "24", "custom map-unit scale should be applied before formatting");
	require_test(quader_editor_core::measurement_number_text(0.5, true) == "0.50", "forced numeric measurement should keep two decimals");
}

void selected_object_measurements_follow_custom_map_units()
{
	quader_editor_core::EditorSession session = selected_box_session();
	quader_editor_core::SelectionHoverState hover;
	quader_editor_core::MeasurementFrameInput input;
	input.session = &session;
	input.hover = &hover;
	input.camera = front_camera();
	input.map_units_per_world_unit = 16.0;

	const quader_editor_core::MeasurementFrame frame = quader_editor_core::build_measurement_frame(input);
	require_test(frame.labels.size() >= 3, "selected object should emit dimension labels");
	require_test(frame_has_label(frame, "32"), "2 world-unit selected object should report 32 units at custom scale 16");
}

void hover_edge_measurement_forces_decimal_text()
{
	quader_editor_core::EditorSession session = selected_box_session();
	const std::vector<quader_poly::Edge> edges = quader_poly::document_edges(session.objects().front().document);
	require_test(!edges.empty(), "hover edge measurement fixture should expose document edges");

	quader_editor_core::SelectionHoverState hover;
	hover.object_id = session.objects().front().id;
	hover.kind = quader_editor_core::SelectionHoverKind::edge;
	hover.edge = edges.front();

	quader_editor_core::MeasurementFrameInput input;
	input.session = &session;
	input.hover = &hover;
	input.camera = front_camera();

	const quader_editor_core::MeasurementFrame frame = quader_editor_core::build_measurement_frame(input);
	require_test(frame_has_label(frame, "64.00"), "hovered 2 world-unit edge should report forced decimal map units");
}

void box_height_measurements_report_footprint_and_height()
{
	quader_editor_core::BoxToolState box;
	require_test(quader_editor_core::set_box_construction_plane(box, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }).ok,
		"box measurement fixture should set a construction plane");
	require_test(quader_editor_core::begin_box_footprint(box, { 0.0F, 0.0F, 0.0F }, { 2.0F, 0.0F, 3.0F }, true, 1.0).ok,
		"box measurement fixture should begin a footprint");
	box.height_distance = 1.0;
	require_test(quader_editor_core::begin_box_height(box, { 400.0F, 300.0F }, 1.0, true, true, 1.0).ok,
		"box measurement fixture should begin height preview");
	require_test(quader_editor_core::update_box_height_preview(box, 1.0, true, { 400.0F, 250.0F }, true, 1.0).ok,
		"box measurement fixture should update height preview");

	quader_editor_core::MeasurementFrameInput input;
	input.box = &box;
	input.camera = front_camera();
	input.box_grid_size = 1.0;

	const quader_editor_core::MeasurementFrame frame = quader_editor_core::build_measurement_frame(input);
	require_test(frame.labels.size() >= 3, "box height preview should emit footprint and height labels");
	require_test(frame_has_label(frame, "64"), "2 world-unit box footprint width should report 64 map units");
	require_test(frame_has_label(frame, "96"), "3 world-unit box footprint depth should report 96 map units");
	require_test(frame_has_label(frame, "32"), "1 world-unit box height should report 32 map units");
}

} // namespace

TEST_CASE("qdr_editor_core_measurement_tests", "[editor-core-measurement]")
{
	measurement_text_formatting_is_stable();
	selected_object_measurements_follow_custom_map_units();
	hover_edge_measurement_forces_decimal_text();
	box_height_measurements_report_footprint_and_height();
}
