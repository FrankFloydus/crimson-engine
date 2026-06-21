#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class SplitEdgesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "split_edges",
                .operationId = "split_edges",
                .label = "Split Edges",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::SplitEdges;
    }
};

} // namespace quader
