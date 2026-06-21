#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class RecalculateOutsideMeshTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "recalculate_outside_mesh",
                .operationId = "recalculate_face_normals",
                .label = "Recalculate Outside",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "recalculate_outside_mesh";
    }
};

} // namespace quader
