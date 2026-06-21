#pragma once

#include "modeling/tools/SdkInstantOperationTool.h"

namespace quader {

class DeleteSelectionTool final : public SdkInstantOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "delete_selection",
                .operationId = "delete_selection",
                .label = "Delete",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] ModelingInstantOperationId sdkInstantOperation() const override {
        return ModelingInstantOperationId::DeleteSelection;
    }
};

} // namespace quader
