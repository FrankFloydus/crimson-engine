#pragma once

#include "viewport/tools/base/DragCommitTool.h"

namespace quader {

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
