#include <quader_editor_core/repeat_last_action.h>

#include <quader_editor_core/tool/rotate_tool.h>
#include <quader_editor_core/tool/scale_tool.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace quader_editor_core {

namespace {

bool face_contains_directed_edge(
		const quader_poly::Face& face,
		quader_poly::ElementId from_id,
		quader_poly::ElementId to_id)
{
	if (face.vertices.size() < 2U ||
			from_id == quader_poly::invalid_element_id ||
			to_id == quader_poly::invalid_element_id ||
			from_id == to_id) {
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

std::optional<quader_poly::Vec3> selected_edge_forward_direction(const EditorSession& session)
{
	quader_poly::Vec3 direction;
	for (const PolygonObject& object : session.objects()) {
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
				const quader_poly::Vec3 edge_direction = quader_poly::normalize_or_zero(end->position - start->position);
				const quader_poly::Vec3 normal = quader_poly::normalize_or_zero(quader_poly::face_normal(object.document, face));
				const quader_poly::Vec3 outward = quader_poly::normalize_or_zero(quader_poly::cross(edge_direction, normal));
				if (quader_poly::length_squared(outward) > 0.000001F) {
					direction += outward;
					break;
				}
			}
		}
	}
	if (quader_poly::length_squared(direction) <= 0.000001F) {
		return std::nullopt;
	}
	return quader_poly::normalize_or_zero(direction);
}

std::optional<quader_poly::Vec3> selected_face_forward_direction(const EditorSession& session)
{
	quader_poly::Vec3 direction;
	for (const PolygonObject& object : session.objects()) {
		if (!object.selected) {
			continue;
		}
		for (quader_poly::ElementId face_id : object.selection.faces) {
			const quader_poly::Face* face = quader_poly::find_face(object.document, face_id);
			if (face == nullptr) {
				continue;
			}
			const quader_poly::Vec3 normal = quader_poly::normalize_or_zero(quader_poly::face_normal(object.document, *face));
			if (quader_poly::length_squared(normal) > 0.000001F) {
				direction += normal;
			}
		}
	}
	if (quader_poly::length_squared(direction) <= 0.000001F) {
		return std::nullopt;
	}
	return quader_poly::normalize_or_zero(direction);
}

std::optional<quader_poly::Vec3> selected_component_forward_direction(const EditorSession& session)
{
	if (session.selection_mode() == SelectionMode::edge) {
		return selected_edge_forward_direction(session);
	}
	if (session.selection_mode() == SelectionMode::face) {
		return selected_face_forward_direction(session);
	}
	return std::nullopt;
}

CommandResult commit_repeat_transform_preview(EditorSession& session, const CommandResult& update_result)
{
	if (!update_result.ok) {
		(void)session.cancel_preview_transaction(PreviewTransactionKind::transform);
		return update_result;
	}
	const PreviewTransactionStatus status = session.preview_transaction_status();
	const bool changed =
		status.active &&
		status.kind == PreviewTransactionKind::transform &&
		status.changed;
	(void)session.commit_preview_transaction(PreviewTransactionKind::transform);
	return { true, changed, {} };
}

} // namespace

bool operation_is_repeatable(EditorOperationId operation_id)
{
	return operation_id != EditorOperationId::delete_selection;
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
	repeat_command.phase = EditorOperationCommandPhase::execute;

	switch (command.phase) {
	case EditorOperationCommandPhase::execute:
		if (result.ok && result.changed) {
			state.last_action_kind = RepeatableActionKind::operation;
			state.last_operation = repeat_command;
		}
		return;
	case EditorOperationCommandPhase::begin_preview:
	case EditorOperationCommandPhase::update_preview:
		if (result.ok) {
			state.pending_preview_operation = repeat_command;
		}
		return;
	case EditorOperationCommandPhase::commit_preview:
		if (result.ok && result.changed &&
				state.pending_preview_operation.has_value() &&
				state.pending_preview_operation->operation_id == command.operation_id) {
			state.last_action_kind = RepeatableActionKind::operation;
			state.last_operation = state.pending_preview_operation;
		}
		state.pending_preview_operation.reset();
		return;
	case EditorOperationCommandPhase::cancel_preview:
		if (state.pending_preview_operation.has_value() &&
				state.pending_preview_operation->operation_id == command.operation_id) {
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
	if (!drag.active) {
		return;
	}

	RepeatTransformAction action;
	action.tool = drag.tool;
	action.operation = drag.operation;
	action.axis = drag.axis;
	action.duplicate_selected = duplicate_selected;
	action.move_delta = drag.move.preview_delta;
	action.rotate_angle_degrees = drag.rotate.preview_angle_degrees;
	action.scale = drag.scale.preview_scale;

	if (drag.tool == ViewportInteractionTool::move &&
			drag.operation == TransformDragOperation::extrude &&
			quader_poly::length_squared(drag.move.preview_delta) > 0.000001F) {
		if (const std::optional<quader_poly::Vec3> forward = selected_component_forward_direction(session)) {
			const quader_poly::Vec3 normalized_delta = quader_poly::normalize_or_zero(drag.move.preview_delta);
			const float alignment = quader_poly::dot(normalized_delta, *forward);
			if (std::abs(alignment) >= 0.85F) {
				action.direction_mode = RepeatTransformDirectionMode::selection_forward;
				action.signed_forward_distance =
					quader_poly::length(drag.move.preview_delta) * (alignment < 0.0F ? -1.0F : 1.0F);
			}
		}
	}

	state.last_action_kind = RepeatableActionKind::transform;
	state.pending_preview_operation.reset();
	state.last_transform = action;
}

CommandResult execute_repeatable_transform(
		EditorSession& session,
		const RepeatTransformAction& action)
{
	if (!session.has_selection()) {
		return { false, false, "Select something before repeating the last transform." };
	}

	CommandResult begin_result = session.begin_transform_preview(action.duplicate_selected);
	if (!begin_result.ok) {
		return begin_result;
	}

	CommandResult update_result;
	if (action.tool == ViewportInteractionTool::move) {
		quader_poly::Vec3 delta = action.move_delta;
		if (action.operation == TransformDragOperation::extrude) {
			if (action.direction_mode == RepeatTransformDirectionMode::selection_forward) {
				const std::optional<quader_poly::Vec3> forward = selected_component_forward_direction(session);
				if (!forward.has_value()) {
					(void)session.cancel_preview_transaction(PreviewTransactionKind::transform);
					return { false, false, "Could not resolve the current selection forward direction." };
				}
				delta = *forward * action.signed_forward_distance;
			}
			update_result = session.update_transform_extrude_preview(delta);
		} else {
			update_result = session.update_transform_translate_preview(delta, false);
		}
		return commit_repeat_transform_preview(session, update_result);
	}

	const quader_poly::Vec3 pivot = session.selection_center().value_or(quader_poly::Vec3 {});
	if (action.tool == ViewportInteractionTool::rotate) {
		RotateToolState rotate;
		rotate.active = true;
		rotate.axis = action.axis;
		rotate.object_ids = session.selected_object_ids();
		rotate.pivot = pivot;
		const RotatePreviewResult preview =
			preview_rotate_from_angle_degrees(rotate, action.rotate_angle_degrees, 0.0);
		if (!preview.ok) {
			(void)session.cancel_preview_transaction(PreviewTransactionKind::transform);
			return { false, false, preview.message.empty() ? "Rotate repeat failed." : preview.message };
		}
		update_result = session.update_transform_matrix_preview(preview.transform, pivot, false, false);
		return commit_repeat_transform_preview(session, update_result);
	}

	if (action.tool == ViewportInteractionTool::scale) {
		ScaleToolState scale;
		scale.active = true;
		scale.axis = action.axis;
		scale.object_ids = session.selected_object_ids();
		scale.pivot = pivot;
		const ScalePreviewResult preview = preview_scale_from_scale(scale, action.scale);
		if (!preview.ok) {
			(void)session.cancel_preview_transaction(PreviewTransactionKind::transform);
			return { false, false, preview.message.empty() ? "Scale repeat failed." : preview.message };
		}
		if (action.operation == TransformDragOperation::inset) {
			update_result = session.update_transform_inset_preview(preview.transform, pivot);
		} else {
			update_result = session.update_transform_matrix_preview(preview.transform, pivot, false, false);
		}
		return commit_repeat_transform_preview(session, update_result);
	}

	(void)session.cancel_preview_transaction(PreviewTransactionKind::transform);
	return { false, false, "Unsupported repeat transform tool." };
}

CommandResult execute_repeat_last_action(EditorSession& session, RepeatLastActionState& state)
{
	if (state.last_action_kind == RepeatableActionKind::transform) {
		if (!state.last_transform.has_value()) {
			return { false, false, "No repeatable transform is available." };
		}
		return execute_repeatable_transform(session, *state.last_transform);
	}

	if (state.last_action_kind == RepeatableActionKind::operation &&
			state.last_operation.has_value()) {
		EditorOperationCommand command = *state.last_operation;
		command.phase = EditorOperationCommandPhase::execute;
		const EditorOperationCommandResult result = execute_command(session, command);
		return result.command;
	}

	return { false, false, "No repeatable action is available." };
}

} // namespace quader_editor_core
