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

std::size_t line_count_for_layer_and_object(
    const quader_editor_core::SelectionOverlayFrame &frame,
    quader_editor_core::SelectionOverlayLayer layer, int object_id)
{
  return static_cast<std::size_t>(
      std::count_if(frame.lines.begin(), frame.lines.end(),
                    [layer, object_id](
                        const quader_editor_core::SelectionOverlayLine &line) {
                      return line.layer == layer &&
                             line.object_id == object_id;
                    }));
}

quader_poly::Document make_open_quad_document()
{
  quader_poly::Document document;
  const quader_poly::ElementId bottom_left =
      quader_poly::add_vertex(document, {-1.0F, 0.0F, -1.0F});
  const quader_poly::ElementId bottom_right =
      quader_poly::add_vertex(document, {1.0F, 0.0F, -1.0F});
  const quader_poly::ElementId top_right =
      quader_poly::add_vertex(document, {1.0F, 0.0F, 1.0F});
  const quader_poly::ElementId top_left =
      quader_poly::add_vertex(document, {-1.0F, 0.0F, 1.0F});
  const std::array<quader_poly::ElementId, 4> vertices{
      bottom_left, bottom_right, top_right, top_left};
  [[maybe_unused]] const quader_poly::ElementId face =
      quader_poly::add_face(document, vertices);
  return document;
}

std::size_t triangle_count_for_layer_and_object(
    const quader_editor_core::SelectionOverlayFrame &frame,
    quader_editor_core::SelectionOverlayLayer layer, int object_id)
{
  return static_cast<std::size_t>(std::count_if(
      frame.triangles.begin(), frame.triangles.end(),
      [layer, object_id](
          const quader_editor_core::SelectionOverlayTriangle &triangle) {
        return triangle.layer == layer && triangle.object_id == object_id;
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

std::size_t point_count_for_layer_and_object(
    const quader_editor_core::SelectionOverlayFrame &frame,
    quader_editor_core::SelectionOverlayLayer layer, int object_id)
{
  return static_cast<std::size_t>(
      std::count_if(frame.points.begin(), frame.points.end(),
                    [layer, object_id](
                        const quader_editor_core::SelectionOverlayPoint &point) {
                      return point.layer == layer &&
                             point.object_id == object_id;
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

bool position_matches(quader::QVec3 left, quader::QVec3 right, float epsilon = 0.0001F)
{
	return nearly_equal(left.x, right.x, epsilon) &&
		nearly_equal(left.y, right.y, epsilon) &&
		nearly_equal(left.z, right.z, epsilon);
}

bool document_has_vertex_at(const quader_poly::Document& document, quader::QVec3 position)
{
	return std::any_of(document.vertices.begin(), document.vertices.end(), [position](const quader_poly::Vertex& vertex) {
		return position_matches(vertex.position, position);
	});
}

quader_editor_core::PolygonObject *object_by_id(
    std::vector<quader_editor_core::PolygonObject> &objects, int object_id)
{
  const auto found = std::find_if(
      objects.begin(), objects.end(),
      [object_id](const quader_editor_core::PolygonObject &object) {
        return object.id == object_id;
      });
  return found == objects.end() ? nullptr : &*found;
}

quader_editor_core::PolygonObject cube_object_for_overlay(int object_id)
{
  quader_editor_core::PolygonObject object;
  object.id = object_id;
  object.document = quader_poly::make_cube({});
  return object;
}

void select_first_component_for_overlay_mode(
    quader_editor_core::PolygonObject &object,
    quader_editor_core::SelectionMode mode)
{
  object.selected = true;
  object.component_edit_target = true;
  object.selection = quader_poly::Selection();
  switch (mode) {
  case quader_editor_core::SelectionMode::Vertex:
    require_test(!object.document.vertices.empty(),
                 "component source-wire fixture should have vertices");
    object.selection.mode = quader_poly::SelectionMode::Vertex;
    object.selection.vertices.push_back(object.document.vertices.front().id);
    object.selection.has_active = true;
    object.selection.active_kind = quader_poly::ElementKind::Vertex;
    object.selection.active_vertex = object.selection.vertices.front();
    return;
  case quader_editor_core::SelectionMode::Edge: {
    const std::vector<quader_poly::Edge> edges =
        quader_poly::document_edges(object.document);
    require_test(!edges.empty(),
                 "component source-wire fixture should have edges");
    object.selection.mode = quader_poly::SelectionMode::Edge;
    object.selection.edges.push_back(edges.front());
    object.selection.has_active = true;
    object.selection.active_kind = quader_poly::ElementKind::Edge;
    object.selection.active_edge = object.selection.edges.front();
    return;
  }
  case quader_editor_core::SelectionMode::Face:
    require_test(!object.document.faces.empty(),
                 "component source-wire fixture should have faces");
    object.selection.mode = quader_poly::SelectionMode::Face;
    object.selection.faces.push_back(object.document.faces.front().id);
    object.selection.has_active = true;
    object.selection.active_kind = quader_poly::ElementKind::Face;
    object.selection.active_face = object.selection.faces.front();
    return;
  case quader_editor_core::SelectionMode::Object:
    return;
  }
}

quader_editor_core::PolygonObject diagnostic_single_edge_object(
	int object_id,
	quader::QVec3 start,
	quader::QVec3 end)
{
	quader_editor_core::PolygonObject object;
	object.id = object_id;
	object.selected = false;
	const quader_poly::ElementId start_id = quader_poly::add_vertex(object.document, start);
	const quader_poly::ElementId end_id = quader_poly::add_vertex(object.document, end);
	const std::array face_vertices { start_id, end_id, end_id };
        require_test(quader_poly::add_face(object.document, face_vertices) !=
                         quader_poly::kInvalidElementId,
                     "diagnostic edge fixture should create a degenerate "
                     "edge-only face");
        require_test(
		quader_poly::document_edges(object.document).size() == 1U,
		"diagnostic edge fixture should expose exactly one document edge");
	return object;
}

quader_editor_core::PolygonObject diagnostic_triangle_object(
	int object_id,
	quader::QVec3 a,
	quader::QVec3 b,
	quader::QVec3 c)
{
	quader_editor_core::PolygonObject object;
	object.id = object_id;
	object.selected = false;
	const std::array face_vertices {
		quader_poly::add_vertex(object.document, a),
		quader_poly::add_vertex(object.document, b),
		quader_poly::add_vertex(object.document, c),
	};
        require_test(quader_poly::add_face(object.document, face_vertices) !=
                         quader_poly::kInvalidElementId,
                     "diagnostic triangle fixture should create a face");
        return object;
}

bool diagnostic_line_exists_for_object(
	const quader_editor_core::SelectionOverlayFrame& frame,
	int object_id)
{
  return std::any_of(
      frame.lines.begin(), frame.lines.end(),
      [object_id](const quader_editor_core::SelectionOverlayLine &line) {
        return line.layer ==
                   quader_editor_core::SelectionOverlayLayer::DiagnosticEdge &&
               line.object_id == object_id &&
               line.edge.a != quader_poly::kInvalidElementId &&
               line.edge.b != quader_poly::kInvalidElementId;
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

quader_poly::Edge require_edge_between_positions(
	const quader_poly::Document& document,
	quader::QVec3 first,
	quader::QVec3 second)
{
	for (const quader_poly::Edge edge : quader_poly::document_edges(document)) {
		const quader_poly::Vertex* a = quader_poly::find_vertex(document, edge.a);
		const quader_poly::Vertex* b = quader_poly::find_vertex(document, edge.b);
		if (a == nullptr || b == nullptr) {
			continue;
		}
		const bool direct = position_matches(a->position, first) && position_matches(b->position, second);
		const bool reversed = position_matches(a->position, second) && position_matches(b->position, first);
		if (direct || reversed) {
			return edge;
		}
	}
	require_test(false, "edge fixture should find the requested document edge");
	return {};
}

/**
 * Stores overlapping source-wire edge fixture data for active topology hover tests.
 */
struct OverlappingSourceWireEdgeFixture {
	quader_editor_core::PolygonObject object;
	quader_poly::Edge selected_edge;
	quader_poly::Edge front_edge;
};

OverlappingSourceWireEdgeFixture overlapping_source_wire_edge_fixture()
{
	OverlappingSourceWireEdgeFixture fixture;
	fixture.object.id = 7;
	fixture.object.selected = true;
	quader_poly::Document& document = fixture.object.document;

	const quader_poly::ElementId near_bottom_left = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId near_top_left = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId near_top_right = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId near_bottom_right = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.0F });
        require_test(
            quader_poly::add_face(
                document, std::array{near_bottom_left, near_top_left,
                                     near_top_right, near_bottom_right}) !=
                quader_poly::kInvalidElementId,
            "source-wire fixture should create the nearer front-facing face");
        fixture.front_edge = quader_poly::make_edge(near_bottom_right, near_top_right);

	const quader_poly::ElementId far_bottom_left = quader_poly::add_vertex(document, { -1.0F, -1.0F, 1.0F });
	const quader_poly::ElementId far_top_left = quader_poly::add_vertex(document, { -1.0F, 1.0F, 1.0F });
	const quader_poly::ElementId far_top_right = quader_poly::add_vertex(document, { 1.0F, 1.0F, 1.0F });
	const quader_poly::ElementId far_bottom_right = quader_poly::add_vertex(document, { 1.0F, -1.0F, 1.0F });
        require_test(
            quader_poly::add_face(
                document, std::array{far_bottom_left, far_top_left,
                                     far_top_right, far_bottom_right}) !=
                quader_poly::kInvalidElementId,
            "source-wire fixture should create the farther front-facing face");
        fixture.selected_edge = quader_poly::make_edge(far_bottom_right, far_top_right);

        fixture.object.selection.mode = quader_poly::SelectionMode::Edge;
        fixture.object.selection.edges.push_back(fixture.selected_edge);
	fixture.object.selection.has_active = true;
        fixture.object.selection.active_kind = quader_poly::ElementKind::Edge;
        fixture.object.selection.active_edge = fixture.selected_edge;
	return fixture;
}

/**
 * Stores inverted open cube edge fixture data for inner-edge hover tests.
 */
struct InvertedOpenCubeEdgeFixture {
	quader_editor_core::PolygonObject object;
	quader_poly::Edge inner_edge;
	quader_poly::Ray inner_edge_ray;
};

InvertedOpenCubeEdgeFixture inverted_open_cube_edge_fixture()
{
	InvertedOpenCubeEdgeFixture fixture;
	fixture.object.id = 9;
	fixture.object.selected = true;
	fixture.object.document = quader_poly::make_cube({});
	quader_poly::Document& document = fixture.object.document;
	document.faces.erase(
		std::remove_if(
			document.faces.begin(),
			document.faces.end(),
			[&document](const quader_poly::Face& face) {
				return quader::dot(quader_poly::face_normal(document, face), { 0.0F, 1.0F, 0.0F }) > 0.5F;
			}),
		document.faces.end());

	quader_poly::Selection all_faces;
        all_faces.mode = quader_poly::SelectionMode::Face;
        for (const quader_poly::Face& face : document.faces) {
		all_faces.faces.push_back(face.id);
	}
	require_test(quader_poly::flip_selected_face_normals(document, all_faces).changed,
		"inverted open cube fixture should flip remaining faces inward");

	fixture.inner_edge = require_edge_between_positions(document, { 1.0F, -1.0F, -1.0F }, { 1.0F, -1.0F, 1.0F });
	fixture.inner_edge_ray = { { 0.0F, 0.0F, 0.0F }, quader::normalize_or_zero(quader::QVec3 { 1.0F, -1.0F, 0.0F }) };
        fixture.object.selection.mode = quader_poly::SelectionMode::Edge;
        fixture.object.selection.edges.push_back(fixture.inner_edge);
	fixture.object.selection.has_active = true;
        fixture.object.selection.active_kind = quader_poly::ElementKind::Edge;
        fixture.object.selection.active_edge = fixture.inner_edge;
	return fixture;
}

void selected_object_overlay_uses_xray_fill_and_draw_on_top_active_wire()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"selected object overlay fixture should create a box");

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), {});
        require_test(
            triangle_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::SelectedXrayFace) >
                0,
            "selected object overlay should include x-ray face fill");
        const quader_editor_core::SelectionOverlayStyle style;
        require_test(line_count_for_layer_and_color(
                         frame,
                         quader_editor_core::SelectionOverlayLayer::SourceWire,
                         style.source_wire_color_abgr) == 0U,
                     "selected object active wire should not be emitted as "
                     "ordinary source/candidate wire");
        require_test(triangle_count_for_layer(
                         frame, quader_editor_core::SelectionOverlayLayer::
                                    SourceWireDepthStamp) == 0U,
                     "selected object active wire should not emit the "
                     "component source-wire depth stamp");
        require_test(
            line_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::SelectedEdge) == 12U,
            "selected object active wire should use the draw-on-top selected "
            "edge layer");
        require_test(
            line_count_for_layer_and_color(
                frame, quader_editor_core::SelectionOverlayLayer::SelectedEdge,
                style.selected_edge_color_abgr) == 12U,
            "selected object active wire should keep the selected-wire color");
        require_test(
            line_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge) ==
                0U,
            "selected object active wire should not use selected component "
            "face-edge handles");
        require_test(
            line_count_for_layer(
                frame, quader_editor_core::SelectionOverlayLayer::OpenEdge) ==
                0,
            "closed selected object should not expose open-edge hatching");
}

void selected_mesh_source_wire_uses_source_color_in_component_mode()
{
  for (const quader_editor_core::SelectionMode mode : {
           quader_editor_core::SelectionMode::Face,
           quader_editor_core::SelectionMode::Edge,
           quader_editor_core::SelectionMode::Vertex,
       }) {
    quader_editor_core::EditorSession session;
    require_test(
        session
            .create_box_from_bounds({-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F})
            .changed,
        "selected mesh source-wire color fixture should create a box");
    session.set_selection_mode(mode);
    select_first_component_for_overlay_mode(session.objects().front(), mode);

    const quader_editor_core::SelectionOverlayFrame frame =
        quader_editor_core::build_selection_overlay_frame(
            session.objects(), session.selection_mode(), {});
    const quader_editor_core::SelectionOverlayStyle style;
    const std::size_t source_wire_count = line_count_for_layer(
        frame, quader_editor_core::SelectionOverlayLayer::SourceWire);
    require_test(source_wire_count == 12U,
                 "component mode selected mesh should keep the active mesh "
                 "topology source wire visible");
    require_test(
        triangle_count_for_layer(
            frame,
            quader_editor_core::SelectionOverlayLayer::SourceWireDepthStamp) >
            0U,
        "component mode selected mesh should stamp the mesh depth for inverted "
        "source-wire interiors");
    require_test(line_count_for_layer_and_color(
                     frame,
                     quader_editor_core::SelectionOverlayLayer::SourceWire,
                     style.source_wire_color_abgr) == source_wire_count,
                 "component mode selected mesh source wire should use ordinary "
                 "source-wire color");
    require_test(line_count_for_layer_and_color(
                     frame,
                     quader_editor_core::SelectionOverlayLayer::SourceWire,
                     style.selected_edge_color_abgr) == 0U,
                 "component mode selected mesh source wire should not use "
                 "selected-wire color");
    const std::size_t selected_edge_count = line_count_for_layer(
        frame, quader_editor_core::SelectionOverlayLayer::SelectedEdge);
    require_test(
        selected_edge_count < source_wire_count,
        "component mode selected mesh should not promote the full mesh source "
        "wire to draw-on-top selected edges");
  }
}

void component_modes_do_not_emit_object_xray_fill()
{
  for (const quader_editor_core::SelectionMode mode : {
           quader_editor_core::SelectionMode::Face,
           quader_editor_core::SelectionMode::Edge,
           quader_editor_core::SelectionMode::Vertex,
       }) {
    quader_editor_core::EditorSession selected_session;
    require_test(
        selected_session
            .create_box_from_bounds({-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F})
            .changed,
        "component no-xray selected fixture should create a box");
    selected_session.set_selection_mode(mode);
    select_first_component_for_overlay_mode(selected_session.objects().front(),
                                            mode);

    const quader_editor_core::SelectionOverlayFrame selected_frame =
        quader_editor_core::build_selection_overlay_frame(
            selected_session.objects(), selected_session.selection_mode(), {});
    require_test(
        line_count_for_layer(
            selected_frame,
            quader_editor_core::SelectionOverlayLayer::SourceWire) == 12U,
        "component modes should keep the selected mesh source wire visible");
    require_test(
        triangle_count_for_layer(
            selected_frame,
            quader_editor_core::SelectionOverlayLayer::SourceWireDepthStamp) >
            0U,
        "component modes should keep source-wire depth-stamp metadata for "
        "inverted mesh visualization");
    require_test(
        triangle_count_for_layer(
            selected_frame,
            quader_editor_core::SelectionOverlayLayer::SelectedXrayFace) == 0U,
        "component modes must not emit object-mode x-ray face fill for the "
        "active mesh");

    quader_editor_core::EditorSession hover_session = two_box_session();
    hover_session.clear_selection();
    hover_session.set_selection_mode(mode);
    const quader_editor_core::SelectionHoverUpdateResult hover =
        quader_editor_core::update_selection_hover(
            hover_session.objects(), hover_session.selection_mode(), {},
            {{0.0F, 0.0F, -4.0F}, {0.0F, 0.0F, 1.0F}}, false);
    require_test(hover.hover.candidate_object_id == 1,
                 "component no-xray hover fixture should track the hovered "
                 "source-wire candidate");

    const quader_editor_core::SelectionOverlayFrame hover_frame =
        quader_editor_core::build_selection_overlay_frame(
            hover_session.objects(), hover_session.selection_mode(),
            hover.hover);
    require_test(
        line_count_for_layer(
            hover_frame,
            quader_editor_core::SelectionOverlayLayer::SourceWire) > 0U,
        "component hover candidates should keep source-wire visualization");
    require_test(
        triangle_count_for_layer(
            hover_frame,
            quader_editor_core::SelectionOverlayLayer::SourceWireDepthStamp) >
            0U,
        "component hover candidates should keep source-wire depth-stamp "
        "metadata");
    require_test(
        triangle_count_for_layer(
            hover_frame,
            quader_editor_core::SelectionOverlayLayer::SelectedXrayFace) == 0U,
        "component hover candidates must not emit object-mode x-ray face fill");
  }
}

void component_modes_emit_source_wire_only_for_active_edit_mesh()
{
  for (const quader_editor_core::SelectionMode mode : {
           quader_editor_core::SelectionMode::Face,
           quader_editor_core::SelectionMode::Edge,
           quader_editor_core::SelectionMode::Vertex,
       }) {
    quader_editor_core::EditorSession session = two_box_session();
    session.set_selected_object_ids({1, 2}, 2);
    session.set_selection_mode(mode);
    require_test(session.active_object_id() == 2,
                 "multi-object source-wire fixture should keep the second "
                 "mesh active");
    quader_editor_core::PolygonObject *active_object =
        object_by_id(session.objects(), 2);
    require_test(active_object != nullptr,
                 "multi-object source-wire fixture should find the active "
                 "mesh");
    select_first_component_for_overlay_mode(*active_object, mode);

    const quader_editor_core::SelectionOverlayFrame frame =
        quader_editor_core::build_selection_overlay_frame(
            session.objects(), session.selection_mode(),
            session.active_object_id(), {});

    require_test(
        line_count_for_layer_and_object(
            frame, quader_editor_core::SelectionOverlayLayer::SourceWire, 1) ==
            0U,
        "component source wire should not x-ray inactive selected meshes");
    require_test(
        line_count_for_layer_and_object(
            frame, quader_editor_core::SelectionOverlayLayer::SourceWire, 2) ==
            12U,
        "component source wire should stay visible for the active edit mesh");
    require_test(
        triangle_count_for_layer_and_object(
            frame,
            quader_editor_core::SelectionOverlayLayer::SourceWireDepthStamp,
            1) == 0U,
        "inactive selected meshes should not emit source-wire depth stamps");
    require_test(
        triangle_count_for_layer_and_object(
            frame,
            quader_editor_core::SelectionOverlayLayer::SourceWireDepthStamp,
            2) > 0U,
        "active edit mesh should keep source-wire depth stamps for inverted "
        "normal cases");
  }
}

void component_source_wire_does_not_stick_without_mode_selection()
{
  std::vector<quader_editor_core::PolygonObject> objects{
      cube_object_for_overlay(1), cube_object_for_overlay(2)};
  objects[0].selected = true;
  quader_editor_core::SelectionHoverState hover;
  hover.candidate_object_id = 2;

  const quader_editor_core::SelectionOverlayFrame hover_frame =
      quader_editor_core::build_selection_overlay_frame(
          objects, quader_editor_core::SelectionMode::Face, 1, hover, {});

  require_test(
      line_count_for_layer_and_object(
          hover_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          1) == 0U,
      "face-mode source wire should not stick to an active object without "
      "selected faces");
  require_test(
      line_count_for_layer_and_object(
          hover_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          2) == 12U,
      "face-mode source wire should move to the hover candidate when the "
      "previous object has no selected faces");

  const quader_editor_core::SelectionOverlayFrame empty_frame =
      quader_editor_core::build_selection_overlay_frame(
          objects, quader_editor_core::SelectionMode::Face, 1, {}, {});
  require_test(line_count_for_layer(empty_frame,
                                    quader_editor_core::SelectionOverlayLayer::
                                        SourceWire) == 12U,
               "active object selection should show component source wire "
               "while there is no hover candidate");
}

void component_source_wire_sticks_to_component_edit_targets()
{
  std::vector<quader_editor_core::PolygonObject> objects{
      cube_object_for_overlay(1), cube_object_for_overlay(2)};
  quader_editor_core::SelectionHoverState hover;
  hover.candidate_object_id = 2;

  select_first_component_for_overlay_mode(
      objects[0], quader_editor_core::SelectionMode::Face);
  const quader_editor_core::SelectionOverlayFrame face_frame =
      quader_editor_core::build_selection_overlay_frame(
          objects, quader_editor_core::SelectionMode::Face, 1, hover, {});
  require_test(
      line_count_for_layer_and_object(
          face_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          1) == 12U,
      "face-mode source wire should stick to an object with selected faces");
  require_test(
      line_count_for_layer_and_object(
          face_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          2) == 12U,
      "face-mode hover candidate should show source wire without erasing the "
      "selected edit target");

  const quader_editor_core::SelectionOverlayFrame edge_mismatch_frame =
      quader_editor_core::build_selection_overlay_frame(
          objects, quader_editor_core::SelectionMode::Edge, 1, hover, {});
  require_test(
      line_count_for_layer_and_object(
          edge_mismatch_frame,
          quader_editor_core::SelectionOverlayLayer::SourceWire, 1) == 12U,
      "edge-mode source wire should stick to a component edit target even "
      "after mode conversion empties the current bucket");
  require_test(
      line_count_for_layer_and_object(
          edge_mismatch_frame,
          quader_editor_core::SelectionOverlayLayer::SourceWire, 2) == 12U,
      "edge-mode hover candidate should remain visible alongside the selected "
      "edit target");

  select_first_component_for_overlay_mode(
      objects[0], quader_editor_core::SelectionMode::Edge);
  const quader_editor_core::SelectionOverlayFrame edge_frame =
      quader_editor_core::build_selection_overlay_frame(
          objects, quader_editor_core::SelectionMode::Edge, 1, hover, {});
  require_test(
      line_count_for_layer_and_object(
          edge_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          1) == 12U,
      "edge-mode source wire should stick to an object with selected edges");
  require_test(
      line_count_for_layer_and_object(
          edge_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          2) == 12U,
      "edge-mode hover candidate should show source wire next to selected "
      "edges");

  select_first_component_for_overlay_mode(
      objects[0], quader_editor_core::SelectionMode::Vertex);
  const quader_editor_core::SelectionOverlayFrame vertex_frame =
      quader_editor_core::build_selection_overlay_frame(
          objects, quader_editor_core::SelectionMode::Vertex, 1, hover, {});
  require_test(
      line_count_for_layer_and_object(
          vertex_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          1) == 12U,
      "vertex-mode source wire should stick to an object with selected "
      "vertices");
  require_test(
      line_count_for_layer_and_object(
          vertex_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          2) == 12U,
      "vertex-mode hover candidate should show source wire next to selected "
      "vertices");
}

void component_edit_target_source_wire_survives_mode_switch()
{
  std::vector<quader_editor_core::PolygonObject> objects{
      cube_object_for_overlay(1), cube_object_for_overlay(2)};
  select_first_component_for_overlay_mode(
      objects[0], quader_editor_core::SelectionMode::Vertex);
  select_first_component_for_overlay_mode(
      objects[1], quader_editor_core::SelectionMode::Vertex);
  int active_object_id = 2;

  quader_editor_core::set_selection_mode(
      objects, quader_editor_core::SelectionMode::Edge, active_object_id,
      quader_editor_core::SelectionMode::Vertex);

  const quader_editor_core::SelectionOverlayFrame frame =
      quader_editor_core::build_selection_overlay_frame(
          objects, quader_editor_core::SelectionMode::Edge, active_object_id,
          {}, {});

  require_test(
      line_count_for_layer_and_object(
          frame, quader_editor_core::SelectionOverlayLayer::SourceWire, 1) ==
          12U,
      "component mode change should keep the first selected mesh source wire");
  require_test(
      line_count_for_layer_and_object(
          frame, quader_editor_core::SelectionOverlayLayer::SourceWire, 2) ==
          12U,
      "component mode change should keep the second selected mesh source wire");
}

void selected_vertex_hover_candidate_exposes_unselected_handles()
{
  std::vector<quader_editor_core::PolygonObject> objects{
      cube_object_for_overlay(1), cube_object_for_overlay(2)};
  select_first_component_for_overlay_mode(
      objects[0], quader_editor_core::SelectionMode::Vertex);
  objects[1].selected = true;
  objects[1].component_edit_target = false;
  objects[1].selection = quader_poly::Selection();

  quader_editor_core::SelectionHoverState hover;
  hover.candidate_object_id = 2;

  const quader_editor_core::SelectionOverlayFrame full_frame =
      quader_editor_core::build_selection_overlay_frame(
          objects, quader_editor_core::SelectionMode::Vertex, 1, hover, {});
  require_test(
      line_count_for_layer_and_object(
          full_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          1) == 12U,
      "hover candidate should not erase the selected mesh source wire");
  require_test(
      line_count_for_layer_and_object(
          full_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          2) == 12U,
      "selected hover candidate without vertices should show source wire");
  require_test(
      point_count_for_layer_and_object(
          full_frame, quader_editor_core::SelectionOverlayLayer::UnselectedVertex,
          2) > 0U,
      "selected hover candidate without vertices should expose vertex handles");

  const quader_editor_core::SelectionOverlayFrame hover_frame =
      quader_editor_core::build_selection_hover_overlay_frame(
          objects, quader_editor_core::SelectionMode::Vertex, hover, {});
  require_test(
      line_count_for_layer_and_object(
          hover_frame, quader_editor_core::SelectionOverlayLayer::SourceWire,
          2) == 12U,
      "hover-only overlay should expose selected candidate source wire");
  require_test(
      point_count_for_layer_and_object(
          hover_frame,
          quader_editor_core::SelectionOverlayLayer::UnselectedVertex, 2) > 0U,
      "hover-only overlay should expose selected candidate vertex handles");
}

void selected_component_does_not_cut_holes_in_active_mesh_source_wire()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"selected component source-wire fixture should create a box");
        session.set_selection_mode(quader_editor_core::SelectionMode::Face);
        require_test(
            session.pick_select_face(
                quader_editor_core::SelectionEditOperation::Replace,
                {{0.0F, 0.0F, -4.0F}, {0.0F, 0.0F, 1.0F}}),
            "selected component source-wire fixture should select a face");

        const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), {});
	const quader_editor_core::SelectionOverlayStyle style;
        require_test(
            line_count_for_layer(
                frame, quader_editor_core::SelectionOverlayLayer::SourceWire) ==
                12U,
            "selected component overlays should keep the full active mesh "
            "topology source wire");
        require_test(triangle_count_for_layer(
                         frame, quader_editor_core::SelectionOverlayLayer::
                                    SourceWireDepthStamp) > 0U,
                     "selected component overlays should keep the source-wire "
                     "depth stamp");
        require_test(line_count_for_layer_and_color(
                         frame,
                         quader_editor_core::SelectionOverlayLayer::SourceWire,
                         style.source_wire_color_abgr) == 12U,
                     "active mesh topology source wire should keep ordinary "
                     "source-wire color under selected highlights");
        require_test(
            line_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge) >=
                4U,
            "selected face should still draw selected face-edge highlights "
            "over source wire");
}

void selected_inner_edge_hover_uses_active_source_wire_selected_color()
{
	const OverlappingSourceWireEdgeFixture fixture = overlapping_source_wire_edge_fixture();
	std::vector<quader_editor_core::PolygonObject> objects { fixture.object };
	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Edge;
        options.edge_radius = 0.12F;
	options.pick_backfaces = true;
	const quader_poly::Ray ray_through_overlapping_edges { { 1.0F, 0.0F, -2.0F }, { 0.0F, 0.0F, 1.0F } };

        const quader_editor_core::SelectionHoverUpdateResult hover =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Edge, {},
                ray_through_overlapping_edges, true, options);

        require_test(hover.hover.kind ==
                             quader_editor_core::SelectionHoverKind::Edge &&
                         hover.hover.edge == fixture.selected_edge,
                     "edge hover should use the selected active source-wire "
                     "edge instead of the nearer shaded edge");
        require_test(
            quader_editor_core::selection_hover_target_matches_selection(
                objects, quader_editor_core::SelectionMode::Edge, hover.hover),
            "selected source-wire edge hover should match the existing edge "
            "selection");

        const quader_editor_core::SelectionOverlayStyle style;
        const quader_editor_core::SelectionOverlayFrame frame =
            quader_editor_core::build_selection_overlay_frame(
                objects, quader_editor_core::SelectionMode::Edge, hover.hover,
                style);
        require_test(frame.hover_target_selected,
		"selected source-wire edge hover with the modifier should become remove-preview hover");
        require_test(line_count_for_layer_and_color(
                         frame,
                         quader_editor_core::SelectionOverlayLayer::HoverEdge,
                         style.hover_selected_line_color_abgr) == 1U,
                     "selected source-wire edge hover should draw the deselect "
                     "hover color");

        quader_editor_core::SelectionMode mode =
            quader_editor_core::SelectionMode::Edge;
        int active_object_id = fixture.object.id;
        require_test(quader_editor_core::pick_select(
                         objects, mode,
                         quader_editor_core::SelectionEditOperation::Remove,
                         ray_through_overlapping_edges, active_object_id),
                     "edge remove should use the same selected active "
                     "source-wire edge as hover");
        require_test(objects.front().selection.edges.empty(),
		"edge remove should deselect the selected active source-wire edge");
}

void inverted_open_cube_inner_edge_hover_uses_selected_deselect_color()
{
	const InvertedOpenCubeEdgeFixture fixture = inverted_open_cube_edge_fixture();
	std::vector<quader_editor_core::PolygonObject> objects { fixture.object };
	quader_poly::PickOptions options;
        options.mode = quader_poly::SelectionMode::Edge;
        options.edge_radius = 0.12F;
	options.pick_backfaces = true;

        const quader_editor_core::SelectionHoverUpdateResult hover =
            quader_editor_core::update_selection_hover(
                objects, quader_editor_core::SelectionMode::Edge, {},
                fixture.inner_edge_ray, true, options);

        require_test(hover.hover.kind ==
                             quader_editor_core::SelectionHoverKind::Edge &&
                         hover.hover.edge == fixture.inner_edge,
                     "inverted open cube inner edge hover should resolve to "
                     "the selected source-wire edge");
        require_test(
            quader_editor_core::selection_hover_target_matches_selection(
                objects, quader_editor_core::SelectionMode::Edge, hover.hover),
            "inverted open cube inner edge hover should match the selected "
            "edge");

        const quader_editor_core::SelectionOverlayStyle style;
        const quader_editor_core::SelectionOverlayFrame frame =
            quader_editor_core::build_selection_overlay_frame(
                objects, quader_editor_core::SelectionMode::Edge, hover.hover,
                style);
        require_test(frame.hover_target_selected,
		"inverted open cube selected edge hover should become a remove-preview hover");
        require_test(
            line_count_for_layer(
                frame, quader_editor_core::SelectionOverlayLayer::SourceWire) >
                0U,
            "inverted open cube should keep active mesh source-wire lines for "
            "inner edges");
        require_test(triangle_count_for_layer(
                         frame, quader_editor_core::SelectionOverlayLayer::
                                    SourceWireDepthStamp) > 0U,
                     "inverted open cube should stamp active mesh depth for "
                     "inner source-wire edges");
        require_test(line_count_for_layer_and_color(
                         frame,
                         quader_editor_core::SelectionOverlayLayer::HoverEdge,
                         style.hover_selected_line_color_abgr) == 1U,
                     "inverted open cube selected edge hover should draw the "
                     "deselect hover color");

        quader_editor_core::SelectionMode mode =
            quader_editor_core::SelectionMode::Edge;
        int active_object_id = fixture.object.id;
        require_test(quader_editor_core::pick_select(
                         objects, mode,
                         quader_editor_core::SelectionEditOperation::Remove,
                         fixture.inner_edge_ray, active_object_id),
                     "inverted open cube edge remove should use the selected "
                     "active source-wire edge");
        require_test(objects.front().selection.edges.empty(),
		"inverted open cube edge remove should deselect the selected active source-wire edge");
}

void component_deselect_keeps_active_mesh_source_wire_visible()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"component deselect source-wire fixture should create a box");
        session.set_selection_mode(quader_editor_core::SelectionMode::Face);
        const quader_poly::Ray front_center_ray { { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } };

        require_test(
            session.pick_select_face(
                quader_editor_core::SelectionEditOperation::Replace,
                front_center_ray),
            "component deselect source-wire fixture should select a face");
        require_test(session.has_selection(), "selected face should count as a component selection");
	const int active_object_id = session.active_object_id();

        require_test(session.pick_select_face(
                         quader_editor_core::SelectionEditOperation::Remove,
                         front_center_ray),
                     "component deselect should remove the selected face");
        require_test(!session.has_selection(), "removing the last component should leave no selected component");
	require_test(session.active_object_id() == active_object_id, "removing the last component should keep the mesh as the active edit target");

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), {});
        require_test(
            line_count_for_layer(
                frame, quader_editor_core::SelectionOverlayLayer::SourceWire) ==
                12U,
            "removing the last component should keep the active mesh source "
            "wire visible");
        require_test(triangle_count_for_layer(
                         frame, quader_editor_core::SelectionOverlayLayer::
                                    SourceWireDepthStamp) > 0U,
                     "removing the last component should keep the active mesh "
                     "source-wire depth stamp");
}

void component_hover_source_wire_candidate_is_sticky_until_replaced()
{
	quader_editor_core::EditorSession session = two_box_session();
	session.clear_selection();
        session.set_selection_mode(quader_editor_core::SelectionMode::Face);

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
        const std::size_t first_source_wire_count = line_count_for_layer(
            first_frame, quader_editor_core::SelectionOverlayLayer::SourceWire);
        require_test(first_source_wire_count > 0, "component hover candidate should draw source wire");
        require_test(
            triangle_count_for_layer(first_frame,
                                     quader_editor_core::SelectionOverlayLayer::
                                         SourceWireDepthStamp) > 0U,
            "component hover candidate should stamp source-wire depth");
        const quader_editor_core::SelectionOverlayStyle style;
        require_test(line_count_for_layer_and_color(
                         first_frame,
                         quader_editor_core::SelectionOverlayLayer::SourceWire,
                         style.source_wire_color_abgr) ==
                         first_source_wire_count,
                     "component hover candidate source wire should keep the "
                     "ordinary source-wire color");

        const quader_editor_core::SelectionHoverUpdateResult empty_hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		first_hover.hover,
		{ { 64.0F, 64.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
        require_test(empty_hover.hover.kind ==
                             quader_editor_core::SelectionHoverKind::None &&
                         empty_hover.hover.candidate_object_id == 1,
                     "empty component hover should keep the previous "
                     "source-wire candidate");
        const quader_editor_core::SelectionOverlayFrame empty_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), empty_hover.hover);
        require_test(
            line_count_for_layer(
                empty_frame,
                quader_editor_core::SelectionOverlayLayer::SourceWire) ==
                first_source_wire_count,
            "empty hover should preserve candidate source-wire lines");
        require_test(
            triangle_count_for_layer(empty_frame,
                                     quader_editor_core::SelectionOverlayLayer::
                                         SourceWireDepthStamp) > 0U,
            "empty hover should preserve candidate source-wire depth stamp");

        const quader_editor_core::SelectionHoverUpdateResult second_hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		empty_hover.hover,
		{ { 5.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
	require_test(second_hover.hover.candidate_object_id == 2,
		"hovering a different component object should replace the source-wire candidate");
}

void object_hover_tracks_candidate_without_source_wire_before_selection()
{
	quader_editor_core::EditorSession session = two_box_session();
	session.clear_selection();
        session.set_selection_mode(quader_editor_core::SelectionMode::Object);

        const quader_editor_core::SelectionHoverUpdateResult first_hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		{},
		{ { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
	require_test(first_hover.changed && first_hover.hover.object_id == 1 && first_hover.hover.candidate_object_id == 1,
		"object hover should still track the hovered mesh for click selection");

	const quader_editor_core::SelectionOverlayFrame first_frame =
		quader_editor_core::build_selection_hover_overlay_frame(session.objects(), session.selection_mode(), first_hover.hover);
        require_test(
            line_count_for_layer(
                first_frame,
                quader_editor_core::SelectionOverlayLayer::SourceWire) == 0U,
            "object hover should not draw mesh source wire before click "
            "selection");
        const quader_editor_core::SelectionOverlayFrame first_base_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), first_hover.hover);
        require_test(
            line_count_for_layer(
                first_base_frame,
                quader_editor_core::SelectionOverlayLayer::SourceWire) == 0U,
            "object hover should not draw mesh source wire in the base "
            "selection overlay");

        session.set_selected_object_ids({ 1 }, 1);
	const quader_editor_core::SelectionOverlayFrame selected_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), {});
        require_test(
            line_count_for_layer(
                selected_frame,
                quader_editor_core::SelectionOverlayLayer::SelectedEdge) > 0U,
            "selected object should draw selected mesh wire");
        require_test(
            line_count_for_layer(
                selected_frame,
                quader_editor_core::SelectionOverlayLayer::SourceWire) == 0U,
            "selected object should not draw ordinary source wire in mesh "
            "mode");
        session.clear_selection();
        session.set_selection_mode(quader_editor_core::SelectionMode::Object);

        const quader_editor_core::SelectionHoverUpdateResult empty_hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		first_hover.hover,
		{ { 64.0F, 64.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
        require_test(empty_hover.hover.kind ==
                             quader_editor_core::SelectionHoverKind::None &&
                         empty_hover.hover.object_id == -1 &&
                         empty_hover.hover.candidate_object_id == 1,
                     "empty object hover may keep the previous candidate "
                     "without drawing it");
        const quader_editor_core::SelectionOverlayFrame empty_frame =
		quader_editor_core::build_selection_hover_overlay_frame(session.objects(), session.selection_mode(), empty_hover.hover);
        require_test(
            line_count_for_layer(
                empty_frame,
                quader_editor_core::SelectionOverlayLayer::SourceWire) == 0U,
            "empty object hover should not preserve mesh source-wire lines in "
            "the hover frame");
        const quader_editor_core::SelectionOverlayFrame empty_base_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), empty_hover.hover);
        require_test(
            line_count_for_layer(
                empty_base_frame,
                quader_editor_core::SelectionOverlayLayer::SourceWire) == 0U,
            "empty object hover should not preserve mesh source-wire lines in "
            "the base selection overlay");

        const quader_editor_core::SelectionHoverUpdateResult second_hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		empty_hover.hover,
		{ { 5.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
	require_test(second_hover.hover.object_id == 2 && second_hover.hover.candidate_object_id == 2,
		"hovering a different mesh should replace the object source-wire candidate");
}

void vertex_mode_hover_candidate_exposes_unselected_handles()
{
	quader_editor_core::EditorSession session = two_box_session();
	session.clear_selection();
        session.set_selection_mode(quader_editor_core::SelectionMode::Vertex);

        const quader_editor_core::SelectionHoverUpdateResult hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		{},
		{ { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), hover.hover);
        require_test(
            point_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::UnselectedVertex) >
                0,
            "vertex hover candidate should expose unselected vertex handles "
            "before click selection");
        require_test(
            std::all_of(
                frame.points.begin(), frame.points.end(),
                [&](const quader_editor_core::SelectionOverlayPoint &point) {
                  return point.layer !=
                             quader_editor_core::SelectionOverlayLayer::
                                 UnselectedVertex ||
                         (point.object_id == hover.hover.candidate_object_id &&
                          point.vertex_id != quader_poly::kInvalidElementId);
                }),
            "vertex hover candidate handles should carry owner object and "
            "vertex ids");
}

void hover_only_overlay_omits_selected_base_geometry()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"hover-only overlay fixture should create a box");
        session.set_selection_mode(quader_editor_core::SelectionMode::Face);
        require_test(session.select_all().changed, "hover-only overlay fixture should select the box faces");

	const quader_editor_core::SelectionHoverUpdateResult hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		{},
		{ { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		false);
        require_test(hover.hover.kind ==
                         quader_editor_core::SelectionHoverKind::Face,
                     "hover-only overlay fixture should hover a face");

        const quader_editor_core::SelectionOverlayFrame full_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), hover.hover);
        require_test(
            line_count_for_layer(
                full_frame,
                quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge) >
                0,
            "full selected overlay should contain selected face edges");

        const quader_editor_core::SelectionOverlayFrame hover_frame =
		quader_editor_core::build_selection_hover_overlay_frame(session.objects(), session.selection_mode(), hover.hover);
        require_test(triangle_count_for_layer(
                         hover_frame,
                         quader_editor_core::SelectionOverlayLayer::HoverFace) >
                         0,
                     "hover-only overlay should include hover face fill");
        require_test(
            line_count_for_layer(
                hover_frame,
                quader_editor_core::SelectionOverlayLayer::HoverFaceEdge) > 0,
            "hover-only overlay should include hover face edges");
        require_test(
            line_count_for_layer(
                hover_frame,
                quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge) ==
                0,
            "hover-only overlay should not rebuild selected face edges");
        require_test(
            line_count_for_layer(
                hover_frame,
                quader_editor_core::SelectionOverlayLayer::SelectedEdge) == 0,
            "hover-only overlay should not rebuild selected visible edges");
        require_test(
            triangle_count_for_layer(hover_frame,
                                     quader_editor_core::SelectionOverlayLayer::
                                         SelectedVisibleFace) == 0,
            "hover-only overlay should not rebuild selected face fill");
        require_test(
            triangle_count_for_layer(
                hover_frame,
                quader_editor_core::SelectionOverlayLayer::SelectedXrayFace) ==
                0,
            "hover-only overlay should not rebuild selected x-ray fill");
        require_test(
            triangle_count_for_layer(
                hover_frame,
                quader_editor_core::SelectionOverlayLayer::SelectedFaceMask) ==
                0,
            "hover-only overlay should not rebuild selected face masks");
}

void selected_component_hover_keeps_normal_hover_primitives()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"selected component hover fixture should create a box");
        session.set_selection_mode(quader_editor_core::SelectionMode::Face);
        const quader_poly::Ray front_center_ray { { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } };
        require_test(session.pick_select_face(
                         quader_editor_core::SelectionEditOperation::Replace,
                         front_center_ray),
                     "selected component hover fixture should select a face");

        const quader_editor_core::SelectionHoverUpdateResult hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		{},
		front_center_ray,
		false);
        require_test(
            hover.hover.kind == quader_editor_core::SelectionHoverKind::Face,
            "selected component hover fixture should hover the selected face");
        require_test(
		quader_editor_core::selection_hover_target_matches_selection(session.objects(), session.selection_mode(), hover.hover),
		"selected component hover fixture should confirm the hovered face is selected");

	quader_editor_core::SelectionOverlayStyle style;
	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), hover.hover, style);
	require_test(!frame.hover_target_selected,
		"selected component hover without the modifier should not become remove-preview hover");
        require_test(
            line_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge) >
                0,
            "selected component hover should keep the selected face edges");
        require_test(
            triangle_count_for_layer(
                frame, quader_editor_core::SelectionOverlayLayer::HoverFace) >
                0,
            "selected component hover should keep the normal hover face fill");
        require_test(
            line_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::HoverFaceEdge) > 0,
            "selected component hover should keep the normal hover face edges");
        require_test(triangle_count_for_layer_and_color(
                         frame,
                         quader_editor_core::SelectionOverlayLayer::HoverFace,
                         style.hover_face_color_abgr) > 0,
                     "selected component hover without the modifier should use "
                     "normal hover face color");
        require_test(
            line_count_for_layer_and_color(
                frame, quader_editor_core::SelectionOverlayLayer::HoverFaceEdge,
                style.hover_face_edge_color_abgr) > 0,
            "selected component hover without the modifier should use normal "
            "hover edge color");

        quader_editor_core::SelectionHoverState remove_hover = hover.hover;
	remove_hover.selected_modifier_pressed = true;
	const quader_editor_core::SelectionOverlayFrame remove_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), remove_hover, style);
	require_test(remove_frame.hover_target_selected,
		"selected component hover with the modifier should become remove-preview hover");
        require_test(triangle_count_for_layer_and_color(
                         remove_frame,
                         quader_editor_core::SelectionOverlayLayer::HoverFace,
                         style.hover_selected_face_color_abgr) > 0,
                     "selected component hover with the modifier should use "
                     "selected-hover face color");
        require_test(
            line_count_for_layer_and_color(
                remove_frame,
                quader_editor_core::SelectionOverlayLayer::HoverFaceEdge,
                style.hover_selected_line_color_abgr) > 0,
            "selected component hover with the modifier should use "
            "selected-hover edge color");
}

void selected_hover_overlay_primitives_carry_stable_owner_keys()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"selected-hover owner fixture should create a box");
        session.set_selection_mode(quader_editor_core::SelectionMode::Face);
        require_test(session.select_all().changed, "selected-hover owner fixture should select the box faces");

	const quader_editor_core::SelectionHoverUpdateResult hover = quader_editor_core::update_selection_hover(
		session.objects(),
		session.selection_mode(),
		{},
		{ { 0.0F, 0.0F, -4.0F }, { 0.0F, 0.0F, 1.0F } },
		true);
        require_test(hover.hover.kind ==
                         quader_editor_core::SelectionHoverKind::Face,
                     "selected-hover owner fixture should hover a face");

        const quader_editor_core::SelectionOverlayFrame base_frame =
		quader_editor_core::build_selection_overlay_frame(session.objects(), session.selection_mode(), {});
        const bool base_has_owned_selected_face = std::any_of(
            base_frame.triangles.begin(), base_frame.triangles.end(),
            [](const quader_editor_core::SelectionOverlayTriangle &triangle) {
              return (triangle.layer ==
                          quader_editor_core::SelectionOverlayLayer::
                              SelectedXrayFace ||
                      triangle.layer ==
                          quader_editor_core::SelectionOverlayLayer::
                              SelectedVisibleFace ||
                      triangle.layer ==
                          quader_editor_core::SelectionOverlayLayer::
                              SelectedFaceMask) &&
                     triangle.object_id > 0 &&
                     triangle.face_id != quader_poly::kInvalidElementId;
            });
        require_test(base_has_owned_selected_face,
		"cached selected overlay triangles should carry object and face owner keys");

	const quader_editor_core::SelectionOverlayFrame hover_frame =
		quader_editor_core::build_selection_hover_overlay_frame(session.objects(), session.selection_mode(), hover.hover);
	for (const quader_editor_core::SelectionOverlayTriangle& triangle : hover_frame.triangles) {
          if (triangle.layer !=
              quader_editor_core::SelectionOverlayLayer::HoverFace) {
            continue;
          }
                require_test(triangle.object_id == hover.hover.object_id && triangle.face_id == hover.hover.face_id,
			"hover face triangles should carry the hovered object and face owner keys");
	}
	for (const quader_editor_core::SelectionOverlayLine& line : hover_frame.lines) {
          if (line.layer !=
              quader_editor_core::SelectionOverlayLayer::HoverFaceEdge) {
            continue;
          }
          require_test(line.object_id == hover.hover.object_id &&
                           line.face_id == hover.hover.face_id &&
                           line.edge.a != quader_poly::kInvalidElementId &&
                           line.edge.b != quader_poly::kInvalidElementId,
                       "hover face lines should carry object, face, and edge "
                       "owner keys");
        }
}

void selected_plane_overlay_marks_open_edges()
{
	quader_editor_core::PolygonObject plane;
	plane.id = 1;
	plane.selected = true;
	plane.document = make_open_quad_document();
        plane.selection.mode = quader_poly::SelectionMode::Edge;
        plane.selection.edges = quader_poly::document_edges(plane.document);

        const quader_editor_core::SelectionOverlayFrame frame =
            quader_editor_core::build_selection_overlay_frame(
                {plane}, quader_editor_core::SelectionMode::Edge, {});
        require_test(
            line_count_for_layer(
                frame, quader_editor_core::SelectionOverlayLayer::OpenEdge) > 0,
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
        selection.mode = quader_poly::SelectionMode::Edge;
        selection.edges = top_loop_edges(cube);
	require_test(selection.edges.size() == 4U, "top-loop bevel overlay fixture should select the four cube top edges");

	quader_poly::EdgeBevelSettings settings;
	settings.width = 0.9F;
	settings.profile = 0.5F;
	settings.segments = 10;
	const quader_poly::OperationResult bevel = quader_poly::bevel_selected_edges(cube, selection, settings);
	require_test(bevel.changed, "top-loop bevel overlay fixture should generate bevel topology");
        require_test(selection.mode == quader_poly::SelectionMode::Face &&
                         selection.faces.size() == 40U,
                     "top-loop bevel overlay fixture should select the "
                     "generated strip faces");

        quader_editor_core::PolygonObject object;
	object.id = 1;
	object.selected = true;
	object.document = cube;
	object.selection = selection;
        const quader_editor_core::SelectionOverlayFrame frame =
            quader_editor_core::build_selection_overlay_frame(
                {object}, quader_editor_core::SelectionMode::Face, {});
        const std::size_t selected_edge_count = line_count_for_layer(
            frame, quader_editor_core::SelectionOverlayLayer::SelectedEdge);
        const std::size_t selected_face_edge_count = line_count_for_layer(
            frame, quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge);
        require_test(selected_edge_count >= 40U && selected_face_edge_count >= 40U,
		"top-loop bevel overlay should emit selected cyan wire for the generated strip topology");
	for (const quader_editor_core::SelectionOverlayLine& line : frame.lines) {
          if (line.layer !=
                  quader_editor_core::SelectionOverlayLayer::SelectedEdge &&
              line.layer !=
                  quader_editor_core::SelectionOverlayLayer::SelectedFaceEdge) {
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

        require_test(
            line_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::DiagnosticEdge) ==
                2U,
            "overlapping cross-object edges should mark both whole edges");
        require_test(diagnostic_line_exists_for_object(frame, 1) && diagnostic_line_exists_for_object(frame, 2),
		"diagnostic overlap lines should preserve the owning object ids");
	for (const quader_editor_core::SelectionOverlayLine& line : frame.lines) {
          if (line.layer !=
              quader_editor_core::SelectionOverlayLayer::DiagnosticEdge) {
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

        require_test(
            line_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::DiagnosticEdge) ==
                0U,
            "separated ordinary meshes should not emit diagnostic edges");
}

void diagnostic_edges_ignore_same_object_adjacent_endpoint_contact()
{
	const quader_editor_core::PolygonObject object =
		diagnostic_triangle_object(1, { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 1.0F, 1.0F, 0.0F });
	const std::vector<quader_editor_core::PolygonObject> objects { object };

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_diagnostic_edge_overlay_frame(objects);

        require_test(
            line_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::DiagnosticEdge) ==
                0U,
            "normal same-object adjacent edges sharing only an endpoint should "
            "not be diagnostics");
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

        require_test(
            line_count_for_layer(
                frame,
                quader_editor_core::SelectionOverlayLayer::DiagnosticEdge) ==
                2U,
            "crossing edges should mark both whole edge segments");
        require_test(diagnostic_line_exists_for_object(frame, 1) && diagnostic_line_exists_for_object(frame, 2),
		"diagnostic crossing lines should preserve the owning object ids");
}

} // namespace

TEST(QdrModelingRuntimeOverlayTests, QdrEditorCoreOverlayTests) {
	selected_object_overlay_uses_xray_fill_and_draw_on_top_active_wire();
	selected_mesh_source_wire_uses_source_color_in_component_mode();
	component_modes_do_not_emit_object_xray_fill();
	component_modes_emit_source_wire_only_for_active_edit_mesh();
	component_source_wire_does_not_stick_without_mode_selection();
	component_source_wire_sticks_to_component_edit_targets();
	component_edit_target_source_wire_survives_mode_switch();
	selected_vertex_hover_candidate_exposes_unselected_handles();
	selected_component_does_not_cut_holes_in_active_mesh_source_wire();
	selected_inner_edge_hover_uses_active_source_wire_selected_color();
	inverted_open_cube_inner_edge_hover_uses_selected_deselect_color();
	component_deselect_keeps_active_mesh_source_wire_visible();
	component_hover_source_wire_candidate_is_sticky_until_replaced();
	object_hover_tracks_candidate_without_source_wire_before_selection();
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
