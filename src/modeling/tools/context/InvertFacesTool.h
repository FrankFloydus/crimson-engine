#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class InvertFacesTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "invert_faces",
                .operationId = "flip_face_normals",
                .label = "Invert Faces",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "invert_faces";
    }
};

} // namespace quader
