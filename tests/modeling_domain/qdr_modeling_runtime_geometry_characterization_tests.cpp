////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/interaction_api.hpp>
#include <mesh/polygon/document.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool nearly_equal(double left, double right, double epsilon = 0.0001)
{
	return std::abs(left - right) <= epsilon;
}

bool vec2_nearly_equal(quader::QVec2 left, quader::QVec2 right, double epsilon = 0.0001)
{
	return nearly_equal(left.x, right.x, epsilon) && nearly_equal(left.y, right.y, epsilon);
}

bool vec3_nearly_equal(quader::QVec3 left, quader::QVec3 right, double epsilon = 0.0001)
{
	return nearly_equal(left.x, right.x, epsilon) && nearly_equal(left.y, right.y, epsilon) && nearly_equal(left.z, right.z, epsilon);
}

quader_editor_core::ViewportCameraState front_camera()
{
	quader_editor_core::ViewportCameraState camera;
        camera.projection = quader_editor_core::CameraProjection::Perspective;
        camera.transform.origin = { 0.0F, 0.0F, 5.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 800.0F, 600.0F };
	camera.fov_degrees = 70.0;
	return camera;
}

void viewport_projection_and_ray_construction_are_stable()
{
	const quader_editor_core::ViewportCameraState camera = front_camera();
	quader::QVec2 projected;
	require_test(quader_editor_core::project_viewport_point(camera, { 0.0F, 0.0F, 0.0F }, projected),
		"perspective camera should project a point in front of the camera");
	require_test(vec2_nearly_equal(projected, { 400.0F, 300.0F }),
		"front-camera world origin should project to the viewport center");
	require_test(!quader_editor_core::project_viewport_point(camera, { 0.0F, 0.0F, 6.0F }, projected),
		"perspective camera should reject points behind the camera");

	const quader_poly::Ray center_ray = quader_editor_core::ray_from_viewport_screen_point(camera, { 400.0F, 300.0F });
	require_test(vec3_nearly_equal(center_ray.origin, camera.transform.origin),
		"perspective viewport rays should start at the camera origin");
	require_test(vec3_nearly_equal(center_ray.direction, { 0.0F, 0.0F, -1.0F }),
		"perspective center ray should follow camera forward");

	quader_editor_core::ViewportCameraState ortho = camera;
        ortho.projection = quader_editor_core::CameraProjection::Orthographic;
        ortho.orthographic_size = 6.0;
	const quader_poly::Ray top_right_ray = quader_editor_core::ray_from_viewport_screen_point(ortho, { 800.0F, 0.0F });
	require_test(vec3_nearly_equal(top_right_ray.origin, { 4.0F, 3.0F, 5.0F }),
		"orthographic rays should offset the origin by viewport position");
	require_test(vec3_nearly_equal(top_right_ray.direction, { 0.0F, 0.0F, -1.0F }),
		"orthographic rays should keep a parallel forward direction");
	require_test(quader_editor_core::project_viewport_point(ortho, { 4.0F, 3.0F, 0.0F }, projected) &&
			vec2_nearly_equal(projected, { 800.0F, 0.0F }),
		"orthographic projection should invert the constructed ray origin");
}

void screen_segment_distance_and_gizmo_pick_ordering_are_stable()
{
	require_test(nearly_equal(quader_editor_core::distance_to_screen_segment_squared({ 5.0F, 5.0F }, { 0.0F, 0.0F }, { 10.0F, 0.0F }), 25.0),
		"screen segment distance should project onto the segment interior");
	require_test(nearly_equal(quader_editor_core::distance_to_screen_segment_squared({ 14.0F, 3.0F }, { 0.0F, 0.0F }, { 10.0F, 0.0F }), 25.0),
		"screen segment distance should clamp beyond the segment endpoint");

	std::vector<quader_editor_core::TransformGizmoPickCandidate> candidates;
        candidates.push_back({quader_editor_core::TransformAxis::X,
                              {1.0F, 0.0F, 0.0F},
                              {1.0F, 0.0F, 0.0F},
                              {100.0F, 0.0F},
                              true});
        candidates.push_back({quader_editor_core::TransformAxis::Y,
                              {0.0F, 1.0F, 0.0F},
                              {0.0F, 1.0F, 0.0F},
                              {0.0F, 100.0F},
                              true});
        candidates.push_back({quader_editor_core::TransformAxis::Z,
                              {0.0F, 0.0F, 1.0F},
                              {0.0F, 0.0F, 1.0F},
                              {98.0F, 0.0F},
                              false});

        quader_editor_core::TransformGizmoPickHit hit =
		quader_editor_core::pick_move_gizmo_axis({ 98.0F, 1.0F }, {}, candidates, 8.0);
        require_test(hit.hit &&
                         hit.axis == quader_editor_core::TransformAxis::X &&
                         nearly_equal(hit.distance, std::sqrt(5.0)),
                     "move gizmo picking should prefer visible axis tips "
                     "before segment hits");

        hit = quader_editor_core::pick_move_gizmo_axis({ 50.0F, 2.0F }, {}, candidates, 8.0);
        require_test(hit.hit &&
                         hit.axis == quader_editor_core::TransformAxis::X &&
                         nearly_equal(hit.distance, 2.0),
                     "move gizmo picking should fall back to visible axis "
                     "segments when no tip is hit");

        const quader_editor_core::TransformGizmoPickHit center_hit =
		quader_editor_core::pick_scale_gizmo_axis({ 3.0F, 4.0F }, {}, candidates, 8.0, 48.0);
        require_test(center_hit.hit &&
                         center_hit.axis ==
                             quader_editor_core::TransformAxis::All &&
                         nearly_equal(center_hit.distance, 5.0),
                     "scale gizmo picking should expose the center all-axis "
                     "handle before axis handles");

        const std::vector<quader_editor_core::TransformGizmoRingPickCandidate>
            ring_candidates{
                {quader_editor_core::TransformAxis::Z,
                 {0.0F, 0.0F, 1.0F},
                 {0.0F, 0.0F, 1.0F},
                 {0.0F, 10.0F},
                 {10.0F, 10.0F}},
            };
        const quader_editor_core::TransformGizmoPickHit ring_hit =
		quader_editor_core::pick_rotate_gizmo_axis({ 4.0F, 12.0F }, ring_candidates, 4.0);
        require_test(ring_hit.hit &&
                         ring_hit.axis ==
                             quader_editor_core::TransformAxis::Z &&
                         nearly_equal(ring_hit.distance, 2.0),
                     "rotate gizmo picking should use ring segment distance");
}

void box_construction_ray_plane_behavior_is_stable()
{
	quader_editor_core::BoxToolState state;
	const quader_editor_core::BoxToolStateResult plane =
		quader_editor_core::set_box_construction_plane(state, { 0.0F, 2.0F, 0.0F }, { 0.0F, 4.0F, 0.0F });
	require_test(plane.ok && vec3_nearly_equal(state.plane.normal, { 0.0F, 1.0F, 0.0F }),
		"box construction plane should normalize the supplied normal");
	require_test(vec3_nearly_equal(state.plane.axis_u, { 1.0F, 0.0F, 0.0F }) &&
			vec3_nearly_equal(state.plane.axis_v, { 0.0F, 0.0F, -1.0F }),
		"box construction plane should preserve the established basis orientation");

	require_test(quader_editor_core::set_box_construction_plane(state, { 0.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 0.0F }).ok &&
			vec3_nearly_equal(state.plane.normal, { 0.0F, 1.0F, 0.0F }) &&
			vec3_nearly_equal(state.plane.axis_u, { 1.0F, 0.0F, 0.0F }) &&
			vec3_nearly_equal(state.plane.axis_v, { 0.0F, 0.0F, -1.0F }),
		"box construction plane should keep the zero-normal fallback basis stable");

	require_test(quader_editor_core::set_box_construction_plane(state, { 0.0F, 0.0F, 0.0F }, { 2.0F, 0.0F, 0.0F }).ok &&
			vec3_nearly_equal(state.plane.normal, { 1.0F, 0.0F, 0.0F }) &&
			vec3_nearly_equal(state.plane.axis_u, { 0.0F, 0.0F, 1.0F }) &&
			vec3_nearly_equal(state.plane.axis_v, { 0.0F, -1.0F, 0.0F }),
		"box construction plane should keep the x-parallel preferred-axis fallback stable");

	require_test(quader_editor_core::set_box_construction_plane(state, { 0.0F, 2.0F, 0.0F }, { 0.0F, 4.0F, 0.0F }).ok,
		"box construction plane should restore the y-up fixture plane");

	quader_editor_core::BoxConstructionPointResult point = quader_editor_core::box_construction_point_from_ray(
		state.plane,
		{ { 8.0F, 6.0F, 24.0F }, { 0.0F, -1.0F, 0.0F } });
	require_test(point.ok && vec3_nearly_equal(point.position, { 8.0F, 2.0F, 24.0F }) && nearly_equal(point.distance, 4.0),
		"box construction ray should intersect the active plane in front of the ray");

	point = quader_editor_core::box_construction_point_from_ray(
		state.plane,
		{ { 8.0F, 6.0F, 24.0F }, { 0.0F, -2.0F, 0.0F } });
	require_test(point.ok && vec3_nearly_equal(point.position, { 8.0F, 2.0F, 24.0F }) && nearly_equal(point.distance, 2.0),
		"box construction ray distance should remain an unnormalized ray parameter");

	point = quader_editor_core::box_construction_point_from_ray(
		state.plane,
		{ { 8.0F, 6.0F, 24.0F }, { 1.0F, 0.0F, 0.0F } });
	require_test(!point.ok && point.message == "Construction ray is parallel to the plane.",
		"box construction ray should reject rays parallel to the plane");

	point = quader_editor_core::box_construction_point_from_ray(
		state.plane,
		{ { 8.0F, 6.0F, 24.0F }, { 0.0F, 1.0F, 0.0F } });
	require_test(!point.ok && point.message == "Construction plane is behind the ray." && point.distance < 0.0,
		"box construction ray should reject intersections behind the ray origin");
}

void box_preview_bounds_are_computed_in_editor_core()
{
	quader_editor_core::BoxToolState state;
        state.drag_state = quader_editor_core::BoxDragState::Height;
        state.plane.normal = { 0.0F, 1.0F, 0.0F };
	state.plane.axis_u = { 1.0F, 0.0F, 0.0F };
	state.plane.axis_v = { 0.0F, 0.0F, 1.0F };
	state.start_point = { 2.0F, 5.0F, -1.0F };
	state.end_point = { -3.0F, 5.0F, 4.0F };
	state.height_distance = -2.0;

	const quader_editor_core::BoxPreviewDescriptor preview =
		quader_editor_core::box_preview_descriptor(state, true, 1.0);
	require_test(preview.ok && !preview.bounds.empty,
		"box preview descriptor should expose computed bounds");
	require_test(vec3_nearly_equal(preview.bounds.min, { -3.0F, 3.0F, -1.0F }) &&
			vec3_nearly_equal(preview.bounds.max, { 2.0F, 5.0F, 4.0F }),
		"box preview descriptor bounds should cover the footprint and signed height");

        state.drag_state = quader_editor_core::BoxDragState::Footprint;
        const quader_editor_core::BoxPreviewDescriptor footprint_preview =
		quader_editor_core::box_preview_descriptor(state, true, 1.0);
	require_test(footprint_preview.ok && !footprint_preview.bounds.empty &&
			vec3_nearly_equal(footprint_preview.bounds.min, { -3.0F, 5.0F, -1.0F }) &&
			vec3_nearly_equal(footprint_preview.bounds.max, { 2.0F, 5.0F, 4.0F }),
		"box preview descriptor bounds should cover only footprint points outside height drag");
}

void cut_collinear_rejection_and_status_are_stable()
{
	quader_editor_core::RectangleSelectionState selection;
        selection.mode = quader_editor_core::SelectionMode::Object;
        selection.selected_object_ids = { 201, 202 };

	quader_editor_core::CutToolState state;
	quader_editor_core::CutToolResult result = quader_editor_core::capture_first_cut_point(
		state,
		selection,
		{ 0.0F, 0.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F });
	require_test(result.ok && result.changed && state.target_object_ids.size() == 2 && state.dragging_new_point,
		"cut first point should expand the selected target set and begin new-point dragging");

	result = quader_editor_core::capture_second_cut_point(state, { 2.0F, 0.0F, 0.0F });
	require_test(result.ok && state.points.size() == 2 && state.dragging_point_index == 1 && !state.dragging_new_point,
		"cut second point should store a line endpoint and switch to endpoint dragging");

        const quader_editor_core::CutPreviewRequest two_point_preview =
            quader_editor_core::cut_preview_request(
                state, quader_editor_core::CutMode::KeepFront);
        require_test(two_point_preview.ok && two_point_preview.can_preview && two_point_preview.points.size() == 3 &&
			vec3_nearly_equal(two_point_preview.points[2], { 0.0F, 1.0F, 0.0F }),
		"two-point cut preview should synthesize a third plane point from the captured face normal");

	quader_editor_core::CutPreviewCounts counts;
	counts.target_count = 2;
	counts.split_count = 1;
	counts.removed_count = 1;
        require_test(
            quader_editor_core::set_cut_preview_result(
                state, quader_editor_core::CutMode::KeepFront, true, counts)
                    .ok &&
                state.preview_valid,
            "cut preview result should store preview metadata");
        const std::string status = quader_editor_core::cut_preview_status(
            state, quader_editor_core::CutMode::KeepFront);
        require_test(status.find("keep front") != std::string::npos && status.find("1/2 split") != std::string::npos &&
			status.find("1 removed") != std::string::npos,
		"cut preview status should summarize mode, split count, target count, and removal count");
}

void overlay_occluder_visibility_is_stable()
{
	quader_editor_core::OverlayVisibilityOccluder occluder;
	const std::vector<quader::QVec3> vertices {
		{ -1.0F, -1.0F, 0.0F },
		{ 1.0F, -1.0F, 0.0F },
		{ 1.0F, 1.0F, 0.0F },
		{ -1.0F, 1.0F, 0.0F },
	};
	const std::vector<std::uint32_t> indices { 0, 1, 2, 0, 2, 3 };
	occluder.set_mesh(vertices, indices);

	require_test(!occluder.empty(), "overlay occluder should build triangles from indexed mesh input");
	require_test(!occluder.line_segment_is_visible({ -0.5F, 0.0F, -1.0F }, { 0.5F, 0.0F, -1.0F }, front_camera()),
		"overlay occluder should hide line segments behind an occluding mesh");
	require_test(occluder.line_segment_is_visible({ -0.5F, 0.0F, 1.0F }, { 0.5F, 0.0F, 1.0F }, front_camera()),
		"overlay occluder should keep line segments in front of the mesh visible");
        require_test(
            !quader_editor_core::OverlayVisibilityOccluder::
                line_layer_uses_visibility(
                    quader_editor_core::SelectionOverlayLayer::SourceWire),
            "source-wire overlay lines should be preserved as draw-on-top "
            "topology handles");
        require_test(
            !quader_editor_core::OverlayVisibilityOccluder::
                line_layer_uses_visibility(
                    quader_editor_core::SelectionOverlayLayer::SelectedEdge),
            "selected edge overlay lines should stay visible through "
            "occluders");
        require_test(!quader_editor_core::OverlayVisibilityOccluder::
                         line_layer_uses_visibility(
                             quader_editor_core::SelectionOverlayLayer::
                                 SelectedFaceEdge),
                     "selected face outline overlay lines should stay visible "
                     "through occluders");
        require_test(
            !quader_editor_core::OverlayVisibilityOccluder::
                line_layer_uses_visibility(
                    quader_editor_core::SelectionOverlayLayer::HoverFaceEdge),
            "hover face outlines should stay visible through occluders");
        require_test(
            !quader_editor_core::OverlayVisibilityOccluder::
                line_layer_uses_visibility(
                    quader_editor_core::SelectionOverlayLayer::HoverEdge),
            "hover edge overlay lines should stay visible through occluders");
        require_test(
            !quader_editor_core::OverlayVisibilityOccluder::
                line_layer_uses_visibility(
                    quader_editor_core::SelectionOverlayLayer::OpenEdge),
            "open edge overlay lines should stay visible through occluders");
        require_test(
            !quader_editor_core::OverlayVisibilityOccluder::
                line_layer_uses_visibility(
                    quader_editor_core::SelectionOverlayLayer::DiagnosticEdge),
            "diagnostic edge overlay lines should stay visible through "
            "occluders");
        require_test(
            !quader_editor_core::OverlayVisibilityOccluder::
                line_layer_uses_visibility(
                    quader_editor_core::SelectionOverlayLayer::HoverVertex),
            "point-style overlay layers should not participate in line "
            "visibility filtering");
}

void overlay_occluder_bounds_partitioning_is_stable()
{
	quader_editor_core::OverlayVisibilityOccluder occluder;
	std::vector<quader::QVec3> vertices;
	std::vector<std::uint32_t> indices;
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			const float min_x = static_cast<float>((column * 3) - 4);
			const float min_y = static_cast<float>((row * 3) - 4);
			const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
			vertices.push_back({ min_x, min_y, 0.0F });
			vertices.push_back({ min_x + 1.0F, min_y, 0.0F });
			vertices.push_back({ min_x + 1.0F, min_y + 1.0F, 0.0F });
			vertices.push_back({ min_x, min_y + 1.0F, 0.0F });
			indices.insert(indices.end(), {
				base,
				base + 1U,
				base + 2U,
				base,
				base + 2U,
				base + 3U,
			});
		}
	}
	occluder.set_mesh(vertices, indices);

	require_test(!occluder.empty(), "overlay occluder should build a bounds tree for multi-quad mesh input");
	require_test(!occluder.line_segment_is_visible({ -0.2F, -0.2F, -1.0F }, { 0.2F, 0.2F, -1.0F }, front_camera()),
		"overlay occluder bounds traversal should find the central blocking tile");
	require_test(occluder.line_segment_is_visible({ 8.0F, 8.0F, -1.0F }, { 9.0F, 8.0F, -1.0F }, front_camera()),
		"overlay occluder bounds traversal should not hide segments outside every occluder node");
	require_test(occluder.line_segment_is_visible({ -0.2F, -0.2F, 0.001F }, { 0.2F, 0.2F, 0.001F }, front_camera()),
		"overlay occluder depth tolerance should keep coplanar overlay segments visible");
}

void overlay_visibility_preserves_edit_wire_layers()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"overlay visibility fixture should create an occluding box");

	quader_editor_core::OverlayVisibilityOccluder occluder;
	occluder.set_objects(session.objects());
	require_test(!occluder.empty(),
		"overlay visibility occluder should build from editor-core objects through document mesh output");

        const std::vector<quader_editor_core::SelectionOverlayLine> lines{
            {
                quader_editor_core::SelectionOverlayLayer::SourceWire,
                {-0.5F, 0.0F, -2.0F},
                {0.5F, 0.0F, -2.0F},
                0xffffffffU,
                1.0F,
            },
            {
                quader_editor_core::SelectionOverlayLayer::SelectedEdge,
                {-0.5F, 0.0F, -2.0F},
                {0.5F, 0.0F, -2.0F},
                0xffffffffU,
                1.0F,
            },
            {
                quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge,
                {-0.5F, 0.0F, 2.0F},
                {0.5F, 0.0F, 2.0F},
                0xffffffffU,
                1.0F,
            },
            {
                quader_editor_core::SelectionOverlayLayer::HoverVertex,
                {-0.5F, 0.0F, -2.0F},
                {0.5F, 0.0F, -2.0F},
                0xffffffffU,
                1.0F,
            },
        };
        require_test(!quader_editor_core::selection_overlay_lines_use_visibility(lines),
		"editor-core overlay visibility policy should not camera-cull edit wire layers");

	const std::vector<quader_editor_core::SelectionOverlayLine> visible =
		occluder.visible_lines(lines, front_camera());
	require_test(visible.size() == 4,
		"editor-core overlay visibility filtering should preserve current edit wire layers");
        require_test(visible[0].layer ==
                         quader_editor_core::SelectionOverlayLayer::SourceWire,
                     "source wire behind the occluder should be preserved as "
                     "draw-on-top topology");
        require_test(
            visible[1].layer ==
                quader_editor_core::SelectionOverlayLayer::SelectedEdge,
            "selected edges behind the occluder should be preserved as x-ray "
            "handles");
        require_test(
            visible[2].layer ==
                quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge,
            "selected face outlines should be preserved by visibility "
            "filtering");
        require_test(visible[3].layer ==
                         quader_editor_core::SelectionOverlayLayer::HoverVertex,
                     "non-line-visibility overlay layers should pass through "
                     "editor-core filtering unchanged");
}

} // namespace

TEST(QdrModelingRuntimeGeometryCharacterizationTests, QdrEditorCoreGeometryCharacterizationTests) {
	viewport_projection_and_ray_construction_are_stable();
	screen_segment_distance_and_gizmo_pick_ordering_are_stable();
	box_construction_ray_plane_behavior_is_stable();
	box_preview_bounds_are_computed_in_editor_core();
	cut_collinear_rejection_and_status_are_stable();
	overlay_occluder_visibility_is_stable();
	overlay_occluder_bounds_partitioning_is_stable();
	overlay_visibility_preserves_edit_wire_layers();
}
