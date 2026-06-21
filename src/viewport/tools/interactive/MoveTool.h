#pragma once

#include "viewport/tools/base/TransformViewportTool.h"

namespace quader {

class MoveTool final : public TransformViewportTool {
public:
    MoveTool()
        : TransformViewportTool({
                  .tool = ViewportToolId::Move,
                  .modelingTool = ModelingViewportTool::Move,
                  .id = "move",
                  .label = "Move",
                  .shortcut = "W",
                  .kind = ToolKind::Transform,
                  .menuGroup = ToolMenuGroup::Navigation,
          },
                  ModelingTransformTool::Move) {}
};

} // namespace quader
