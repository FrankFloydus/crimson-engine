////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/tool_identity.h>
#include <editor/interactions/cut_tool.h>

namespace quader_editor_core {

TransformAxis axis_from_id(std::string_view id)
{
	if (id == "x") {
		return TransformAxis::x;
	}
	if (id == "y") {
		return TransformAxis::y;
	}
	if (id == "z") {
		return TransformAxis::z;
	}
	if (id == "xy") {
		return TransformAxis::xy;
	}
	if (id == "xz") {
		return TransformAxis::xz;
	}
	if (id == "yz") {
		return TransformAxis::yz;
	}
	if (id == "all") {
		return TransformAxis::all;
	}
	return TransformAxis::none;
}

std::string_view axis_id(TransformAxis axis)
{
	switch (axis) {
		case TransformAxis::x:
			return "x";
		case TransformAxis::y:
			return "y";
		case TransformAxis::z:
			return "z";
		case TransformAxis::xy:
			return "xy";
		case TransformAxis::xz:
			return "xz";
		case TransformAxis::yz:
			return "yz";
		case TransformAxis::all:
			return "all";
		case TransformAxis::none:
		default:
			return "";
	}
}

std::string_view view_axis_id(ViewAxis axis)
{
	switch (axis) {
		case ViewAxis::x:
			return "x";
		case ViewAxis::y:
			return "y";
		case ViewAxis::z:
		default:
			return "z";
	}
}

std::string_view view_axis_label(ViewAxis axis, int sign)
{
	switch (axis) {
		case ViewAxis::x:
			return sign < 0 ? "-X" : "X";
		case ViewAxis::y:
			return sign < 0 ? "-Y" : "Y";
		case ViewAxis::z:
		default:
			return sign < 0 ? "-Z" : "Z";
	}
}

CutMode cut_mode_from_id(int id)
{
	if (id == static_cast<int>(CutMode::keep_back)) {
		return CutMode::keep_back;
	}
	if (id == static_cast<int>(CutMode::keep_front)) {
		return CutMode::keep_front;
	}
	return CutMode::keep_both;
}

int cut_mode_id(CutMode mode)
{
	return static_cast<int>(mode);
}

std::string_view cut_mode_label(CutMode mode)
{
	switch (mode) {
		case CutMode::keep_back:
			return "keep back";
		case CutMode::keep_front:
			return "keep front";
		case CutMode::keep_both:
		default:
			return "keep both";
	}
}

CutMode next_cut_mode(CutMode mode)
{
	if (mode == CutMode::keep_back) {
		return CutMode::keep_front;
	}
	if (mode == CutMode::keep_front) {
		return CutMode::keep_both;
	}
	return CutMode::keep_back;
}

ViewportInteractionTool viewport_interaction_tool_from_id(std::string_view tool_id)
{
	if (tool_id == "select") {
		return ViewportInteractionTool::select;
	}
	if (tool_id == "move") {
		return ViewportInteractionTool::move;
	}
	if (tool_id == "extend") {
		return ViewportInteractionTool::extend;
	}
	if (tool_id == "extrude") {
		return ViewportInteractionTool::extrude;
	}
	if (tool_id == "rotate") {
		return ViewportInteractionTool::rotate;
	}
	if (tool_id == "scale") {
		return ViewportInteractionTool::scale;
	}
	if (tool_id == "box") {
		return ViewportInteractionTool::box;
	}
	if (tool_id == "insert_edge_loop") {
		return ViewportInteractionTool::insert_edge_loop;
	}
	if (tool_id == "knife") {
		return ViewportInteractionTool::knife;
	}
	if (tool_id == "cut") {
		return ViewportInteractionTool::cut;
	}
	if (tool_id == "mirror") {
		return ViewportInteractionTool::mirror;
	}
	if (tool_id == "poly") {
		return ViewportInteractionTool::poly;
	}
	return ViewportInteractionTool::unknown;
}

std::string_view viewport_interaction_tool_id(ViewportInteractionTool tool)
{
	switch (tool) {
		case ViewportInteractionTool::select:
			return "select";
		case ViewportInteractionTool::move:
			return "move";
		case ViewportInteractionTool::extend:
			return "extend";
		case ViewportInteractionTool::extrude:
			return "extrude";
		case ViewportInteractionTool::rotate:
			return "rotate";
		case ViewportInteractionTool::scale:
			return "scale";
		case ViewportInteractionTool::box:
			return "box";
		case ViewportInteractionTool::insert_edge_loop:
			return "insert_edge_loop";
		case ViewportInteractionTool::knife:
			return "knife";
		case ViewportInteractionTool::cut:
			return "cut";
		case ViewportInteractionTool::mirror:
			return "mirror";
		case ViewportInteractionTool::poly:
			return "poly";
		case ViewportInteractionTool::unknown:
		default:
			return "";
	}
}

} // namespace quader_editor_core
