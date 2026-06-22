////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/modes/base/TransformViewportTool.h"

namespace quader {

/**
 * Implements Rotate Tool behavior for the viewport input, layout, and camera layer.
 */
class RotateTool final : public TransformViewportTool {
public:
    RotateTool()
        : TransformViewportTool({
                  .tool = ViewportToolId::Rotate,
                  .modelingTool = ModelingViewportTool::Rotate,
                  .id = "rotate",
                  .label = "Rotate",
                  .shortcut = "R",
                  .kind = ToolKind::Transform,
                  .menuGroup = ToolMenuGroup::Navigation,
          },
                  ModelingTransformTool::Rotate) {}
};

} // namespace quader
