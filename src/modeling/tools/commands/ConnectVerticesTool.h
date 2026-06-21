#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class ConnectVerticesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "connect_vertices",
                .operationId = "connect_vertices",
                .label = "Connect Vertices",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::ConnectVertices;
    }
};

} // namespace quader
