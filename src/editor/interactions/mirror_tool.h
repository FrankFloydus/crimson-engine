////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.h>
#include <editor/interactions/drawn_plane_tool.h>
#include <editor/interactions/tool_identity.h>

#include <span>
#include <string>
#include <vector>

namespace quader_editor_core {

struct EditorRuntimeState;

/**
 * Enumerates ViewportFlipOrientation values used by the modeling layer.
 */
enum class ViewportFlipOrientation {
	horizontal = 0,
	vertical = 1,
};

/**
 * Represents a Mirror Preview Counts value used by the modal modeling interaction layer.
 */
struct MirrorPreviewCounts {
	int target_count = 0;
	int mirrored_count = 0;
};

/**
 * Stores the Mirror Tool State data contract used by the modal modeling interaction layer.
 */
struct MirrorToolState {
	DrawnPlaneToolState plane;
	std::vector<int> target_object_ids;
	bool preview_valid = false;
	MirrorPreviewCounts preview_counts;
};

/**
 * Stores the Mirror Tool Result data contract used by the modal modeling interaction layer.
 */
struct MirrorToolResult {
	bool ok = false;
	bool changed = false;
	std::string message;
	int target_count = 0;
	std::vector<int> dirty_object_ids;
};

/**
 * Stores the Mirror Preview Request data contract used by the modal modeling interaction layer.
 */
struct MirrorPreviewRequest {
	bool ok = false;
	std::string message;
	std::vector<int> object_ids;
	std::vector<quader::QVec3> points;
	bool can_preview = false;
	int point_count = 0;
	int target_count = 0;
	bool preview_valid = false;
};

/**
 * Represents a Mirror Preview Metadata value used by the modal modeling interaction layer.
 */
struct MirrorPreviewMetadata {
	bool preview_valid = false;
	bool can_preview = false;
	int point_count = 0;
	int target_count = 0;
	int mirrored_count = 0;
};

using MirrorRayUpdateInput = DrawnPlaneRayUpdateInput;
using MirrorHoverState = DrawnPlaneHoverState;

void mirror_document_across_drawn_plane(quader_poly::Document& document, const DrawnPlaneSurface& surface);
DrawnPlaneSurface mirror_world_axis_surface(TransformAxis axis);
std::vector<quader::QVec3> mirror_world_axis_plane_points(TransformAxis axis);
DrawnPlaneSurface mirror_mesh_flip_surface(
	ViewportFlipOrientation orientation,
	quader::QVec3 pivot);
CommandResult mirror_objects_across_surface(
	std::vector<PolygonObject>& objects,
	std::span<const int> object_ids,
	const DrawnPlaneSurface& surface,
	int& next_object_id,
	int& active_object_id,
	SelectionMode& mode);
CommandResult flip_objects_by_orientation(
	std::vector<PolygonObject>& objects,
	std::span<const int> object_ids,
	ViewportFlipOrientation orientation,
	int& active_object_id,
	SelectionMode& mode);
std::vector<int> mirror_target_object_ids_for_selection(const RectangleSelectionState& selection);
MirrorToolResult set_mirror_targets_from_selection(MirrorToolState& state, const RectangleSelectionState& selection);
MirrorToolResult capture_first_mirror_point(MirrorToolState& state, const RectangleSelectionState& selection, quader::QVec3 position, quader::QVec3 plane_normal);
MirrorToolResult capture_second_mirror_point(MirrorToolState& state, quader::QVec3 position);
MirrorToolResult update_dragged_new_mirror_point(MirrorToolState& state, quader::QVec3 position);
MirrorToolResult update_dragged_mirror_point(MirrorToolState& state, quader::QVec3 position, quader::QVec3 plane_normal);
MirrorToolResult begin_mirror_point_drag(MirrorToolState& state, int point_index);
MirrorToolResult begin_mirror_line_drag(MirrorToolState& state, quader::QVec3 anchor);
MirrorToolResult update_dragged_mirror_line(MirrorToolState& state, quader::QVec3 position);
MirrorToolResult end_mirror_point_drag(MirrorToolState& state);
MirrorToolResult end_new_mirror_point_drag(MirrorToolState& state);
MirrorToolResult end_mirror_line_drag(MirrorToolState& state);
std::vector<quader::QVec3> mirror_overlay_points(const MirrorToolState& state, quader::QVec3 hover_position, bool has_hover);
std::vector<quader::QVec3> mirror_plane_points(const MirrorToolState& state);
bool mirror_point_repeats_existing(const MirrorToolState& state, quader::QVec3 point);
bool mirror_point_repeats_other(const MirrorToolState& state, quader::QVec3 point, int point_index);
MirrorPreviewRequest mirror_preview_request(MirrorToolState& state);
MirrorToolResult set_mirror_preview_result(MirrorToolState& state, bool ok, const MirrorPreviewCounts& counts, std::string message = {});
MirrorToolResult invalidate_mirror_preview(MirrorToolState& state);
MirrorToolResult commit_reset_mirror(MirrorToolState& state, const std::vector<int>& result_object_ids);
void reset_mirror(MirrorToolState& state);
bool mirror_is_dragging(const MirrorToolState& state);
bool mirror_is_active(const MirrorToolState& state);
bool mirror_can_preview(const MirrorToolState& state);
MirrorPreviewMetadata mirror_preview_metadata(const MirrorToolState& state);
std::string mirror_preview_status(const MirrorToolState& state, const MirrorPreviewCounts& fallback_counts = {});
bool update_mirror_hover(MirrorHoverState& state, bool valid, int object_id, quader::QVec3 position, double epsilon);

MirrorToolResult refresh_mirror_preview(MirrorToolState& state, std::span<const PolygonObject> objects);
MirrorToolResult refresh_mirror_preview_from_selection(
	MirrorToolState& state,
	const RectangleSelectionState& selection,
	std::span<const PolygonObject> objects);

std::vector<quader_poly::Document> mirror_preview_documents(
	const MirrorToolState& state,
	std::span<const PolygonObject> objects);

MirrorToolResult update_mirror_hover_from_ray(
	EditorRuntimeState& runtime,
	const RectangleSelectionState& selection,
	std::span<const PolygonObject> objects,
	const MirrorRayUpdateInput& input);

MirrorToolResult click_mirror_from_ray(
	EditorRuntimeState& runtime,
	const RectangleSelectionState& selection,
	std::span<const PolygonObject> objects,
	const MirrorRayUpdateInput& input);

MirrorToolResult begin_mirror_line_drag_from_ray(
	EditorRuntimeState& runtime,
	const MirrorRayUpdateInput& input);

} // namespace quader_editor_core
