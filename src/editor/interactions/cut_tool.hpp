////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/viewport_projection.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader_editor_core {

struct EditorRuntimeState;

/**
 * Enumerates CutMode values used by the modeling layer.
 */
enum class CutMode {
  KeepBack = 0,
  KeepFront = 1,
  KeepBoth = 2,
};

/**
 * Represents a Cut Preview Counts value used by the modal modeling interaction layer.
 */
struct CutPreviewCounts {
	int target_count = 0;
	int split_count = 0;
	int removed_count = 0;
};

/**
 * Stores the Cut Tool State data contract used by the modal modeling interaction layer.
 */
struct CutToolState {
	std::vector<quader::QVec3> points;
	std::vector<int> target_object_ids;
	int source_object_id = -1;
	quader::QVec3 face_normal;
        CutMode mode = CutMode::KeepBoth;
        int dragging_point_index = -1;
	bool dragging_new_point = false;
	bool dragging_line = false;
	quader::QVec3 drag_line_anchor;
	bool preview_valid = false;
	CutPreviewCounts preview_counts;
};

/**
 * Stores the Cut Tool Result data contract used by the modal modeling interaction layer.
 */
struct CutToolResult {
	bool ok = false;
	bool changed = false;
	std::string message;
	int target_count = 0;
	std::vector<int> dirty_object_ids;
};

/**
 * Stores the Cut Preview Request data contract used by the modal modeling interaction layer.
 */
struct CutPreviewRequest {
	bool ok = false;
	std::string message;
	std::vector<int> object_ids;
	std::vector<quader::QVec3> points;
        CutMode mode = CutMode::KeepBoth;
        bool can_preview = false;
	int point_count = 0;
	int target_count = 0;
	bool preview_valid = false;
};

/**
 * Represents a Cut Preview Metadata value used by the modal modeling interaction layer.
 */
struct CutPreviewMetadata {
	bool preview_valid = false;
	bool can_preview = false;
        CutMode mode = CutMode::KeepBoth;
        int point_count = 0;
	int target_count = 0;
	int split_count = 0;
	int removed_count = 0;
};

/**
 * Stores the Cut Hover State data contract used by the modal modeling interaction layer.
 */
struct CutHoverState {
	bool valid = false;
	int object_id = -1;
	quader::QVec3 position;
};

/**
 * Enumerates CutPreviewMeshSide values used by the modeling layer.
 */
enum class CutPreviewMeshSide {
  Keep,
  Discard,
};

/**
 * Represents a Cut Ray Update Input value used by the modal modeling interaction layer.
 */
struct CutRayUpdateInput {
	ViewportCameraState camera;
	quader_poly::Ray ray;
	double grid_size = 1.0;
	bool dragging = false;
};

CutMode cut_mode_from_id(int id);
int cut_mode_id(CutMode mode);
std::string_view cut_mode_label(CutMode mode);
CutMode next_cut_mode(CutMode mode);

quader_poly::PlaneCutMode plane_cut_mode_from_cut_mode(CutMode mode);
quader_poly::PlaneCutRequest plane_cut_request_from_cut_state(const CutToolState& state);

std::vector<int> cut_target_object_ids_for_selection(const RectangleSelectionState& selection);
CutToolResult set_cut_targets_from_selection(CutToolState& state, const RectangleSelectionState& selection);
CutToolResult capture_first_cut_point(CutToolState& state, const RectangleSelectionState& selection, quader::QVec3 position, quader::QVec3 plane_normal);
CutToolResult capture_second_cut_point(CutToolState& state, quader::QVec3 position);
CutToolResult update_dragged_new_cut_point(CutToolState& state, quader::QVec3 position);
CutToolResult update_dragged_cut_point(CutToolState& state, quader::QVec3 position, quader::QVec3 plane_normal);
CutToolResult begin_cut_point_drag(CutToolState& state, int point_index);
CutToolResult begin_cut_line_drag(CutToolState& state, quader::QVec3 anchor);
CutToolResult update_dragged_cut_line(CutToolState& state, quader::QVec3 position);
CutToolResult end_cut_point_drag(CutToolState& state);
CutToolResult end_new_cut_point_drag(CutToolState& state);
CutToolResult end_cut_line_drag(CutToolState& state);
CutToolResult set_cut_mode(CutToolState& state, CutMode mode);
std::vector<quader::QVec3> cut_overlay_points(const CutToolState& state, quader::QVec3 hover_position, bool has_hover);
std::vector<quader::QVec3> cut_plane_points(const CutToolState& state);
std::vector<int> dirty_object_ids_for_cut(const std::vector<int>& source_ids, const std::vector<int>& result_ids);
bool cut_point_repeats_existing(const CutToolState& state, quader::QVec3 point);
bool cut_point_repeats_other(const CutToolState& state, quader::QVec3 point, int point_index);
CutPreviewRequest cut_preview_request(CutToolState& state, CutMode mode);
CutToolResult set_cut_preview_result(CutToolState& state, CutMode mode, bool ok, const CutPreviewCounts& counts, std::string message = {});
CutToolResult invalidate_cut_preview(CutToolState& state);
CutToolResult commit_reset_cut(CutToolState& state, const std::vector<int>& result_object_ids);
void reset_cut(CutToolState& state);
bool cut_is_dragging(const CutToolState& state);
bool cut_is_active(const CutToolState& state);
bool cut_can_preview(const CutToolState& state);
CutPreviewMetadata cut_preview_metadata(const CutToolState& state);
std::string cut_preview_status(const CutToolState& state, CutMode mode, const CutPreviewCounts& fallback_counts = {});
bool update_cut_hover(CutHoverState& state, bool valid, int object_id, quader::QVec3 position, double epsilon);

CutToolResult refresh_cut_preview(CutToolState& state, std::span<const PolygonObject> objects);
CutToolResult refresh_cut_preview_from_selection(
	CutToolState& state,
	const RectangleSelectionState& selection,
	std::span<const PolygonObject> objects);

std::vector<quader_poly::Document> cut_preview_documents(
	const CutToolState& state,
	std::span<const PolygonObject> objects,
	CutPreviewMeshSide side);

CutToolResult update_cut_hover_from_ray(
	EditorRuntimeState& runtime,
	const RectangleSelectionState& selection,
	std::span<const PolygonObject> objects,
	const CutRayUpdateInput& input);

CutToolResult click_cut_from_ray(
	EditorRuntimeState& runtime,
	const RectangleSelectionState& selection,
	std::span<const PolygonObject> objects,
	const CutRayUpdateInput& input);

CutToolResult begin_cut_line_drag_from_ray(
	EditorRuntimeState& runtime,
	const CutRayUpdateInput& input);

} // namespace quader_editor_core
