#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class DuplicateMeshesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "duplicate_meshes",
                .operationId = "duplicate_meshes",
                .label = "Duplicate",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::DuplicateMeshes;
    }
};

} // namespace quader
