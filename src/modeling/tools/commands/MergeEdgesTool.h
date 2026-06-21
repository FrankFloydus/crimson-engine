#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class MergeEdgesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "merge_edges",
                .operationId = "merge_edges",
                .label = "Merge Edges",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::MergeEdges;
    }
};

} // namespace quader
