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
 * Implements Mirror Tool behavior for the viewport input, layout, and camera layer.
 */
class MirrorTool final : public DrawnPlaneTool {
public:
    MirrorTool()
        : DrawnPlaneTool({
                  .tool = ViewportToolId::Mirror,
                  .modelingTool = ModelingViewportTool::Mirror,
                  .id = "mirror",
                  .label = "Mirror",
                  .shortcut = "",
                  .kind = ToolKind::Modal,
                  .menuGroup = ToolMenuGroup::Modeling,
                  .ownsViewport = true,
                  .blocksViewportFocusShortcuts = true,
          }) {}
};

} // namespace quader
