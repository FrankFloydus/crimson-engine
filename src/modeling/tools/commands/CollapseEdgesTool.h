#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class CollapseEdgesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "collapse_edges",
                .operationId = "collapse_edges",
                .label = "Collapse Edges",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::CollapseEdges;
    }
};

} // namespace quader
