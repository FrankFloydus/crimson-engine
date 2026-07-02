////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/tool_identity.hpp>
#include <editor/interactions/cut_tool.hpp>

namespace quader_editor_core {

TransformAxis axis_from_id(std::string_view id)
{
	if (id == "x") {
          return TransformAxis::X;
        }
	if (id == "y") {
          return TransformAxis::Y;
        }
	if (id == "z") {
          return TransformAxis::Z;
        }
	if (id == "xy") {
          return TransformAxis::Xy;
        }
	if (id == "xz") {
          return TransformAxis::Xz;
        }
	if (id == "yz") {
          return TransformAxis::Yz;
        }
	if (id == "all") {
          return TransformAxis::All;
        }
        return TransformAxis::None;
}

std::string_view axis_id(TransformAxis axis)
{
	switch (axis) {
        case TransformAxis::X:
          return "x";
        case TransformAxis::Y:
          return "y";
        case TransformAxis::Z:
          return "z";
        case TransformAxis::Xy:
          return "xy";
        case TransformAxis::Xz:
          return "xz";
        case TransformAxis::Yz:
          return "yz";
        case TransformAxis::All:
          return "all";
        case TransformAxis::None:
        default:
          return "";
	}
}

std::string_view view_axis_id(ViewAxis axis)
{
	switch (axis) {
        case ViewAxis::X:
          return "x";
        case ViewAxis::Y:
          return "y";
        case ViewAxis::Z:
        default:
          return "z";
	}
}

std::string_view view_axis_label(ViewAxis axis, int sign)
{
	switch (axis) {
        case ViewAxis::X:
          return sign < 0 ? "-X" : "X";
        case ViewAxis::Y:
          return sign < 0 ? "-Y" : "Y";
        case ViewAxis::Z:
        default:
          return sign < 0 ? "-Z" : "Z";
	}
}

CutMode cut_mode_from_id(int id)
{
  if (id == static_cast<int>(CutMode::KeepBack)) {
    return CutMode::KeepBack;
  }
  if (id == static_cast<int>(CutMode::KeepFront)) {
    return CutMode::KeepFront;
  }
  return CutMode::KeepBoth;
}

int cut_mode_id(CutMode mode)
{
	return static_cast<int>(mode);
}

std::string_view cut_mode_label(CutMode mode)
{
	switch (mode) {
        case CutMode::KeepBack:
          return "keep back";
        case CutMode::KeepFront:
          return "keep front";
        case CutMode::KeepBoth:
        default:
          return "keep both";
	}
}

CutMode next_cut_mode(CutMode mode)
{
  if (mode == CutMode::KeepBack) {
    return CutMode::KeepFront;
  }
  if (mode == CutMode::KeepFront) {
    return CutMode::KeepBoth;
  }
  return CutMode::KeepBack;
}

ViewportInteractionTool viewport_interaction_tool_from_id(std::string_view tool_id)
{
	if (tool_id == "select") {
          return ViewportInteractionTool::Select;
        }
	if (tool_id == "move") {
          return ViewportInteractionTool::Move;
        }
	if (tool_id == "extend") {
          return ViewportInteractionTool::Extend;
        }
	if (tool_id == "extrude") {
          return ViewportInteractionTool::Extrude;
        }
	if (tool_id == "rotate") {
          return ViewportInteractionTool::Rotate;
        }
	if (tool_id == "scale") {
          return ViewportInteractionTool::Scale;
        }
	if (tool_id == "box") {
          return ViewportInteractionTool::Box;
        }
	if (tool_id == "insert_edge_loop") {
          return ViewportInteractionTool::InsertEdgeLoop;
        }
	if (tool_id == "knife") {
          return ViewportInteractionTool::Knife;
        }
	if (tool_id == "cut") {
          return ViewportInteractionTool::Cut;
        }
	if (tool_id == "mirror") {
          return ViewportInteractionTool::Mirror;
        }
	if (tool_id == "poly") {
          return ViewportInteractionTool::Poly;
        }
        return ViewportInteractionTool::Unknown;
}

std::string_view viewport_interaction_tool_id(ViewportInteractionTool tool)
{
	switch (tool) {
        case ViewportInteractionTool::Select:
          return "select";
        case ViewportInteractionTool::Move:
          return "move";
        case ViewportInteractionTool::Extend:
          return "extend";
        case ViewportInteractionTool::Extrude:
          return "extrude";
        case ViewportInteractionTool::Rotate:
          return "rotate";
        case ViewportInteractionTool::Scale:
          return "scale";
        case ViewportInteractionTool::Box:
          return "box";
        case ViewportInteractionTool::InsertEdgeLoop:
          return "insert_edge_loop";
        case ViewportInteractionTool::Knife:
          return "knife";
        case ViewportInteractionTool::Cut:
          return "cut";
        case ViewportInteractionTool::Mirror:
          return "mirror";
        case ViewportInteractionTool::Poly:
          return "poly";
        case ViewportInteractionTool::Unknown:
        default:
          return "";
	}
}

} // namespace quader_editor_core
