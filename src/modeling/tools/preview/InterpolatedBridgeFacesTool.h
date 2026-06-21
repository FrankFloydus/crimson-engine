#pragma once

#include "modeling/tools/SdkPreviewOperationTool.h"

namespace quader {

class InterpolatedBridgeFacesTool final : public SdkPreviewOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "interpolated_bridge_faces",
                .operationId = "bridge",
                .label = "Interpolated Bridge Faces",
                .kind = CommandToolKind::Preview,
                .previewKind = ModelingActionPreviewKind::Bridge,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "interpolated_bridge_faces";
    }
};

} // namespace quader
