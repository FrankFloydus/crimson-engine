////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/tool_identity.hpp>
#include <editor/interactions/tool_transform_math.hpp>

#include <string>
#include <vector>

namespace quader_editor_core {

/**
 * Stores the Scale Tool State data contract used by the modal modeling interaction layer.
 */
struct ScaleToolState {
	bool active = false;
        TransformAxis axis = TransformAxis::None;
        quader_poly::Transform3 orientation;
        std::vector<int> object_ids;
	quader::QVec3 pivot;
	quader::QVec2 screen_pivot;
	quader::QVec2 start_mouse;
	quader::QVec2 screen_axis { 1.0F, 0.0F };
	ToolBounds3 bounds;
	bool snap_bounds_to_grid = false;
	bool snap_uniform_to_grid = false;
	quader::QVec3 preview_scale { 1.0F, 1.0F, 1.0F };
};

/**
 * Represents a Scale Begin Input value used by the modal modeling interaction layer.
 */
struct ScaleBeginInput {
  TransformAxis axis = TransformAxis::None;
  quader_poly::Transform3 orientation;
  std::vector<int> object_ids;
  quader::QVec3 pivot;
  quader::QVec2 screen_position;
  bool has_screen_pivot = false;
  quader::QVec2 screen_pivot;
  bool has_screen_tip = false;
  quader::QVec2 screen_tip;
  ToolBounds3 bounds;
  // Component bounds use absolute coordinates in the captured orientation.
  // Other callers retain world bounds and the dimensions-based snap policy.
  bool snap_bounds_to_grid = false;
  // Topology tools retain their dimensions-based uniform grid snapping.
  bool snap_uniform_to_grid = false;
};

/**
 * Stores the Scale Begin Result data contract used by the modal modeling interaction layer.
 */
struct ScaleBeginResult {
	bool ok = false;
	std::string message;
	ScaleToolState state;
};

/**
 * Stores the Scale Preview Result data contract used by the modal modeling interaction layer.
 */
struct ScalePreviewResult {
	bool ok = false;
	std::string message;
	quader::QVec3 scale { 1.0F, 1.0F, 1.0F };
	quader::QVec3 preview_scale { 1.0F, 1.0F, 1.0F };
	quader_poly::Transform3 transform;
	quader_poly::Transform3 pivoted_transform;
};

ScaleBeginResult begin_scale_drag(const ScaleBeginInput& input);
ScalePreviewResult preview_scale_from_screen_position(ScaleToolState& state, quader::QVec2 screen_position, bool grid_enabled, double grid_size);
ScalePreviewResult preview_scale_from_scale(ScaleToolState& state, quader::QVec3 scale);
void reset_scale(ScaleToolState& state);

} // namespace quader_editor_core
