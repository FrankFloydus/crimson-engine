#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class SnapVerticesToActiveTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "snap_vertices_to_active",
                .operationId = "snap_vertices_to_active",
                .label = "Snap to Vertex",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::SnapVerticesToActive;
    }
};

} // namespace quader
