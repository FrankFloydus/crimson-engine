////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/modeling/modeling_uv_peel.hpp"
#include "editor/modeling/services/modeling_interaction_settings_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"

namespace quader {

/// Application-lifetime options, separate from scene content and live previews.
/// Path Array and Extrude retain their existing independently supplied defaults.
struct ToolSessionDefaults final {
  ModelingInteractionSettingsService modeling;
  UvPeelSessionDefaults uv_peel;
  LinearArraySessionDefaults linear_array;
  RadialArraySessionDefaults radial_array;
  CurveSessionDefaults curve;
  PivotSessionDefaults pivot;
  PrimitiveCreationSessionDefaults primitive;
};

} // namespace quader
