#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class BevelVerticesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "bevel_vertices",
                .operationId = "bevel_vertices",
                .label = "Bevel Vertex",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::BevelVertices;
    }
};

} // namespace quader
