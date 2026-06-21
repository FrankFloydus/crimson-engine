#pragma once

#include "modeling/tools/SdkPreviewOperationTool.h"

namespace quader {

class CreateOuterHullTool final : public SdkPreviewOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "create_outer_hull",
                .operationId = "thicken",
                .label = "Create Outer Hull",
                .kind = CommandToolKind::Preview,
                .previewKind = ModelingActionPreviewKind::Thicken,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "create_outer_hull";
    }
};

} // namespace quader
