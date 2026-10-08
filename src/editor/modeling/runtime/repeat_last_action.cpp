////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/runtime/repeat_last_action.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace quader_editor_core {

namespace {

bool face_contains_directed_edge(
		const quader_poly::Face& face,
		quader_poly::ElementId from_id,
		quader_poly::ElementId to_id)
{
  if (face.vertices.size() < 2U || from_id == quader_poly::kInvalidElementId ||
      to_id == quader_poly::kInvalidElementId || from_id == to_id) {
    return false;
  }
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
		if (face.vertices[index] == from_id &&
				face.vertices[(index + 1U) % face.vertices.size()] == to_id) {
			return true;
		}
	}
	return false;
}

std::optional<std::pair<quader_poly::ElementId, quader_poly::ElementId>> oriented_edge_in_face(
		const quader_poly::Face& face,
		quader_poly::Edge edge)
{
	edge = quader_poly::make_edge(edge.a, edge.b);
	if (face_contains_directed_edge(face, edge.a, edge.b)) {
		return std::pair<quader_poly::ElementId, quader_poly::ElementId> { edge.a, edge.b };
	}
	if (face_contains_directed_edge(face, edge.b, edge.a)) {
		return std::pair<quader_poly::ElementId, quader_poly::ElementId> { edge.b, edge.a };
	}
	return std::nullopt;
}

bool edge_exists(const quader_poly::Document& document, quader_poly::Edge edge)
{
	edge = quader_poly::make_edge(edge.a, edge.b);
	const std::vector<quader_poly::Edge> edges = quader_poly::document_edges(document);
	return std::find(edges.begin(), edges.end(), edge) != edges.end();
}

std::optional<quader::QVec3> selected_edge_forward_direction(
		std::span<const PolygonObject> objects)
{
	quader::QVec3 direction;
	for (const PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		for (quader_poly::Edge edge : object.selection.edges) {
			edge = quader_poly::make_edge(edge.a, edge.b);
			if (!edge_exists(object.document, edge)) {
				continue;
			}
			for (const quader_poly::Face& face : object.document.faces) {
				const std::optional<std::pair<quader_poly::ElementId, quader_poly::ElementId>> oriented =
					oriented_edge_in_face(face, edge);
				if (!oriented.has_value()) {
					continue;
				}
				const quader_poly::Vertex* start = quader_poly::find_vertex(object.document, oriented->first);
				const quader_poly::Vertex* end = quader_poly::find_vertex(object.document, oriented->second);
				if (start == nullptr || end == nullptr) {
					continue;
				}
				const quader::QVec3 edge_direction = quader::normalize_or_zero(end->position - start->position);
				const quader::QVec3 normal = quader::normalize_or_zero(quader_poly::face_normal(object.document, face));
				const quader::QVec3 outward = quader::normalize_or_zero(quader::cross(edge_direction, normal));
				if (quader::length_squared(outward) > 0.000001F) {
					direction += outward;
					break;
				}
			}
		}
	}
	if (quader::length_squared(direction) <= 0.000001F) {
		return std::nullopt;
	}
	return quader::normalize_or_zero(direction);
}

std::optional<quader::QVec3> selected_face_forward_direction(
		std::span<const PolygonObject> objects)
{
	quader::QVec3 direction;
	for (const PolygonObject& object : objects) {
		if (!object.selected) {
			continue;
		}
		for (quader_poly::ElementId face_id : object.selection.faces) {
			const quader_poly::Face* face = quader_poly::find_face(object.document, face_id);
			if (face == nullptr) {
				continue;
			}
			const quader::QVec3 normal = quader::normalize_or_zero(quader_poly::face_normal(object.document, *face));
			if (quader::length_squared(normal) > 0.000001F) {
				direction += normal;
			}
		}
	}
	if (quader::length_squared(direction) <= 0.000001F) {
		return std::nullopt;
	}
	return quader::normalize_or_zero(direction);
}

std::optional<quader::QVec3> selected_component_forward_direction(
		std::span<const PolygonObject> objects,
		SelectionMode selection_mode)
{
  if (selection_mode == SelectionMode::Edge) {
    return selected_edge_forward_direction(objects);
  }
  if (selection_mode == SelectionMode::Face) {
    return selected_face_forward_direction(objects);
  }
        return std::nullopt;
}

} // namespace

bool operation_is_repeatable(EditorOperationId operation_id)
{
  return operation_id != EditorOperationId::DeleteSelection;
}

void record_repeatable_operation(
		RepeatLastActionState& state,
		const EditorOperationCommand& command,
		const CommandResult& result)
{
	if (!operation_is_repeatable(command.operation_id)) {
		return;
	}

	EditorOperationCommand repeat_command = command;
        repeat_command.phase = EditorOperationCommandPhase::Execute;

        switch (command.phase) {
        case EditorOperationCommandPhase::Execute:
          if (result.ok && result.changed) {
            state.last_action_kind = RepeatableActionKind::Operation;
            state.last_operation = repeat_command;
          }
          return;
        case EditorOperationCommandPhase::BeginPreview:
        case EditorOperationCommandPhase::UpdatePreview:
          if (result.ok) {
            state.pending_preview_operation = repeat_command;
          }
          return;
        case EditorOperationCommandPhase::CommitPreview:
          if (result.ok && result.changed &&
              state.pending_preview_operation.has_value() &&
              state.pending_preview_operation->operation_id ==
                  command.operation_id) {
            state.last_action_kind = RepeatableActionKind::Operation;
            state.last_operation = state.pending_preview_operation;
          }
          state.pending_preview_operation.reset();
          return;
        case EditorOperationCommandPhase::CancelPreview:
          if (state.pending_preview_operation.has_value() &&
              state.pending_preview_operation->operation_id ==
                  command.operation_id) {
            state.pending_preview_operation.reset();
          }
          return;
	}
}

void record_repeatable_transform(
		RepeatLastActionState& state,
		const EditorSession& session,
		const TransformDragState& drag,
		bool duplicate_selected)
{
	record_repeatable_transform(state, session.objects(),
			session.selection_mode(), drag, duplicate_selected);
}

void record_repeatable_transform(
		RepeatLastActionState& state,
		std::span<const PolygonObject> selection_objects,
		SelectionMode selection_mode,
		const TransformDragState& drag,
		bool duplicate_selected)
{
	if (!drag.active) {
		return;
	}

	RepeatTransformAction action;
	action.tool = drag.tool;
	action.operation = drag.operation;
	action.axis = drag.axis;
	action.space = drag.space;
	action.orientation = drag.orientation;
	action.duplicate_selected = duplicate_selected;
        action.move_auto_merge_vertices = drag.move_auto_merge_vertices;
        action.extrusion_grid_size = drag.extrusion_grid_size;
	action.pivot = drag.pivot;
	action.move_delta = drag.move.preview_delta;
	action.rotate_angle_degrees = drag.rotate.preview_angle_degrees;
	action.trackball_rotation = drag.rotate.trackball_rotation;
	action.scale = drag.scale.preview_scale;

        if (drag.tool == ViewportInteractionTool::Move &&
            drag.operation == TransformDragOperation::Extrude &&
            quader::length_squared(drag.move.preview_delta) > 0.000001F) {
          if (const std::optional<quader::QVec3> forward =
                  selected_component_forward_direction(selection_objects,
                                                       selection_mode)) {
            const quader::QVec3 normalized_delta =
                quader::normalize_or_zero(drag.move.preview_delta);
            const float alignment = quader::dot(normalized_delta, *forward);
            if (std::abs(alignment) >= 0.85F) {
              action.direction_mode =
                  RepeatTransformDirectionMode::SelectionForward;
              action.signed_forward_distance =
                  quader::length(drag.move.preview_delta) *
                  (alignment < 0.0F ? -1.0F : 1.0F);
            }
          }
        }

        state.last_action_kind = RepeatableActionKind::Transform;
        state.pending_preview_operation.reset();
	state.last_transform = action;
}

} // namespace quader_editor_core
