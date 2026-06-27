////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <string_view>
#include <vector>

namespace quader_editor_core {
namespace {

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

bool selection_has_valid_face(const quader_poly::Document& document, const quader_poly::Selection& selection)
{
	for (quader_poly::ElementId face_id : selection.faces) {
		if (quader_poly::find_face(document, face_id) != nullptr) {
			return true;
		}
	}
	return false;
}

} // namespace

CommandResult flip_selected_face_normals(std::vector<PolygonObject>& objects, SelectionMode mode)
{
	bool changed = false;
	std::string last_message = "Select a polygon face or object first.";
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		quader_poly::Selection selection = mode == SelectionMode::object ? all_face_selection(object.document) : object.selection;
		selection.mode = quader_poly::SelectionMode::face;
		const quader_poly::OperationResult result = quader_poly::flip_selected_face_normals(object.document, selection);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}
		changed = true;
	}
	return changed ? ok_result(true) : error_result(last_message);
}

CommandResult recalculate_selected_face_normals(std::vector<PolygonObject>& objects, SelectionMode mode, bool outside)
{
	if (mode != SelectionMode::face && mode != SelectionMode::object) {
		return error_result("Recalculate normals works in face or object mode.");
	}

	bool changed = false;
	std::string last_message = outside ? "Select faces or meshes before recalculating outside." : "Select faces or meshes before recalculating inside.";
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		if (mode == SelectionMode::face && object.selection.faces.empty()) {
			continue;
		}

		quader_poly::Selection selection = all_face_selection(object.document);
		selection.mode = quader_poly::SelectionMode::face;
		const quader_poly::OperationResult result = quader_poly::recalculate_selected_face_normals(object.document, selection, outside);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}
		changed = true;
	}
	return changed ? ok_result(true) : error_result(last_message);
}

CommandResult shade_selected_faces_smooth(std::vector<PolygonObject>& objects, SelectionMode mode)
{
	if (mode != SelectionMode::face && mode != SelectionMode::object) {
		return error_result("Shade Smooth works in face or object mode.");
	}

	bool changed = false;
	std::string last_message = "Select faces or meshes before shading smooth.";
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		if (mode == SelectionMode::face && object.selection.faces.empty()) {
			continue;
		}

		quader_poly::Selection selection = mode == SelectionMode::object ? all_face_selection(object.document) : object.selection;
		selection.mode = quader_poly::SelectionMode::face;
		const quader_poly::OperationResult result = quader_poly::shade_selected_faces_smooth(object.document, selection);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}
		changed = true;
	}
	return changed ? ok_result(true) : error_result(last_message);
}

CommandResult shade_selected_faces_flat(std::vector<PolygonObject>& objects, SelectionMode mode)
{
	if (mode != SelectionMode::face && mode != SelectionMode::object) {
		return error_result("Shade Flat works in face or object mode.");
	}

	bool changed = false;
	std::string last_message = "Select faces or meshes before shading flat.";
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		if (mode == SelectionMode::face && object.selection.faces.empty()) {
			continue;
		}

		quader_poly::Selection selection = mode == SelectionMode::object ? all_face_selection(object.document) : object.selection;
		selection.mode = quader_poly::SelectionMode::face;
		const quader_poly::OperationResult result = quader_poly::shade_selected_faces_flat(object.document, selection);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}
		changed = true;
	}
	return changed ? ok_result(true) : error_result(last_message);
}

CommandResult harden_selected_edge_normals(std::vector<PolygonObject>& objects, SelectionMode mode)
{
	if (mode != SelectionMode::edge) {
		return error_result("Hard Normal works in edge mode.");
	}

	bool changed = false;
	std::string last_message = "Select edges before setting hard normals.";
	for (PolygonObject& object : objects) {
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}

		quader_poly::Selection selection = object.selection;
		selection.mode = quader_poly::SelectionMode::edge;
		const quader_poly::OperationResult result = quader_poly::harden_selected_edge_normals(object.document, selection);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}
		changed = true;
	}
	return changed ? ok_result(true) : error_result(last_message);
}

CommandResult soften_selected_edge_normals(std::vector<PolygonObject>& objects, SelectionMode mode)
{
	if (mode != SelectionMode::edge) {
		return error_result("Soft Normal works in edge mode.");
	}

	bool changed = false;
	std::string last_message = "Select edges before setting soft normals.";
	for (PolygonObject& object : objects) {
		if (!object.selected || object.selection.edges.empty()) {
			continue;
		}

		quader_poly::Selection selection = object.selection;
		selection.mode = quader_poly::SelectionMode::edge;
		const quader_poly::OperationResult result = quader_poly::soften_selected_edge_normals(object.document, selection);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}
		changed = true;
	}
	return changed ? ok_result(true) : error_result(last_message);
}

CommandResult apply_uv_operation(std::vector<PolygonObject>& objects, SelectionMode mode, std::string_view operation, float shift_world_amount)
{
	bool changed = false;
	std::string last_message = "Select a polygon face or object first.";
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		quader_poly::Selection selection = mode == SelectionMode::object ? all_face_selection(object.document) : object.selection;
		const quader_poly::OperationResult result = quader_poly::apply_uv_operation(object.document, selection, operation, shift_world_amount);
		if (!result.changed) {
			if (!result.message.empty()) {
				last_message = result.message;
			}
			continue;
		}
		changed = true;
	}
	return changed ? ok_result(true) : error_result(last_message);
}

CommandResult assign_material_to_selection(std::vector<PolygonObject>& objects, SelectionMode mode, std::uint32_t material_slot)
{
	if (mode != SelectionMode::face && mode != SelectionMode::object) {
		return error_result("Apply Material works in face or object mode.");
	}

	bool saw_target = false;
	bool changed = false;
	for (PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}

		quader_poly::Selection selection = mode == SelectionMode::object ? all_face_selection(object.document) : object.selection;
		selection.mode = quader_poly::SelectionMode::face;
		if (selection_has_valid_face(object.document, selection)) {
			saw_target = true;
			const quader_poly::OperationResult result =
				quader_poly::assign_selected_face_material_slot(object.document, selection, material_slot);
			if (result.changed) {
				changed = true;
			}
		}
	}

	if (!saw_target) {
		return error_result(mode == SelectionMode::object ? "Select one or more meshes before applying material." : "Select one or more faces before applying material.");
	}
	return ok_result(changed);
}

CommandResult assign_material_to_face(std::vector<PolygonObject>& objects, int object_id, quader_poly::ElementId face_id, std::uint32_t material_slot)
{
	const int object_index = object_index_by_id(objects, object_id);
	if (object_index < 0) {
		return error_result("Material target mesh was not found.");
	}

	quader_poly::Document& document = objects[static_cast<std::size_t>(object_index)].document;
	const quader_poly::Face *face = quader_poly::find_face(document, face_id);
	if (face == nullptr) {
		return error_result("Material target face was not found.");
	}

	if (face->material_slot == material_slot) {
		return ok_result(false);
	}

	const quader_poly::OperationResult result = quader_poly::assign_face_material_slot(document, face_id, material_slot);
	return result.changed ? ok_result(true) : error_result(result.message);
}

} // namespace quader_editor_core
