#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class SnapEdgesToActiveTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "snap_edges_to_active",
                .operationId = "snap_edges_to_active",
                .label = "Snap to Edge",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::SnapEdgesToActive;
    }
};

} // namespace quader
