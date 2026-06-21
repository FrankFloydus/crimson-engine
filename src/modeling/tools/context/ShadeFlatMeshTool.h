#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class ShadeFlatMeshTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "shade_flat_mesh",
                .operationId = "shade_faces_flat",
                .label = "Shade Flat",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "shade_flat_mesh";
    }
};

} // namespace quader
