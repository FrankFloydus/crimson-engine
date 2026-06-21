#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class RadialAlignFacesTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "radial_align_faces",
                .operationId = "radial_align",
                .label = "Radial Align",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "radial_align_faces";
    }
};

} // namespace quader
