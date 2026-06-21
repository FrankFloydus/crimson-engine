#pragma once

#include "viewport/tools/base/PointSequenceTool.h"

namespace quader {

class KnifeTool final : public PointSequenceTool {
public:
    KnifeTool()
        : PointSequenceTool({
                  .tool = ViewportToolId::Knife,
                  .modelingTool = ModelingViewportTool::Knife,
                  .id = "knife",
                  .label = "Knife",
                  .shortcut = "C",
                  .kind = ToolKind::Modal,
                  .menuGroup = ToolMenuGroup::Modeling,
                  .ownsViewport = true,
                  .blocksViewportFocusShortcuts = true,
          }) {}
};

} // namespace quader
