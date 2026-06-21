#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class ShadeFacesSmoothTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "shade_faces_smooth",
                .operationId = "shade_faces_smooth",
                .label = "Shade Smooth",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::ShadeFacesSmooth;
    }
};

} // namespace quader
