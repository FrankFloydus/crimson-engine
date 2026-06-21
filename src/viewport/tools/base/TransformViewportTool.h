#pragma once

#include "viewport/tools/base/InteractiveViewportTool.h"

namespace quader {

class TransformViewportTool : public InteractiveViewportTool {
public:
    TransformViewportTool(ToolDescriptor descriptor, ModelingTransformTool transformTool)
        : InteractiveViewportTool(descriptor),
          transformTool_(transformTool) {}

    [[nodiscard]] ModelingTransformTool transformTool() const override { return transformTool_; }

private:
    ModelingTransformTool transformTool_ = ModelingTransformTool::Move;
};

} // namespace quader
