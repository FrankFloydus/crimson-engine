////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/interactions/tool_transform_math.h>

#include <string>

namespace quader_editor_core {

class EditorTool;

/**
 * Stores the Extend Tool State data contract used by the modal modeling interaction layer.
 */
struct ExtendToolState {
	bool active = false;
	int source_object_id = -1;
	int source_face_index = -1;
	quader::QVec3 face_center;
	quader::QVec3 face_normal;
	quader::QVec2 screen_axis { 1.0F, 0.0F };
	double preview_distance = 0.0;
	quader::QVec3 preview_delta;
};

/**
 * Represents an Extend Begin Input value used by the modal modeling interaction layer.
 */
struct ExtendBeginInput {
	int source_object_id = -1;
	int source_face_index = -1;
	quader::QVec3 face_center;
	quader::QVec3 face_normal;
	quader::QVec2 screen_axis { 1.0F, 0.0F };
};

/**
 * Stores the Extend State Result data contract used by the modal modeling interaction layer.
 */
struct ExtendStateResult {
	bool ok = false;
	bool changed = false;
	std::string message;
	ExtendToolState state;
};

using ExtendBeginResult = ExtendStateResult;
using ExtendCommitResult = ExtendStateResult;

/**
 * Represents an Extend Handle Primitive Input value used by the modal modeling interaction layer.
 */
struct ExtendHandlePrimitiveInput {
	quader::QVec3 center;
	quader::QVec3 normal;
	quader_poly::Transform3 camera_transform;
	quader::QVec2 screen_center;
	bool has_projected_axis_delta = false;
	quader::QVec2 projected_axis_delta;
	double handle_screen_radius = 56.0;
	double ring_radius = 54.0;
	double camera_parallel_dot = 0.995;
};

/**
 * Represents an Extend Handle Primitives value used by the modal modeling interaction layer.
 */
struct ExtendHandlePrimitives {
	bool ok = false;
	quader::QVec3 center;
	quader::QVec3 normal;
	quader::QVec3 handle_axis;
	quader::QVec2 screen_axis { 1.0F, 0.0F };
	quader::QVec2 handle_start;
	quader::QVec2 handle_end;
	quader::QVec2 handle_center;
	quader::QVec2 ring_center;
	double ring_radius = 54.0;
	bool stick_visible = false;
};

[[nodiscard]] const EditorTool& extend_tool();

ExtendStateResult begin_extend_drag(const ExtendBeginInput& input);
double snap_extend_distance(double distance, bool precision, bool grid_enabled, double grid_size);
double extend_distance_for_drag_pixels(double pixels, bool precision, bool grid_enabled, double grid_size);
ExtendStateResult preview_extend_from_distance(ExtendToolState& state, double distance, bool precision, bool grid_enabled, double grid_size);
ExtendStateResult preview_extend_from_drag_pixels(ExtendToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size);
quader::QVec3 extend_handle_axis_for_normal(quader::QVec3 normal);
ExtendHandlePrimitives build_extend_handle_primitives(const ExtendHandlePrimitiveInput& input);
void reset_extend(ExtendToolState& state);

} // namespace quader_editor_core
