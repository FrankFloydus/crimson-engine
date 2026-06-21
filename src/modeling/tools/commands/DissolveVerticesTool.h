#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class DissolveVerticesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "dissolve_vertices",
                .operationId = "dissolve_vertices",
                .label = "Dissolve Vertices",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::DissolveVertices;
    }
};

} // namespace quader
