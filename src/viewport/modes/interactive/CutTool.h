////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/modes/base/DrawnPlaneTool.h"

namespace quader {

/**
 * Implements Cut Tool behavior for the viewport input, layout, and camera layer.
 */
class CutTool final : public DrawnPlaneTool {
public:
    CutTool()
        : DrawnPlaneTool({
                  .tool = ViewportToolId::Cut,
                  .modelingTool = ModelingViewportTool::Cut,
                  .id = "cut",
                  .label = "Cut",
                  .shortcut = "Shift+C",
                  .kind = ToolKind::Modal,
                  .menuGroup = ToolMenuGroup::Modeling,
                  .ownsViewport = true,
                  .blocksViewportFocusShortcuts = true,
          }) {}
};

} // namespace quader
