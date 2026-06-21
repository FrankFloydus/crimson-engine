#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class CollapseFacesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "collapse_faces",
                .operationId = "collapse_faces",
                .label = "Collapse Faces",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::CollapseFaces;
    }
};

} // namespace quader
