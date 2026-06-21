#pragma once

#include "viewport/tools/base/ModalViewportTool.h"

namespace quader {

class DragCommitTool : public ModalViewportTool {
public:
    explicit DragCommitTool(ToolDescriptor descriptor) : ModalViewportTool(descriptor) {}

    [[nodiscard]] ViewportToolResult handleEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event) const override;
};

} // namespace quader
