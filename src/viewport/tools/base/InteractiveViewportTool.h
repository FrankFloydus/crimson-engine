#pragma once

#include "viewport/tools/Tool.h"

namespace quader {

class InteractiveViewportTool : public ViewportTool {
public:
    explicit InteractiveViewportTool(ToolDescriptor descriptor) : ViewportTool(descriptor) {}
};

} // namespace quader
