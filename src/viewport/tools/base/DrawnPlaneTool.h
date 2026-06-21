#pragma once

#include "viewport/tools/base/ModalViewportTool.h"

namespace quader {

class DrawnPlaneTool : public ModalViewportTool {
public:
    explicit DrawnPlaneTool(ToolDescriptor descriptor) : ModalViewportTool(descriptor) {}

    [[nodiscard]] ViewportToolResult handleEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event) const override;
};

} // namespace quader
