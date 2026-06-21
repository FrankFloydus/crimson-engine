#pragma once

#include <quader_editor_core/session.h>

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
