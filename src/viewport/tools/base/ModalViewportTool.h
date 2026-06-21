#pragma once

#include "viewport/tools/base/InteractiveViewportTool.h"

namespace quader {

class ModalViewportTool : public InteractiveViewportTool {
public:
    explicit ModalViewportTool(ToolDescriptor descriptor) : InteractiveViewportTool(descriptor) {}

    [[nodiscard]] ViewportToolResult handleEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event) const override;

protected:
    [[nodiscard]] ViewportToolResult cancelActiveModalTool(
            ViewportToolContext& context,
            bool exitTool) const;
};

} // namespace quader
