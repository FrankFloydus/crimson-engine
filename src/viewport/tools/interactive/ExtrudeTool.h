#pragma once

#include "viewport/tools/base/TransformViewportTool.h"

namespace quader {

class ExtrudeTool final : public TransformViewportTool {
public:
    ExtrudeTool()
        : TransformViewportTool({
                  .tool = ViewportToolId::Extrude,
                  .modelingTool = ModelingViewportTool::Extrude,
                  .id = "extrude",
                  .label = "Extrude",
                  .shortcut = "",
                  .kind = ToolKind::Transform,
                  .menuGroup = ToolMenuGroup::Modeling,
          },
                  ModelingTransformTool::Move) {}
};

} // namespace quader
