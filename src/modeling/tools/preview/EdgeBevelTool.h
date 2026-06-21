#pragma once

#include "modeling/tools/SdkPreviewOperationTool.h"

namespace quader {

class EdgeBevelTool final : public SdkPreviewOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "bevel_edges",
                .operationId = "edge_bevel",
                .label = "Bevel Edges",
                .kind = CommandToolKind::Preview,
                .previewKind = ModelingActionPreviewKind::EdgeBevel,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "bevel_edges";
    }
};

} // namespace quader
