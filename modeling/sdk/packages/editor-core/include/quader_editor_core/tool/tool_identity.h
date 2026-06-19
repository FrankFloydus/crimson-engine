#pragma once

#include <string_view>

namespace quader_editor_core {

enum class TransformAxis {
	none,
	x,
	y,
	z,
	xy,
	xz,
	yz,
	all,
};

enum class ViewportInteractionTool {
	select,
	move,
	extend,
	extrude,
	rotate,
	scale,
	box,
	insert_edge_loop,
	knife,
	cut,
	mirror,
	poly,
	unknown,
};

enum class ViewAxis {
	x,
	y,
	z,
};

TransformAxis axis_from_id(std::string_view id);
std::string_view axis_id(TransformAxis axis);
std::string_view view_axis_id(ViewAxis axis);
std::string_view view_axis_label(ViewAxis axis, int sign);
ViewportInteractionTool viewport_interaction_tool_from_id(std::string_view tool_id);
std::string_view viewport_interaction_tool_id(ViewportInteractionTool tool);

} // namespace quader_editor_core
