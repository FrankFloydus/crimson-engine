#pragma once

#include "viewport/tools/base/DrawnPlaneTool.h"

namespace quader {

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
