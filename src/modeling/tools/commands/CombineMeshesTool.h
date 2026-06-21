#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class CombineMeshesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "combine_meshes",
                .operationId = "combine_meshes",
                .label = "Combine Meshes",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::CombineMeshes;
    }
};

} // namespace quader
