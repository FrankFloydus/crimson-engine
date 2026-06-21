#pragma once

#include "viewport/tools/base/PointSequenceTool.h"

namespace quader {

class PolyTool final : public PointSequenceTool {
public:
    PolyTool()
        : PointSequenceTool({
                  .tool = ViewportToolId::Poly,
                  .modelingTool = ModelingViewportTool::Poly,
                  .id = "poly",
                  .label = "Poly",
                  .shortcut = "P",
                  .kind = ToolKind::Modal,
                  .menuGroup = ToolMenuGroup::Modeling,
                  .ownsViewport = true,
                  .blocksViewportFocusShortcuts = true,
          }) {}
};

} // namespace quader
