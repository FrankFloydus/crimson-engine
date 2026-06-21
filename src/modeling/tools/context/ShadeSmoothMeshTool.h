#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class ShadeSmoothMeshTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "shade_smooth_mesh",
                .operationId = "shade_faces_smooth",
                .label = "Shade Smooth",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "shade_smooth_mesh";
    }
};

} // namespace quader
