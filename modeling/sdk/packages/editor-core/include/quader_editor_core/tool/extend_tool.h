#pragma once

#include <quader_editor_core/tool/tool_transform_math.h>

#include <string>

namespace quader_editor_core {

class EditorTool;

struct ExtendToolState {
	bool active = false;
	int source_object_id = -1;
	int source_face_index = -1;
	quader_poly::Vec3 face_center;
	quader_poly::Vec3 face_normal;
	quader_poly::Vec2 screen_axis { 1.0F, 0.0F };
	double preview_distance = 0.0;
	quader_poly::Vec3 preview_delta;
};

struct ExtendBeginInput {
	int source_object_id = -1;
	int source_face_index = -1;
	quader_poly::Vec3 face_center;
	quader_poly::Vec3 face_normal;
	quader_poly::Vec2 screen_axis { 1.0F, 0.0F };
};

struct ExtendStateResult {
	bool ok = false;
	bool changed = false;
	std::string message;
	ExtendToolState state;
};

using ExtendBeginResult = ExtendStateResult;
using ExtendCommitResult = ExtendStateResult;

struct ExtendHandlePrimitiveInput {
	quader_poly::Vec3 center;
	quader_poly::Vec3 normal;
	quader_poly::Transform3 camera_transform;
	quader_poly::Vec2 screen_center;
	bool has_projected_axis_delta = false;
	quader_poly::Vec2 projected_axis_delta;
	double handle_screen_radius = 56.0;
	double ring_radius = 54.0;
	double camera_parallel_dot = 0.995;
};

struct ExtendHandlePrimitives {
	bool ok = false;
	quader_poly::Vec3 center;
	quader_poly::Vec3 normal;
	quader_poly::Vec3 handle_axis;
	quader_poly::Vec2 screen_axis { 1.0F, 0.0F };
	quader_poly::Vec2 handle_start;
	quader_poly::Vec2 handle_end;
	quader_poly::Vec2 handle_center;
	quader_poly::Vec2 ring_center;
	double ring_radius = 54.0;
	bool stick_visible = false;
};

[[nodiscard]] const EditorTool& extend_tool();

ExtendStateResult begin_extend_drag(const ExtendBeginInput& input);
double snap_extend_distance(double distance, bool precision, bool grid_enabled, double grid_size);
double extend_distance_for_drag_pixels(double pixels, bool precision, bool grid_enabled, double grid_size);
ExtendStateResult preview_extend_from_distance(ExtendToolState& state, double distance, bool precision, bool grid_enabled, double grid_size);
ExtendStateResult preview_extend_from_drag_pixels(ExtendToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size);
quader_poly::Vec3 extend_handle_axis_for_normal(quader_poly::Vec3 normal);
ExtendHandlePrimitives build_extend_handle_primitives(const ExtendHandlePrimitiveInput& input);
void reset_extend(ExtendToolState& state);

} // namespace quader_editor_core
