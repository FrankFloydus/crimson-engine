////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/base/two_stage_box_tool.hpp"

namespace quader {

/**
 * Implements the unified primitive creation viewport interaction. Primitive
 * type remains session-owned settings; every type shares this volume gesture.
 */
class CreatePrimitiveTool final : public TwoStageBoxTool {
public:
  explicit CreatePrimitiveTool(ViewportToolId tool = ViewportToolId::CreatePrimitive)
      : TwoStageBoxTool(
            viewport_tool_descriptor(tool)) {}
  [[nodiscard]] bool handles_wheel_events() const override { return true; }
  [[nodiscard]] ViewportToolResult
  handle_event(ViewportToolContext &context,
               const ViewportToolEvent &event) const override;
};

} // namespace quader
