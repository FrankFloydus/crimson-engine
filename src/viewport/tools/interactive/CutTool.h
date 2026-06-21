#pragma once

#include "viewport/tools/base/DrawnPlaneTool.h"

namespace quader {

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
