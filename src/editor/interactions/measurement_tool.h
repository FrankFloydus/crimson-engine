////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/interactions/tool_identity.h>
#include <editor/interactions/viewport_projection.h>
#include <editor/modeling/runtime/tool_constants.h>

#include <string>
#include <vector>

namespace quader_editor_core {

struct BoxToolState;
struct Bounds3;
struct EditorSession;
struct SelectionHoverState;
struct TransformDragState;

/**
 * Represents a Measurement Line value used by the modal modeling interaction layer.
 */
struct MeasurementLine {
	TransformAxis axis = TransformAxis::none;
	quader::QVec2 start;
	quader::QVec2 end;
};

/**
 * Represents a Measurement Label value used by the modal modeling interaction layer.
 */
struct MeasurementLabel {
	TransformAxis axis = TransformAxis::none;
	bool text_color = false;
	quader::QVec2 screen_position;
	std::string text;
};

/**
 * Stores the Measurement Frame data contract used by the modal modeling interaction layer.
 */
struct MeasurementFrame {
	std::vector<MeasurementLine> lines;
	std::vector<MeasurementLabel> labels;
};

/**
 * Represents a Measurement Frame Input value used by the modal modeling interaction layer.
 */
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
