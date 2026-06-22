////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/modes/base/DragCommitTool.h"

namespace quader {

/**
 * Implements Insert Edge Loop Tool behavior for the viewport input, layout, and camera layer.
 */
class InsertEdgeLoopTool final : public DragCommitTool {
public:
    InsertEdgeLoopTool()
        : DragCommitTool({
                  .tool = ViewportToolId::InsertEdgeLoop,
                  .modelingTool = ModelingViewportTool::InsertEdgeLoop,
                  .id = "insert_edge_loop",
                  .label = "Insert Edge Loop",
                  .shortcut = "Shift+R",
                  .kind = ToolKind::Modal,
                  .menuGroup = ToolMenuGroup::Modeling,
                  .ownsViewport = true,
          }) {}
};

} // namespace quader
