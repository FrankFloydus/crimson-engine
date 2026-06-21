#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class MergeVerticesToActiveTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "merge_vertices_to_active",
                .operationId = "merge_vertices_to_active",
                .label = "Merge Vertices To Active",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::MergeVerticesToActive;
    }
};

} // namespace quader
