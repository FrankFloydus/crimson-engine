////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string_view>

namespace quader_editor_core {

/**
 * Enumerates TransformAxis values used by the modeling layer.
 */
enum class TransformAxis {
  None,
  X,
  Y,
  Z,
  Xy,
  Xz,
  Yz,
  All,
};

/**
 * Enumerates ViewportInteractionTool values used by the modeling layer.
 */
enum class ViewportInteractionTool {
  Select,
  Move,
  Extrude,
  Rotate,
  Scale,
  Box,
  InsertEdgeLoop,
  Knife,
  Cut,
  Mirror,
  Poly,
  Unknown,
};

/**
 * Enumerates ViewAxis values used by the modeling layer.
 */
enum class ViewAxis {
  X,
  Y,
  Z,
};

TransformAxis axis_from_id(std::string_view id);
std::string_view axis_id(TransformAxis axis);
std::string_view view_axis_id(ViewAxis axis);
std::string_view view_axis_label(ViewAxis axis, int sign);
ViewportInteractionTool viewport_interaction_tool_from_id(std::string_view tool_id);
std::string_view viewport_interaction_tool_id(ViewportInteractionTool tool);

} // namespace quader_editor_core
