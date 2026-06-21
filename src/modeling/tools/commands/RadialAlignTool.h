#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class RadialAlignTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "radial_align",
                .operationId = "radial_align",
                .label = "Radial Align",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::RadialAlign;
    }
};

} // namespace quader
