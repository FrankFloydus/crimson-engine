////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/base/drawn_plane_tool.hpp"

namespace quader {

/**
 * Implements Cut Tool behavior for editor-owned viewport tool interactions.
 */
class CutTool final : public DrawnPlaneTool {
public:
  CutTool() : DrawnPlaneTool(viewport_tool_descriptor(ViewportToolId::Cut)) {}

  [[nodiscard]] ViewportPointerCursor pointer_cursor() const override {
    return ViewportPointerCursor::BoxCutter;
  }
};

} // namespace quader
