#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class InvertMeshTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "invert_mesh",
                .operationId = "flip_face_normals",
                .label = "Invert Mesh",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "invert_mesh";
    }
};

} // namespace quader
