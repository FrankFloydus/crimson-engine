#pragma once

#include "viewport/tools/base/TwoStageBoxTool.h"

namespace quader {

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
