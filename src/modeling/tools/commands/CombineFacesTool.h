#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class CombineFacesTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "combine_faces",
                .operationId = "combine_faces",
                .label = "Combine Faces",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::CombineFaces;
    }
};

} // namespace quader
