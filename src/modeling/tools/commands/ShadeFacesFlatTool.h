#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class ShadeFacesFlatTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "shade_faces_flat",
                .operationId = "shade_faces_flat",
                .label = "Shade Flat",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::ShadeFacesFlat;
    }
};

} // namespace quader
