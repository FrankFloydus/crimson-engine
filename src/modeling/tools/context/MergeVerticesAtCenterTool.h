#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class MergeVerticesAtCenterTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "merge_vertices_at_center",
                .operationId = "merge_vertices_to_center",
                .label = "Merge at Center",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "merge_vertices_at_center";
    }
};

} // namespace quader
