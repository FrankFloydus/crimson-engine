#pragma once

#include <quader_editor_core/tool/tool_identity.h>
#include <quader_editor_core/tool/viewport_projection.h>
#include <quader_editor_core/tool_constants.h>

#include <string>
#include <vector>

namespace quader_editor_core {

struct BoxToolState;
struct Bounds3;
struct EditorSession;
struct SelectionHoverState;
struct TransformDragState;

struct MeasurementLine {
	TransformAxis axis = TransformAxis::none;
	quader_poly::Vec2 start;
	quader_poly::Vec2 end;
};

struct MeasurementLabel {
	TransformAxis axis = TransformAxis::none;
	bool text_color = false;
	quader_poly::Vec2 screen_position;
	std::string text;
};

struct MeasurementFrame {
	std::vector<MeasurementLine> lines;
	std::vector<MeasurementLabel> labels;
};

struct MeasurementFrameInput {
	const EditorSession *session = nullptr;
	const SelectionHoverState *hover = nullptr;
	const BoxToolState *box = nullptr;
	const TransformDragState *transform_drag = nullptr;
	const Bounds3 *selected_bounds_override = nullptr;
	ViewportCameraState camera;
	double box_grid_size = 1.0;
	double map_units_per_world_unit = measurement_default_map_units_per_world_unit;
};

std::string measurement_number_text(double value, bool force_decimals);
std::string measurement_length_text(double world_length, double map_units_per_world_unit = measurement_default_map_units_per_world_unit, bool force_decimals = false);
MeasurementFrame build_measurement_frame(const MeasurementFrameInput& input);

} // namespace quader_editor_core
