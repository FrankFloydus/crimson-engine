////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.h"

#include <modeling/runtime/session.h>
#include <modeling/interactions/interaction_api.h>
#include <modeling/polygon/document.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

std::size_t triangle_count_for_layer(const quader_editor_core::SelectionOverlayFrame& frame, quader_editor_core::SelectionOverlayLayer layer)
{
	return static_cast<std::size_t>(std::count_if(frame.triangles.begin(), frame.triangles.end(), [layer](const quader_editor_core::SelectionOverlayTriangle& triangle) {
		return triangle.layer == layer;
	}));
}

std::size_t line_count_for_layer(const quader_editor_core::SelectionOverlayFrame& frame, quader_editor_core::SelectionOverlayLayer layer)
{
	return static_cast<std::size_t>(std::count_if(frame.lines.begin(), frame.lines.end(), [layer](const quader_editor_core::SelectionOverlayLine& line) {
		return line.layer == layer;
	}));
}

std::size_t line_count_for_layer_and_color(
	const quader_editor_core::SelectionOverlayFrame& frame,
	quader_editor_core::SelectionOverlayLayer layer,
	std::uint32_t color_abgr)
{
	return static_cast<std::size_t>(std::count_if(frame.lines.begin(), frame.lines.end(), [layer, color_abgr](const quader_editor_core::SelectionOverlayLine& line) {
		return line.layer == layer && line.color_abgr == color_abgr;
	}));
}

std::size_t triangle_count_for_layer_and_color(
	const quader_editor_core::SelectionOverlayFrame& frame,
	quader_editor_core::SelectionOverlayLayer layer,
	std::uint32_t color_abgr)
{
	return static_cast<std::size_t>(std::count_if(frame.triangles.begin(), frame.triangles.end(), [layer, color_abgr](const quader_editor_core::SelectionOverlayTriangle& triangle) {
		return triangle.layer == layer && triangle.color_abgr == color_abgr;
	}));
}

std::size_t point_count_for_layer(const quader_editor_core::SelectionOverlayFrame& frame, quader_editor_core::SelectionOverlayLayer layer)
{
	return static_cast<std::size_t>(std::count_if(frame.points.begin(), frame.points.end(), [layer](const quader_editor_core::SelectionOverlayPoint& point) {
		return point.layer == layer;
	}));
}

std::size_t point_count_for_layer_and_color(
	const quader_editor_core::SelectionOverlayFrame& frame,
	quader_editor_core::SelectionOverlayLayer layer,
	std::uint32_t color_abgr)
{
	return static_cast<std::size_t>(std::count_if(frame.points.begin(), frame.points.end(), [layer, color_abgr](const quader_editor_core::SelectionOverlayPoint& point) {
		return point.layer == layer && point.color_abgr == color_abgr;
	}));
}

bool nearly_equal(float left, float right, float epsilon = 0.0001F)
{
	return std::abs(left - right) <= epsilon;
}

bool position_matches(quader_poly::Vec3 left, quader_poly::Vec3 right, float epsilon = 0.0001F)
{
	return nearly_equal(left.x, right.x, epsilon) &&
		nearly_equal(left.y, right.y, epsilon) &&
		nearly_equal(left.z, right.z, epsilon);
}

bool document_has_vertex_at(const quader_poly::Document& document, quader_poly::Vec3 position)
{
	return std::any_of(document.vertices.begin(), document.vertices.end(), [position](const quader_poly::Vertex& vertex) {
		return position_matches(vertex.position, position);
	});
}

quader_editor_core::PolygonObject diagnostic_single_edge_object(
	int object_id,
	quader_poly::Vec3 start,
	quader_poly::Vec3 end)
{
	quader_editor_core::PolygonObject object;
	object.id = object_id;
	object.selected = false;
	const quader_poly::ElementId start_id = quader_poly::add_vertex(object.document, start);
	const quader_poly::ElementId end_id = quader_poly::add_vertex(object.document, end);
	const std::array face_vertices { start_id, end_id, end_id };
	require_test(
		quader_poly::add_face(object.document, face_vertices) != quader_poly::invalid_element_id,
		"diagnostic edge fixture should create a degenerate edge-only face");
	require_test(
		quader_poly::document_edges(object.document).size() == 1U,
		"diagnostic edge fixture should expose exactly one document edge");
	return object;
}

quader_editor_core::PolygonObject diagnostic_triangle_object(
	int object_id,
	quader_poly::Vec3 a,
	quader_poly::Vec3 b,
	quader_poly::Vec3 c)
{
	quader_editor_core::PolygonObject object;
	object.id = object_id;
	object.selected = false;
	const std::array face_vertices {
		quader_poly::add_vertex(object.document, a),
		quader_poly::add_vertex(object.document, b),
		quader_poly::add_vertex(object.document, c),
	};
	require_test(
		quader_poly::add_face(object.document, face_vertices) != quader_poly::invalid_element_id,
		"diagnostic triangle fixture should create a face");
	return object;
}

bool diagnostic_line_exists_for_object(
	const quader_editor_core::SelectionOverlayFrame& frame,
	int object_id)
{
	return std::any_of(frame.lines.begin(), frame.lines.end(), [object_id](const quader_editor_core::SelectionOverlayLine& line) {
		return line.layer == quader_editor_core::SelectionOverlayLayer::diagnostic_edge &&
			line.object_id == object_id &&
			line.edge.a != quader_poly::invalid_element_id &&
			line.edge.b != quader_poly::invalid_element_id;
	});
}

std::vector<quader_poly::Edge> top_loop_edges(const quader_poly::Document& document)
{
	std::vector<quader_poly::Edge> edges;
	for (const quader_poly::Edge& edge : quader_poly::document_edges(document)) {
		const quader_poly::Vertex* a = quader_poly::find_vertex(document, edge.a);
		const quader_poly::Vertex* b = quader_poly::find_vertex(document, edge.b);
		if (a != nullptr && b != nullptr &&
				nearly_equal(a->position.y, 64.0F) &&
				nearly_equal(b->position.y, 64.0F)) {
			edges.push_back(edge);
		}
	}
	return edges;
}

quader_editor_core::EditorSession two_box_session()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"overlay fixture should create the first box");
	require_test(session.create_box_from_bounds({ 4.0F, -1.0F, -1.0F }, { 6.0F, 1.0F, 1.0F }).changed,
		"overlay fixture should create the second box");
	return session;
}

void selected_object_overlay_uses_xray_fill_and_depth_tested_active_wire()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"selected object overlay fixture should create a box");

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), {});
	require_test(triangle_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::selected_xray_face) > 0,
		"selected object overlay should include x-ray face fill");
	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::source_wire) == 12U,
		"selected object active wire should use the depth-tested source wire layer");
	const quader_editor_core::SelectionOverlayStyle style;
	require_test(line_count_for_layer_and_color(frame, quader_editor_core::SelectionOverlayLayer::source_wire, style.selected_edge_color_abgr) == 12U,
		"selected object source wire should keep the selected-wire color while remaining depth-tested");
	require_test(line_count_for_layer_and_color(frame, quader_editor_core::SelectionOverlayLayer::source_wire, style.source_wire_color_abgr) == 0U,
		"selected object source wire should not be colored as ordinary source/candidate wire");
	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::selected_edge) == 0U,
		"selected object active wire should not use selected component edge handles");
	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::selected_face_edge) == 0U,
		"selected object active wire should not use selected component face-edge handles");
	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::open_edge) == 0,
		"closed selected object should not expose open-edge hatching");
}

void selected_mesh_source_wire_uses_source_color_in_component_mode()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"selected mesh source-wire color fixture should create a box");
	session.set_selection_mode(quader_editor_core::SelectionMode::face);

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), {});
	const quader_editor_core::SelectionOverlayStyle style;
	const std::size_t source_wire_count = line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::source_wire);
	require_test(source_wire_count > 0U,
		"component mode selected mesh should still draw active source wire");
	require_test(line_count_for_layer_and_color(frame, quader_editor_core::SelectionOverlayLayer::source_wire, style.source_wire_color_abgr) == source_wire_count,
		"component mode selected mesh source wire should use ordinary source-wire color");
	require_test(line_count_for_layer_and_color(frame, quader_editor_core::SelectionOverlayLayer::source_wire, style.selected_edge_color_abgr) == 0U,
		"component mode selected mesh source wire should not use selected-wire color");
}

void component_deselect_keeps_active_mesh_source_wire_visible()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"component deselect source-wire fixture should create a box");
	session.set_selection_mode(quader_editor_core::SelectionMode::face);
	const quader_poly::Ray front_center_ray { { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } };

	require_test(
		session.pick_select_face(quader_editor_core::SelectionEditOperation::replace, front_center_ray),
		"component deselect source-wire fixture should select a face");
	require_test(session.has_selection(), "selected face should count as a component selection");
	const int active_object_id = session.active_object_id();

	require_test(
		session.pick_select_face(quader_editor_core::SelectionEditOperation::remove, front_center_ray),
		"component deselect should remove the selected face");
	require_test(!session.has_selection(), "removing the last component should leave no selected component");
	require_test(session.active_object_id() == active_object_id, "removing the last component should keep the mesh as the active edit target");

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), {});
	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::source_wire) == 12U,
		"removing the last component should keep the active mesh source wire visible");
}

void component_hover_source_wire_candidate_is_sticky_until_replaced()
{
	quader_editor_core::EditorSession session = two_box_session();
	session.clear_selection();
	session.set_selection_mode(quader_editor_core::SelectionMode::face);

	const quader_editor_core::SelectionHoverUpdateResult first_hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		{},
		{ { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
	require_test(first_hover.changed && first_hover.hover.candidate_object_id == 1,
		"component hover should tag the first hovered object as source-wire candidate");

	const quader_editor_core::SelectionOverlayFrame first_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), first_hover.hover);
	const std::size_t first_source_wire_count = line_count_for_layer(first_frame, quader_editor_core::SelectionOverlayLayer::source_wire);
	require_test(first_source_wire_count > 0, "component hover candidate should draw source wire");
	const quader_editor_core::SelectionOverlayStyle style;
	require_test(line_count_for_layer_and_color(first_frame, quader_editor_core::SelectionOverlayLayer::source_wire, style.source_wire_color_abgr) == first_source_wire_count,
		"component hover candidate source wire should keep the ordinary source-wire color");

	const quader_editor_core::SelectionHoverUpdateResult empty_hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		first_hover.hover,
		{ { 64.0F, 64.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
	require_test(empty_hover.hover.kind == quader_editor_core::SelectionHoverKind::none && empty_hover.hover.candidate_object_id == 1,
		"empty component hover should keep the previous source-wire candidate");
	const quader_editor_core::SelectionOverlayFrame empty_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), empty_hover.hover);
	require_test(line_count_for_layer(empty_frame, quader_editor_core::SelectionOverlayLayer::source_wire) == first_source_wire_count,
		"empty hover should preserve candidate source-wire lines");

	const quader_editor_core::SelectionHoverUpdateResult second_hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		empty_hover.hover,
		{ { 5.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
	require_test(second_hover.hover.candidate_object_id == 2,
		"hovering a different component object should replace the source-wire candidate");
}

void vertex_mode_hover_candidate_exposes_unselected_handles()
{
	quader_editor_core::EditorSession session = two_box_session();
	session.clear_selection();
	session.set_selection_mode(quader_editor_core::SelectionMode::vertex);

	const quader_editor_core::SelectionHoverUpdateResult hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		{},
		{ { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), hover.hover);
	require_test(point_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::unselected_vertex) > 0,
		"vertex hover candidate should expose unselected vertex handles before click selection");
	require_test(std::all_of(frame.points.begin(), frame.points.end(), [&](const quader_editor_core::SelectionOverlayPoint& point) {
			return point.layer != quader_editor_core::SelectionOverlayLayer::unselected_vertex ||
				(point.object_id == hover.hover.candidate_object_id &&
					point.vertex_id != quader_poly::invalid_element_id);
		}),
		"vertex hover candidate handles should carry owner object and vertex ids");
}

void hover_only_overlay_omits_selected_base_geometry()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"hover-only overlay fixture should create a box");
	session.set_selection_mode(quader_editor_core::SelectionMode::face);
	require_test(session.select_all().changed, "hover-only overlay fixture should select the box faces");

	const quader_editor_core::SelectionHoverUpdateResult hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		{},
		{ { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
	require_test(hover.hover.kind == quader_editor_core::SelectionHoverKind::face,
		"hover-only overlay fixture should hover a face");

	const quader_editor_core::SelectionOverlayFrame full_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), hover.hover);
	require_test(line_count_for_layer(full_frame, quader_editor_core::SelectionOverlayLayer::selected_face_edge) > 0,
		"full selected overlay should contain selected face edges");

	const quader_editor_core::SelectionOverlayFrame hover_frame =
		quader_editor_core::build_selection_hover_overlay_frame(session.objects(), session.selection_mode(), hover.hover);
	require_test(triangle_count_for_layer(hover_frame, quader_editor_core::SelectionOverlayLayer::hover_face) > 0,
		"hover-only overlay should include hover face fill");
	require_test(line_count_for_layer(hover_frame, quader_editor_core::SelectionOverlayLayer::hover_face_edge) > 0,
		"hover-only overlay should include hover face edges");
	require_test(line_count_for_layer(hover_frame, quader_editor_core::SelectionOverlayLayer::selected_face_edge) == 0,
		"hover-only overlay should not rebuild selected face edges");
	require_test(line_count_for_layer(hover_frame, quader_editor_core::SelectionOverlayLayer::selected_edge) == 0,
		"hover-only overlay should not rebuild selected visible edges");
	require_test(triangle_count_for_layer(hover_frame, quader_editor_core::SelectionOverlayLayer::selected_visible_face) == 0,
		"hover-only overlay should not rebuild selected face fill");
	require_test(triangle_count_for_layer(hover_frame, quader_editor_core::SelectionOverlayLayer::selected_xray_face) == 0,
		"hover-only overlay should not rebuild selected x-ray fill");
	require_test(triangle_count_for_layer(hover_frame, quader_editor_core::SelectionOverlayLayer::selected_face_mask) == 0,
		"hover-only overlay should not rebuild selected face masks");
}

void selected_component_hover_keeps_normal_hover_primitives()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"selected component hover fixture should create a box");
	session.set_selection_mode(quader_editor_core::SelectionMode::face);
	const quader_poly::Ray front_center_ray { { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } };
	require_test(
		session.pick_select_face(quader_editor_core::SelectionEditOperation::replace, front_center_ray),
		"selected component hover fixture should select a face");

	const quader_editor_core::SelectionHoverUpdateResult hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		{},
		front_center_ray,
		false);
	require_test(hover.hover.kind == quader_editor_core::SelectionHoverKind::face,
		"selected component hover fixture should hover the selected face");
	require_test(
		quader_editor_core::selection_hover_target_matches_selection(session.objects(), session.selection_mode(), hover.hover),
		"selected component hover fixture should confirm the hovered face is selected");

	quader_editor_core::SelectionOverlayStyle style;
	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), hover.hover, style);
	require_test(!frame.hover_target_selected,
		"selected component hover without the modifier should not become remove-preview hover");
	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::selected_face_edge) > 0,
		"selected component hover should keep the selected face edges");
	require_test(triangle_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::hover_face) > 0,
		"selected component hover should keep the normal hover face fill");
	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::hover_face_edge) > 0,
		"selected component hover should keep the normal hover face edges");
	require_test(triangle_count_for_layer_and_color(frame, quader_editor_core::SelectionOverlayLayer::hover_face, style.hover_face_color_abgr) > 0,
		"selected component hover without the modifier should use normal hover face color");
	require_test(line_count_for_layer_and_color(frame, quader_editor_core::SelectionOverlayLayer::hover_face_edge, style.hover_face_edge_color_abgr) > 0,
		"selected component hover without the modifier should use normal hover edge color");

	quader_editor_core::SelectionHoverState remove_hover = hover.hover;
	remove_hover.selected_modifier_pressed = true;
	const quader_editor_core::SelectionOverlayFrame remove_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), remove_hover, style);
	require_test(remove_frame.hover_target_selected,
		"selected component hover with the modifier should become remove-preview hover");
	require_test(triangle_count_for_layer_and_color(remove_frame, quader_editor_core::SelectionOverlayLayer::hover_face, style.hover_selected_face_color_abgr) > 0,
		"selected component hover with the modifier should use selected-hover face color");
	require_test(line_count_for_layer_and_color(remove_frame, quader_editor_core::SelectionOverlayLayer::hover_face_edge, style.hover_selected_line_color_abgr) > 0,
		"selected component hover with the modifier should use selected-hover edge color");
}

void selected_hover_overlay_primitives_carry_stable_owner_keys()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"selected-hover owner fixture should create a box");
	session.set_selection_mode(quader_editor_core::SelectionMode::face);
	require_test(session.select_all().changed, "selected-hover owner fixture should select the box faces");

	const quader_editor_core::SelectionHoverUpdateResult hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		{},
		{ { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		true);
	require_test(hover.hover.kind == quader_editor_core::SelectionHoverKind::face,
		"selected-hover owner fixture should hover a face");

	const quader_editor_core::SelectionOverlayFrame base_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), {});
	const bool base_has_owned_selected_face =
		std::any_of(base_frame.triangles.begin(), base_frame.triangles.end(), [](const quader_editor_core::SelectionOverlayTriangle& triangle) {
			return (triangle.layer == quader_editor_core::SelectionOverlayLayer::selected_xray_face ||
					triangle.layer == quader_editor_core::SelectionOverlayLayer::selected_visible_face ||
					triangle.layer == quader_editor_core::SelectionOverlayLayer::selected_face_mask) &&
				triangle.object_id > 0 &&
				triangle.face_id != quader_poly::invalid_element_id;
		});
	require_test(base_has_owned_selected_face,
		"cached selected overlay triangles should carry object and face owner keys");

	const quader_editor_core::SelectionOverlayFrame hover_frame =
		quader_editor_core::build_selection_hover_overlay_frame(session.objects(), session.selection_mode(), hover.hover);
	for (const quader_editor_core::SelectionOverlayTriangle& triangle : hover_frame.triangles) {
		if (triangle.layer != quader_editor_core::SelectionOverlayLayer::hover_face) {
			continue;
		}
		require_test(triangle.object_id == hover.hover.object_id && triangle.face_id == hover.hover.face_id,
			"hover face triangles should carry the hovered object and face owner keys");
	}
	for (const quader_editor_core::SelectionOverlayLine& line : hover_frame.lines) {
		if (line.layer != quader_editor_core::SelectionOverlayLayer::hover_face_edge) {
			continue;
		}
		require_test(line.object_id == hover.hover.object_id &&
				line.face_id == hover.hover.face_id &&
				line.edge.a != quader_poly::invalid_element_id &&
				line.edge.b != quader_poly::invalid_element_id,
			"hover face lines should carry object, face, and edge owner keys");
	}
}

void selected_plane_overlay_marks_open_edges()
{
	quader_editor_core::PolygonObject plane;
	plane.id = 1;
	plane.selected = true;
	plane.document = quader_poly::make_plane({});
	plane.selection.mode = quader_poly::SelectionMode::edge;
	plane.selection.edges = quader_poly::document_edges(plane.document);

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame({ plane }, quader_editor_core::SelectionMode::edge, {});
	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::open_edge) > 0,
		"selected open plane edges should expose open-edge overlay lines");
}

void top_loop_bevel_overlay_emits_selected_strip_topology()
{
	quader_poly::CubePrimitiveSpec cube_spec;
	cube_spec.width = 128.0F;
	cube_spec.height = 128.0F;
	cube_spec.depth = 128.0F;
	quader_poly::Document cube = quader_poly::make_cube(cube_spec);
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::edge;
	selection.edges = top_loop_edges(cube);
	require_test(selection.edges.size() == 4U, "top-loop bevel overlay fixture should select the four cube top edges");

	quader_poly::EdgeBevelSettings settings;
	settings.width = 0.9F;
	settings.profile = 0.5F;
	settings.segments = 10;
	const quader_poly::OperationResult bevel = quader_poly::bevel_selected_edges(cube, selection, settings);
	require_test(bevel.changed, "top-loop bevel overlay fixture should generate bevel topology");
	require_test(selection.mode == quader_poly::SelectionMode::face && selection.faces.size() == 40U,
		"top-loop bevel overlay fixture should select the generated strip faces");

	quader_editor_core::PolygonObject object;
	object.id = 1;
	object.selected = true;
	object.document = cube;
	object.selection = selection;
	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame({ object }, quader_editor_core::SelectionMode::face, {});
	const std::size_t selected_edge_count = line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::selected_edge);
	const std::size_t selected_face_edge_count = line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::selected_face_edge);
	require_test(selected_edge_count >= 40U && selected_face_edge_count >= 40U,
		"top-loop bevel overlay should emit selected cyan wire for the generated strip topology");
	for (const quader_editor_core::SelectionOverlayLine& line : frame.lines) {
		if (line.layer != quader_editor_core::SelectionOverlayLayer::selected_edge &&
				line.layer != quader_editor_core::SelectionOverlayLayer::selected_face_edge) {
			continue;
		}
		require_test(document_has_vertex_at(cube, line.start) && document_has_vertex_at(cube, line.end),
			"top-loop bevel overlay line endpoints should come from the current bevel document vertices");
	}
}

void diagnostic_edges_mark_overlapping_edges_across_unselected_objects()
{
	const quader_editor_core::PolygonObject first =
		diagnostic_single_edge_object(1, { 0.0F, 0.0F, 0.0F }, { 3.0F, 0.0F, 0.0F });
	const quader_editor_core::PolygonObject second =
		diagnostic_single_edge_object(2, { 1.0F, 0.0F, 0.0F }, { 2.0F, 0.0F, 0.0F });
	const std::vector<quader_editor_core::PolygonObject> objects { first, second };
	const quader_editor_core::SelectionOverlayStyle style;

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_diagnostic_edge_overlay_frame(objects, style);

	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::diagnostic_edge) == 2U,
		"overlapping cross-object edges should mark both whole edges");
	require_test(diagnostic_line_exists_for_object(frame, 1) && diagnostic_line_exists_for_object(frame, 2),
		"diagnostic overlap lines should preserve the owning object ids");
	for (const quader_editor_core::SelectionOverlayLine& line : frame.lines) {
		if (line.layer != quader_editor_core::SelectionOverlayLayer::diagnostic_edge) {
			continue;
		}
		require_test(line.color_abgr == style.diagnostic_edge_color_abgr,
			"diagnostic edges should use the diagnostic red style color");
		require_test(nearly_equal(line.width_pixels, style.diagnostic_edge_line_width_pixels),
			"diagnostic edges should carry the diagnostic line width");
	}
}

void diagnostic_edges_ignore_ordinary_non_overlapping_meshes()
{
	const quader_editor_core::PolygonObject first =
		diagnostic_triangle_object(1, { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });
	const quader_editor_core::PolygonObject second =
		diagnostic_triangle_object(2, { 5.0F, 0.0F, 0.0F }, { 6.0F, 0.0F, 0.0F }, { 5.0F, 1.0F, 0.0F });
	const std::vector<quader_editor_core::PolygonObject> objects { first, second };

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_diagnostic_edge_overlay_frame(objects);

	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::diagnostic_edge) == 0U,
		"separated ordinary meshes should not emit diagnostic edges");
}

void diagnostic_edges_ignore_same_object_adjacent_endpoint_contact()
{
	const quader_editor_core::PolygonObject object =
		diagnostic_triangle_object(1, { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 1.0F, 1.0F, 0.0F });
	const std::vector<quader_editor_core::PolygonObject> objects { object };

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_diagnostic_edge_overlay_frame(objects);

	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::diagnostic_edge) == 0U,
		"normal same-object adjacent edges sharing only an endpoint should not be diagnostics");
}

void diagnostic_edges_mark_crossing_edges()
{
	const quader_editor_core::PolygonObject first =
		diagnostic_single_edge_object(1, { -1.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F });
	const quader_editor_core::PolygonObject second =
		diagnostic_single_edge_object(2, { 0.0F, -1.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });
	const std::vector<quader_editor_core::PolygonObject> objects { first, second };

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_diagnostic_edge_overlay_frame(objects);

	require_test(line_count_for_layer(frame, quader_editor_core::SelectionOverlayLayer::diagnostic_edge) == 2U,
		"crossing edges should mark both whole edge segments");
	require_test(diagnostic_line_exists_for_object(frame, 1) && diagnostic_line_exists_for_object(frame, 2),
		"diagnostic crossing lines should preserve the owning object ids");
}

} // namespace

TEST_CASE("qdr_editor_core_overlay_tests", "[editor-core-overlay]")
{
	selected_object_overlay_uses_xray_fill_and_depth_tested_active_wire();
	selected_mesh_source_wire_uses_source_color_in_component_mode();
	component_deselect_keeps_active_mesh_source_wire_visible();
	component_hover_source_wire_candidate_is_sticky_until_replaced();
	vertex_mode_hover_candidate_exposes_unselected_handles();
	hover_only_overlay_omits_selected_base_geometry();
	selected_component_hover_keeps_normal_hover_primitives();
	selected_hover_overlay_primitives_carry_stable_owner_keys();
	selected_plane_overlay_marks_open_edges();
	top_loop_bevel_overlay_emits_selected_strip_topology();
	diagnostic_edges_mark_overlapping_edges_across_unselected_objects();
	diagnostic_edges_ignore_ordinary_non_overlapping_meshes();
	diagnostic_edges_ignore_same_object_adjacent_endpoint_contact();
	diagnostic_edges_mark_crossing_edges();
}
