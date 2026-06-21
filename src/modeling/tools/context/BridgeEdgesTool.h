#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class BridgeEdgesTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "bridge_edges",
                .operationId = "bridge",
                .label = "Bridge",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "bridge_edges";
    }
};

} // namespace quader
