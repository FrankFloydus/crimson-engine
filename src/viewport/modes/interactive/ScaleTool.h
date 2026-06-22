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
 * Implements Scale Tool behavior for the viewport input, layout, and camera layer.
 */
class ScaleTool final : public TransformViewportTool {
public:
    ScaleTool()
        : TransformViewportTool({
                  .tool = ViewportToolId::Scale,
                  .modelingTool = ModelingViewportTool::Scale,
                  .id = "scale",
                  .label = "Scale",
                  .shortcut = "S",
                  .kind = ToolKind::Transform,
                  .menuGroup = ToolMenuGroup::Navigation,
          },
                  ModelingTransformTool::Scale) {}
};

} // namespace quader
