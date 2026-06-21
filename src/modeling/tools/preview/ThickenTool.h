#pragma once

#include "modeling/tools/SdkPreviewOperationTool.h"

namespace quader {

class ThickenTool final : public SdkPreviewOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "thicken_faces",
                .operationId = "thicken",
                .label = "Thicken Faces",
                .kind = CommandToolKind::Preview,
                .previewKind = ModelingActionPreviewKind::Thicken,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "thicken_faces";
    }
};

} // namespace quader
