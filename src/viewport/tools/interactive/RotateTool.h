#pragma once

#include "viewport/tools/base/TransformViewportTool.h"

namespace quader {

class RotateTool final : public TransformViewportTool {
public:
    RotateTool()
        : TransformViewportTool({
                  .tool = ViewportToolId::Rotate,
                  .modelingTool = ModelingViewportTool::Rotate,
                  .id = "rotate",
                  .label = "Rotate",
                  .shortcut = "R",
                  .kind = ToolKind::Transform,
                  .menuGroup = ToolMenuGroup::Navigation,
          },
                  ModelingTransformTool::Rotate) {}
};

} // namespace quader
