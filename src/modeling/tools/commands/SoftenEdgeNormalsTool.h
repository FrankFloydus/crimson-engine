#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class SoftenEdgeNormalsTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "soften_edge_normals",
                .operationId = "soften_edge_normals",
                .label = "Soften Edge Normals",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::SoftenEdgeNormals;
    }
};

} // namespace quader
