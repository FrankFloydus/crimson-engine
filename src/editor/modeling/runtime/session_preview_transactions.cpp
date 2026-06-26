////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.h"

#include <diagnostics/profile.h>

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

void EditorSession::begin_preview_transaction(PreviewTransactionKind kind)
{
	cancel_preview_transaction();
	preview_transaction_.kind = kind;
	preview_transaction_.active = true;
	preview_transaction_.changed = false;
	preview_transaction_.base_state = state_;
	preview_transaction_.history_state = preview_transaction_.base_state;
}

bool EditorSession::preview_transaction_active(PreviewTransactionKind kind) const
{
	return preview_transaction_.active && preview_transaction_.kind == kind;
}

CommandResult EditorSession::finish_preview_begin(CommandResult result)
{
	if (!result.ok) {
		clear_preview_transaction();
	}
	return result;
}

CommandResult EditorSession::finish_preview_update_from_base(
		const SessionState& base,
		CommandResult result,
		CommandResult unchanged_result)
{
	if (!result.ok) {
		set_state(base);
		preview_transaction_.base_state = base;
		preview_transaction_.changed = false;
		return result;
	}
	if (!result.changed) {
		set_state(base);
		preview_transaction_.base_state = base;
		preview_transaction_.changed = false;
		return unchanged_result;
	}

	preview_transaction_.base_state = base;
	preview_transaction_.changed = true;
	return result;
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
	begin_preview_transaction(PreviewTransactionKind::edge_bevel);
	return finish_preview_begin(update_edge_bevel_preview_from_base(settings));
}

CommandResult EditorSession::begin_edge_bevel_preview(EdgeBevelOperationSettings settings)
{
	return begin_edge_bevel_preview(to_edge_bevel_settings(settings));
}

CommandResult EditorSession::update_edge_bevel_preview(const quader_poly::EdgeBevelSettings& settings)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_edge_bevel_preview");
	if (!preview_transaction_active(PreviewTransactionKind::edge_bevel)) {
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
	begin_preview_transaction(PreviewTransactionKind::thicken);
	return finish_preview_begin(update_thicken_preview_from_base(thickness, from_center));
}

CommandResult EditorSession::begin_thicken_preview(ThickenOperationSettings settings)
{
	settings = normalize_thicken_operation_settings(settings);
	return begin_thicken_preview(static_cast<float>(settings.thickness), settings.from_center);
}

CommandResult EditorSession::update_thicken_preview(float thickness, bool from_center)
{
	if (!preview_transaction_active(PreviewTransactionKind::thicken)) {
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
	begin_preview_transaction(PreviewTransactionKind::slice_quad);
	return finish_preview_begin(update_slice_quad_preview_from_base(x_slices, y_slices));
}

CommandResult EditorSession::begin_slice_quad_preview(SliceQuadOperationSettings settings)
{
	settings = normalize_slice_quad_operation_settings(settings);
	return begin_slice_quad_preview(settings.x_slices, settings.y_slices);
}

CommandResult EditorSession::update_slice_quad_preview(int x_slices, int y_slices)
{
	if (!preview_transaction_active(PreviewTransactionKind::slice_quad)) {
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
	begin_preview_transaction(PreviewTransactionKind::merge_by_distance);
	return finish_preview_begin(update_merge_by_distance_preview_from_base(tolerance));
}

CommandResult EditorSession::begin_merge_by_distance_preview(MergeByDistanceOperationSettings settings)
{
	settings = normalize_merge_by_distance_operation_settings(settings);
	return begin_merge_by_distance_preview(settings.tolerance);
}

CommandResult EditorSession::update_merge_by_distance_preview(double tolerance)
{
	if (!preview_transaction_active(PreviewTransactionKind::merge_by_distance)) {
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
	begin_preview_transaction(PreviewTransactionKind::transform);

	if (duplicate_selected &&
			state_.selection_mode == SelectionMode::object &&
			!quader_editor_core::selected_object_ids(state_.objects).empty()) {
		const CommandResult duplicate = duplicate_selected_objects(quader::QVec3 {});
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

CommandResult EditorSession::update_transform_translate_preview(quader::QVec3 delta, bool texture_lock_enabled)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_transform_translate_preview");
	if (!preview_transaction_active(PreviewTransactionKind::transform)) {
		return error_result("No transform preview is active.");
	}
	set_state(preview_transaction_.base_state);
	if (quader::length(delta) <= transform_point_epsilon) {
		const bool base_changed = state_revisions_differ(preview_transaction_.base_state, preview_transaction_.history_state);
		preview_transaction_.changed = base_changed;
		return ok_result(base_changed);
	}
	return update_transform_preview_from_base(translate_selected(delta, texture_lock_enabled));
}

CommandResult EditorSession::update_transform_matrix_preview(
		const quader_poly::Transform3& transform,
		quader::QVec3 pivot,
		bool texture_lock_enabled,
		bool texture_lock_scale_enabled)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_transform_matrix_preview");
	if (!preview_transaction_active(PreviewTransactionKind::transform)) {
		return error_result("No transform preview is active.");
	}
	set_state(preview_transaction_.base_state);
	return update_transform_preview_from_base(transform_selected(transform, pivot, texture_lock_enabled, texture_lock_scale_enabled));
}

CommandResult EditorSession::update_transform_extrude_preview(quader::QVec3 offset, float closed_edge_ledge_size)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_transform_extrude_preview");
	if (!preview_transaction_active(PreviewTransactionKind::transform)) {
		return error_result("No transform preview is active.");
	}
	set_state(preview_transaction_.base_state);
	if (quader::length(offset) <= transform_point_epsilon) {
		const bool base_changed = state_revisions_differ(preview_transaction_.base_state, preview_transaction_.history_state);
		preview_transaction_.changed = base_changed;
		return ok_result(base_changed);
	}
	return update_transform_preview_from_base(extrude_selected_elements(offset, closed_edge_ledge_size));
}

CommandResult EditorSession::update_transform_inset_preview(const quader_poly::Transform3& transform, quader::QVec3 pivot)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_transform_inset_preview");
	if (!preview_transaction_active(PreviewTransactionKind::transform)) {
		return error_result("No transform preview is active.");
	}
	set_state(preview_transaction_.base_state);
	if (basis_is_identity(transform) && quader::length(transform.origin) <= transform_point_epsilon) {
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
	return finish_preview_update_from_base(
		base,
		result,
		CommandResult { false, false, result.message });
}

CommandResult EditorSession::update_thicken_preview_from_base(float thickness, bool from_center)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_thicken_preview_from_base");
	const SessionState base = preview_transaction_.base_state;
	set_state(base);
	if (std::abs(thickness) <= 1.0e-6F) {
		return finish_preview_update_from_base(base, ok_result(false), ok_result(false));
	}

	const CommandResult result = thicken_selected_faces(thickness, from_center);
	return finish_preview_update_from_base(
		base,
		result,
		CommandResult { false, false, result.message });
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
		return finish_preview_update_from_base(base, ok_result(false), ok_result(false));
	}

	const CommandResult result = slice_selected_quads(settings);
	return finish_preview_update_from_base(
		base,
		result,
		CommandResult { false, false, result.message });
}

CommandResult EditorSession::update_merge_by_distance_preview_from_base(double tolerance)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.update_merge_by_distance_preview_from_base");
	const SessionState base = preview_transaction_.base_state;
	set_state(base);
	const CommandResult result = merge_selected_vertices_by_distance(tolerance);
	return finish_preview_update_from_base(base, result, ok_result(false));
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
