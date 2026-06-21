#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class FlipFaceNormalsTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "flip_face_normals",
                .operationId = "flip_face_normals",
                .label = "Flip Face Normals",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::FlipFaceNormals;
    }
};

} // namespace quader
