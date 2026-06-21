#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class ApplyActiveMaterialTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "apply_active_material",
                .operationId = "assign_material",
                .label = "Apply Active Material",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "apply_active_material";
    }
};

} // namespace quader
