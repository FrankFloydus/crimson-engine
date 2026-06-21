#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class MergeVerticesTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "merge_vertices",
                .operationId = "merge_vertices_to_active",
                .label = "Merge",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "merge_vertices";
    }
};

} // namespace quader
