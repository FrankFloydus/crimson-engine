#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class RadialAlignEdgesTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "radial_align_edges",
                .operationId = "radial_align",
                .label = "Radial Align",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "radial_align_edges";
    }
};

} // namespace quader
