#pragma once

#include "viewport/tools/base/InteractiveViewportTool.h"

namespace quader {

class SelectTool final : public InteractiveViewportTool {
public:
    SelectTool()
        : InteractiveViewportTool({
                  .tool = ViewportToolId::Select,
                  .modelingTool = ModelingViewportTool::Select,
                  .id = "select",
                  .label = "Select",
                  .shortcut = "Q",
                  .kind = ToolKind::Selection,
                  .menuGroup = ToolMenuGroup::Navigation,
          }) {}
};

} // namespace quader
