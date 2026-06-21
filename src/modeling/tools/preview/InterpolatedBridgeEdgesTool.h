#pragma once

#include "modeling/tools/SdkPreviewOperationTool.h"

namespace quader {

class InterpolatedBridgeEdgesTool final : public SdkPreviewOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "interpolated_bridge_edges",
                .operationId = "bridge",
                .label = "Interpolated Bridge Edges",
                .kind = CommandToolKind::Preview,
                .previewKind = ModelingActionPreviewKind::Bridge,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "interpolated_bridge_edges";
    }
};

} // namespace quader
