////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <editor/interactions/interaction_api.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace quader_editor_core {
namespace {

/**
 * Stores the Object Selection Snapshot data contract used by the modeling runtime domain.
 */
struct ObjectSelectionSnapshot {
	int id = -1;
	bool selected = false;
	quader_poly::Selection selection;
};

/**
 * Stores the Selection Snapshot data contract used by the modeling runtime domain.
 */
struct SelectionSnapshot {
	SelectionMode mode = SelectionMode::object;
	int active_object_id = -1;
	std::vector<ObjectSelectionSnapshot> objects;
};

SelectionSnapshot capture_selection_snapshot(
		const std::vector<PolygonObject>& objects,
		SelectionMode mode,
		int active_object_id)
{
	SelectionSnapshot snapshot;
	snapshot.mode = mode;
	snapshot.active_object_id = active_object_id;
	snapshot.objects.reserve(objects.size());
	for (const PolygonObject& object : objects) {
		snapshot.objects.push_back({ object.id, object.selected, object.selection });
	}
	return snapshot;
}

bool selection_snapshot_matches(const SelectionSnapshot& left, const SelectionSnapshot& right)
{
	if (left.mode != right.mode ||
			left.active_object_id != right.active_object_id ||
			left.objects.size() != right.objects.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.objects.size(); ++index) {
		const ObjectSelectionSnapshot& left_object = left.objects[index];
		const ObjectSelectionSnapshot& right_object = right.objects[index];
		if (left_object.id != right_object.id ||
				left_object.selected != right_object.selected ||
				!polygon_selection_matches(left_object.selection, right_object.selection)) {
			return false;
		}
	}
	return true;
}

bool selection_changed_since(
		const SelectionSnapshot& before,
		const std::vector<PolygonObject>& objects,
		SelectionMode mode,
		int active_object_id)
{
	return !selection_snapshot_matches(before, capture_selection_snapshot(objects, mode, active_object_id));
}

} // namespace

void EditorSession::set_selection_mode(SelectionMode mode)
{
	if (state_.selection_mode == mode) {
		return;
	}
	state_.selection_mode = mode;
	quader_editor_core::set_selection_mode(state_.objects, state_.selection_mode, state_.active_object_id);
	note_selection_changed();
}

void EditorSession::clear_selection()
{
	const bool mesh_selection = quader_editor_core::has_selection(state_.objects, state_.selection_mode, state_.active_object_id) ||
		state_.active_object_id > 0;
	const bool light_selection = !state_.lights.empty() &&
		(!quader_editor_core::selected_light_ids(state_.lights).empty() || state_.active_light_id > 0);
	if (!mesh_selection && !light_selection) {
		return;
	}
	quader_editor_core::clear_selection(state_.objects, state_.active_object_id);
	clear_light_selection_state(state_.lights, state_.active_light_id);
	note_selection_changed();
}

void EditorSession::set_selected_object_ids(const std::vector<int>& object_ids, int active_id)
{
	state_.selection_mode = SelectionMode::object;
	for (PolygonObject& object : state_.objects) {
		object.selection = quader_poly::Selection();
		object.selected = std::find(object_ids.begin(), object_ids.end(), object.id) != object_ids.end();
	}
	state_.active_object_id = active_id > 0 ? active_id : (object_ids.empty() ? -1 : object_ids.back());
	normalize_active_object();
	clear_light_selection_state(state_.lights, state_.active_light_id);
	note_selection_changed();
}

void EditorSession::set_component_selection(int object_id, quader_poly::Selection selection, SelectionMode mode)
{
	state_.selection_mode = mode == SelectionMode::object ? SelectionMode::vertex : mode;
	for (PolygonObject& object : state_.objects) {
		if (object.id != object_id) {
			object.selected = false;
			object.selection = quader_poly::Selection();
			continue;
		}
		object.selection = std::move(selection);
		object.selection.mode = component_selection_mode(state_.selection_mode);
		object.selected = !object.selection.empty();
		state_.active_object_id = object.selected ? object.id : -1;
	}
	normalize_active_object();
	clear_light_selection_state(state_.lights, state_.active_light_id);
	note_selection_changed();
}

bool EditorSession::has_selection() const
{
	return quader_editor_core::has_selection(state_.objects, state_.selection_mode, state_.active_object_id) ||
		!quader_editor_core::selected_light_ids(state_.lights).empty();
}

std::optional<quader::QVec3> EditorSession::selection_center() const
{
	const std::optional<quader::QVec3> mesh_center =
		quader_editor_core::selection_center(state_.objects, state_.selection_mode, state_.active_object_id);
	const std::optional<quader::QVec3> light_center = selected_light_center(state_.lights);
	if (mesh_center.has_value() && light_center.has_value()) {
		return (mesh_center.value() + light_center.value()) * 0.5F;
	}
	return mesh_center.has_value() ? mesh_center : light_center;
}

Bounds3 EditorSession::document_bounds() const
{
	Bounds3 bounds = quader_editor_core::document_bounds(state_.objects);
	const Bounds3 lights = light_bounds(state_.lights);
	if (lights.empty) {
		return bounds;
	}
	if (bounds.empty) {
		return lights;
	}
	bounds.min.x = std::min(bounds.min.x, lights.min.x);
	bounds.min.y = std::min(bounds.min.y, lights.min.y);
	bounds.min.z = std::min(bounds.min.z, lights.min.z);
	bounds.max.x = std::max(bounds.max.x, lights.max.x);
	bounds.max.y = std::max(bounds.max.y, lights.max.y);
	bounds.max.z = std::max(bounds.max.z, lights.max.z);
	return bounds;
}

Bounds3 EditorSession::selection_bounds() const
{
	Bounds3 bounds = quader_editor_core::selection_bounds(state_.objects, state_.selection_mode);
	const Bounds3 light_bounds = selected_light_bounds(state_.lights);
	if (light_bounds.empty) {
		return bounds;
	}
	if (bounds.empty) {
		return light_bounds;
	}
	bounds.min.x = std::min(bounds.min.x, light_bounds.min.x);
	bounds.min.y = std::min(bounds.min.y, light_bounds.min.y);
	bounds.min.z = std::min(bounds.min.z, light_bounds.min.z);
	bounds.max.x = std::max(bounds.max.x, light_bounds.max.x);
	bounds.max.y = std::max(bounds.max.y, light_bounds.max.y);
	bounds.max.z = std::max(bounds.max.z, light_bounds.max.z);
	return bounds;
}

ActiveSelection EditorSession::active_selection() const
{
	return quader_editor_core::active_selection(state_.objects, state_.selection_mode, state_.active_object_id);
}

std::vector<int> EditorSession::selected_object_ids() const
{
	return quader_editor_core::selected_object_ids(state_.objects);
}

std::optional<PickHitRecord> EditorSession::pick_nearest(const PickRequest& request) const
{
	return quader_editor_core::pick_nearest(state_.objects, request);
}

std::vector<PickHitRecord> EditorSession::pick_all(const PickRequest& request) const
{
	return quader_editor_core::pick_all(state_.objects, request);
}

std::string EditorSession::pick_hits_signature(const PickRequest& request) const
{
	return quader_editor_core::pick_hits_signature(pick_all(request));
}

bool EditorSession::pick_select(
		SelectionEditOperation operation,
		const quader_poly::Ray& ray,
		quader_poly::PickOptions pick_options,
		bool allow_component_fallback)
{
	const SelectionSnapshot before = capture_selection_snapshot(state_.objects, state_.selection_mode, state_.active_object_id);
	quader_editor_core::pick_select(
			state_.objects,
			state_.selection_mode,
			operation,
			ray,
			state_.active_object_id,
			pick_options,
			allow_component_fallback);
	const bool changed = selection_changed_since(before, state_.objects, state_.selection_mode, state_.active_object_id);
	if (changed) {
		clear_light_selection_state(state_.lights, state_.active_light_id);
		note_selection_changed();
	}
	return changed;
}

bool EditorSession::pick_select_face(
		SelectionEditOperation operation,
		const quader_poly::Ray& ray,
		quader_poly::PickOptions pick_options)
{
	const SelectionSnapshot before = capture_selection_snapshot(state_.objects, state_.selection_mode, state_.active_object_id);
	quader_editor_core::pick_select_face(state_.objects, state_.selection_mode, operation, ray, state_.active_object_id, pick_options);
	const bool changed = selection_changed_since(before, state_.objects, state_.selection_mode, state_.active_object_id);
	if (changed) {
		clear_light_selection_state(state_.lights, state_.active_light_id);
		note_selection_changed();
	}
	return changed;
}

bool EditorSession::pick_select_all_faces(
		SelectionEditOperation operation,
		const quader_poly::Ray& ray,
		quader_poly::PickOptions pick_options)
{
	const SelectionSnapshot before = capture_selection_snapshot(state_.objects, state_.selection_mode, state_.active_object_id);
	quader_editor_core::pick_select_all_faces(state_.objects, state_.selection_mode, operation, ray, state_.active_object_id, pick_options);
	const bool changed = selection_changed_since(before, state_.objects, state_.selection_mode, state_.active_object_id);
	if (changed) {
		clear_light_selection_state(state_.lights, state_.active_light_id);
		note_selection_changed();
	}
	return changed;
}

bool EditorSession::pick_select_edge_loop(SelectionEditOperation operation, const quader_poly::Ray& ray, quader_poly::PickOptions pick_options)
{
	const SelectionSnapshot before = capture_selection_snapshot(state_.objects, state_.selection_mode, state_.active_object_id);
	quader_editor_core::pick_select_edge_loop(state_.objects, state_.selection_mode, operation, ray, state_.active_object_id, pick_options);
	const bool changed = selection_changed_since(before, state_.objects, state_.selection_mode, state_.active_object_id);
	if (changed) {
		clear_light_selection_state(state_.lights, state_.active_light_id);
		note_selection_changed();
	}
	return changed;
}

bool EditorSession::pick_select_all_vertices(SelectionEditOperation operation, const quader_poly::Ray& ray, quader_poly::PickOptions pick_options)
{
	const SelectionSnapshot before = capture_selection_snapshot(state_.objects, state_.selection_mode, state_.active_object_id);
	quader_editor_core::pick_select_all_vertices(state_.objects, state_.selection_mode, operation, ray, state_.active_object_id, pick_options);
	const bool changed = selection_changed_since(before, state_.objects, state_.selection_mode, state_.active_object_id);
	if (changed) {
		clear_light_selection_state(state_.lights, state_.active_light_id);
		note_selection_changed();
	}
	return changed;
}

CommandResult EditorSession::select_all()
{
	const CommandResult result = quader_editor_core::select_all(state_.objects, state_.selection_mode, state_.active_object_id);
	bool changed = result.changed;
	if (state_.selection_mode == SelectionMode::object) {
		for (LightEntity& light : state_.lights) {
			if (!light.selected) {
				changed = true;
			}
			light.selected = true;
			state_.active_light_id = light.id;
		}
		normalize_active_object();
	}
	if (changed) {
		note_selection_changed();
	}
	return result.ok ? ok_result(changed) : result;
}

CommandResult EditorSession::invert_selection()
{
	const CommandResult result = quader_editor_core::invert_selection(state_.objects, state_.selection_mode, state_.active_object_id);
	bool changed = result.changed;
	if (state_.selection_mode == SelectionMode::object) {
		for (LightEntity& light : state_.lights) {
			light.selected = !light.selected;
			changed = true;
		}
		normalize_active_object();
	}
	if (changed) {
		note_selection_changed();
	}
	return result.ok ? ok_result(changed) : result;
}

CommandResult EditorSession::translate_selected(quader::QVec3 delta, bool texture_lock_enabled)
{
	const CommandResult mesh_result = quader_editor_core::translate_selected(state_.objects, state_.selection_mode, delta, texture_lock_enabled);
	if (!mesh_result.ok) {
		return mesh_result;
	}
	const CommandResult light_result = translate_selected_lights(state_.lights, delta);
	if (!light_result.ok) {
		return light_result;
	}
	const CommandResult result = ok_result(mesh_result.changed || light_result.changed);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::transform_selected(const quader_poly::Transform3& transform, quader::QVec3 pivot, bool texture_lock_enabled, bool texture_lock_scale_enabled)
{
	const CommandResult mesh_result = quader_editor_core::transform_selected(state_.objects, state_.selection_mode, transform, pivot, texture_lock_enabled, texture_lock_scale_enabled);
	if (!mesh_result.ok) {
		return mesh_result;
	}
	const CommandResult light_result = transform_selected_lights(state_.lights, transform, pivot);
	if (!light_result.ok) {
		return light_result;
	}
	const CommandResult result = ok_result(mesh_result.changed || light_result.changed);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::snap_selected_vertices_to_active()
{
	const CommandResult result = quader_editor_core::snap_selected_vertices_to_active(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::snap_selected_edges_to_active()
{
	const CommandResult result = quader_editor_core::snap_selected_edges_to_active(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::merge_selected_vertices_to_active()
{
	const CommandResult result = quader_editor_core::merge_selected_vertices_to_active(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::merge_selected_vertices_to_center()
{
	const CommandResult result = quader_editor_core::merge_selected_vertices_to_center(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::merge_selected_vertices_by_distance(double tolerance)
{
	const CommandResult result =
		quader_editor_core::merge_selected_vertices_by_distance(state_.objects, state_.selection_mode, state_.active_object_id, tolerance);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::merge_selected_vertices_by_distance(MergeByDistanceOperationSettings settings)
{
	settings = normalize_merge_by_distance_operation_settings(settings);
	return merge_selected_vertices_by_distance(settings.tolerance);
}

CommandResult EditorSession::bevel_selected_vertices(float distance)
{
	const CommandResult result = quader_editor_core::bevel_selected_vertices(state_.objects, state_.selection_mode, state_.active_object_id, distance);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::bevel_selected_vertices(BevelVerticesOperationSettings settings)
{
	return bevel_selected_vertices(bevel_vertices_distance_from_settings(settings));
}

CommandResult EditorSession::connect_selected_vertices()
{
	const CommandResult result = quader_editor_core::connect_selected_vertices(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::dissolve_selected_vertices()
{
	const CommandResult result = quader_editor_core::dissolve_selected_vertices(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::connect_selected_edges()
{
	const CommandResult result = quader_editor_core::connect_selected_edges(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::split_selected_edges()
{
	const CommandResult result = quader_editor_core::split_selected_edges(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::dissolve_selected_edges()
{
	const CommandResult result = quader_editor_core::dissolve_selected_edges(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::fill_hole_from_selected_edges()
{
	const CommandResult result = quader_editor_core::fill_hole_from_selected_edges(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		state_.selection_mode = SelectionMode::face;
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::bevel_selected_edges(const quader_poly::EdgeBevelSettings& settings)
{
	const CommandResult result = quader_editor_core::bevel_selected_edges(state_.objects, state_.selection_mode, state_.active_object_id, settings);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::bevel_selected_edges(EdgeBevelOperationSettings settings)
{
	return bevel_selected_edges(to_edge_bevel_settings(settings));
}

CommandResult EditorSession::collapse_selected_edges()
{
	const CommandResult result = quader_editor_core::collapse_selected_edges(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::merge_selected_edges()
{
	const CommandResult result = quader_editor_core::merge_selected_edges(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::delete_selected()
{
	const bool mesh_selected = quader_editor_core::has_selection(state_.objects, state_.selection_mode, state_.active_object_id);
	const bool light_selected = !quader_editor_core::selected_light_ids(state_.lights).empty();
	if (!mesh_selected && !light_selected) {
		return error_result("Select a polygon object, element, or light first.");
	}

	bool changed = false;
	if (mesh_selected) {
		const CommandResult result = quader_editor_core::delete_selected(state_.objects, state_.selection_mode, state_.active_object_id);
		if (!result.ok) {
			return result;
		}
		changed = changed || result.changed;
	}

	if (light_selected) {
		const std::size_t old_size = state_.lights.size();
		state_.lights.erase(
			std::remove_if(state_.lights.begin(), state_.lights.end(), [](const LightEntity& light) {
				return light.selected;
			}),
			state_.lights.end());
		changed = changed || old_size != state_.lights.size();
		normalize_active_object();
	}

	if (changed) {
		note_content_result(ok_result(true));
		note_selection_changed();
	}
	return ok_result(changed);
}

CommandResult EditorSession::create_box_from_bounds(quader::QVec3 min_bounds, quader::QVec3 max_bounds)
{
	state_.next_object_id = next_polygon_object_id(state_.objects, state_.next_object_id);
	const int object_id = state_.next_object_id++;
	const CommandResult result = quader_editor_core::create_box_from_bounds(state_.objects, object_id, min_bounds, max_bounds, state_.active_object_id, state_.selection_mode);
	if (!result.ok) {
		state_.next_object_id = std::max(1, object_id);
		return result;
	}
	note_content_result(result);
	note_selection_changed();
	return result;
}

CommandResult EditorSession::create_box_from_corners(std::span<const quader::QVec3> corners)
{
	state_.next_object_id = next_polygon_object_id(state_.objects, state_.next_object_id);
	const int object_id = state_.next_object_id++;
	const CommandResult result = quader_editor_core::create_box_from_corners(
		state_.objects,
		object_id,
		corners,
		state_.active_object_id,
		state_.selection_mode);
	if (!result.ok) {
		state_.next_object_id = std::max(1, object_id);
		return result;
	}
	note_content_result(result);
	note_selection_changed();
	return result;
}

CommandResult EditorSession::create_polygon_from_points(std::span<const quader::QVec3> points, quader::QVec3 face_normal)
{
	state_.next_object_id = next_polygon_object_id(state_.objects, state_.next_object_id);
	const int object_id = state_.next_object_id++;
	const CommandResult result = quader_editor_core::create_polygon_from_points(
		state_.objects,
		object_id,
		points,
		face_normal,
		state_.active_object_id,
		state_.selection_mode);
	if (!result.ok) {
		state_.next_object_id = std::max(1, object_id);
		return result;
	}
	note_content_result(result);
	note_selection_changed();
	return result;
}

CommandResult EditorSession::create_object_from_document(
		quader_poly::Document document,
		std::string name,
		std::uint32_t material_id)
{
	(void)name;
	if (document.vertices.empty() || document.faces.empty()) {
		return error_result("Document mesh must contain vertices and faces.");
	}

	for (PolygonObject& object : state_.objects) {
		object.selected = false;
		object.selection = quader_poly::Selection();
	}
	clear_light_selection_state(state_.lights, state_.active_light_id);

	state_.next_object_id = next_polygon_object_id(state_.objects, state_.next_object_id);
	PolygonObject object;
	object.id = state_.next_object_id++;
	object.material_id = static_cast<int>(material_id);
	object.selected = true;
	object.document = std::move(document);
	state_.active_object_id = object.id;
	state_.selection_mode = SelectionMode::object;
	state_.objects.push_back(std::move(object));

	const CommandResult result = ok_result(true);
	note_content_result(result);
	note_selection_changed();
	return result;
}

CommandResult EditorSession::extract_selected_faces()
{
	state_.next_object_id = next_polygon_object_id(state_.objects, state_.next_object_id);
	const int object_id = state_.next_object_id++;
	const CommandResult result = quader_editor_core::extract_selected_faces(state_.objects, object_id, state_.selection_mode, state_.active_object_id);
	if (!result.ok) {
		state_.next_object_id = std::max(1, object_id);
		return result;
	}
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::detach_selected_faces()
{
	const CommandResult result = quader_editor_core::detach_selected_faces(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::thicken_selected_faces(float thickness, bool from_center)
{
	const CommandResult result = quader_editor_core::thicken_selected_faces(
		state_.objects,
		state_.selection_mode,
		state_.next_object_id,
		state_.active_object_id,
		thickness,
		from_center);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::thicken_selected_faces(ThickenOperationSettings settings)
{
	settings = normalize_thicken_operation_settings(settings);
	return thicken_selected_faces(static_cast<float>(settings.thickness), settings.from_center);
}

CommandResult EditorSession::slice_selected_quads(int x_slices, int y_slices)
{
	const CommandResult result = quader_editor_core::slice_selected_quads(state_.objects, state_.selection_mode, state_.active_object_id, x_slices, y_slices);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::slice_selected_quads(SliceQuadOperationSettings settings)
{
	settings = normalize_slice_quad_operation_settings(settings);
	return slice_selected_quads(settings.x_slices, settings.y_slices);
}

CommandResult EditorSession::combine_selected_faces()
{
	const CommandResult result = quader_editor_core::combine_selected_faces(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::combine_selected_meshes()
{
	const SelectionMode mode_before = state_.selection_mode;
	const CommandResult result =
		quader_editor_core::combine_selected_meshes(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed || state_.selection_mode != mode_before) {
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::collapse_selected_faces()
{
	const CommandResult result = quader_editor_core::collapse_selected_faces(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::radial_align_selected()
{
	const CommandResult result = quader_editor_core::radial_align_selected(state_.objects, state_.selection_mode, state_.active_object_id);
	note_content_result(result);
	if (result.changed) {
		align_component_selection_to_mode(state_.objects, state_.selection_mode, state_.active_object_id);
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::extrude_selected_elements(quader::QVec3 offset, float closed_edge_ledge_size)
{
	const CommandResult result = quader_editor_core::extrude_selected_elements(state_.objects, state_.selection_mode, offset, closed_edge_ledge_size);
	note_content_result(result);
	if (result.changed) {
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::inset_selected_elements(const quader_poly::Transform3& transform, quader::QVec3 pivot)
{
	const CommandResult result = quader_editor_core::inset_selected_elements(state_.objects, state_.selection_mode, transform, pivot);
	note_content_result(result);
	if (result.changed) {
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::extrude_selected_faces(float distance)
{
	const CommandResult result = quader_editor_core::extrude_selected_faces(state_.objects, distance);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::inset_selected_faces(float amount)
{
	const CommandResult result = quader_editor_core::inset_selected_faces(state_.objects, amount);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::commit_cut_tool(CutToolState& state)
{
	if (state.points.size() < 2) {
		reset_cut(state);
		return error_result("Cut needs at least two points.");
	}
	if (state.target_object_ids.empty()) {
		state.preview_valid = false;
		state.preview_counts = {};
		return error_result("Select meshes or elements before committing the cut.");
	}

	const quader_poly::PlaneCutRequest request = plane_cut_request_from_cut_state(state);
	const std::vector<int> target_object_ids = state.target_object_ids;
	const CommandResult result = apply_history_command([&target_object_ids, &request](EditorSession& editor_session) {
		return editor_session.plane_cut(target_object_ids, request);
	});
	if (!result.ok || !result.changed) {
		state.preview_valid = false;
		state.preview_counts = {};
		return result.ok ? error_result("Could not apply the cut.") : result;
	}

	(void)commit_reset_cut(state, selected_object_ids());
	return result;
}

CommandResult EditorSession::commit_mirror_tool(MirrorToolState& state)
{
	if (state.plane.points.size() < 2U) {
		reset_mirror(state);
		return error_result("Mirror needs at least two points.");
	}
	if (state.target_object_ids.empty()) {
		state.preview_valid = false;
		state.preview_counts = {};
		return error_result("Select one or more mesh objects before committing the mirror.");
	}

	const std::vector<quader::QVec3> plane_points = mirror_plane_points(state);
	const std::vector<int> target_object_ids = state.target_object_ids;
	const CommandResult result = apply_history_command([&target_object_ids, &plane_points](EditorSession& editor_session) {
		return editor_session.mirror_objects_across_plane(target_object_ids, plane_points);
	});
	if (!result.ok || !result.changed) {
		state.preview_valid = false;
		state.preview_counts = {};
		return result.ok ? error_result("Could not apply the mirror.") : result;
	}

	(void)commit_reset_mirror(state, selected_object_ids());
	return result;
}

CommandResult EditorSession::commit_poly_tool(PolyToolState& state)
{
	if (state.points.size() < 3U) {
		rebuild_poly_overlay(state);
		return error_result("Poly needs at least three vertices.");
	}
	if (!poly_can_commit(state)) {
		rebuild_poly_overlay(state);
		return error_result("Poly needs a non-degenerate polygon.");
	}

	const std::vector<quader::QVec3> points = state.points;
	const quader::QVec3 face_normal = state.face_normal;
	const CommandResult result = apply_history_command([&points, face_normal](EditorSession& editor_session) {
		return editor_session.create_polygon_from_points(points, face_normal);
	});
	if (!result.ok || !result.changed) {
		rebuild_poly_overlay(state);
		return result.ok ? error_result("Could not create the poly.") : result;
	}

	reset_poly(state);
	return result;
}

CommandResult EditorSession::plane_cut(std::span<const int> object_ids, const quader_poly::PlaneCutRequest& request)
{
	const SelectionMode mode_before = state_.selection_mode;
	const CommandResult result = quader_editor_core::plane_cut(
		state_.objects,
		object_ids,
		request,
		state_.next_object_id,
		state_.active_object_id,
		state_.selection_mode);
	note_content_result(result);
	if (result.changed || state_.selection_mode != mode_before) {
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::mirror_objects_across_plane(std::span<const int> object_ids, std::span<const quader::QVec3> plane_points)
{
	const SelectionMode mode_before = state_.selection_mode;
	const CommandResult result = quader_editor_core::mirror_objects_across_plane(
		state_.objects,
		object_ids,
		plane_points,
		state_.next_object_id,
		state_.active_object_id,
		state_.selection_mode);
	note_content_result(result);
	if (result.changed || state_.selection_mode != mode_before) {
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::mirror_selected_objects_across_axis(TransformAxis axis)
{
	const std::vector<int> object_ids = selected_object_ids();
	const SelectionMode mode_before = state_.selection_mode;
	const CommandResult result = quader_editor_core::mirror_objects_across_world_axis(
		state_.objects,
		object_ids,
		axis,
		state_.next_object_id,
		state_.active_object_id,
		state_.selection_mode);
	note_content_result(result);
	if (result.changed || state_.selection_mode != mode_before) {
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::flip_selected_objects_by_orientation(ViewportFlipOrientation orientation)
{
	if (state_.selection_mode != SelectionMode::object) {
		return error_result("Flip works only in object/mesh selection mode.");
	}
	const std::vector<int> object_ids = selected_object_ids();
	if (object_ids.empty()) {
		return error_result("Select one or more mesh objects to flip.");
	}

	const SelectionMode mode_before = state_.selection_mode;
	const int active_before = state_.active_object_id;
	const CommandResult result = quader_editor_core::flip_objects_by_orientation(
		state_.objects,
		object_ids,
		state_.selection_mode,
		orientation,
		state_.active_object_id);
	note_content_result(result);
	if (result.changed && (state_.selection_mode != mode_before || state_.active_object_id != active_before)) {
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::flip_selected_face_normals()
{
	const CommandResult result = quader_editor_core::flip_selected_face_normals(state_.objects, state_.selection_mode);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::recalculate_selected_face_normals(bool outside)
{
	const CommandResult result = quader_editor_core::recalculate_selected_face_normals(state_.objects, state_.selection_mode, outside);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::shade_selected_faces_smooth()
{
	const CommandResult result = quader_editor_core::shade_selected_faces_smooth(state_.objects, state_.selection_mode);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::shade_selected_faces_flat()
{
	const CommandResult result = quader_editor_core::shade_selected_faces_flat(state_.objects, state_.selection_mode);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::harden_selected_edge_normals()
{
	const CommandResult result = quader_editor_core::harden_selected_edge_normals(state_.objects, state_.selection_mode);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::soften_selected_edge_normals()
{
	const CommandResult result = quader_editor_core::soften_selected_edge_normals(state_.objects, state_.selection_mode);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::apply_uv_operation(std::string_view operation, float shift_world_amount)
{
	const CommandResult result = quader_editor_core::apply_uv_operation(state_.objects, state_.selection_mode, operation, shift_world_amount);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::assign_material(AssignMaterialOperationSettings settings)
{
	return settings.explicit_target ?
		assign_material_to_face(settings.object_id, settings.face_id, settings.material_slot) :
		assign_material_to_selection(settings.material_slot);
}

CommandResult EditorSession::assign_material_to_selection(std::uint32_t material_slot)
{
	const CommandResult result = quader_editor_core::assign_material_to_selection(state_.objects, state_.selection_mode, material_slot);
	note_content_result(result);
	return result;
}

CommandResult EditorSession::assign_material_to_face(int object_id, quader_poly::ElementId face_id, std::uint32_t material_slot)
{
	const CommandResult result = quader_editor_core::assign_material_to_face(state_.objects, object_id, face_id, material_slot);
	note_content_result(result);
	return result;
}

std::vector<PolygonObject> EditorSession::selected_object_copies() const
{
	return quader_editor_core::selected_object_copies(state_.objects, state_.selection_mode);
}

CommandResult EditorSession::duplicate_selected_objects(quader::QVec3 offset)
{
	const CommandResult result = quader_editor_core::duplicate_selected_objects(state_.objects, state_.next_object_id, state_.selection_mode, state_.active_object_id, offset);
	note_content_result(result);
	if (result.changed) {
		note_selection_changed();
	}
	return result;
}

CommandResult EditorSession::duplicate_selected_objects(DuplicateMeshesOperationSettings settings)
{
	return duplicate_selected_objects(duplicate_meshes_offset_from_settings(settings));
}

CommandResult EditorSession::paste_objects(const std::vector<PolygonObject>& copied_objects, std::optional<quader::QVec3> target_center)
{
	const CommandResult result = quader_editor_core::paste_objects(state_.objects, state_.next_object_id, state_.selection_mode, state_.active_object_id, copied_objects, target_center);
	note_content_result(result);
	if (result.changed) {
		note_selection_changed();
	}
	return result;
}

} // namespace quader_editor_core
