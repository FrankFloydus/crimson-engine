#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class ExtractFacesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "extract_faces",
                .operationId = "extract_faces",
                .label = "Extract Faces",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::ExtractFaces;
    }
};

} // namespace quader
