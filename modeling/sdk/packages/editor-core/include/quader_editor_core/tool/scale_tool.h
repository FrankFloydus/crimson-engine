#pragma once

#include <quader_editor_core/session.h>
#include <quader_editor_core/tool/tool_identity.h>
#include <quader_editor_core/tool/tool_transform_math.h>

#include <string>
#include <vector>

namespace quader_editor_core {

struct ScaleToolState {
	bool active = false;
	TransformAxis axis = TransformAxis::none;
	std::vector<int> object_ids;
	quader_poly::Vec3 pivot;
	quader_poly::Vec2 screen_pivot;
	quader_poly::Vec2 start_mouse;
	quader_poly::Vec2 screen_axis { 1.0F, 0.0F };
	ToolBounds3 bounds;
	quader_poly::Vec3 preview_scale { 1.0F, 1.0F, 1.0F };
};

struct ScaleBeginInput {
	TransformAxis axis = TransformAxis::none;
	std::vector<int> object_ids;
	quader_poly::Vec3 pivot;
	quader_poly::Vec2 screen_position;
	bool has_screen_pivot = false;
	quader_poly::Vec2 screen_pivot;
	bool has_screen_tip = false;
	quader_poly::Vec2 screen_tip;
	ToolBounds3 bounds;
};

struct ScaleBeginResult {
	bool ok = false;
	std::string message;
	ScaleToolState state;
};

struct ScalePreviewResult {
	bool ok = false;
	std::string message;
	quader_poly::Vec3 scale { 1.0F, 1.0F, 1.0F };
	quader_poly::Vec3 preview_scale { 1.0F, 1.0F, 1.0F };
	quader_poly::Transform3 transform;
	quader_poly::Transform3 pivoted_transform;
};

ScaleBeginResult begin_scale_drag(const ScaleBeginInput& input);
ScalePreviewResult preview_scale_from_screen_position(ScaleToolState& state, quader_poly::Vec2 screen_position, bool grid_enabled, double grid_size);
ScalePreviewResult preview_scale_from_scale(ScaleToolState& state, quader_poly::Vec3 scale);
void reset_scale(ScaleToolState& state);

} // namespace quader_editor_core
