#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class RecalculateFaceNormalsTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "recalculate_face_normals",
                .operationId = "recalculate_face_normals",
                .label = "Recalculate Face Normals",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::RecalculateFaceNormals;
    }
};

} // namespace quader
