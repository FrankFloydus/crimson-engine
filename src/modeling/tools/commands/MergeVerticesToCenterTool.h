#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class MergeVerticesToCenterTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "merge_vertices_to_center",
                .operationId = "merge_vertices_to_center",
                .label = "Merge Vertices To Center",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::MergeVerticesToCenter;
    }
};

} // namespace quader
