#pragma once

#include "modeling/tools/SdkPreviewOperationTool.h"

namespace quader {

class MergeByDistanceTool final : public SdkPreviewOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "merge_by_distance",
                .operationId = "merge_by_distance",
                .label = "Merge by Distance",
                .kind = CommandToolKind::Preview,
                .previewKind = ModelingActionPreviewKind::MergeByDistance,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "merge_by_distance";
    }
};

} // namespace quader
