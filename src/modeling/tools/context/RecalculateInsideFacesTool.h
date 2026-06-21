#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class RecalculateInsideFacesTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "recalculate_inside_faces",
                .operationId = "recalculate_face_normals",
                .label = "Recalculate Inside",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "recalculate_inside_faces";
    }
};

} // namespace quader
