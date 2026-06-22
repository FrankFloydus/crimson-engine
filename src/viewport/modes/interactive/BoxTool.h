////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/modes/base/TwoStageBoxTool.h"

namespace quader {

/**
 * Implements Box Tool behavior for the viewport input, layout, and camera layer.
 */
class BoxTool final : public TwoStageBoxTool {
public:
    BoxTool()
        : TwoStageBoxTool({
                  .tool = ViewportToolId::Box,
                  .modelingTool = ModelingViewportTool::Box,
                  .id = "box",
                  .label = "Box",
                  .shortcut = "B",
                  .kind = ToolKind::Modal,
                  .menuGroup = ToolMenuGroup::Modeling,
                  .ownsViewport = true,
          }) {}
};

} // namespace quader
