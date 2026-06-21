#include "session_internal.h"

#include <quader_diagnostics/profile.h>

#include <cmath>

namespace quader_editor_core {

PreviewTransactionStatus EditorSession::preview_transaction_status() const
{
	return {
		preview_transaction_.active ? preview_transaction_.kind : PreviewTransactionKind::none,
		preview_transaction_.active,
		preview_transaction_.changed,
	};
}

void EditorSession::clear_preview_transaction()
{
	preview_transaction_ = {};
}

bool EditorSession::discard_preview_transactions(bool restore_transform_preview)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.discard_preview_transactions");
	bool restored_transform_preview = false;
	if (restore_transform_preview) {
		restored_transform_preview =
			cancel_preview_transaction(PreviewTransactionKind::transform).before.active;
	}
	const bool restored_preview = cancel_preview_transaction().before.active;
	return restored_transform_preview || restored_preview;
}

CommandResult EditorSession::begin_edge_bevel_preview(const quader_poly::EdgeBevelSettings& settings)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.begin_edge_bevel_preview");
	cancel_preview_transaction();
	preview_transaction_.kind = PreviewTransactionKind::edge_bevel;
	preview_transaction_.active = true;
	preview_transaction_.changed = false;
	preview_transaction_.base_state = state_;
	preview_transaction_.history_state = preview_transaction_.base_state;
	const CommandResult result = update_edge_bevel_preview_from_base(settings);
	if (!result.ok) {
		clear_preview_transaction();
	}
	return result;
}

CommandResult EditorSession::begin_edge_bevel_preview(EdgeBevelOperationSettings settings)
{
	return begin_edge_bevel_preview(to_edge_bevel_settings(settings));
}

CommandResult EditorSession::update_edge_bevel_preview(const quader_poly::EdgeBevelSettings& settings)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_edge_bevel_preview");
	if (!preview_transaction_.active || preview_transaction_.kind != PreviewTransactionKind::edge_bevel) {
		return error_result("No edge bevel preview is active.");
	}
	return update_edge_bevel_preview_from_base(settings);
}

CommandResult EditorSession::update_edge_bevel_preview(EdgeBevelOperationSettings settings)
{
	return update_edge_bevel_preview(to_edge_bevel_settings(settings));
}

CommandResult EditorSession::begin_thicken_preview(float thickness, bool from_center)
{
	cancel_preview_transaction();
	preview_transaction_.kind = PreviewTransactionKind::thicken;
	preview_transaction_.active = true;
	preview_transaction_.changed = false;
	preview_transaction_.base_state = state_;
	preview_transaction_.history_state = preview_transaction_.base_state;
	const CommandResult result = update_thicken_preview_from_base(thickness, from_center);
	if (!result.ok) {
		clear_preview_transaction();
	}
	return result;
}

CommandResult EditorSession::begin_thicken_preview(ThickenOperationSettings settings)
{
	settings = normalize_thicken_operation_settings(settings);
	return begin_thicken_preview(static_cast<float>(settings.thickness), settings.from_center);
}

CommandResult EditorSession::update_thicken_preview(float thickness, bool from_center)
{
	if (!preview_transaction_.active || preview_transaction_.kind != PreviewTransactionKind::thicken) {
		return error_result("No thicken preview is active.");
	}
	return update_thicken_preview_from_base(thickness, from_center);
}

CommandResult EditorSession::update_thicken_preview(ThickenOperationSettings settings)
{
	settings = normalize_thicken_operation_settings(settings);
	return update_thicken_preview(static_cast<float>(settings.thickness), settings.from_center);
}

CommandResult EditorSession::begin_slice_quad_preview(int x_slices, int y_slices)
{
	cancel_preview_transaction();
	preview_transaction_.kind = PreviewTransactionKind::slice_quad;
	preview_transaction_.active = true;
	preview_transaction_.changed = false;
	preview_transaction_.base_state = state_;
	preview_transaction_.history_state = preview_transaction_.base_state;
	const CommandResult result = update_slice_quad_preview_from_base(x_slices, y_slices);
	if (!result.ok) {
		clear_preview_transaction();
	}
	return result;
}

CommandResult EditorSession::begin_slice_quad_preview(SliceQuadOperationSettings settings)
{
	settings = normalize_slice_quad_operation_settings(settings);
	return begin_slice_quad_preview(settings.x_slices, settings.y_slices);
}

CommandResult EditorSession::update_slice_quad_preview(int x_slices, int y_slices)
{
	if (!preview_transaction_.active || preview_transaction_.kind != PreviewTransactionKind::slice_quad) {
		return error_result("No slice quad preview is active.");
	}
	return update_slice_quad_preview_from_base(x_slices, y_slices);
}

CommandResult EditorSession::update_slice_quad_preview(SliceQuadOperationSettings settings)
{
	settings = normalize_slice_quad_operation_settings(settings);
	return update_slice_quad_preview(settings.x_slices, settings.y_slices);
}

CommandResult EditorSession::begin_merge_by_distance_preview(double tolerance)
{
	tolerance = normalize_merge_by_distance_operation_settings({ tolerance }).tolerance;
	cancel_preview_transaction();
	preview_transaction_.kind = PreviewTransactionKind::merge_by_distance;
	preview_transaction_.active = true;
	preview_transaction_.changed = false;
	preview_transaction_.base_state = state_;
	preview_transaction_.history_state = preview_transaction_.base_state;
	const CommandResult result = update_merge_by_distance_preview_from_base(tolerance);
	if (!result.ok) {
		clear_preview_transaction();
	}
	return result;
}

CommandResult EditorSession::begin_merge_by_distance_preview(MergeByDistanceOperationSettings settings)
{
	settings = normalize_merge_by_distance_operation_settings(settings);
	return begin_merge_by_distance_preview(settings.tolerance);
}

CommandResult EditorSession::update_merge_by_distance_preview(double tolerance)
{
	if (!preview_transaction_.active || preview_transaction_.kind != PreviewTransactionKind::merge_by_distance) {
		return error_result("No merge by distance preview is active.");
	}
	tolerance = normalize_merge_by_distance_operation_settings({ tolerance }).tolerance;
	return update_merge_by_distance_preview_from_base(tolerance);
}

CommandResult EditorSession::update_merge_by_distance_preview(MergeByDistanceOperationSettings settings)
{
	settings = normalize_merge_by_distance_operation_settings(settings);
	return update_merge_by_distance_preview(settings.tolerance);
}

CommandResult EditorSession::begin_transform_preview(bool duplicate_selected)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.begin_transform_preview");
	cancel_preview_transaction();
	preview_transaction_.kind = PreviewTransactionKind::transform;
	preview_transaction_.active = true;
	preview_transaction_.changed = false;
	preview_transaction_.base_state = state_;
	preview_transaction_.history_state = state_;

	if (duplicate_selected &&
			state_.selection_mode == SelectionMode::object &&
			!quader_editor_core::selected_object_ids(state_.objects).empty()) {
		const CommandResult duplicate = duplicate_selected_objects(quader_poly::Vec3 {});
		if (!duplicate.ok) {
			set_state(preview_transaction_.history_state);
			clear_preview_transaction();
			return duplicate;
		}
		if (!duplicate.changed) {
			set_state(preview_transaction_.history_state);
			clear_preview_transaction();
			return ok_result(false);
		}
		preview_transaction_.base_state = state_;
		preview_transaction_.changed = true;
	}

	return ok_result(preview_transaction_.changed);
}

CommandResult EditorSession::update_transform_translate_preview(quader_poly::Vec3 delta, bool texture_lock_enabled)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_transform_translate_preview");
	if (!preview_transaction_.active || preview_transaction_.kind != PreviewTransactionKind::transform) {
		return error_result("No transform preview is active.");
	}
	set_state(preview_transaction_.base_state);
	if (quader_poly::length(delta) <= transform_point_epsilon) {
		const bool base_changed = state_revisions_differ(preview_transaction_.base_state, preview_transaction_.history_state);
		preview_transaction_.changed = base_changed;
		return ok_result(base_changed);
	}
	return update_transform_preview_from_base(translate_selected(delta, texture_lock_enabled));
}

CommandResult EditorSession::update_transform_matrix_preview(
		const quader_poly::Transform3& transform,
		quader_poly::Vec3 pivot,
		bool texture_lock_enabled,
		bool texture_lock_scale_enabled)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_transform_matrix_preview");
	if (!preview_transaction_.active || preview_transaction_.kind != PreviewTransactionKind::transform) {
		return error_result("No transform preview is active.");
	}
	set_state(preview_transaction_.base_state);
	return update_transform_preview_from_base(transform_selected(transform, pivot, texture_lock_enabled, texture_lock_scale_enabled));
}

CommandResult EditorSession::update_transform_extrude_preview(quader_poly::Vec3 offset, float closed_edge_ledge_size)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_transform_extrude_preview");
	if (!preview_transaction_.active || preview_transaction_.kind != PreviewTransactionKind::transform) {
		return error_result("No transform preview is active.");
	}
	set_state(preview_transaction_.base_state);
	if (quader_poly::length(offset) <= transform_point_epsilon) {
		const bool base_changed = state_revisions_differ(preview_transaction_.base_state, preview_transaction_.history_state);
		preview_transaction_.changed = base_changed;
		return ok_result(base_changed);
	}
	return update_transform_preview_from_base(extrude_selected_elements(offset, closed_edge_ledge_size));
}

CommandResult EditorSession::update_transform_inset_preview(const quader_poly::Transform3& transform, quader_poly::Vec3 pivot)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_transform_inset_preview");
	if (!preview_transaction_.active || preview_transaction_.kind != PreviewTransactionKind::transform) {
		return error_result("No transform preview is active.");
	}
	set_state(preview_transaction_.base_state);
	if (basis_is_identity(transform) && quader_poly::length(transform.origin) <= transform_point_epsilon) {
		const bool base_changed = state_revisions_differ(preview_transaction_.base_state, preview_transaction_.history_state);
		preview_transaction_.changed = base_changed;
		return ok_result(base_changed);
	}
	return update_transform_preview_from_base(inset_selected_elements(transform, pivot));
}

PreviewTransactionFinishResult EditorSession::commit_preview_transaction(PreviewTransactionKind expected_kind)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.commit_preview_transaction");
	const PreviewTransactionStatus before = preview_transaction_status();
	if (!preview_transaction_.active ||
			(expected_kind != PreviewTransactionKind::none && preview_transaction_.kind != expected_kind)) {
		return { before, false };
	}

	if (!preview_transaction_.changed) {
		set_state(preview_transaction_.history_state);
	} else {
		record_history(preview_transaction_.history_state);
	}
	clear_preview_transaction();
	return { before, before.active && !before.changed };
}

PreviewTransactionFinishResult EditorSession::cancel_preview_transaction(PreviewTransactionKind expected_kind)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.cancel_preview_transaction");
	const PreviewTransactionStatus before = preview_transaction_status();
	if (!preview_transaction_.active ||
			(expected_kind != PreviewTransactionKind::none && preview_transaction_.kind != expected_kind)) {
		return { before, false };
	}

	set_state(preview_transaction_.history_state);
	clear_preview_transaction();
	return { before, true };
}

CommandResult EditorSession::update_edge_bevel_preview_from_base(const quader_poly::EdgeBevelSettings& settings)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_edge_bevel_preview_from_base");
	const SessionState base = preview_transaction_.base_state;
	set_state(base);
	const CommandResult result = bevel_selected_edges(settings);
	if (!result.ok || !result.changed) {
		set_state(base);
		preview_transaction_.base_state = base;
		preview_transaction_.changed = false;
		return result.ok ? CommandResult { false, false, result.message } : result;
	}

	preview_transaction_.base_state = base;
	preview_transaction_.changed = true;
	return result;
}

CommandResult EditorSession::update_thicken_preview_from_base(float thickness, bool from_center)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_thicken_preview_from_base");
	const SessionState base = preview_transaction_.base_state;
	set_state(base);
	if (std::abs(thickness) <= 1.0e-6F) {
		preview_transaction_.base_state = base;
		preview_transaction_.changed = false;
		return ok_result(false);
	}

	const CommandResult result = thicken_selected_faces(thickness, from_center);
	if (!result.ok || !result.changed) {
		set_state(base);
		preview_transaction_.base_state = base;
		preview_transaction_.changed = false;
		return result.ok ? CommandResult { false, false, result.message } : result;
	}

	preview_transaction_.base_state = base;
	preview_transaction_.changed = true;
	return result;
}

CommandResult EditorSession::update_slice_quad_preview_from_base(int x_slices, int y_slices)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_slice_quad_preview_from_base");
	const SessionState base = preview_transaction_.base_state;
	set_state(base);
	SliceQuadOperationSettings settings;
	settings.x_slices = x_slices;
	settings.y_slices = y_slices;
	settings = normalize_slice_quad_operation_settings(settings);
	if (settings.x_slices == 0 && settings.y_slices == 0) {
		preview_transaction_.base_state = base;
		preview_transaction_.changed = false;
		return ok_result(false);
	}

	const CommandResult result = slice_selected_quads(settings);
	if (!result.ok || !result.changed) {
		set_state(base);
		preview_transaction_.base_state = base;
		preview_transaction_.changed = false;
		return result.ok ? CommandResult { false, false, result.message } : result;
	}

	preview_transaction_.base_state = base;
	preview_transaction_.changed = true;
	return result;
}

CommandResult EditorSession::update_merge_by_distance_preview_from_base(double tolerance)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_merge_by_distance_preview_from_base");
	const SessionState base = preview_transaction_.base_state;
	set_state(base);
	const CommandResult result = merge_selected_vertices_by_distance(tolerance);
	if (!result.ok || !result.changed) {
		set_state(base);
		preview_transaction_.base_state = base;
		preview_transaction_.changed = false;
		return result.ok ? ok_result(false) : result;
	}

	preview_transaction_.base_state = base;
	preview_transaction_.changed = true;
	return result;
}

CommandResult EditorSession::update_transform_preview_from_base(CommandResult result)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_transform_preview_from_base");
	const SessionState base = preview_transaction_.base_state;
	const bool base_changed = state_revisions_differ(preview_transaction_.base_state, preview_transaction_.history_state);
	if (!result.ok) {
		set_state(base);
		preview_transaction_.base_state = base;
		preview_transaction_.changed = base_changed;
		return result;
	}
	if (!result.changed) {
		set_state(base);
		preview_transaction_.base_state = base;
		preview_transaction_.changed = base_changed;
		return ok_result(base_changed);
	}

	preview_transaction_.base_state = base;
	preview_transaction_.changed = true;
	return result;
}

} // namespace quader_editor_core
