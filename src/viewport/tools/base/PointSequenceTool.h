#pragma once

#include "viewport/tools/base/ModalViewportTool.h"

namespace quader {

class PointSequenceTool : public ModalViewportTool {
public:
    explicit PointSequenceTool(ToolDescriptor descriptor) : ModalViewportTool(descriptor) {}

    [[nodiscard]] ViewportToolResult handleEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event) const override;
};

} // namespace quader
