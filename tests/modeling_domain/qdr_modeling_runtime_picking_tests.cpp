////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.h"

#include <editor/modeling/runtime/session.h>
#include <mesh/polygon/document.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace {

constexpr std::uint32_t hidden_slot = 99U;

quader_editor_core::PolygonObject cube_object(int id, quader::QVec3 offset = {})
{
	quader_editor_core::PolygonObject object;
	object.id = id;
	object.document = quader_poly::make_cube({});
	for (quader_poly::Vertex& vertex : object.document.vertices) {
		vertex.position.x += offset.x;
		vertex.position.y += offset.y;
		vertex.position.z += offset.z;
	}
	return object;
}

quader_editor_core::PolygonObject open_top_cube_object(int id)
{
	quader_editor_core::PolygonObject object = cube_object(id);
	quader_poly::Document& document = object.document;
	document.faces.erase(
		std::remove_if(
			document.faces.begin(),
			document.faces.end(),
			[&document](const quader_poly::Face& face) {
				return quader::dot(
					quader_poly::face_normal(document, face),
					{ 0.0F, 1.0F, 0.0F }) > 0.5F;
			}),
		document.faces.end());
	return object;
}

quader_poly::Ray front_center_ray()
{
	return { { 0.0F, 0.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } };
}

quader_poly::Ray miss_ray()
{
	return { { 50.0F, 50.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } };
}

bool nearly_equal(float a, float b)
{
	return std::abs(a - b) <= 0.000001F;
}

quader_poly::PickOptions hidden_slot_pick_options(quader_poly::SelectionMode mode)
{
	quader_poly::PickOptions options;
	options.mode = mode;
	options.has_hidden_material_slot = true;
	options.hidden_material_slot = hidden_slot;
	return options;
}

quader_editor_core::PolygonObject stacked_face_object()
{
	quader_editor_core::PolygonObject object;
	object.id = 7;
	quader_poly::Document& document = object.document;
	const quader_poly::ElementId back0 = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId back1 = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId back2 = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId back3 = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId front0 = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.2F });
	const quader_poly::ElementId front1 = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.2F });
	const quader_poly::ElementId front2 = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.2F });
	const quader_poly::ElementId front3 = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.2F });
	[[maybe_unused]] const quader_poly::ElementId back_face =
		quader_poly::add_face(document, std::array { back0, back1, back2, back3 }, 0U);
	[[maybe_unused]] const quader_poly::ElementId front_face =
		quader_poly::add_face(document, std::array { front0, front1, front2, front3 }, hidden_slot);
	return object;
}

quader_editor_core::PolygonObject adjacent_face_object()
{
	quader_editor_core::PolygonObject object;
	object.id = 7;
	quader_poly::Document& document = object.document;
	const quader_poly::ElementId left0 = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId shared0 = quader_poly::add_vertex(document, { 0.0F, -1.0F, 0.0F });
	const quader_poly::ElementId shared1 = quader_poly::add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const quader_poly::ElementId left1 = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId right0 = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId right1 = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.0F });
	[[maybe_unused]] const quader_poly::ElementId visible_face =
		quader_poly::add_face(document, std::array { left0, shared0, shared1, left1 }, 0U);
	[[maybe_unused]] const quader_poly::ElementId hidden_face =
		quader_poly::add_face(document, std::array { shared0, right0, right1, shared1 }, hidden_slot);
	return object;
}

quader_editor_core::PickObjectBounds pick_bounds_for_object(const quader_editor_core::PolygonObject& object)
{
	quader_editor_core::PickObjectBounds bounds;
	bounds.object_id = object.id;
	for (const quader_poly::Vertex& vertex : object.document.vertices) {
		if (bounds.empty) {
			bounds.min = vertex.position;
			bounds.max = vertex.position;
			bounds.empty = false;
			continue;
		}
		bounds.min.x = std::min(bounds.min.x, vertex.position.x);
		bounds.min.y = std::min(bounds.min.y, vertex.position.y);
		bounds.min.z = std::min(bounds.min.z, vertex.position.z);
		bounds.max.x = std::max(bounds.max.x, vertex.position.x);
		bounds.max.y = std::max(bounds.max.y, vertex.position.y);
		bounds.max.z = std::max(bounds.max.z, vertex.position.z);
	}
	return bounds;
}

quader_editor_core::PickHitRecord face_hit(int object_id, quader_poly::ElementId face_id, float ray_distance)
{
	quader_editor_core::PickHitRecord hit;
	hit.hit = true;
	hit.object_id = object_id;
	hit.kind = quader_poly::ElementKind::face;
	hit.face_id = face_id;
	hit.ray_distance = ray_distance;
	return hit;
}

quader_editor_core::PickHitRecord edge_hit(int object_id, quader_poly::Edge edge, float ray_distance)
{
	quader_editor_core::PickHitRecord hit;
	hit.hit = true;
	hit.object_id = object_id;
	hit.kind = quader_poly::ElementKind::edge;
	hit.edge = edge;
	hit.ray_distance = ray_distance;
	return hit;
}

void hover_tracks_face_candidates_and_selected_feedback()
{
	std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };

	const quader_editor_core::SelectionHoverUpdateResult hover_result =
		quader_editor_core::update_selection_hover(
			objects,
			quader_editor_core::SelectionMode::face,
			{},
			front_center_ray(),
			true);

	require_test(hover_result.changed, "face hover should report a changed target for a fresh hit");
	require_test(hover_result.hover.object_id == 7 && hover_result.hover.candidate_object_id == 7,
		"face hover should update both the active object and candidate object");
	require_test(hover_result.hover.kind == quader_editor_core::SelectionHoverKind::face &&
			hover_result.hover.face_id != quader_poly::invalid_element_id,
		"face hover should keep the picked face id");
	require_test(hover_result.hover.selected_modifier_pressed,
		"face hover should remember the selected-target modifier when hovering a component");

	objects[0].selected = true;
	objects[0].selection.mode = quader_poly::SelectionMode::face;
	objects[0].selection.faces.push_back(hover_result.hover.face_id);
	objects[0].selection.has_active = true;
	objects[0].selection.active_kind = quader_poly::ElementKind::face;
	objects[0].selection.active_face = hover_result.hover.face_id;

	require_test(
		quader_editor_core::selection_hover_target_matches_selection(
			objects,
			quader_editor_core::SelectionMode::face,
			hover_result.hover),
		"hover feedback should recognize when the hovered face is already selected");
	require_test(
		quader_editor_core::selection_hover_target_is_selected(
			objects,
			quader_editor_core::SelectionMode::face,
			hover_result.hover),
		"selected-target hover should require the modifier flag");

	const quader_editor_core::SelectionOverlayFrame frame =
		quader_editor_core::build_selection_overlay_frame(
			objects,
			quader_editor_core::SelectionMode::face,
			hover_result.hover);
	require_test(frame.hover_target_selected, "selection overlay frame should receive selected-hover feedback");
}

void component_hover_preserves_candidate_on_miss()
{
	const std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };
	quader_editor_core::SelectionHoverState current_hover;
	current_hover.candidate_object_id = 7;

	const quader_editor_core::SelectionHoverUpdateResult edge_miss =
		quader_editor_core::update_selection_hover(
			objects,
			quader_editor_core::SelectionMode::edge,
			current_hover,
			miss_ray(),
			false);
	require_test(!edge_miss.changed && edge_miss.hover.candidate_object_id == 7,
		"component hover misses should keep the previous candidate object");
	require_test(edge_miss.hover.object_id == -1 && edge_miss.hover.kind == quader_editor_core::SelectionHoverKind::none,
		"component hover misses should clear the immediate hover target");

	const quader_editor_core::SelectionHoverUpdateResult object_miss =
		quader_editor_core::update_selection_hover(
			objects,
			quader_editor_core::SelectionMode::object,
			current_hover,
			miss_ray(),
			false);
	require_test(object_miss.changed && object_miss.hover.candidate_object_id == -1,
		"object hover misses should clear component candidate state");
}

void hover_can_use_precomputed_object_bounds()
{
	const std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };
	const std::vector<quader_editor_core::PickObjectBounds> bounds { pick_bounds_for_object(objects[0]) };

	const quader_editor_core::SelectionHoverUpdateResult uncached =
		quader_editor_core::update_selection_hover(
			objects,
			quader_editor_core::SelectionMode::face,
			{},
			front_center_ray(),
			false);
	const quader_editor_core::SelectionHoverUpdateResult cached =
		quader_editor_core::update_selection_hover(
			objects,
			quader_editor_core::SelectionMode::face,
			{},
			front_center_ray(),
			false,
			{},
			bounds);

	require_test(cached.changed == uncached.changed &&
			cached.hover.object_id == uncached.hover.object_id &&
			cached.hover.kind == uncached.hover.kind &&
			cached.hover.face_id == uncached.hover.face_id,
		"precomputed object bounds should preserve hover picking results");

	std::vector<quader_editor_core::PickObjectBounds> miss_bounds = bounds;
	miss_bounds[0].min = { 40.0F, 40.0F, 40.0F };
	miss_bounds[0].max = { 42.0F, 42.0F, 42.0F };
	const quader_editor_core::SelectionHoverUpdateResult missed =
		quader_editor_core::update_selection_hover(
			objects,
			quader_editor_core::SelectionMode::face,
			{},
			front_center_ray(),
			false,
			{},
			miss_bounds);
	require_test(!missed.changed && missed.hover.kind == quader_editor_core::SelectionHoverKind::none,
		"precomputed object bounds should be used as the hover broadphase filter");
}

void hover_can_use_precomputed_face_geometry()
{
	const std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };
	const std::vector<quader_editor_core::PickObjectBounds> bounds { pick_bounds_for_object(objects[0]) };
	std::vector<quader_editor_core::PickObjectFaceGeometry> geometry =
		quader_editor_core::build_pick_face_geometry(objects);
	require_test(!geometry.empty() && geometry[0].object_id == 7 && !geometry[0].triangles.empty(),
		"face pick geometry cache should build triangles for the object");

	const quader_editor_core::SelectionHoverUpdateResult uncached =
		quader_editor_core::update_selection_hover(
			objects,
			quader_editor_core::SelectionMode::face,
			{},
			front_center_ray(),
			false);
	const quader_editor_core::SelectionHoverUpdateResult cached =
		quader_editor_core::update_selection_hover(
			objects,
			quader_editor_core::SelectionMode::face,
			{},
			front_center_ray(),
			false,
			{},
			bounds,
			geometry);

	require_test(cached.changed == uncached.changed &&
			cached.hover.object_id == uncached.hover.object_id &&
			cached.hover.kind == uncached.hover.kind &&
			cached.hover.face_id == uncached.hover.face_id,
		"precomputed face geometry should preserve hover picking results");

	geometry[0].triangles.clear();
	const quader_editor_core::SelectionHoverUpdateResult missed =
		quader_editor_core::update_selection_hover(
			objects,
			quader_editor_core::SelectionMode::face,
			{},
			front_center_ray(),
			false,
			{},
			bounds,
			geometry);
	require_test(!missed.changed && missed.hover.kind == quader_editor_core::SelectionHoverKind::none,
		"cached face geometry should be used instead of retriangulating the document on hover");
}

void open_mesh_hover_picks_visible_back_sides()
{
	const std::vector<quader_editor_core::PolygonObject> objects { open_top_cube_object(7) };
	const std::vector<quader_editor_core::PickObjectBounds> bounds { pick_bounds_for_object(objects[0]) };
	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::face;
	options.pick_backfaces = true;
	const quader_poly::Ray inside_to_side_ray { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };

	const std::optional<quader_editor_core::PickHitRecord> uncached_hit =
		quader_editor_core::pick_nearest(objects, { inside_to_side_ray, options });
	require_test(uncached_hit.has_value() && uncached_hit->kind == quader_poly::ElementKind::face,
		"uncached face picking should hit a visible back side in an open mesh");

	std::vector<quader_editor_core::PickObjectFaceGeometry> geometry =
		quader_editor_core::build_pick_face_geometry(objects, options);
	const std::optional<quader_editor_core::PickHitRecord> cached_hit =
		quader_editor_core::pick_nearest(objects, { inside_to_side_ray, options }, bounds, geometry);
	require_test(cached_hit.has_value() && cached_hit->face_id == uncached_hit->face_id,
		"cached face picking should also hit the visible back side");

	const quader_editor_core::SelectionHoverUpdateResult hover =
		quader_editor_core::update_selection_hover(
			objects,
			quader_editor_core::SelectionMode::face,
			{},
			inside_to_side_ray,
			false,
			options,
			bounds,
			geometry);
	require_test(hover.changed &&
			hover.hover.object_id == 7 &&
			hover.hover.kind == quader_editor_core::SelectionHoverKind::face &&
			hover.hover.face_id == cached_hit->face_id,
		"face hover should produce hover wires for a visible back side in an open mesh");
}

void session_pick_selects_faces_through_shared_picking()
{
	std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };
	quader_editor_core::SelectionMode mode = quader_editor_core::SelectionMode::object;
	int active_object_id = -1;

	const bool selected =
		quader_editor_core::pick_select_face(
			objects,
			mode,
			quader_editor_core::SelectionEditOperation::replace,
			front_center_ray(),
			active_object_id);

	require_test(selected && mode == quader_editor_core::SelectionMode::face,
		"pick-select-face should switch to face selection mode");
	require_test(active_object_id == 7 && objects[0].selected && !objects[0].selection.faces.empty(),
		"pick-select-face should select a face on the hit object and make it active");

	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::face;
	const std::optional<quader_editor_core::PickHitRecord> hit =
		quader_editor_core::pick_nearest(objects, { front_center_ray(), options });
	require_test(hit.has_value() && hit->object_id == 7 && hit->kind == quader_poly::ElementKind::face,
		"pick_nearest should expose the same face hit through editor-core records");
}

void hidden_material_slot_filter_rebuilds_cached_face_geometry()
{
	const std::vector<quader_editor_core::PolygonObject> objects { stacked_face_object() };
	const std::vector<quader_editor_core::PickObjectBounds> bounds { pick_bounds_for_object(objects[0]) };
	quader_poly::PickOptions face_options;
	face_options.mode = quader_poly::SelectionMode::face;
	std::vector<quader_editor_core::PickObjectFaceGeometry> unfiltered_geometry =
		quader_editor_core::build_pick_face_geometry(objects, face_options);
	std::vector<quader_editor_core::PickObjectFaceGeometry> filtered_geometry =
		quader_editor_core::build_pick_face_geometry(objects, hidden_slot_pick_options(quader_poly::SelectionMode::face));
	require_test(unfiltered_geometry[0].triangles.size() > filtered_geometry[0].triangles.size(),
		"hidden-slot face geometry should exclude hidden triangles");

	const std::optional<quader_editor_core::PickHitRecord> unfiltered_hit =
		quader_editor_core::pick_nearest(objects, { front_center_ray(), face_options }, bounds, unfiltered_geometry);
	const std::optional<quader_editor_core::PickHitRecord> filtered_hit =
		quader_editor_core::pick_nearest(
			objects,
			{ front_center_ray(), hidden_slot_pick_options(quader_poly::SelectionMode::face) },
			bounds,
			filtered_geometry);
	require_test(unfiltered_hit.has_value() && filtered_hit.has_value(),
		"cached picking should hit with and without hidden-slot filtering");
	require_test(unfiltered_hit->face_id != filtered_hit->face_id,
		"filtered cached picking should not reuse the unfiltered hidden face");
}

void hidden_material_slot_filter_limits_all_face_selection()
{
	std::vector<quader_editor_core::PolygonObject> objects { adjacent_face_object() };
	quader_editor_core::SelectionMode mode = quader_editor_core::SelectionMode::face;
	int active_object_id = -1;
	const bool selected =
		quader_editor_core::pick_select_all_faces(
			objects,
			mode,
			quader_editor_core::SelectionEditOperation::replace,
			{ { -0.5F, 0.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } },
			active_object_id,
			hidden_slot_pick_options(quader_poly::SelectionMode::face));
	require_test(selected && objects[0].selection.faces.size() == 1U,
		"all-face selection should select only pickable visible faces");
	const quader_poly::Face* selected_face = quader_poly::find_face(objects[0].document, objects[0].selection.faces.front());
	require_test(selected_face != nullptr && selected_face->material_slot == 0U,
		"all-face selection should not include hidden material-slot faces");
}

void hidden_material_slot_filter_limits_all_vertex_selection()
{
	std::vector<quader_editor_core::PolygonObject> objects { adjacent_face_object() };
	quader_editor_core::SelectionMode mode = quader_editor_core::SelectionMode::vertex;
	int active_object_id = -1;
	const bool selected =
		quader_editor_core::pick_select_all_vertices(
			objects,
			mode,
			quader_editor_core::SelectionEditOperation::replace,
			{ { 0.0F, 1.0F, 5.0F }, { 0.0F, 0.0F, -1.0F } },
			active_object_id,
			hidden_slot_pick_options(quader_poly::SelectionMode::vertex));
	require_test(selected && objects[0].selection.vertices.size() == 4U,
		"all-vertex selection should select only vertices incident to visible faces");

	const quader_poly::ElementId hidden_only_vertex = objects[0].document.faces.back().vertices[1];
	require_test(!quader_poly::selection_contains(objects[0].selection, hidden_only_vertex),
		"all-vertex selection should not include hidden-only vertices");
}

void editor_session_pick_select_reports_only_material_selection_changes()
{
	quader_editor_core::EditorSession session;
	require_test(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed,
		"pick-select revision fixture should create a box");
	session.set_selection_mode(quader_editor_core::SelectionMode::face);

	require_test(
		session.pick_select_face(
			quader_editor_core::SelectionEditOperation::replace,
			front_center_ray()),
		"first face pick should report a changed selection");
	const std::uint64_t selected_revision = session.state().selection_revision;

	require_test(
		!session.pick_select_face(
			quader_editor_core::SelectionEditOperation::add,
			front_center_ray()),
		"adding an already selected face should not report a selection change");
	require_test(session.state().selection_revision == selected_revision,
		"adding an already selected face should not bump selection revision");

	require_test(
		session.pick_select_face(
			quader_editor_core::SelectionEditOperation::remove,
			front_center_ray()),
		"removing the selected face should report a changed selection");
	require_test(session.state().selection_revision > selected_revision,
		"removing the selected face should bump selection revision");
}

void default_component_picking_has_editor_leeway()
{
	const quader_poly::Document document = quader_poly::make_cube({});

	quader_poly::PickOptions edge_options;
	edge_options.mode = quader_poly::SelectionMode::edge;
	const quader_poly::PickResult near_edge = quader_poly::pick_element(
		document,
		{ { 0.0F, 1.09F, 5.0F }, { 0.0F, 0.0F, -1.0F } },
		edge_options);
	require_test(near_edge.hit && near_edge.kind == quader_poly::ElementKind::edge,
		"default edge picking should allow a small near-edge screen miss");

	quader_poly::PickOptions vertex_options;
	vertex_options.mode = quader_poly::SelectionMode::vertex;
	const quader_poly::PickResult near_vertex = quader_poly::pick_element(
		document,
		{ { 1.078F, 1.078F, 5.0F }, { 0.0F, 0.0F, -1.0F } },
		vertex_options);
	require_test(near_vertex.hit && near_vertex.kind == quader_poly::ElementKind::vertex,
		"default vertex picking should allow a small near-vertex screen miss");
}

void selection_pick_options_normalize_host_radius_inputs()
{
	const quader_poly::PickOptions defaulted =
		quader_editor_core::selection_pick_options_from_radii(
			std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity());
	require_test(nearly_equal(defaulted.vertex_radius, 0.12F),
		"selection pick options should default non-finite vertex radii");
	require_test(nearly_equal(defaulted.edge_radius, 0.10F),
		"selection pick options should default non-finite edge radii");

	const quader_poly::PickOptions clamped =
		quader_editor_core::selection_pick_options_from_radii(-4.0, 50.0);
	require_test(nearly_equal(clamped.vertex_radius, 0.001F),
		"selection pick options should clamp vertex radius to the editor minimum");
	require_test(nearly_equal(clamped.edge_radius, 2.0F),
		"selection pick options should clamp edge radius to the editor maximum");

	const quader_poly::PickOptions preserved =
		quader_editor_core::selection_pick_options_from_radii(0.25, 0.75);
	require_test(nearly_equal(preserved.vertex_radius, 0.25F),
		"selection pick options should preserve finite in-range vertex radii");
	require_test(nearly_equal(preserved.edge_radius, 0.75F),
		"selection pick options should preserve finite in-range edge radii");
}

void component_pick_distance_uses_geometry_ray_distance()
{
	std::vector<quader_editor_core::PolygonObject> objects { cube_object(7) };
	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::vertex;
	options.vertex_radius = 0.12F;

	const quader_poly::Ray ray {
		{ 1.05F, 1.0F, 5.0F },
		{ 0.0F, 0.0F, -2.0F },
	};
	const std::optional<quader_editor_core::PickHitRecord> hit =
		quader_editor_core::pick_nearest(objects, { ray, options });

	require_test(hit.has_value() && hit->kind == quader_poly::ElementKind::vertex,
		"vertex picking should hit through non-unit rays");
	require_test(std::abs(hit->pick_distance - 0.05F) <= 0.0001F,
		"component pick distance should be perpendicular distance to the geometry ray");
	require_test(std::abs(hit->ray_distance - 4.0F) <= 0.0001F,
		"component ray distance should preserve document picking's normalized ray distance");
}

void click_and_drag_pick_plans_stay_in_editor_core()
{
	const std::vector<quader_editor_core::PickHitRecord> surface_hits {
		face_hit(1, 11, 1.0F),
		face_hit(2, 12, 2.0F),
	};
	quader_editor_core::PickSelectionState object_selection;
	object_selection.mode = quader_editor_core::SelectionMode::object;
	quader_editor_core::ClickPickRequest click_request;
	click_request.screen_position = { 12.0F, 24.0F };
	quader_editor_core::PickCycleState cycle;

	quader_editor_core::ClickPickPlan click_plan =
		quader_editor_core::plan_click_pick(cycle, object_selection, {}, surface_hits, click_request);
	require_test(click_plan.has_target && click_plan.hit_set == quader_editor_core::PickHitSet::surface &&
			click_plan.hit_index == 0 && click_plan.used_pick_cycle,
		"replace click planning should choose the front surface hit through the pick cycle");

	click_plan = quader_editor_core::plan_click_pick(cycle, object_selection, {}, surface_hits, click_request);
	require_test(click_plan.has_target && click_plan.hit_index == 1 && click_plan.cycle.cycled,
		"repeat replace clicks at the same screen position should cycle to the next object hit");

	const quader_poly::Edge selected_edge = quader_poly::make_edge(3, 4);
	const std::vector<quader_editor_core::PickHitRecord> handle_hits {
		edge_hit(2, selected_edge, 0.5F),
	};
	click_request.handles_enabled = true;
	click_plan = quader_editor_core::plan_click_pick(cycle, object_selection, handle_hits, surface_hits, click_request);
	require_test(click_plan.has_target && click_plan.hit_set == quader_editor_core::PickHitSet::handle &&
			click_plan.hit_index == 0,
		"click planning should prefer enabled handle hits before surface hits");

	quader_editor_core::PickSelectionState edge_selection;
	edge_selection.mode = quader_editor_core::SelectionMode::edge;
	edge_selection.selected_edges.push_back({ 2, quader_poly::ElementKind::edge, quader_poly::invalid_element_id, selected_edge });
	quader_editor_core::DragSeedPickRequest drag_request;
	drag_request.handles_enabled = true;
	drag_request.allow_selected_target = true;
	drag_request.additive = true;
	drag_request.preserve_selected_additive_target = true;
	const quader_editor_core::DragSeedPickPlan drag_plan =
		quader_editor_core::plan_drag_seed_pick(edge_selection, handle_hits, surface_hits, drag_request);
	require_test(drag_plan.has_target && drag_plan.hit_set == quader_editor_core::PickHitSet::handle &&
			drag_plan.keep_existing_selection && drag_plan.preferred_object_id == 2,
		"drag seed planning should preserve selected additive handle targets when requested");
}

} // namespace

TEST_CASE("qdr_editor_core_picking_tests", "[editor-core-picking]")
{
	hover_tracks_face_candidates_and_selected_feedback();
	component_hover_preserves_candidate_on_miss();
	hover_can_use_precomputed_object_bounds();
	hover_can_use_precomputed_face_geometry();
	open_mesh_hover_picks_visible_back_sides();
	session_pick_selects_faces_through_shared_picking();
	hidden_material_slot_filter_rebuilds_cached_face_geometry();
	hidden_material_slot_filter_limits_all_face_selection();
	hidden_material_slot_filter_limits_all_vertex_selection();
	editor_session_pick_select_reports_only_material_selection_changes();
	default_component_picking_has_editor_leeway();
	selection_pick_options_normalize_host_radius_inputs();
	component_pick_distance_uses_geometry_ray_distance();
	click_and_drag_pick_plans_stay_in_editor_core();
}
