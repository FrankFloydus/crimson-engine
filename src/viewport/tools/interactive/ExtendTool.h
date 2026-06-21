#pragma once

#include "viewport/tools/base/TransformViewportTool.h"

namespace quader {

class ExtendTool final : public TransformViewportTool {
public:
    ExtendTool()
        : TransformViewportTool({
                  .tool = ViewportToolId::Extend,
                  .modelingTool = ModelingViewportTool::Extend,
                  .id = "extend",
                  .label = "Extend",
                  .shortcut = "",
                  .kind = ToolKind::Transform,
                  .menuGroup = ToolMenuGroup::Modeling,
          },
                  ModelingTransformTool::Move) {}
};

} // namespace quader
