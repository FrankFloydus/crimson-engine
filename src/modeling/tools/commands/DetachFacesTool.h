#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class DetachFacesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "detach_faces",
                .operationId = "detach_faces",
                .label = "Detach Faces",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::DetachFaces;
    }
};

} // namespace quader
