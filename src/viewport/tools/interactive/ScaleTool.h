#pragma once

#include "viewport/tools/base/TransformViewportTool.h"

namespace quader {

class ScaleTool final : public TransformViewportTool {
public:
    ScaleTool()
        : TransformViewportTool({
                  .tool = ViewportToolId::Scale,
                  .modelingTool = ModelingViewportTool::Scale,
                  .id = "scale",
                  .label = "Scale",
                  .shortcut = "S",
                  .kind = ToolKind::Transform,
                  .menuGroup = ToolMenuGroup::Navigation,
          },
                  ModelingTransformTool::Scale) {}
};

} // namespace quader
