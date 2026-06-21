#pragma once

#include "modeling/tools/SdkPreviewOperationTool.h"

namespace quader {

class SliceQuadTool final : public SdkPreviewOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "slice_quad",
                .operationId = "slice_quad",
                .label = "Slice Quad",
                .kind = CommandToolKind::Preview,
                .previewKind = ModelingActionPreviewKind::SliceQuad,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "slice_quad";
    }
};

} // namespace quader
