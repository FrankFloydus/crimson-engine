#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class DissolveEdgesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "dissolve_edges",
                .operationId = "dissolve_edges",
                .label = "Dissolve Edges",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::DissolveEdges;
    }
};

} // namespace quader
