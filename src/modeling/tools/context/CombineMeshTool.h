#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class CombineMeshTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "combine_mesh",
                .operationId = "combine_meshes",
                .label = "Combine Mesh",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "combine_mesh";
    }
};

} // namespace quader
