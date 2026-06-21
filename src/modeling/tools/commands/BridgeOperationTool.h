#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class BridgeOperationTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "bridge",
                .operationId = "bridge",
                .label = "Bridge",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::Bridge;
    }
};

} // namespace quader
