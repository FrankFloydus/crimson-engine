////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.h>

namespace quader_editor_core {

void reset_poly(PolyToolState& state);
void rebuild_poly_overlay(PolyToolState& state);
bool poly_can_commit(const PolyToolState& state);
PolyToolPreview poly_tool_preview(const PolyToolState& state);
PolyToolResult update_poly_hover(
	PolyToolState& state,
	quader_poly::Vec3 position,
	quader_poly::Vec3 plane_normal,
	bool constrain_axis,
	double grid_size,
	bool snap_enabled);
PolyToolResult accept_poly_current_point(PolyToolState& state);

} // namespace quader_editor_core
