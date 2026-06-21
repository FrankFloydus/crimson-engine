#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class HardenEdgeNormalsTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "harden_edge_normals",
                .operationId = "harden_edge_normals",
                .label = "Harden Edge Normals",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::HardenEdgeNormals;
    }
};

} // namespace quader
