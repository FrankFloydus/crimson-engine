#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class ConnectEdgesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "connect_edges",
                .operationId = "connect_edges",
                .label = "Connect Edges",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::ConnectEdges;
    }
};

} // namespace quader
