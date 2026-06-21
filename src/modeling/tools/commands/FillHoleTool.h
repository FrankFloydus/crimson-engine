#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class FillHoleTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "fill_hole",
                .operationId = "fill_hole",
                .label = "Fill Hole",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::FillHole;
    }
};

} // namespace quader
